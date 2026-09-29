import argparse
import json
import re
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def ReadDds(file_path):
    data = file_path.read_bytes()
    if data[:4] != b"DDS ":
        raise ValueError(f"Unsupported DDS header: {file_path}")
    height, width = struct.unpack_from("<II", data, 12)
    if data[84:88] == b"DX10":
        format_id = struct.unpack_from("<I", data, 128)[0]
        dtype = {2: "<f4", 10: "<f2"}.get(format_id)
        offset = 148
    else:
        format_id = struct.unpack_from("<I", data, 84)[0]
        dtype = {116: "<f4", 113: "<f2"}.get(format_id)
        offset = 128
    if dtype is None:
        raise ValueError(f"Unsupported DDS format {format_id}: {file_path}")
    return np.frombuffer(data, dtype=dtype, count=width * height * 4, offset=offset).reshape(height, width, 4).astype(np.float32)


def DisplayPixels(image):
    rgb = np.maximum(np.nan_to_num(image[..., :3], nan=0, posinf=0, neginf=0), 0)
    rgb = rgb / (1 + rgb)
    rgb = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * rgb ** (1 / 2.4) - 0.055)
    return np.rint(np.clip(rgb, 0, 1) * 255).astype(np.uint8)


def Rms(values):
    return float(np.sqrt(np.mean(np.square(values.astype(np.float64)))))


def AnalyzeSequence(root, label, capture):
    directory = Path(capture["status"]["output_directory"])
    manifest = json.loads((directory / "capture.json").read_text(encoding="utf-8"))
    frames = sorted(manifest["frames"], key=lambda frame: frame["capture_index"])
    engine_frames = [frame["engine_frame"] for frame in frames]
    result = {"state": manifest["state"], "requested": manifest["frame_count"], "actual": len(frames),
              "dimensions": [manifest["width"], manifest["height"]], "engine_frames": engine_frames,
              "capture_indices": [frame["capture_index"] for frame in frames],
              "engine_frame_gaps": [b - a for a, b in zip(engine_frames, engine_frames[1:]) if b - a != 1],
              "elapsed_ms": capture["elapsed_ms"]}
    if manifest["output_format"] != "dds_native":
        result["preview_files"] = [str((directory / frame["file"]).relative_to(root)).replace("\\", "/") for frame in frames]
        return result
    images = np.stack([ReadDds(directory / frame["file"]) for frame in frames])
    finite = np.isfinite(images)
    rgb = np.nan_to_num(images[..., :3], nan=0, posinf=0, neginf=0)
    result.update({"nonfinite_rgba_values": int((~finite).sum()), "nonfinite_rgb_values": int((~finite[..., :3]).sum()),
                   "rgb_min": float(rgb.min()), "rgb_max": float(rgb.max()), "rgb_p999": float(np.percentile(rgb, 99.9)),
                   "alpha_min": float(np.nanmin(images[..., 3])), "alpha_max": float(np.nanmax(images[..., 3])),
                   "mean_rgb_per_frame": rgb.mean(axis=(1, 2, 3)).astype(float).tolist(),
                   "black_frames": [index for index, frame in enumerate(rgb) if float(np.max(frame)) < 1e-7]})
    previews = root / "previews" / label
    previews.mkdir(parents=True, exist_ok=True)
    result["preview_files"] = []
    for index, frame in enumerate(images):
        file_path = previews / f"{index:04}.png"
        Image.fromarray(DisplayPixels(frame)).save(file_path)
        result["preview_files"].append(str(file_path.relative_to(root)).replace("\\", "/"))
    if len(images) > 2:
        height, width = rgb.shape[1:3]
        result["regions"] = {}
        for name, region in {"inner": (0.1, 0.1, 0.9, 0.9), "left": (0.08, 0.3, 0.3, 0.75), "center": (0.4, 0.3, 0.7, 0.8)}.items():
            x0, y0, x1, y1 = region
            values = rgb[:, int(y0 * height):int(y1 * height), int(x0 * width):int(x1 * width)]
            differences = values[1:] - values[:-1]
            two_frame_differences = values[2:] - values[:-2]
            displayed = DisplayPixels(images).astype(np.float32)[:, int(y0 * height):int(y1 * height), int(x0 * width):int(x1 * width)]
            display_differences = displayed[1:] - displayed[:-1]
            window = min(8, len(differences))
            result["regions"][name] = {"relative_rect": region,
                "early_radiance_rms": Rms(differences[:window]), "late_radiance_rms": Rms(differences[-window:]),
                "early_display_rms": Rms(display_differences[:window]), "late_display_rms": Rms(display_differences[-window:]),
                "late_display_p99": float(np.percentile(np.abs(display_differences[-window:]), 99)),
                "late_adjacent_vs_two_frame_rms": Rms(differences[-window:]) / max(Rms(two_frame_differences[-window:]), 1e-15)}
    return result


def Main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    parser.add_argument("--log")
    args = parser.parse_args()
    root = Path(args.directory)
    validation = json.loads((root / "validation.json").read_text(encoding="utf-8"))
    analysis = {"display_transform": "Fixed exposure 1, Reinhard rgb/(1+rgb), linear to sRGB; DDS metrics use unmodified linear radiance", "sequences": {}}
    for capture in validation["captures"]:
        if capture["status"]["state"] == "completed":
            analysis["sequences"][capture["label"]] = AnalyzeSequence(root, capture["label"], capture)
    if args.log:
        log = Path(args.log).read_text(encoding="utf-8", errors="replace")
        pattern = r"Graph target session=(\d+) pass=(.*?) resource=(.*?) input=(\d+) index=(\d+) texture=(\w+) handle=\((\d+), (\d+)\) size=(\d+)x(\d+)"
        graph_records = [{"session": int(match[0]), "pass": match[1], "resource": match[2], "input": match[3] == "1",
                          "index": int(match[4]), "texture": match[5], "handle": f"{match[6]}:{match[7]}", "dimensions": [int(match[8]), int(match[9])]}
                         for match in re.findall(pattern, log)]
        source_pattern = r"Readback source session=(\d+) id=(\d+) texture=(\w+) name=(.*?) native=(\w+) handle=\((\d+), (\d+)\) size=(\d+)x(\d+)"
        sources = {(int(match[0]), int(match[1])): {"resource": match[3], "texture": match[2],
                   "handle": f"{match[5]}:{match[6]}", "dimensions": [int(match[7]), int(match[8])]}
                   for match in re.findall(source_pattern, log)}
        mapped = {(int(session), int(frame)): sources.get((int(session), int(readback_id)))
                  for session, readback_id, frame in re.findall(r"Readback mapped session=(\d+) id=(\d+) frame=(\d+)", log)}
        analysis["resources"] = {}
        for capture in validation["captures"]:
            session_id = capture["status"]["session_id"]
            # Graph-target messages precede TryCapture and can include rejected attempts.
            # Match actual GPU readbacks to manifest engine frames instead of log order.
            engine_frames = analysis["sequences"][capture["label"]]["engine_frames"]
            sequence = [{"engine_frame": frame, **mapped[(session_id, frame)]} for frame in engine_frames
                        if mapped.get((session_id, frame)) is not None]
            handles = [record["handle"] for record in sequence]
            analysis["resources"][capture["label"]] = {"records": sequence,
                "missing_readback_frames": [frame for frame in engine_frames if mapped.get((session_id, frame)) is None],
                "graph_targets": [record for record in graph_records if record["session"] == session_id],
                "distinct_handles": sorted(set(handles)), "resource_names": sorted(set(record["resource"] for record in sequence)),
                "adjacent_repeated_handles": sum(a == b for a, b in zip(handles, handles[1:]))}
    for backend in ["compute", "dxr"]:
        raw = analysis["sequences"].get(f"{backend}_raw")
        temporal = analysis["sequences"].get(f"{backend}_temporal_start")
        if not raw or not temporal:
            continue
        columns = [("Raw 1 spp", raw, 0)] + [(f"Temporal frame {index + 1}", temporal, index) for index in [0, 7, 31, len(temporal["preview_files"]) - 1] if index < len(temporal["preview_files"])]
        width, height = raw["dimensions"]
        canvas = Image.new("RGB", (width * len(columns), height + 28), (25, 25, 25))
        draw = ImageDraw.Draw(canvas)
        for column, (title, sequence, index) in enumerate(columns):
            canvas.paste(Image.open(root / sequence["preview_files"][index]).convert("RGB"), (column * width, 28))
            draw.text((column * width + 8, 8), title, fill="white")
        canvas.save(root / f"{backend}_convergence.png")
    (root / "analysis.json").write_text(json.dumps(analysis, indent=2), encoding="utf-8")
    dataset = {name: {"files": sequence["preview_files"], "engine_frames": sequence["engine_frames"]} for name, sequence in analysis["sequences"].items()}
    html = """<!doctype html><meta charset=utf-8><title>PathTracer Stage 1 frames</title><style>body{background:#171717;color:white;font:16px sans-serif}img{width:min(95vw,1152px);image-rendering:pixelated}input{width:600px}select,button{font:inherit}p{color:#bbb}</style><h2>PathTracer Stage 1 capture viewer</h2><p>DDS previews: fixed exposure 1, Reinhard tone mapping and sRGB. Engine frames are recorded for every image.</p><select id=sequence></select> <button id=play>Play</button><br><input id=frame type=range min=0 value=0><span id=info></span><br><img id=image><script>const data=DATA;const sel=document.getElementById('sequence'),slider=document.getElementById('frame');for(const name in data)sel.add(new Option(name,name));function show(){const d=data[sel.value],i=+slider.value;slider.max=d.files.length-1;document.getElementById('image').src=d.files[i];document.getElementById('info').textContent=` ${i+1}/${d.files.length}, engine frame ${d.engine_frames[i]}`;}sel.onchange=()=>{slider.value=0;show()};slider.oninput=show;let timer;document.getElementById('play').onclick=()=>{if(timer){clearInterval(timer);timer=null;return}timer=setInterval(()=>{slider.value=(+slider.value+1)%data[sel.value].files.length;show()},125)};show();</script>"""
    (root / "viewer.html").write_text(html.replace("DATA", json.dumps(dataset)), encoding="utf-8")
    print(json.dumps({name: {key: value for key, value in sequence.items() if key not in ["preview_files", "engine_frames", "capture_indices", "mean_rgb_per_frame"]} for name, sequence in analysis["sequences"].items()}, indent=2))


if __name__ == "__main__":
    Main()
