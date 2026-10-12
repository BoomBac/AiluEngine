"""Validate the four-angle current/old BMW bake comparison and retain lamp detail evidence."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from AnalyzeTaaSceneAcceptance import Decode, Rms

fresh, old, output = map(Path, sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
gates, metrics, pictures = {}, {}, {}
for label, root in (("current", fresh), ("old", old)):
    for angle in ("original", "closer", "frontal", "side"):
        for kind in ("snapshot", "present"):
            name = angle + "_" + kind
            manifest_path, = (root / name).rglob("capture.json")
            manifest = json.loads(manifest_path.read_text())
            frames = sorted(manifest["frames"], key=lambda item: item["capture_index"])
            key = label + "_" + name
            gates[key + "_complete"] = manifest["state"] == "completed" and len(frames) == 4
            gates[key + "_contiguous"] = all(b["engine_frame"] == a["engine_frame"] + 1
                for a, b in zip(frames, frames[1:]))
            arrays = []
            finite_encoding = True
            for frame in frames:
                file = manifest_path.parent / frame["file"]
                if kind == "present":
                    arrays.append(np.asarray(Image.open(file).convert("RGB"), dtype=np.float32) / 255)
                else:
                    arrays.append(Decode(file, manifest["format"], manifest["width"], manifest["height"])[..., :3])
                    if manifest["format"] == 26:
                        size = manifest["width"] * manifest["height"]
                        data = file.read_bytes()
                        packed = np.frombuffer(data, dtype="<u4", offset=len(data) - size * 4)
                        finite_encoding &= not any(np.any(((packed >> shift) & 31) == 31) for shift in (6, 17, 27))
            array = np.stack(arrays)
            gates[key + "_finite"] = finite_encoding and bool(np.isfinite(array).all())
            pictures[key] = array.mean(axis=0)
            metrics[key] = {"frames": len(frames), "peak": float(array.max()), "adjacent_rms": Rms(np.diff(array, axis=0))}
    gates[label + "_driver_exit"] = json.loads((root / "run_summary.json").read_text())["exit_code"] == 0
    gates[label + "_files_restored"] = all(item["restored"] for item in json.loads((root / "restoration.json").read_text()))
    gates[label + "_runtime_restored"] = (json.loads((root / "original_settings.json").read_text()) ==
                                         json.loads((root / "restored_settings.json").read_text()))
for angle in ("original", "closer", "frontal", "side"):
    current_camera = json.loads((fresh / (angle + "_camera.json")).read_text())
    old_camera = json.loads((old / (angle + "_camera.json")).read_text())
    gates[angle + "_same_camera"] = all(current_camera[key] == old_camera[key]
        for key in ("position", "target_position", "pitch_degrees", "yaw_degrees", "aa_mode"))
    metrics[angle + "_snapshot_difference"] = Rms(pictures["current_" + angle + "_snapshot"] -
                                                 pictures["old_" + angle + "_snapshot"])
canvas = Image.new("RGB", (1500, 425), "#181818")
draw = ImageDraw.Draw(canvas)
for index, (key, label) in enumerate((("old_closer_present", "Old bake module"),
                                      ("current_closer_present", "Current bake module / fresh Full Sync"))):
    picture = Image.fromarray(np.uint8(np.clip(pictures[key], 0, 1) * 255))
    picture = picture.crop((845, 380, 1015, 470)).resize((750, 397))
    canvas.paste(picture, (index * 750, 28))
    draw.text((index * 750 + 8, 8), label, fill="white")
canvas.save(output / "lamp_bake_comparison.png")
(output / "analysis.json").write_text(json.dumps({"gates": gates, "metrics": metrics}, indent=2))
print(json.dumps({"passed": sum(gates.values()), "total": len(gates), "metrics": metrics}, indent=2))
sys.exit(0 if all(gates.values()) else 1)
