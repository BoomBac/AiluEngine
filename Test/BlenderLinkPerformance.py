"""Fresh-process native export and exact-packet replay benchmark; restores Editor state in finally."""
import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True)
parser.add_argument("--stage", required=True)
parser.add_argument("--replays", type=int, default=2)
parser.add_argument("--replay-stage")
parser.add_argument("--skip-native", action="store_true")
parser.add_argument("--disable-mesh-cache", action="store_true")
parser.add_argument("--installed-addon", action="store_true")
parser.add_argument("--bake-reference")
parser.add_argument("--material-reference")
parser.add_argument("--full-sync-budget-ms", type=int, choices=[4, 16, 32, 64])
parser.add_argument("--scene", choices=["bmw", "classroom", "both"], default="both")
args = parser.parse_args()
output = Path(args.output)
editor = Path("F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe")
blender = Path("D:/blender5.0/blender.exe")
project = Path("C:/Users/BoomBac/OneDrive/AiluTestProj/empty.json")
if output.parent != editor.parent:
    raise RuntimeError("Evidence must be beside the selected Editor.exe")
stage = output / args.stage
stage.mkdir(parents=True, exist_ok=False)
backup_dir = stage / "backups"
backup_dir.mkdir()
state_paths = [root / p for p in ["Editor/EditorConfig.json", "Editor/EngineConfig.json",
    "Editor/Res/UI/main_widget.json", "Editor/dock_layout.json", "FileAppender_log.txt", "imgui.ini",
    "render_graph.dot", "render_graph_timeline.dot"]] + [project.parent / ".ailu/editor_session.json"]
state = []
for index, source in enumerate(state_paths):
    saved = backup_dir / str(index)
    existed = source.exists()
    if existed:
        shutil.copy2(source, saved)
    state.append((source, saved, existed))

def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def restore_file(saved, source):
    deadline = time.monotonic() + 15
    while True:
        try:
            shutil.copy2(saved, source)
            return
        except OSError as error:
            if getattr(error, "winerror", None) != 1224 or time.monotonic() >= deadline:
                raise OSError(f"State restoration failed for {source}: {error}") from error
            time.sleep(0.1)

def start_process(command, stdout, stderr, env=None):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    return subprocess.Popen(command, cwd=root, stdout=stdout, stderr=stderr,
        env=env, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)

def stop_editor(process):
    if process is None or process.poll() is not None:
        return
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def close_window(window, _):
        owner = ctypes.c_ulong()
        ctypes.windll.user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == process.pid:
            ctypes.windll.user32.PostMessageW(window, 0x0010, 0, 0)
        return True
    callback = callback_type(close_window)
    ctypes.windll.user32.EnumWindows(callback, 0)
    try:
        process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=10)

def wait_log(pattern, process, timeout=360, blender_process=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Editor exited early: {process.returncode}")
        if blender_process is not None and blender_process.poll() is not None:
            raise RuntimeError(f"Blender exited before FullSync import: {blender_process.returncode}")
        log = root / "FileAppender_log.txt"
        if log.exists():
            text = log.read_text(encoding="utf-8", errors="replace")
            found = re.search(pattern, text)
            if found:
                return found, text
        time.sleep(0.1)
    raise TimeoutError(pattern)

def replay(stream):
    peer = socket.create_connection(("127.0.0.1", 17779), timeout=180)
    hello = struct.pack("<IHHIQ", 0x4B4C4C41, 1, 1, 2, 1) + b"{}"
    peer.sendall(hello)
    response = peer.recv(4096)
    if len(response) < 20 or struct.unpack_from("<H", response, 6)[0] != 2:
        raise RuntimeError("Replay handshake failed")
    with stream.open("rb") as incoming:
        while data := incoming.read(1024 * 1024):
            peer.sendall(data)
    return peer

def parse_metrics(text):
    summary = re.search(r"FullSync import: ([^\r\n]+)", text).group(1)
    result = {key: float(value) for key, value in re.findall(r"(\w+_ms)=([\d.]+)", summary)}
    result["by_type"] = {kind: {"count": int(count), "bytes": int(size), "apply_ms": float(cost)}
        for kind, count, size, cost in re.findall(r"type(\d+)=(\d+)/(\d+)B/([\d.]+)ms", summary)}
    costs = [float(value) for value in re.findall(
        r"Time cost ([\d.]+)ms : Mesh::Generate Bounds and BVH\(BlenderLiveLink_", text)]
    result.update(bvh_count=len(costs), bvh_ms=sum(costs), bvh_max_ms=max(costs, default=0),
        apply_failures=len(re.findall(r"BlenderLiveLink\] Apply failed", text)),
        queue_overflow="Incoming queue limit reached" in text)
    return result

scenes = {"bmw": "I:/Downloads/BMW27.blend/BMW27.blend",
    "classroom": "I:/Downloads/classroom/classroom/classroom.blend"}
manifest = {"editor": str(editor), "editor_sha256": file_hash(editor),
    "engine_sha256": file_hash(editor.parent / "Engine_d.dll"),
    "plugin_sha256": file_hash(root / "Plugins/BlenderLiveLink/bin/debug/AiluBlenderLiveLink.dll"),
    "mesh_cache_enabled": not args.disable_mesh_cache,
    "full_sync_budget_ms": args.full_sync_budget_ms,
    "blender_factory_startup": True,
    "bake_experiment": {name: os.environ.get(name, "") for name in (
        "AILU_BLENDER_BAKE_REUSE_SCENE", "AILU_BLENDER_BAKE_DEVICE", "AILU_BLENDER_BAKE_PNG_PROCESS",
        "AILU_BLENDER_BAKE_CONTEXT_OVERRIDE_ONLY", "AILU_BLENDER_BAKE_REUSE_DEPSGRAPH")},
    "configuration": "Debug, single render thread, original graphics/debug flags, AA disabled for captures",
    "source_hashes": {name: file_hash(Path(path)) for name, path in scenes.items()}, "runs": []}
try:
    config_path = root / "Editor/EngineConfig.json"
    config = json.loads(config_path.read_text(encoding="utf-8-sig"))
    config.update(isMultiThreadRender=False, enable_graphics_job=False)
    config_path.write_text(json.dumps(config, indent=4), encoding="utf-8")
    for name, scene_path in scenes.items():
        if args.scene != "both" and name != args.scene:
            continue
        for trial in range(1 if args.skip_native else 0, args.replays + 1):
            run = stage / f"{name}_{trial}"
            run.mkdir()
            editor_process = blender_process = peer = None
            env = dict(os.environ, AILU_BLENDER_LIVE_LINK_PORT="17779")
            if args.full_sync_budget_ms is not None:
                env["AILU_BLENDER_LIVE_LINK_FULL_SYNC_BUDGET_MS"] = str(args.full_sync_budget_ms)
            if args.installed_addon:
                env["AILU_BLENDER_PERF_INSTALLED"] = "1"
            if args.bake_reference:
                env["AILU_BLENDER_PERF_BAKE_REFERENCE"] = str(Path(args.bake_reference).resolve())
            if args.material_reference:
                env["AILU_BLENDER_PERF_MATERIAL_REFERENCE"] = str(Path(args.material_reference).resolve())
            if args.disable_mesh_cache:
                env["AILU_BLENDER_LIVE_LINK_DISABLE_MESH_CACHE"] = "1"
            with ((run / "editor_stdout.log").open("w") as editor_out,
                  (run / "editor_stderr.log").open("w") as editor_err,
                  (run / "blender.log").open("w") as blender_out,
                  (run / "blender_error.log").open("w") as blender_err):
                try:
                    (root / "FileAppender_log.txt").write_bytes(b"")
                    editor_process = start_process([str(editor), str(project)], editor_out, editor_err, env)
                    wait_log(r"Server listening on 127\.0\.0\.1:17779", editor_process, 120)
                    print("START", args.stage, name, trial, "editor_pid", editor_process.pid, flush=True)
                    started = time.perf_counter()
                    if trial == 0:
                        blender_process = start_process([str(blender), "--background", "--factory-startup", scene_path,
                            "--python-exit-code", "1",
                            "--python", str(root / "Test/BlenderLinkPerformanceExport.py"), "--", str(run)],
                            blender_out, blender_err, env)
                    else:
                        source = (output / args.replay_stage if args.replay_stage else stage) / f"{name}_0"
                        peer = replay(source / "packets.bin")
                        shutil.copy2(source / "camera.json", run / "camera.json")
                    _, log_text = wait_log(r"FullSync import:", editor_process, 420, blender_process)
                    metrics = parse_metrics(log_text)
                    metrics.update(scene=name, trial=trial, mode="native" if trial == 0 else "replay",
                        send_to_apply_end_ms=(time.perf_counter() - started) * 1000, editor_pid=editor_process.pid)
                    if trial:
                        metrics["packet_sha256"] = file_hash(source / "packets.bin")
                    if metrics["apply_failures"] or metrics["queue_overflow"]:
                        raise RuntimeError("Import failed: " + json.dumps(metrics))
                    if trial == 0:
                        deadline = time.monotonic() + 20
                        while not (run / "export.json").exists() and time.monotonic() < deadline:
                            time.sleep(0.1)
                        metrics["export"] = json.loads((run / "export.json").read_text())
                    if trial == 0 or (trial == 1 and args.replay_stage):
                        with (run / "capture.log").open("w") as capture_out:
                            capture = start_process(["node", str(root / "Test/BlenderLinkPerformanceCapture.mjs"),
                                str(project.parent), str(run)], capture_out, subprocess.STDOUT)
                            capture.wait(timeout=120)
                            if capture.returncode:
                                raise RuntimeError("Capture driver failed; see capture.log")
                    (run / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
                    manifest["runs"].append(metrics)
                    print("DONE", name, trial, json.dumps(metrics), flush=True)
                finally:
                    if blender_process:
                        (run / "release").touch()
                        try:
                            blender_process.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            blender_process.terminate()
                            blender_process.wait(timeout=10)
                    if peer:
                        peer.close()
                    stop_editor(editor_process)
                    shutil.copy2(root / "FileAppender_log.txt", run / "engine.log")
finally:
    restoration = []
    for source, saved, existed in state:
        if existed:
            restore_file(saved, source)
        elif source.exists():
            source.unlink()
        restored = file_hash(source) == file_hash(saved) if existed else not source.exists()
        restoration.append({"path": str(source), "restored": restored})
    manifest["restoration"] = restoration
    manifest["source_unchanged"] = {
        name: file_hash(Path(path)) == manifest["source_hashes"][name] for name, path in scenes.items()}
    (stage / "summary.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
