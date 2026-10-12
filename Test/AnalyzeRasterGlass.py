"""Analyze actual BMW raster capture integrity; retain the control images for visual review."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from AnalyzeTaaSceneAcceptance import Decode, Rms

root = Path(sys.argv[1])
control = Path(sys.argv[2]) if len(sys.argv) > 2 else root
gates, sequences, images = {}, {}, {}
for name, count in (("glass_snapshot", 8), ("glass_noaa", 16), ("glass_noaa_present", 3),
                    ("glass_taa", 16), ("glass_taa_present", 3), ("opaque_noaa", 16),
                    ("opaque_noaa_present", 3)):
    sequence_root = control if name.startswith("opaque_") else root
    manifest_path, = (sequence_root / name).rglob("capture.json")
    manifest = json.loads(manifest_path.read_text())
    frames = sorted(manifest["frames"], key=lambda item: item["capture_index"])
    gates[name + "_complete"] = manifest["state"] == "completed" and len(frames) == count
    gates[name + "_contiguous"] = bool(np.all(np.diff([item["engine_frame"] for item in frames]) == 1))
    arrays = []
    finite_encoding = True
    for item in frames:
        file = manifest_path.parent / item["file"]
        if manifest["output_format"] == "png":
            array = np.asarray(Image.open(file).convert("RGB"), dtype=np.float32) / 255
        else:
            array = Decode(file, manifest["format"], manifest["width"], manifest["height"])[..., :3]
            if manifest["format"] == 26:
                size = manifest["width"] * manifest["height"]
                data = file.read_bytes()
                packed = np.frombuffer(data, dtype="<u4", offset=len(data) - size * 4)
                finite_encoding &= not any(np.any(((packed >> shift) & 31) == 31) for shift in (6, 17, 27))
        arrays.append(array)
    array = np.stack(arrays)
    gates[name + "_finite"] = finite_encoding and bool(np.isfinite(array).all())
    gates[name + "_nonempty"] = float(array.max()) > 0.1
    images[name] = array.mean(axis=0)
    sequences[name] = {"frames": len(frames), "dimensions": [manifest["width"], manifest["height"]],
                       "format": manifest["format"], "max": float(array.max()), "mean": float(array.mean()),
                       "adjacent_rms": Rms(np.diff(array, axis=0))}
materials = json.loads((root / "materials.json").read_text())
lamps = [item for item in materials if item["name"].split("@")[0] == "HeadlightGlass"]
windows = [item for item in materials if item["name"].split("@")[0] == "Glass"]
gates["lamp_opaque_alpha_transmission"] = bool(lamps) and all(item["transmission"] == 1 and item["ior"] == 1
    and not item["alpha_blend"] and item["base_color"][3] == 1 for item in lamps)
gates["window_parameters"] = bool(windows) and all(item["transmission"] == 1
    and abs(item["ior"] - 1.45) < 1e-5 and item["alpha_blend"] for item in windows)
gates["raster_enabled"] = not json.loads((root / "settings.json").read_text())["enabled"]
gates["driver_exit"] = json.loads((root / "run_summary.json").read_text())["exit_code"] == 0
gates["configuration_restored"] = all(item["restored"] for item in json.loads((root / "restoration.json").read_text()))
original = json.loads((root / "original_settings.json").read_text())
restored = json.loads((root / "restored_settings.json").read_text())
gates["runtime_settings_restored"] = original == restored
camera = json.loads((root / "original_camera.json").read_text())
restored_camera = json.loads((root / "restored_camera.json").read_text())
gates["camera_restored"] = all(camera[key] == restored_camera[key] for key in
    ("target_position", "pitch_degrees", "yaw_degrees", "fov_degrees", "aa_mode", "projection_type"))
gates["control_driver_exit"] = json.loads((control / "run_summary.json").read_text())["exit_code"] == 0
gates["control_configuration_restored"] = all(item["restored"]
    for item in json.loads((control / "restoration.json").read_text()))
gates["glass_changes_output"] = Rms(images["glass_noaa"] - images["opaque_noaa"]) > 0.001
width, height = 700, 400
comparison = Image.new("RGB", (width * 3, height + 28), "#181818")
labels = ImageDraw.Draw(comparison)
for index, (name, label) in enumerate((("opaque_noaa_present", "Opaque control"),
                                      ("glass_noaa_present", "Thin glass / AA off"),
                                      ("glass_taa_present", "Thin glass / TAA"))):
    picture = Image.fromarray(np.uint8(np.clip(images[name], 0, 1) * 255)).resize((width, height))
    comparison.paste(picture, (index * width, 28))
    labels.text((index * width + 8, 8), label, fill="white")
comparison.save(root / "raster_glass_comparison.png")
details = Image.new("RGB", (width * 3, height + 28), "#181818")
labels = ImageDraw.Draw(details)
for index, (name, label) in enumerate((("opaque_noaa_present", "Opaque control"),
                                      ("glass_noaa_present", "Thin glass / AA off"),
                                      ("glass_taa_present", "Thin glass / TAA"))):
    picture = Image.fromarray(np.uint8(np.clip(images[name], 0, 1) * 255))
    picture = picture.crop((510, 260, 1030, 545)).resize((width, height))
    details.paste(picture, (index * width, 28))
    labels.text((index * width + 8, 8), label, fill="white")
details.save(root / "raster_glass_details.png")
result = {"gates": gates, "sequences": sequences, "materials": lamps + windows,
          "limits": "Image transport/finite pixels are distinct from visual acceptance; review lamp/window interiors."}
(root / "raster_glass_analysis.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps({"passed": sum(gates.values()), "total": len(gates), "sequences": sequences}, indent=2))
sys.exit(0 if all(gates.values()) else 1)
