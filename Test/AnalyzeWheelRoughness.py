"""Verify actual BMW GBuffer roughness against the same-scene old tire material control."""
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from AnalyzeTaaSceneAcceptance import Decode

root, output = map(Path, sys.argv[1:3])
output.mkdir(parents=True, exist_ok=True)
gates, arrays = {}, {}
for name, count in (("wheel_fixed_gbuffer", 8), ("wheel_old_gbuffer", 8),
                    ("wheel_fixed_present", 4), ("wheel_old_present", 4)):
    manifest_path, = (root / name).rglob("capture.json")
    manifest = json.loads(manifest_path.read_text())
    frames = sorted(manifest["frames"], key=lambda item: item["capture_index"])
    gates[name + "_complete"] = manifest["state"] == "completed" and len(frames) == count
    gates[name + "_contiguous"] = all(b["engine_frame"] == a["engine_frame"] + 1 for a, b in zip(frames, frames[1:]))
    pictures = []
    for item in frames:
        file = manifest_path.parent / item["file"]
        if manifest["output_format"] == "png":
            array = np.asarray(Image.open(file).convert("RGB"), dtype=np.float32) / 255
        elif manifest["format"] == 28:
            data = file.read_bytes()
            size = manifest["width"] * manifest["height"] * 4
            array = np.frombuffer(data, dtype=np.uint8, offset=len(data) - size).reshape(
                manifest["height"], manifest["width"], 4).astype(np.float32) / 255
        else:
            array = Decode(file, manifest["format"], manifest["width"], manifest["height"])
        pictures.append(array)
    array = np.stack(pictures)
    gates[name + "_finite"] = bool(np.isfinite(array).all())
    arrays[name] = array.mean(axis=0)
fixed, old = arrays["wheel_fixed_gbuffer"], arrays["wheel_old_gbuffer"]
region = np.zeros(fixed.shape[:2], dtype=bool)
region[420:580, 710:845] = True
# BMWBlack in the central roundel shares the tire's quantized RGB, but remains a Glossy material.
badge = np.zeros_like(region)
badge[475:510, 785:815] = True
region &= ~badge
mask = region & np.all((fixed[..., :3] > 0.005) & (fixed[..., :3] < 0.009), axis=2)
gates["tire_pixels_present"] = int(mask.sum()) > 100
gates["tire_fixed_roughness_one"] = bool(np.all(fixed[..., 3][mask] > 0.99)) and gates["tire_pixels_present"]
gates["tire_old_roughness_zero"] = bool(np.all(old[..., 3][mask] < 0.01)) and gates["tire_pixels_present"]
gates["gbuffer_albedo_unchanged"] = bool(np.allclose(fixed[..., :3], old[..., :3], atol=1e-5))
gates["center_roundel_unchanged"] = bool(np.array_equal(fixed[badge], old[badge]))
materials = json.loads((root / "materials.json").read_text())
tire = next(value for value in materials if value["name"] == "TireRubber")
gates["export_mapping"] = tire["roughness"] == 1 and tire["metallic"] == 0 and not tire["roughness_path"]
gates["driver_exit"] = json.loads((root / "run_summary.json").read_text())["exit_code"] == 0
gates["files_restored"] = all(value["restored"] for value in json.loads((root / "restoration.json").read_text()))
gates["runtime_restored"] = (json.loads((root / "original_settings.json").read_text()) ==
                             json.loads((root / "restored_settings.json").read_text()))
canvas = Image.new("RGB", (1200, 528), "#181818")
draw = ImageDraw.Draw(canvas)
for index, (name, label) in enumerate((("wheel_old_present", "Old tire: roughness 0"),
                                      ("wheel_fixed_present", "Fixed tire: roughness 1 / metallic 0"))):
    picture = Image.fromarray(np.uint8(np.clip(arrays[name], 0, 1) * 255))
    picture = picture.crop((660, 370, 900, 580)).resize((600, 500))
    canvas.paste(picture, (index * 600, 28))
    draw.text((index * 600 + 8, 8), label, fill="white")
canvas.save(output / "wheel_comparison.png")
(output / "analysis.json").write_text(json.dumps({"gates": gates, "tire_pixels": int(mask.sum()),
    "tire_fixed_roughness": float(fixed[..., 3][mask].mean()) if mask.any() else None,
    "tire_old_roughness": float(old[..., 3][mask].mean()) if mask.any() else None}, indent=2))
print(json.dumps({"passed": sum(gates.values()), "total": len(gates), "tire_pixels": int(mask.sum())}))
sys.exit(0 if all(gates.values()) else 1)
