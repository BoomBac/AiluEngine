import argparse
import html
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from analyze_path_tracer_stage1 import ReadDds
from analyze_path_tracer_stage2 import Analyze


def ReadSurfaceDds(file_path):
    data = file_path.read_bytes()
    height, width = struct.unpack_from("<II", data, 12)
    format_id = struct.unpack_from("<I", data, 84)[0]
    offset = 128
    if data[84:88] == b"DX10":
        format_id = struct.unpack_from("<I", data, 128)[0]
        offset = 148
    dtype, channels = {112: ("<f2", 2), 114: ("<f4", 1), 34: ("<f2", 2), 41: ("<f4", 1)}[format_id]
    return np.frombuffer(data, dtype=dtype, offset=offset).reshape(height, width, channels).astype(np.float32)


def Main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    args = parser.parse_args()
    root = Path(args.directory)
    results = {}
    rows = []
    sections = []
    for validation_file in sorted(root.glob("*/validation.json")):
        validation = json.loads(validation_file.read_text(encoding="utf-8-sig"))
        if "camera" not in validation:
            continue
        for capture in validation["captures"]:
            if "status" not in capture:
                continue
            label = validation_file.parent.name + "/" + capture["kind"]
            directory = Path(capture["status"]["output_directory"])
            manifest = json.loads((directory / "capture.json").read_text(encoding="utf-8"))
            frames = sorted(manifest["frames"], key=lambda frame: frame["capture_index"])
            engine_frames = [frame["engine_frame"] for frame in frames]
            record = {"state": manifest["state"], "requested": manifest["frame_count"], "actual": len(frames),
                      "dimensions": [manifest["width"], manifest["height"]],
                      "engine_frame_gaps": [b - a for a, b in zip(engine_frames, engine_frames[1:]) if b - a != 1],
                      "capture_indices": [frame["capture_index"] for frame in frames],
                      "runtime_restored": validation["original"] == validation.get("restored"),
                      "scene_revision_restored": all(validation["editor"][key] == validation["restored_editor"][key]
                          for key in ["scene_edit_revision", "scene_structure_revision"])}
            results[label] = record
            files = [directory / frame["file"] for frame in frames]
            if capture["kind"] in ["length_native", "validity_native"]:
                native = Analyze(root, {**capture, "label": label.replace("/", "_")})
                record.update(native)
                files = [root / file_path for file_path in native["preview_files"]]
            if manifest["output_format"] == "dds_native":
                if capture["kind"] in ["length_native", "validity_native"]:
                    record["moving_observations"] = len(capture.get("observations", []))
                    record["geometry_reset_peak"] = max(record.get("geometry_reset_fraction_per_frame", [0]))
                    record["geometry_reset_tail"] = record.get("geometry_reset_fraction_per_frame", [])[-16:]
                    manifest = {**manifest, "output_format": "png"}
                else:
                    record["nonfinite_values"] = 0
                    record["back_wall_motion_uv"] = []
                    record["back_wall_motion_pixels"] = []
                    for file_path in files:
                        surface = capture["kind"] in ["normal", "depth"]
                        rgba = ReadSurfaceDds(file_path) if surface else ReadDds(file_path)
                        record["nonfinite_values"] += int((~np.isfinite(rgba)).sum())
                        if surface:
                            continue
                        motion = np.median(rgba[135:180, 385:515, :3], axis=(0, 1))
                        record["back_wall_motion_uv"].append(motion.tolist())
                        record["back_wall_motion_pixels"].append((motion[:2] * record["dimensions"]).tolist())
                    continue
            record["invalid_image_dimensions"] = []
            for index, file_path in enumerate(files):
                with Image.open(file_path) as image:
                    if list(image.size) != record["dimensions"]:
                        record["invalid_image_dimensions"].append(index)
            rows.append((label, [files[index] for index in sorted(set([0, len(files) // 4,
                                                                     len(files) // 2, len(files) - 1]))]))
            urls = [file_path.relative_to(root).as_posix() for file_path in files]
            sections.append(f'<section><h3>{html.escape(label)}</h3><input type="range" min="0" '
                            f'max="{len(urls) - 1}" value="0" oninput="Show(this)"><span>0</span>'
                            f'<img data-frames="{html.escape(json.dumps(urls), quote=True)}" '
                            f'src="{html.escape(urls[0], quote=True)}"></section>')
    sheet = Image.new("RGB", (4 * 420, len(rows) * 330), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for row, (label, files) in enumerate(rows):
        for column, file_path in enumerate(files):
            with Image.open(file_path) as image:
                width, height = image.size
                crop = image.convert("RGB").crop((int(width * 0.29), int(height * 0.21),
                                                   int(width * 0.70), int(height * 0.71))).resize((420, 300))
            x, y = column * 420, row * 330
            sheet.paste(crop, (x, y + 30))
            draw.text((x + 4, y + 6), label + " " + file_path.stem, fill="white")
    sheet.save(root / "back_wall_review.png")
    (root / "analysis.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    page = ('<!doctype html><meta charset="utf-8"><title>Temporal motion captures</title>'
            '<style>body{background:#202020;color:white;font:16px sans-serif}img{display:block;max-width:100%}'
            'input{width:600px}section{margin:24px}</style><h1>Temporal motion captures</h1>'
            '<p>PNG from Debug GI; inspect the back wall and silhouettes. Frame index is zero based.</p>'
            + "".join(sections) + '<script>function Show(e){const s=e.parentElement,i=s.querySelector("img");'
            'i.src=JSON.parse(i.dataset.frames)[Number(e.value)];s.querySelector("span").textContent=e.value}'
            '</script>')
    (root / "viewer.html").write_text(page, encoding="utf-8")
    print(json.dumps({key: {field: value[field] for field in ["state", "actual", "engine_frame_gaps"]}
                      for key, value in results.items()}, indent=2))


if __name__ == "__main__":
    Main()
