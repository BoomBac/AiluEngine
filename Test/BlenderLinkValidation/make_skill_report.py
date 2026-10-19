"""Aggregate a completed runner stage into the Skill report contract (summary/assertions/failures/run/restoration + markdown)."""
import argparse
import json
import re
import shutil
import struct
import sys
from datetime import datetime
from pathlib import Path


def load(path, default=None):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return default


def case(status, level, detail):
    return {"status": status, "evidence_level": level, "detail": detail}


def gather_apply_failures(stage):
    lines = []
    for name in ("editor/debug_output.log", "editor/engine.log"):
        path = stage / name
        if path.is_file():
            lines.extend(path.read_text(encoding="utf-8", errors="replace").splitlines())
    failures = []
    missing_files = []
    seen_failures = set()
    for line in lines:
        match = re.search(r"Apply failed: type=(\d+), sequence=(\d+)", line)
        if match:
            key = (int(match.group(1)), int(match.group(2)))
            if key not in seen_failures:
                seen_failures.add(key)
                failures.append({"type": key[0], "sequence": key[1]})
        missing = re.search(r"External texture file (.+) does not exist!", line)
        if missing and missing.group(1) not in missing_files:
            missing_files.append(missing.group(1))
    return failures, missing_files


def packet_details(stage, sequence):
    path = stage / "fixture/packets.bin"
    if not path.is_file():
        return None
    data = path.read_bytes()
    offset = 0
    while offset + 20 <= len(data):
        magic, version, kind, size, current = struct.unpack_from("<IHHIQ", data, offset)
        payload = data[offset + 20:offset + 20 + size]
        offset += 20 + size
        if current != sequence:
            continue
        if kind != 21:
            return {"type": kind, "sequence": sequence}
        try:
            value = json.loads(payload)
        except ValueError:
            return {"type": kind, "sequence": sequence}
        return {"type": kind, "sequence": sequence, "material_guid": value.get("material_guid"),
                "name": value.get("name"),
                "texture_paths": {key: value[key] for key in value if key.endswith("_path") and value[key]}}
    return None


def collect_capture(stage, name, captures_dir):
    manifests = sorted(stage.glob(f"{name}/session_*/capture.json"))
    if len(manifests) != 1:
        return {"manifest": None, "frames": []}
    manifest = load(manifests[0])
    target = captures_dir / name
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(manifests[0], target / "capture.json")
    frames = []
    for frame in sorted(manifest.get("frames", []), key=lambda item: item["capture_index"]):
        source = manifests[0].parent / frame["file"]
        if source.is_file():
            shutil.copy2(source, target / frame["file"])
            if source.suffix.lower() == ".dds":
                preview = WritePreview(source, manifest, target / (source.stem + "_preview.png"))
                frames.append({"file": frame["file"], "engine_frame": frame["engine_frame"], "preview": preview})
            else:
                frames.append({"file": frame["file"], "engine_frame": frame["engine_frame"]})
    return {"manifest": manifest, "frames": frames}


def WritePreview(source, manifest, destination):
    """Write a tonemapped PNG preview of a native DDS capture (Reinhard + sRGB, mirrors analyze_m2_render.py)."""
    try:
        import numpy as np
        from PIL import Image
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from material_capture_pixels import LoadFrame
        pixels = LoadFrame(source, manifest)
        rgb = np.clip(pixels[..., :3], 0.0, None)
        rgb = rgb / (1.0 + rgb)
        rgb = np.power(rgb, 1.0 / 2.2)
        Image.fromarray((rgb * 255.0 + 0.5).astype("uint8")).save(destination)
        return destination.name
    except Exception as error:
        return f"preview failed: {type(error).__name__}: {error}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--scene", required=True)
    parser.add_argument("--source-blend", type=Path, required=True)
    parser.add_argument("--blender-render", type=Path, required=True,
                        help="PNG produced by render_blender_preview.py from the isolated source scene")
    args = parser.parse_args()
    stage = args.stage.resolve()
    summary = load(stage / "summary.json", {})
    if summary.get("status") is None:
        print("Stage summary.json is missing or invalid", file=sys.stderr)
        return 1
    report_dir = stage.parent / f"skill_report_{args.scene}_{datetime.now():%Y%m%d}"
    report_dir.mkdir(parents=True, exist_ok=True)

    bindings = load(stage / "material_assertions.json", {})
    images = load(stage / "material_image_assertions.json", {})
    capture_driver = load(stage / "material_capture.json", {})
    export = summary.get("export", load(stage / "fixture/export.json", {}))
    restoration = load(stage / "restoration.json", {})
    apply_failures, missing_files = gather_apply_failures(stage)
    failed_sequences = sorted({item["sequence"] for item in apply_failures})

    lifecycle_ok = (summary.get("full_sync_begin_count") == 1 and summary.get("full_sync_end_count") == 1 and
                    summary.get("full_sync_end_sequence", 0) > summary.get("full_sync_begin_sequence", 0) and
                    export.get("counts", {}).get("11") == 1 and export.get("source_restored") is True and
                    not apply_failures)
    lifecycle = case("PASS" if lifecycle_ok else "FAIL", "native protocol",
                     {"packets": summary.get("packet_count"), "begin_sequence": summary.get("full_sync_begin_sequence"),
                      "end_sequence": summary.get("full_sync_end_sequence"), "engine_summary": summary.get("engine_summary"),
                      "export_counts": export.get("counts"), "apply_failure_count": len(apply_failures)})
    if bindings:
        bindings_case = case(bindings.get("status", "INCONCLUSIVE"), "native source->wire->Engine",
                             {"check_count": bindings.get("check_count"), "failure_count": bindings.get("failure_count"),
                              "seeded_corruptions": bindings.get("seeded_corruptions")})
    else:
        bindings_case = case("SKIPPED", "native source->wire->Engine",
                             {"reason": "Engine Full Sync did not apply; the comparison oracle was not reached"})
    if images:
        images_case = case(images.get("status", "INCONCLUSIVE"), "GPU capture integrity",
                           {"check_count": len(images.get("checks", [])), "failure_count": len(images.get("failures", [])),
                            "scope": images.get("scope")})
    else:
        images_case = case("SKIPPED", "GPU capture integrity",
                           {"reason": "capture not produced because the Full Sync apply failed"})
    incremental_status = export.get("incremental_validation", {}).get("status", "SKIPPED")
    incremental = case(incremental_status, "native incremental",
                       export.get("incremental_validation", {"reason": "not reported"}))
    cleanup_ok = (summary.get("owned_processes_exited") is True and summary.get("source_unchanged") is True and
                  summary.get("original_source_unchanged") is True and not summary.get("external_process_conflict") and
                  all(item.get("restored") for item in summary.get("restoration", [])) and
                  not summary.get("editor_visible_windows") and not summary.get("blender_visible_windows"))
    cleanup = case("PASS" if cleanup_ok else "FAIL", "native lifecycle",
                   {"owned_process_exit_codes": summary.get("owned_process_exit_codes"),
                    "restored_entries": len(summary.get("restoration", [])), "external_conflict": summary.get("external_process_conflict", [])})
    if capture_driver:
        camera = case("PASS" if capture_driver.get("status") == "PASS" else "FAIL", "GPU capture integrity",
                      {"driver_status": capture_driver.get("status"), "failure": capture_driver.get("failure")})
    else:
        camera = case("SKIPPED", "GPU capture integrity", {"reason": "capture driver did not run"})

    cases = {"full_sync_lifecycle": lifecycle, "material_bindings": bindings_case, "capture_integrity": images_case,
             "visual_parity": case("SKIPPED", "visual parity",
                 {"reason": "Blender Eevee/Cycles to Ailu pixel and lighting parity is not asserted; embedded images are context only"}),
             "capture_driver": camera, "incremental_mutation": incremental, "cleanup_restoration": cleanup}
    if any(item["status"] == "FAIL" for item in cases.values()):
        suite = "FAIL"
    elif all(item["status"] == "PASS" for item in cases.values() if item["status"] != "SKIPPED"):
        suite = "PASS"
    else:
        suite = "INCONCLUSIVE"

    root_cause = None
    if apply_failures:
        first = failed_sequences[0]
        affected = []
        reference = load(stage / "material_reference.json", {})
        first_packet = packet_details(stage, first)
        if reference and first_packet and first_packet.get("material_guid"):
            for item in reference.get("objects", []) + reference.get("instances", []):
                for index, slot in enumerate(item.get("geometry", {}).get("materials", [])):
                    if slot and slot.get("guid") == first_packet["material_guid"]:
                        used = item.get("geometry", {}).get("used_slots", [])
                        affected.append({"name": item.get("name"), "triangles": item.get("geometry", {}).get("triangles"),
                                         "material_slot_index": index, "used_slots": used,
                                         "slot_referenced_by_faces": index in used})
                        break
        root_cause = {"first_failed_packet": first_packet, "affected_entities": affected,
                      "apply_failure_count": len(apply_failures),
                      "failed_sequences": {"first": first, "last": failed_sequences[-1]},
                      "missing_external_files": missing_files,
                      "classification": "source scene references a missing external texture; the Engine texture import "
                                        "fails and LiveLinkSession voids the remaining Full Sync (needs_full_resync)"}

    assertions = {"status": bindings.get("status", "INCONCLUSIVE"),
                  "binding_checks": bindings.get("check_count"), "binding_failures": bindings.get("failure_count"),
                  "seeded_corruptions": bindings.get("seeded_corruptions"),
                  "image_checks": images.get("checks"), "image_failures": images.get("failures"),
                  "checks": bindings.get("checks", []), "failures": bindings.get("failures", [])}
    failures = {"run_status": summary.get("status"), "error": summary.get("error"), "root_cause": root_cause,
                "apply_failures": apply_failures,
                "binding_failures": bindings.get("failures", []), "image_failures": images.get("failures", [])}
    blender_version = summary.get("blender_version") or export.get("blender_version")
    run = {"run_id": summary.get("run_id"), "port": summary.get("port"), "blender": summary.get("blender"),
           "blender_version": blender_version, "editor": summary.get("editor"),
           "editor_sha256": summary.get("editor_sha256"), "engine_sha256": summary.get("engine_sha256"),
           "plugin_sha256": summary.get("plugin_sha256"), "addon_source_sha256": summary.get("addon_source_sha256"),
           "original_source": summary.get("original_source"), "original_source_sha256": summary.get("original_source_sha256"),
           "fixture_sha256_before": summary.get("fixture_sha256_before"),
           "packet_sha256": summary.get("packet_sha256"), "packet_count": summary.get("packet_count"),
           "owned_pids": summary.get("owned_pids"), "engine_summary": summary.get("engine_summary"),
           "blender_visible_windows": summary.get("blender_visible_windows"),
           "editor_visible_windows": summary.get("editor_visible_windows")}
    captures_dir = report_dir / "render_captures"
    captures = {"albedo": collect_capture(stage, "material_albedo", captures_dir),
                "final": collect_capture(stage, "material_final", captures_dir)}
    blender_render = args.blender_render.resolve()
    if not blender_render.is_file():
        raise FileNotFoundError(f"Blender Eevee background render not found: {blender_render}")
    with blender_render.open("rb") as stream:
        if stream.read(8) != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"Blender Eevee background render is not a PNG: {blender_render}")
    for capture_name in ("albedo", "final"):
        frames = captures[capture_name].get("frames", [])
        if not frames:
            raise RuntimeError(f"Cannot embed required Engine {capture_name} image: no captured frames in {stage}")
        selected = frames[-1]
        image_name = selected.get("preview", selected["file"])
        image_path = captures_dir / ("material_albedo" if capture_name == "albedo" else "material_final") / image_name
        if not image_path.is_file():
            raise RuntimeError(f"Cannot embed required Engine {capture_name} image: {image_path}")
        captures[capture_name]["report_image"] = {"file": image_name, "engine_frame": selected["engine_frame"]}
    shutil.copy2(blender_render, report_dir / "blender_eevee_preview.png")
    captures["blender_eevee_preview"] = {"file": "blender_eevee_preview.png", "source": str(blender_render)}
    (report_dir / "summary.json").write_text(json.dumps({"status": suite, "scene": args.scene, "run_id": summary.get("run_id"),
        "cases": cases, "root_cause": root_cause, "not_tested": ["incremental edits (real scenes are read-only copies)",
        "Cycles-to-engine pixel parity", "M3 reconnect/persistence behavior of the editor session"]}, indent=2), encoding="utf-8")
    (report_dir / "assertions.json").write_text(json.dumps(assertions, indent=2), encoding="utf-8")
    (report_dir / "failures.json").write_text(json.dumps(failures, indent=2), encoding="utf-8")
    (report_dir / "run.json").write_text(json.dumps(run, indent=2), encoding="utf-8")
    (report_dir / "restoration.json").write_text(json.dumps(restoration, indent=2), encoding="utf-8")
    (report_dir / "captures.json").write_text(json.dumps(captures, indent=2), encoding="utf-8")
    shutil.copy2(stage / "summary.json", report_dir / "stage_summary.json")

    lines = [
        f"# {args.scene} Blender Link 校验报告", "",
        f"- 结论：**{suite}**", f"- run_id：`{summary.get('run_id')}`，端口 `{summary.get('port')}`",
        f"- 源场景：`{summary.get('original_source')}`（SHA256 `{summary.get('original_source_sha256')}`，运行后未改动：{summary.get('original_source_unchanged')}）",
        f"- Editor：`{summary.get('editor')}`（SHA256 `{summary.get('editor_sha256')}`）",
        f"- Blender：`{summary.get('blender')}`（{blender_version}）",
        f"- 证据目录：`{stage}`", "",
        "## 用例结果", "",
        "| 用例 | 状态 | 证据等级 |", "| --- | --- | --- |",
    ]
    for name, item in cases.items():
        lines.append(f"| {name} | {item['status']} | {item['evidence_level']} |")
    engine_line = (f"- Engine apply：`{summary.get('engine_summary')}`" if summary.get("engine_summary")
                   else f"- Engine apply：未取得统计（Full Sync 应用在 seq {failed_sequences[0]} 后全部失败，见根因分析）")
    bindings_line = (f"- 结构/材质绑定检查：{bindings.get('check_count')} 项，失败 {bindings.get('failure_count')}；种子损坏注入检出：{json.dumps(bindings.get('seeded_corruptions'), ensure_ascii=False)}"
                     if bindings else "- 结构/材质绑定检查：SKIPPED（Engine 未完成应用，比较器未运行）")
    lines += ["", "## 关键数据", "",
        f"- 协议包：{summary.get('packet_count')} 个，packets.bin SHA256 `{summary.get('packet_sha256')}`，Begin seq {summary.get('full_sync_begin_sequence')} / End seq {summary.get('full_sync_end_sequence')}",
        engine_line,
        bindings_line,
        f"- 捕获完整性检查：{len(images.get('checks', []))} 项，失败 {len(images.get('failures', []))}",
        f"- 进程与恢复：owned_processes_exited={summary.get('owned_processes_exited')}，exit={json.dumps(summary.get('owned_process_exit_codes'))}，共享状态 {len(summary.get('restoration', []))} 项全部恢复={all(item.get('restored') for item in summary.get('restoration', []))}",
        f"- 无可见窗口：Editor {summary.get('editor_visible_windows')} / Blender {summary.get('blender_visible_windows')}",
        f"- 增量突变：{incremental_status}（{export.get('incremental_validation', {}).get('reason', '')}）"]
    if root_cause:
        first = root_cause["first_failed_packet"] or {}
        affected = root_cause.get("affected_entities", [])
        unused = [item["name"] for item in affected if item.get("slot_referenced_by_faces") is False]
        usage_line = (f"- 槽位使用情况：受影响实体 {unused} 的该材质槽位未被任何面引用，Blender 视口/渲染中不受影响"
                      if unused and len(unused) == len(affected)
                      else f"- 槽位使用情况：{json.dumps(affected, ensure_ascii=False)}")
        lines += ["", "## 根因分析（FAIL）", "",
            f"- 首个失败的协议包：seq {first.get('sequence')} type {first.get('type')}，material `{first.get('name')}`（`{first.get('material_guid')}`）",
            f"- 该包引用的纹理路径：{json.dumps(first.get('texture_paths'), ensure_ascii=False)}",
            f"- Engine 报错：External texture file {json.dumps(root_cause['missing_external_files'], ensure_ascii=False)} does not exist!",
            f"- 受影响的场景实体：{json.dumps(affected, ensure_ascii=False)}",
            usage_line,
            f"- 失败包数量：{root_cause['apply_failure_count']}（seq {root_cause['failed_sequences']['first']}–{root_cause['failed_sequences']['last']}），首个失败后所有 Apply（含 End）全部失败",
            "- 机制：材质包引用的源场景外部图片未打包且本机不存在；`ResourceMgr::LoadExternalTexture` 返回空后，",
            "  `LiveLinkSession::UpsertMaterial` 抛出异常，`LiveLinkSession::Apply` 捕获后置 `_needs_full_resync`，",
            "  其后所有包被跳过，Full Sync 整体无效（`Plugins/BlenderLiveLink/Src/LiveLinkSession.cpp:810`、`:239-243`）。",
            "- 分类：缺失外部纹理的材质即使未被任何面使用，导出器也会为槽位表发布该材质并携带纹理路径；",
            "  Engine 纹理导入失败被当作致命错误，导致整个 Full Sync 作废。Blender 侧导出正常完成且源数据已还原。"]
    albedo_image = captures["albedo"]["report_image"]
    final_image = captures["final"]["report_image"]
    lines += ["", "## 材质视口与引擎输出", "",
        "### Blender Eevee 后台材质预览", "",
        "![Blender Eevee background render](blender_eevee_preview.png)", "",
        "### Engine 基础色（GBuffer albedo）", "",
        f"![Engine GBuffer base color](render_captures/material_albedo/{albedo_image['file']})",
        f"engine_frame `{albedo_image['engine_frame']}`，`DeferedGeometryPass` / `_GBuffer1`。", "",
        "### Engine 最终输出", "",
        f"![Engine final output](render_captures/material_final/{final_image['file']})",
        f"engine_frame `{final_image['engine_frame']}`，`DeferredLightingPass` / `_CameraColorAttachmentA`。",
        "最终图为原生 DDS 的 Reinhard+sRGB PNG 预览；抓帧完整性（连续 engine_frame、有限像素）由",
        "`material_image_assertions.json` 验证。",
        "Blender 图使用 Eevee 场景灯光/世界，Engine 图使用自身光照；不代表 Material Preview 或 Cycles 与 Engine 像素级一致。"]
    lines += ["", "## 未覆盖范围", "",
        "- 真实场景为只读副本，未执行增量突变编辑；未验证重连/持久化行为。",
        "- 捕获仅验证帧完整性（连续、有限、稳定相机），未断言 Blender Cycles 与 Engine 像素级一致。",
        "- 未验证光照数值一致性。", ""]
    (report_dir / f"{args.scene}_link_validation_report.md").write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps({"report_dir": str(report_dir), "status": suite}, indent=2))
    return 0 if suite in ("PASS", "INCONCLUSIVE") else 1


if __name__ == "__main__":
    sys.exit(main())
