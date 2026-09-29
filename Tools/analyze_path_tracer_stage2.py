import argparse
import json
import re
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from analyze_path_tracer_stage1 import DisplayPixels, ReadDds, Rms


def DecodeLength(rgb):
    return np.rint(np.where(rgb[..., 0] > 0, 32 + 32 * rgb[..., 0], 32 * rgb[..., 1])).astype(np.int32)


def Analyze(root, capture):
    directory = Path(capture["status"]["output_directory"])
    manifest = json.loads((directory / "capture.json").read_text(encoding="utf-8"))
    frames = sorted(manifest["frames"], key=lambda frame: frame["capture_index"])
    label = capture["label"]
    engine_frames = [frame["engine_frame"] for frame in frames]
    result = {"state": manifest["state"], "requested": manifest["frame_count"], "actual": len(frames),
              "dimensions": [manifest["width"], manifest["height"]], "engine_frames": engine_frames,
              "capture_indices": [frame["capture_index"] for frame in frames],
              "engine_frame_gaps": [b - a for a, b in zip(engine_frames, engine_frames[1:]) if b - a != 1],
              "elapsed_ms": capture["elapsed_ms"], "capture_rate_fps": len(frames) * 1000 / capture["elapsed_ms"],
              "preview_files": []}
    if manifest["output_format"] != "dds_native":
        result["preview_files"] = [str((directory / frame["file"]).relative_to(root)).replace("\\", "/")
                                   for frame in frames]
        return result
    debug = "validity" in label or "length" in label
    previews = root / "previews" / label
    previews.mkdir(parents=True, exist_ok=True)
    result.update({"nonfinite_rgba_values": 0, "rgb_min": 1e30, "rgb_max": -1e30, "black_frames": []})
    roi_differences = {name: [] for name in ["inner", "center"]}
    roi_radiance_differences = {name: [] for name in roi_differences}
    previous = None
    reset_fractions = []
    accepted_fractions = []
    central_lengths = []
    exact_lengths = []
    for index, frame in enumerate(frames):
        rgba = ReadDds(directory / frame["file"])
        rgb = rgba[..., :3]
        result["nonfinite_rgba_values"] += int((~np.isfinite(rgba)).sum())
        result["rgb_min"] = min(result["rgb_min"], float(np.nanmin(rgb)))
        result["rgb_max"] = max(result["rgb_max"], float(np.nanmax(rgb)))
        if np.nanmax(rgb) < 1e-7:
            result["black_frames"].append(index)
        display = np.rint(np.clip(rgb, 0, 1) * 255).astype(np.uint8) if debug else DisplayPixels(rgba)
        preview = previews / f"{index:04}.png"
        Image.fromarray(display).save(preview)
        result["preview_files"].append(str(preview.relative_to(root)).replace("\\", "/"))
        if "validity" in label:
            accepted_fractions.append(float(np.mean(rgb[..., 1] > 0.9)))
        if "length" in label:
            lengths = DecodeLength(rgb)
            geometry = lengths > 0
            reset_fractions.append(float(np.sum(lengths == 1) / max(np.sum(geometry), 1)))
            central_lengths.append(int(lengths[100, 200]))
            if "length_start" in label:
                exact_lengths.append(float(np.mean(lengths[geometry] == index + 1)))
        if previous is not None and not debug:
            height, width = rgb.shape[:2]
            for name, rect in {"inner": (0.1, 0.1, 0.9, 0.9), "center": (0.4, 0.3, 0.7, 0.8)}.items():
                x0, y0, x1, y1 = rect
                roi = np.s_[int(y0 * height):int(y1 * height), int(x0 * width):int(x1 * width)]
                roi_differences[name].append((display[roi].astype(float) - previous[0][roi]).reshape(-1))
                roi_radiance_differences[name].append((rgb[roi] - previous[1][roi]).reshape(-1))
        previous = (display.astype(float), rgb.copy())
    if accepted_fractions:
        result["accepted_fraction_per_frame"] = accepted_fractions
    if central_lengths:
        result["center_history_lengths"] = central_lengths
        result["geometry_reset_fraction_per_frame"] = reset_fractions
        if exact_lengths:
            result["geometry_exact_length_fraction_per_frame"] = exact_lengths
    result["regions"] = {}
    for name, values in roi_differences.items():
        if not values:
            continue
        differences = np.stack(values)
        radiance = np.stack(roi_radiance_differences[name])
        window = min(8, len(values))
        rms_per_pair = np.sqrt(np.mean(differences * differences, axis=1))
        result["regions"][name] = {"early_display_rms": Rms(differences[:window]),
            "late_display_rms": Rms(differences[-window:]),
            "early_radiance_rms": Rms(radiance[:window]), "late_radiance_rms": Rms(radiance[-window:]),
            "late_display_p99": float(np.percentile(np.abs(differences[-window:]), 99)),
            "worst_pair_index": int(np.argmax(rms_per_pair)), "worst_pair_rms": float(np.max(rms_per_pair)),
            "rms_per_pair": rms_per_pair.tolist()}
    return result


def MakeSheet(root, sequences, labels, suffix):
    columns = []
    for label in labels:
        sequence = sequences.get(label)
        if not sequence:
            continue
        indices = sorted(set([0, min(7, sequence["actual"] - 1), sequence["actual"] // 2, sequence["actual"] - 1]))
        for index in indices:
            columns.append((f"{label}: {index + 1}", root / sequence["preview_files"][index]))
    if not columns:
        return
    width, height = Image.open(columns[0][1]).size
    canvas = Image.new("RGB", (width * 4, (height + 28) * ((len(columns) + 3) // 4)), (24, 24, 24))
    draw = ImageDraw.Draw(canvas)
    for column, (title, file_path) in enumerate(columns):
        x, y = column % 4 * width, column // 4 * (height + 28)
        canvas.paste(Image.open(file_path).convert("RGB"), (x, y + 28))
        draw.text((x + 5, y + 5), title, fill="white")
    canvas.save(root / suffix)


def AnalyzeRegisteredWall(root, validation):
    camera = validation["original_camera"]
    x, y, z, w = camera["rotation"]
    rotation = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    xx, yy = np.meshgrid(np.linspace(0.3, 1.0, 24), np.linspace(1.4, 2.2, 24))
    points = np.stack([xx.ravel(), yy.ravel(), np.full(xx.size, -2.2232)], axis=1)
    result = {"world_patch": {"x": [0.3, 1.0], "y": [1.4, 2.2], "z": -2.2232},
              "method": ("Project fixed wall samples with recorded camera quaternion and positions; "
                         "interpolate pose by completed capture count. Pose association is approximate, "
                         "within RPC/render latency."),
              "projection": "Use actual engine PerspectiveFovLH vertical FOV formula, despite FovH property name",
              "sequences": {}}
    for capture in validation["captures"]:
        suffixes = ["motion_raw", "motion_resolved", "temporal_start", "_raw"]
        if not any(capture["label"].endswith(suffix) for suffix in suffixes):
            continue
        if capture.get("status", {}).get("state") != "completed":
            continue
        directory = Path(capture["status"]["output_directory"])
        manifest = json.loads((directory / "capture.json").read_text(encoding="utf-8"))
        frames = sorted(manifest["frames"], key=lambda frame: frame["capture_index"])
        width, height = manifest["width"], manifest["height"]
        focal = height / (2 * np.tan(np.deg2rad(camera["fov_degrees"]) / 2))
        anchors = [(0, camera["position"])]
        anchors.extend((max(0, obs["completed_frames"] - 1), obs["observed"]["position"])
                       for obs in capture.get("observations", []))
        anchors.append((len(frames) - 1, camera["position"]))
        anchors = sorted({index: position for index, position in anchors}.items())
        poses = np.stack([np.interp(np.arange(len(frames)), [anchor[0] for anchor in anchors],
                                   [anchor[1][axis] for anchor in anchors]) for axis in range(3)], axis=1)
        values = []
        for index, frame in enumerate(frames):
            local = (points - poses[index]) @ rotation
            px = width / 2 + focal * local[:, 0] / local[:, 2] - 0.5
            py = height / 2 - focal * local[:, 1] / local[:, 2] - 0.5
            x0, y0 = np.floor(px).astype(int), np.floor(py).astype(int)
            wx, wy = (px - x0)[:, None], (py - y0)[:, None]
            display = DisplayPixels(ReadDds(directory / frame["file"])).astype(float)
            sample = (display[y0, x0] * (1 - wx) + display[y0, x0 + 1] * wx) * (1 - wy)
            sample += (display[y0 + 1, x0] * (1 - wx) + display[y0 + 1, x0 + 1] * wx) * wy
            values.append(sample)
            if index == 0 and capture["label"].endswith("motion_resolved"):
                overlay = Image.fromarray(display.astype(np.uint8))
                draw = ImageDraw.Draw(overlay)
                draw.rectangle((float(px.min()), float(py.min()), float(px.max()), float(py.max())), outline="yellow")
                overlay.save(root / (capture["label"] + "_registered_roi.png"))
        differences = np.diff(np.stack(values), axis=0)
        result["sequences"][capture["label"]] = {"display_rms": Rms(differences),
            "display_p99": float(np.percentile(np.abs(differences), 99)),
            "early_display_rms": Rms(differences[:8]), "late_display_rms": Rms(differences[-8:])}
    return result


def AnalyzeResources(log_path, validation, sequences):
    log = Path(log_path).read_text(encoding="utf-8", errors="replace")
    source_pattern = (r"Readback source session=(\d+) id=(\d+) texture=(\w+) name=(.*?) "
                      r"native=(\w+) handle=\((\d+), (\d+)\) size=(\d+)x(\d+)")
    sources = {(int(match[0]), int(match[1])): {"resource": match[3],
               "handle": f"{match[5]}:{match[6]}", "dimensions": [int(match[7]), int(match[8])]}
               for match in re.findall(source_pattern, log)}
    mapped = {(int(session), int(frame)): sources.get((int(session), int(readback_id)))
              for session, readback_id, frame in re.findall(r"Readback mapped session=(\d+) id=(\d+) frame=(\d+)", log)}
    result = {}
    for capture in validation["captures"]:
        label = capture["label"]
        if label not in sequences:
            continue
        session_id = capture["status"]["session_id"]
        frames = sequences[label]["engine_frames"]
        records = [mapped[(session_id, frame)] for frame in frames if mapped.get((session_id, frame)) is not None]
        handles = [record["handle"] for record in records]
        result[label] = {"matched_readbacks": len(records), "missing_readback_frames": [frame for frame in frames
            if mapped.get((session_id, frame)) is None],
            "resource_names": sorted(set(record["resource"] for record in records)),
            "distinct_handles": sorted(set(handles)),
            "adjacent_repeated_handles": sum(a == b for a, b in zip(handles, handles[1:]))}
    return result


def Main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    parser.add_argument("--log")
    args = parser.parse_args()
    root = Path(args.directory)
    validation = json.loads((root / "validation.json").read_text(encoding="utf-8"))
    analysis = {"radiance_preview": "Exposure 1, Reinhard and sRGB; numerical radiance uses native DDS",
                "debug_preview": ("Native RGB without tone mapping; heatmap decodes exact lengths below 64, "
                                  "64 means >=64"),
                "sequences": {}}
    for capture in validation["captures"]:
        if capture.get("status", {}).get("state") != "completed":
            continue
        print(capture["label"], flush=True)
        analysis["sequences"][capture["label"]] = Analyze(root, capture)
    sequences = analysis["sequences"]
    for backend in ["compute", "dxr"]:
        MakeSheet(root, sequences, [f"{backend}_temporal_start", f"{backend}_length_start"], f"{backend}_static.png")
        MakeSheet(root, sequences, [f"{backend}_motion_raw", f"{backend}_motion_resolved",
                  f"{backend}_motion_validity", f"{backend}_motion_length"], f"{backend}_motion.png")
    analysis["registered_wall"] = AnalyzeRegisteredWall(root, validation)
    if args.log:
        analysis["resources"] = AnalyzeResources(args.log, validation, sequences)
    (root / "analysis.json").write_text(json.dumps(analysis, indent=2), encoding="utf-8")
    dataset = {name: {"files": sequence["preview_files"], "engine_frames": sequence["engine_frames"]}
               for name, sequence in sequences.items()}
    html = """<!doctype html><meta charset=utf-8><title>PathTracer Stage 2 frames</title>
<style>body{background:#171717;color:white;font:16px sans-serif}img{width:min(95vw,1152px);image-rendering:pixelated}
input{width:600px}select,button{font:inherit}p{color:#bbb}</style><h2>PathTracer Stage 2 capture viewer</h2>
<p>Radiance: fixed exposure 1, Reinhard and sRGB. Debug: native RGB. Motion sequences are independent runs.</p>
<select id=sequence></select> <button id=play>Play</button><br><input id=frame type=range min=0 value=0>
<span id=info></span><br><img id=image><script>const data=DATA;const sel=document.getElementById('sequence'),
slider=document.getElementById('frame');for(const name in data)sel.add(new Option(name,name));function show(){
const d=data[sel.value],i=+slider.value;slider.max=d.files.length-1;document.getElementById('image').src=d.files[i];
document.getElementById('info').textContent=` ${i+1}/${d.files.length}, engine frame ${d.engine_frames[i]}`;}
sel.onchange=()=>{slider.value=0;show()};slider.oninput=show;let timer;document.getElementById('play').onclick=()=>{
if(timer){clearInterval(timer);timer=null;return}timer=setInterval(()=>{slider.value=(+slider.value+1)%
data[sel.value].files.length;show()},60)};show();</script>"""
    (root / "viewer.html").write_text(html.replace("DATA", json.dumps(dataset)), encoding="utf-8")


if __name__ == "__main__":
    Main()
