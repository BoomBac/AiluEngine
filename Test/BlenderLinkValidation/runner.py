"""Run an isolated Blender Live Link Full Sync and retain lifecycle evidence."""
import argparse
import atexit
import ctypes
import hashlib
import importlib.util
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import uuid
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXPORTER = ROOT / "Test/BlenderLinkPerformanceExport.py"
DEFAULT_EDITOR = ROOT / "artifacts/out/editor/bin/x64/debug/Editor.exe"
DEFAULT_TEMPLATE = ROOT / "Engine/Res/ProjTemplate/empty/empty.json"
SHARED_STATE = (ROOT / "FileAppender_log.txt", ROOT / "imgui.ini", ROOT / "render_graph.dot",
                ROOT / "render_graph_timeline.dot", ROOT / "Editor/EditorConfig.json",
                ROOT / "Editor/EngineConfig.json", ROOT / "Editor/Res/UI/main_widget.json",
                ROOT / "Editor/dock_layout.json", ROOT / "ObjectDetailLayout.json", ROOT / "crash.log")
END_PACKET = 11
BEGIN_PACKET = 10
PACKET_HEADER = struct.Struct("<IHHIQ")
RUN_LOCK = None


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def read_packets(path):
    packets = []
    data = path.read_bytes()
    offset = 0
    while offset < len(data):
        if len(data) - offset < PACKET_HEADER.size:
            raise RuntimeError("Truncated packet header in packets.bin")
        magic, version, kind, size, sequence = PACKET_HEADER.unpack_from(data, offset)
        if magic != 0x4B4C4C41 or version != 1 or offset + PACKET_HEADER.size + size > len(data):
            raise RuntimeError("Invalid or truncated packet in packets.bin")
        packets.append({"type": kind, "sequence": sequence})
        offset += PACKET_HEADER.size + size
    return packets


def make_fixture(blender, fixture, log_path, timeout, fixture_kind, m1_variant, source_blend=None):
    if fixture_kind in {"m1", "m2", "materials"}:
        script_name = {"m1":"make_m1_fixture.py", "m2":"make_m2_fixture.py", "materials":"make_material_fixture.py"}[fixture_kind]
        ref_name = {"m1":"blender_reference.json", "m2":"blender_render_reference.json", "materials":"material_reference.json"}[fixture_kind]
        command = [str(blender), "--background", "--factory-startup", "--disable-autoexec"]
        if source_blend:
            command.append(str(source_blend))
        command += ["--python-exit-code", "1", "--python",
                   str(ROOT / "Test/BlenderLinkValidation" / script_name), "--", str(fixture.parent), m1_variant]
        if source_blend:
            command.append("--existing")
        with log_path.open("wb") as output:
            process = subprocess.Popen(command, cwd=fixture.parent, stdout=output, stderr=subprocess.STDOUT,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                stop_owned(process)
                raise TimeoutError("Blender M1 fixture generation timed out")
        if code or not fixture.exists() or not (fixture.parent / ref_name).exists():
            raise RuntimeError(f"{fixture_kind.upper()} fixture generation failed (exit {code}); see {log_path}")
        return

    script = fixture.parent / "make_fixture.py"
    fixture_source = ("bpy.ops.mesh.primitive_cube_add()\n" if fixture_kind == "cube" else
                      "empty = bpy.data.objects.new('ValidationEmpty', None)\n"
                      "bpy.context.scene.collection.objects.link(empty)\n")
    script.write_text("import bpy\n"
                      "bpy.ops.object.select_all(action='SELECT')\n"
                      "bpy.ops.object.delete(use_global=False)\n" + fixture_source +
                      f"bpy.ops.wm.save_as_mainfile(filepath={str(fixture)!r})\n", encoding="utf-8")
    with log_path.open("wb") as output:
        process = subprocess.Popen([str(blender), "--background", "--factory-startup", "--python", str(script)],
                                   cwd=fixture.parent, stdout=output, stderr=subprocess.STDOUT,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            raise TimeoutError("Blender fixture generation timed out")
    if code or not fixture.exists():
        raise RuntimeError(f"Fixture generation failed (exit {code}); see {log_path}")


def stop_owned(process, timeout=10):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=timeout)


def close_owned_editor(process):
    if process is None or process.poll() is not None:
        return
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def close_window(window, _):
        owner = ctypes.c_ulong()
        ctypes.windll.user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == process.pid:
            ctypes.windll.user32.PostMessageW(window, 0x0010, 0, 0)
        return True
    ctypes.windll.user32.EnumWindows(callback_type(close_window), 0)
    try:
        process.wait(timeout=120)
    except subprocess.TimeoutExpired:
        stop_owned(process)


def active_interactive_processes():
    result = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
        "Get-Process | Where-Object {$_.ProcessName -in @('blender','Editor')} | "
        "Select-Object ProcessName,Id | ConvertTo-Json -Compress"],
        capture_output=True, text=True, check=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if not result.stdout.strip():
        return []
    active = json.loads(result.stdout)
    return active if isinstance(active, list) else [active]


def release_run_lock():
    global RUN_LOCK
    if RUN_LOCK is not None:
        import msvcrt
        RUN_LOCK.seek(0)
        msvcrt.locking(RUN_LOCK.fileno(), msvcrt.LK_UNLCK, 1)
        RUN_LOCK.close()
        RUN_LOCK = None


def acquire_run_lock(path):
    global RUN_LOCK
    import msvcrt
    RUN_LOCK = path.open("a+b")
    if RUN_LOCK.tell() == 0:
        RUN_LOCK.write(b"0")
        RUN_LOCK.flush()
    RUN_LOCK.seek(0)
    try:
        msvcrt.locking(RUN_LOCK.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        RUN_LOCK.close()
        RUN_LOCK = None
        raise RuntimeError("Another Blender Link validation runner holds the output lock")
    atexit.register(release_run_lock)


def reject_existing_interactive_processes():
    active = active_interactive_processes()
    if active:
        raise RuntimeError("Refusing to run while Blender/Editor processes are already open: " +
                           "; ".join(f"{item['ProcessName']} PID {item['Id']}" for item in active))


def start_hidden(command, cwd, stdout, env):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    return subprocess.Popen(command, cwd=cwd, stdout=stdout, stderr=subprocess.STDOUT, env=env,
                            startupinfo=startup, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))


def visible_windows_for_process(process):
    windows = []
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def inspect_window(window, _):
        owner = ctypes.c_ulong()
        ctypes.windll.user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == process.pid and ctypes.windll.user32.IsWindowVisible(window):
            windows.append(int(window))
        return True

    ctypes.windll.user32.EnumWindows(callback_type(inspect_window), 0)
    return windows


def has_editor_session(project_root, expected_pid):
    try:
        session = json.loads((project_root / ".ailu/editor_session.json").read_text(encoding="utf-8"))
        return session.get("pid") == expected_pid
    except (OSError, ValueError):
        return False


def wait_for(predicate, timeout, label, processes):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for name, process in processes.items():
            if process and process.poll() is not None:
                raise RuntimeError(f"Owned {name} exited early with code {process.returncode}")
        if predicate():
            return
        time.sleep(0.1)
    raise TimeoutError(f"Timed out waiting for {label}")


def parse_engine_result(log_text, start_offset):
    current_log = log_text[start_offset:]
    imports = list(re.finditer(r"\[BlenderLiveLink\] FullSync import: ([^\r\n]+)", current_log))
    if len(imports) != 1:
        raise RuntimeError(f"Expected exactly one current-run FullSync import summary, found {len(imports)}")
    errors = re.findall(r"\[BlenderLiveLink\] Apply failed", current_log)
    if "Incoming queue limit reached" in current_log or errors:
        raise RuntimeError(f"Engine apply errors={len(errors)} queue_overflow={'Incoming queue limit reached' in current_log}")
    return imports[0].group(1)


def run(args):
    blender = args.blender.resolve()
    editor = args.editor.resolve()
    template = args.project_template.resolve()
    if not blender.is_file():
        raise FileNotFoundError(f"Blender executable not found: {blender}")
    if not editor.is_file():
        raise FileNotFoundError(f"Editor executable not found: {editor}")
    if not template.is_file():
        raise FileNotFoundError(f"Project template not found: {template}")
    source_blend = getattr(args, "source_blend", None)
    if source_blend and (args.fixture_kind != "materials" or not source_blend.is_file()):
        raise ValueError("--source-blend requires an existing .blend and --fixture-kind materials")
    node_executable = shutil.which("node") if args.fixture_kind in {"m1", "m2", "materials"} else None
    if args.fixture_kind in {"m1", "m2", "materials"} and node_executable is None:
        raise FileNotFoundError("Node.js is required for Editor automation in M1/M2 mode")
    if args.output.resolve().parent != editor.parent:
        raise ValueError("Evidence directory must be beside the selected Editor.exe")
    if args.dry_run:
        print(json.dumps({"status": "PASS", "mode": "dry-run", "side_effects": False,
                          "blender": str(blender), "editor": str(editor), "project_template": str(template)}, indent=2))
        return 0

    reject_existing_interactive_processes()

    args.output.mkdir(parents=True, exist_ok=True)
    acquire_run_lock(args.output / ".runner.lock")
    reject_existing_interactive_processes()
    run_id = f"{time.strftime('%Y%m%d-%H%M%S')}-{uuid.uuid4().hex[:8]}"
    stage = args.output / run_id
    stage.mkdir()
    for name in ("fixture", "editor", "blender"):
        (stage / name).mkdir()
    port = find_free_port()
    project_root = stage / "project"
    project_root.mkdir()
    project = project_root / "validation.json"
    project_data = json.loads(template.read_text(encoding="utf-8-sig"))
    project_data.update(_project_name="BlenderLinkValidation", _project_guid=str(uuid.uuid4()),
                        _startup_scene="project://scene/empty.almap")
    project.write_text(json.dumps(project_data, indent=2), encoding="utf-8")
    helper_path = ROOT / "Tools/generate_ddgi_test_scenes.py"
    spec = importlib.util.spec_from_file_location("blender_link_scene_helpers", helper_path)
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    scene = helpers.scene_document("BlenderLinkValidation", "Isolated empty startup scene", [])
    scene_path = project_root / "Assets/scene/empty.almap"
    scene_path.parent.mkdir(parents=True, exist_ok=True)
    scene_path.write_text(json.dumps(scene, indent=2), encoding="utf-8")
    database = {"assets": []}
    helpers.register_asset(database, scene["_header"]["_guid"], "project://scene/empty.almap",
                           helpers.ASSET_TYPE_SCENE)
    (project_root / "Library").mkdir(exist_ok=True)
    (project_root / "Library/AssetDatabase.json").write_text(json.dumps(database, indent=2), encoding="utf-8")
    fixture_name = "m1_fixture.blend" if args.fixture_kind == "m1" else (
        "m2_fixture.blend" if args.fixture_kind == "m2" else "materials_fixture.blend" if args.fixture_kind == "materials" else "tiny.blend")
    fixture = stage / "fixture" / fixture_name
    run_summary = {"run_id": run_id, "port": port, "blender": str(blender), "editor": str(editor),
                   "editor_sha256": sha256(editor), "source_blend": None, "owned_pids": [],
                   "status": "FAIL", "restoration": [], "source_unchanged": True}
    if source_blend:
        run_summary.update(original_source=str(source_blend.resolve()), original_source_sha256=sha256(source_blend))
    run_summary["engine_sha256"] = sha256(editor.parent / "Engine_d.dll")
    run_summary["plugin_sha256"] = sha256(ROOT / "Plugins/BlenderLiveLink/bin/debug/AiluBlenderLiveLink.dll")
    run_summary["addon_source_sha256"] = {path.name:sha256(path) for path in (ROOT / "Tools/Blender/ailu_live_link").glob("*.py")}
    if args.fixture_kind == "m1":
        run_summary["m1_variant"] = args.m1_variant
    elif args.fixture_kind == "m2":
        run_summary["m2_variant"] = args.m2_variant
    snapshots = {}
    for path in SHARED_STATE:
        snapshots[path] = (path.exists(), path.read_bytes() if path.is_file() else None)
    editor_process = blender_process = oracle_process = None
    oracle_log_stream = None
    try:
        if source_blend:
            make_fixture(blender, fixture, stage / "fixture/create.log", args.timeout, args.fixture_kind, args.m1_variant, source_blend)
        else:
            make_fixture(blender, fixture, stage / "fixture/create.log", args.timeout, args.fixture_kind, args.m1_variant)
        run_summary["fixture_sha256_before"] = sha256(fixture)
        run_summary["source_blend"] = str(fixture)
        if args.fixture_kind in {"m1", "m2", "materials"}:
            reference_name = {"m1":"blender_reference.json", "m2":"blender_render_reference.json", "materials":"material_reference.json"}[args.fixture_kind]
            reference_path = stage / "fixture" / reference_name
            shutil.copy2(reference_path, stage / reference_name)
        for path in SHARED_STATE:
            if path.name in {"FileAppender_log.txt", "crash.log"} and path.exists():
                path.write_bytes(b"")
        env = dict(os.environ, AILU_BLENDER_LIVE_LINK_PORT=str(port))
        if args.fixture_kind == "materials":
            env["AILU_BLENDER_VALIDATION_MATERIALS"] = "1"
        with (stage / "editor/editor.log").open("wb") as editor_log:
            if getattr(args, "capture_debug_output", False):
                from windows_debug_output import start_editor
                editor_process = start_editor([str(editor), str(project)], ROOT, editor_log, env, stage / "editor/debug_output.log")
            else:
                editor_process = start_hidden([str(editor), str(project)], ROOT, editor_log, env)
            run_summary["owned_pids"].append(editor_process.pid)
            start_offset = 0
            log_path = ROOT / "FileAppender_log.txt"
            if getattr(args, "capture_debug_output", False):
                log_path = stage / "editor/debug_output.log"
            wait_for(lambda: log_path.exists() and
                     f"Server listening on 127.0.0.1:{port}" in log_path.read_text(encoding="utf-8", errors="replace")
                     and "EditorApp::Initialize" in log_path.read_text(encoding="utf-8", errors="replace"),
                     args.startup_timeout, "Editor initialized with Live Link server", {"Editor": editor_process})
            if args.fixture_kind in {"m1", "m2", "materials"}:
                wait_for(lambda: has_editor_session(project_root, editor_process.pid), args.startup_timeout,
                         "owned Editor automation session file", {"Editor": editor_process})
                run_summary["automation_session_pid"] = editor_process.pid
            engine_log = log_path.read_text(encoding="utf-8", errors="replace")
            start_offset = len(engine_log)
            run_summary["editor_visible_windows"] = visible_windows_for_process(editor_process)
            if run_summary["editor_visible_windows"]:
                raise RuntimeError("Editor created a visible window despite background mode")
            if args.fixture_kind in {"m1", "m2", "materials"}:
                oracle_log_stream = (stage / "editor/oracle.log").open("wb")
                driver_name = {"m1":"capture_m1_snapshot.mjs", "m2":"capture_m2_render.mjs", "materials":"capture_material_bindings.mjs"}[args.fixture_kind]
                sync_sentinel_name = args.fixture_kind + "_sync_applied"
                driver_args = [str(ROOT / "Test/BlenderLinkValidation" / driver_name),
                               str(project_root), str(stage), str(editor_process.pid), str(stage / sync_sentinel_name)]
                if args.fixture_kind == "m1":
                    driver_args.insert(3, str(stage / "blender_reference.json"))
                oracle_process = subprocess.Popen([node_executable, *driver_args], cwd=ROOT, stdout=oracle_log_stream,
                    stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                run_summary["owned_pids"].append(oracle_process.pid)
            blender_env = dict(env, AILU_BLENDER_LIVE_LINK_PORT=str(port))
            with (stage / "blender/export.log").open("wb") as blender_log:
                blender_process = start_hidden([str(blender), "--background", "--factory-startup", str(fixture),
                    "--python-exit-code", "1", "--python", str(EXPORTER), "--", str(stage / "fixture")],
                    stage / "fixture", blender_log, blender_env)
                run_summary["owned_pids"].append(blender_process.pid)
                run_summary["blender_visible_windows"] = visible_windows_for_process(blender_process)
                if run_summary["blender_visible_windows"]:
                    raise RuntimeError("Blender created a visible window despite background mode")
                wait_for(lambda: (stage / "fixture/export.json").exists() and log_path.exists() and
                         "FullSync import:" in log_path.read_text(encoding="utf-8", errors="replace")[start_offset:],
                         args.timeout, "current-run FullSync End and Engine apply", {"Editor": editor_process,
                         "Blender": blender_process})
                if args.fixture_kind in {"m1", "m2", "materials"}:
                    (stage / (args.fixture_kind + "_sync_applied")).touch()
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        run_summary["engine_summary"] = parse_engine_result(log_text, start_offset)
        export = json.loads((stage / "fixture/export.json").read_text(encoding="utf-8"))
        release_path = stage / "fixture/release"
        release_path.touch(exist_ok=True)
        if blender_process and blender_process.poll() is None:
            blender_process.wait(timeout=15)
        packets = read_packets(stage / "fixture/packets.bin")
        begins = [packet for packet in packets if packet["type"] == BEGIN_PACKET]
        ends = [packet for packet in packets if packet["type"] == END_PACKET]
        if len(begins) != 1 or len(ends) != 1 or ends[0]["sequence"] <= begins[0]["sequence"]:
            raise RuntimeError("Native packet stream does not contain one ordered Full Sync Begin/End")
        if export["counts"].get(str(END_PACKET), 0) != 1 or not export["source_restored"]:
            raise RuntimeError("Blender exporter did not report one End and restored source datablocks")
        if args.fixture_kind in {"m1", "m2", "materials"}:
            try:
                oracle_process.wait(timeout=args.oracle_timeout)
            except subprocess.TimeoutExpired:
                stop_owned(oracle_process)
                raise TimeoutError(f"{args.fixture_kind.upper()} Editor oracle timed out; see editor/oracle.log")
            oracle_log_stream.close()
            oracle_log_stream = None
            if oracle_process.returncode:
                raise RuntimeError(f"{args.fixture_kind.upper()} Editor oracle failed (exit {oracle_process.returncode}); see editor/oracle.log")
        if args.fixture_kind == "materials":
            commands = [
                [sys.executable, str(ROOT / "Test/BlenderLinkValidation/validate_material_bindings.py"),
                 str(stage / "material_reference.json"), str(stage / ("fixture/packets_baseline.bin" if (stage / "fixture/packets_baseline.bin").exists() else "fixture/packets.bin")), str(stage / "material_engine_snapshot.json"), str(stage)],
                [sys.executable, str(ROOT / "Test/BlenderLinkValidation/analyze_material_capture.py"), str(stage)],
            ]
            for command in commands:
                comparison = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                with (stage / "editor/comparison.log").open("a", encoding="utf-8") as log:
                    log.write(comparison.stdout + comparison.stderr)
                if comparison.returncode:
                    raise RuntimeError("Material source/wire/Engine/image assertion failed; see material_*assertions.json")
            bindings = json.loads((stage / "material_assertions.json").read_text(encoding="utf-8"))
            images = json.loads((stage / "material_image_assertions.json").read_text(encoding="utf-8"))
            run_summary["material_bindings"] = {key:bindings[key] for key in ("status", "check_count", "failure_count", "seeded_corruptions")}
            run_summary["material_images"] = {"status":images["status"], "check_count":len(images["checks"]), "failure_count":len(images["failures"])}
            if (stage / "material_mutation_reference.json").exists():
                mutation_output = stage / "incremental"
                comparison = subprocess.run([sys.executable,str(ROOT / "Test/BlenderLinkValidation/validate_material_bindings.py"),
                    str(stage / "material_mutation_reference.json"),str(stage / "fixture/packets.bin"),
                    str(stage / "material_mutation_engine_snapshot.json"),str(mutation_output)],cwd=ROOT,capture_output=True,text=True,
                    creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
                (stage / "editor/incremental_comparison.log").write_text(comparison.stdout+comparison.stderr,encoding="utf-8")
                if comparison.returncode:
                    raise RuntimeError("Native incremental material binding assertion failed")
                bindings = json.loads((mutation_output / "material_assertions.json").read_text(encoding="utf-8"))
                run_summary["incremental_material_bindings"] = {key:bindings[key] for key in ("status", "check_count", "failure_count")}
        elif args.fixture_kind == "m1":
            comparison = subprocess.run([sys.executable, str(ROOT / "Test/BlenderLinkValidation/compare_m1.py"),
                str(stage / "blender_reference.json"), str(stage / "engine_snapshot.json"), str(stage)],
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            (stage / "editor/comparison.log").write_text(comparison.stdout, encoding="utf-8")
            run_summary["m1_comparison"] = json.loads((stage / "assertions.json").read_text(encoding="utf-8")) \
                if (stage / "assertions.json").exists() else {"status": "FAIL", "log": comparison.stdout}
            if comparison.returncode:
                raise RuntimeError(f"M1 source-to-Engine comparison failed; see failures.json and editor/comparison.log")
        elif args.fixture_kind == "m2":
            comparison = subprocess.run([sys.executable, str(ROOT / "Test/BlenderLinkValidation/analyze_m2_render.py"),
                str(stage), str(stage / "blender_render_reference.json"), str(stage / "m2_capture.json"), str(stage)],
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            (stage / "editor/comparison.log").write_text(comparison.stdout, encoding="utf-8")
            run_summary["m2_render"] = json.loads((stage / "m2_assertions.json").read_text(encoding="utf-8")) \
                if (stage / "m2_assertions.json").exists() else {"status": "FAIL", "log": comparison.stdout}
            if comparison.returncode:
                raise RuntimeError("M2 capture/image oracle failed; see m2_failures.json and editor/comparison.log")
        if args.inject_failure:
            raise RuntimeError("Injected failure after applied Full Sync")
        run_summary.update(status="PASS", blender_version=export["blender_version"],
                           packet_sha256=sha256(stage / "fixture/packets.bin"), packet_count=len(packets),
                           full_sync_begin_sequence=begins[0]["sequence"], full_sync_end_sequence=ends[0]["sequence"],
                           export=export)
    except Exception as error:
        run_summary["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        release = stage / "fixture/release"
        release.touch(exist_ok=True)
        if blender_process and blender_process.poll() is None:
            try:
                blender_process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                stop_owned(blender_process)
        if oracle_process and oracle_process.poll() is None:
            stop_owned(oracle_process)
        if oracle_log_stream and not oracle_log_stream.closed:
            oracle_log_stream.close()
        close_owned_editor(editor_process)
        if run_summary.get("engine_summary") and log_path.exists():
            try:
                run_summary["engine_summary"] = parse_engine_result(log_path.read_text(encoding="utf-8", errors="replace"), start_offset)
            except Exception as error:
                run_summary["error"] = f"Final Engine log validation failed: {error}"
        owned_processes = (blender_process, editor_process, oracle_process)
        run_summary["owned_processes_exited"] = all(process is None or process.poll() is not None
                                                      for process in owned_processes)
        run_summary["owned_process_exit_codes"] = {
            "Blender": blender_process.returncode if blender_process else None,
            "Editor": editor_process.returncode if editor_process else None,
            "NodeOracle": oracle_process.returncode if oracle_process else None,
        }
        for path, (existed, original) in snapshots.items():
            if path.name in {"FileAppender_log.txt", "crash.log"} and path.exists():
                target = "engine.log" if path.name == "FileAppender_log.txt" else "crash.log"
                shutil.copy2(path, stage / "editor" / target)
        owned_pids = set(run_summary["owned_pids"])
        try:
            external_processes = [item for item in active_interactive_processes() if item["Id"] not in owned_pids]
        except Exception as error:
            external_processes = [{"ProcessName": "process-check-error", "Id": 0, "error": str(error)}]
        run_summary["external_process_conflict"] = external_processes
        for path, (existed, original) in snapshots.items():
            if external_processes:
                run_summary["restoration"].append({"path": str(path), "restored": False,
                                                   "reason": "external Editor/Blender process detected; preserved current state"})
                continue
            try:
                if existed:
                    if path.exists() and path.is_file() and path.read_bytes() != original:
                        path.write_bytes(original)
                    elif not path.exists():
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(original)
                    restored = path.is_file() and path.read_bytes() == original
                else:
                    if path.exists():
                        path.unlink()
                    restored = not path.exists()
            except OSError as error:
                restored = False
                run_summary["restoration"].append({"path": str(path), "restored": False,
                                                   "reason": f"{type(error).__name__}: {error}"})
                continue
            run_summary["restoration"].append({"path": str(path), "restored": restored})
        packet_path = stage / "fixture/packets.bin"
        if packet_path.exists() and packet_path.stat().st_size:
            try:
                packets = read_packets(packet_path)
                begins = [item for item in packets if item["type"] == BEGIN_PACKET]
                ends = [item for item in packets if item["type"] == END_PACKET]
                run_summary.update(packet_sha256=sha256(packet_path), packet_count=len(packets),
                                   full_sync_begin_count=len(begins), full_sync_end_count=len(ends),
                                   full_sync_begin_sequence=begins[0]["sequence"] if begins else None,
                                   full_sync_end_sequence=ends[-1]["sequence"] if ends else None)
            except Exception as error:
                run_summary["packet_manifest_error"] = f"{type(error).__name__}: {error}"
        export_path = stage / "fixture/export.json"
        if export_path.exists():
            run_summary["export"] = json.loads(export_path.read_text(encoding="utf-8"))
        crash_path = stage / "editor/crash.log"
        if crash_path.exists() and crash_path.stat().st_size:
            run_summary["crash_report_sha256"] = sha256(crash_path)
        run_summary["source_unchanged"] = fixture.is_file() and sha256(fixture) == run_summary.get("fixture_sha256_before")
        if source_blend:
            run_summary["original_source_unchanged"] = sha256(source_blend) == run_summary["original_source_sha256"]
        if not run_summary["source_unchanged"] or not run_summary.get("original_source_unchanged", True) or not run_summary["owned_processes_exited"] or any(not item["restored"] for item in run_summary["restoration"]):
            run_summary["error"] = run_summary.get("error", "Source/process/shared-state restoration failed")
        if any(code not in (None, 0) for code in run_summary["owned_process_exit_codes"].values()):
            run_summary["error"] = run_summary.get("error", "An owned process exited with an error")
        if "error" in run_summary:
            run_summary["status"] = "FAIL"
        (stage / "summary.json").write_text(json.dumps(run_summary, indent=2), encoding="utf-8")
        (stage / "restoration.json").write_text(json.dumps({"source_unchanged":run_summary["source_unchanged"],
            "original_source_unchanged":run_summary.get("original_source_unchanged"), "owned_processes_exited":run_summary["owned_processes_exited"],
            "shared_state":run_summary["restoration"]}, indent=2), encoding="utf-8")
        (stage / "run.json").write_text(json.dumps(run_summary, indent=2), encoding="utf-8")
        release_run_lock()
    print(json.dumps(run_summary, indent=2))
    return 0 if run_summary["status"] == "PASS" else 1


def cleanup_self_test():
    """Exercise owned-child cleanup on exception without starting Editor or Blender."""
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(600)"], stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        time.sleep(0.2)
        if child.poll() is not None:
            raise RuntimeError(f"owned test child exited before injection: {child.returncode}")
        try:
            raise RuntimeError("injected exception")
        finally:
            stop_owned(child)
    except RuntimeError as error:
        if str(error) != "injected exception":
            raise
    assert child.poll() is not None, "owned child survived exception cleanup"
    print(json.dumps({"status": "PASS", "owned_pid": child.pid, "exit_code": child.returncode,
                      "exception_cleanup": "verified"}, indent=2))
    return 0


def run_m1_suite(args):
    variants = ("hierarchy_mesh", "nested_instances", "lights", "cameras")
    suite = {"status": "PASS", "variants": [], "blender": str(args.blender), "editor": str(args.editor)}
    args.output.mkdir(parents=True, exist_ok=True)
    for variant in variants:
        variant_args = argparse.Namespace(**vars(args))
        variant_args.m1_variant = variant
        try:
            result = run(variant_args)
            latest = max((path for path in args.output.iterdir() if path.is_dir() and
                          (path / "summary.json").is_file()), key=lambda path: path.stat().st_mtime)
            summary = json.loads((latest / "summary.json").read_text(encoding="utf-8"))
            suite["variants"].append({"name": variant, "status": summary["status"],
                                       "run_id": summary["run_id"], "summary": str(latest / "summary.json"),
                                       "assertions": str(latest / "assertions.json")})
            if result or summary["status"] != "PASS":
                suite["status"] = "FAIL"
                break
        except Exception as error:
            suite["status"] = "FAIL"
            latest_dirs = [path for path in args.output.iterdir() if path.is_dir() and (path / "summary.json").is_file()]
            latest = max(latest_dirs, key=lambda path: path.stat().st_mtime) if latest_dirs else None
            suite["variants"].append({"name": variant, "status": "FAIL", "error": f"{type(error).__name__}: {error}",
                "run_id": latest.name if latest else None,
                "summary": str(latest / "summary.json") if latest else None})
            break
    suite_path = args.output / "m1_suite_summary.json"
    suite_path.write_text(json.dumps(suite, indent=2), encoding="utf-8")
    print(json.dumps(suite, indent=2))
    return 0 if suite["status"] == "PASS" else 1


def run_m2_suite(args):
    runs = []
    suite = {"status": "PASS", "runs": [], "blender": str(args.blender), "editor": str(args.editor)}
    args.output.mkdir(parents=True, exist_ok=True)
    for index in range(2):
        variant_args = argparse.Namespace(**vars(args))
        variant_args.m2_variant = "probe"
        try:
            result = run(variant_args)
            latest = max((path for path in args.output.iterdir() if path.is_dir() and
                          (path / "summary.json").is_file()), key=lambda path: path.stat().st_mtime)
            summary = json.loads((latest / "summary.json").read_text(encoding="utf-8"))
            suite["runs"].append({"index": index, "status": summary["status"], "run_id": summary["run_id"],
                                  "summary": str(latest / "summary.json"),
                                  "assertions": str(latest / "m2_assertions.json")})
            runs.append(latest)
            if result or summary["status"] != "PASS":
                suite["status"] = "FAIL"
                break
        except Exception as error:
            suite["status"] = "FAIL"
            suite["error"] = f"{type(error).__name__}: {error}"
            latest_dirs = [path for path in args.output.iterdir() if path.is_dir() and (path / "summary.json").is_file()]
            latest = max(latest_dirs, key=lambda path: path.stat().st_mtime) if latest_dirs else None
            suite["runs"].append({"index": index, "status": "FAIL", "run_id": latest.name if latest else None,
                                  "summary": str(latest / "summary.json") if latest else None})
            break
    if len(runs) == 2 and suite["status"] != "FAIL":
        comparison = subprocess.run([sys.executable, str(ROOT / "Test/BlenderLinkValidation/compare_m2_replays.py"),
                                     str(runs[0]), str(runs[1]), str(args.output)], cwd=ROOT,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        suite["replay_comparison"] = json.loads((args.output / "m2_replay_comparison.json").read_text(encoding="utf-8")) \
            if (args.output / "m2_replay_comparison.json").exists() else {"status": "FAIL", "log": comparison.stdout}
        suite["status"] = "PASS" if comparison.returncode == 0 else "FAIL"
    suite_path = args.output / "m2_suite_summary.json"
    suite_path.write_text(json.dumps(suite, indent=2), encoding="utf-8")
    print(json.dumps(suite, indent=2))
    return 0 if suite["status"] == "PASS" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blender", type=Path, default=Path(os.environ.get("BLENDER_EXE", "blender.exe")))
    parser.add_argument("--editor", type=Path, default=Path(os.environ.get("AILU_EDITOR_EXE", str(DEFAULT_EDITOR))))
    parser.add_argument("--project-template", type=Path, default=DEFAULT_TEMPLATE)
    parser.add_argument("--output", type=Path, default=DEFAULT_EDITOR.parent / "blender_link_validation")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--cleanup-self-test", action="store_true")
    parser.add_argument("--inject-failure", action="store_true", help="raise after native apply to verify cleanup")
    parser.add_argument("--fixture-kind", choices=("empty", "cube", "m1", "m2", "materials"), default="empty")
    parser.add_argument("--source-blend", type=Path, help="Read an original scene and save only an isolated materials fixture copy")
    parser.add_argument("--capture-debug-output", action="store_true", help="Capture owned Editor OutputDebugString when Info file logs are filtered")
    parser.add_argument("--m1-variant", choices=("suite", "full", "hierarchy_mesh", "nested_instances", "directional_light",
        "point_light", "spot_light", "area_light", "lights", "cameras", "perspective_camera",
        "orthographic_camera"), default="suite")
    parser.add_argument("--m2-variant", choices=("probe", "suite"), default="probe")
    parser.add_argument("--startup-timeout", type=int, default=120)
    parser.add_argument("--oracle-timeout", type=int, default=120)
    parser.add_argument("--timeout", type=int, default=420)
    args = parser.parse_args()
    if args.cleanup_self_test:
        return cleanup_self_test()
    try:
        if args.fixture_kind == "m1" and args.m1_variant == "suite":
            return run_m1_suite(args)
        if args.fixture_kind == "m2" and args.m2_variant == "suite":
            return run_m2_suite(args)
        return run(args)
    except Exception as error:
        print(f"FAIL: {type(error).__name__}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
