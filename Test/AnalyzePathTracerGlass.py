"""Validate BMW glass capture transport and material parameters; visual fidelity needs image review."""
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image

root = Path(sys.argv[1])
gates = {}
sequences = {}
lengths = None
for name, expected in (("reference_late", 32), ("reference_present", 3), ("reference_length", 8),
                       ("opaque_late", 32), ("opaque_present", 3)):
    manifest_path, = (root / name).rglob("capture.json")
    manifest = json.loads(manifest_path.read_text())
    frames = sorted(manifest["frames"], key=lambda frame: frame["capture_index"])
    gates[name + "_complete"] = manifest["state"] == "completed" and len(frames) == expected
    gates[name + "_contiguous"] = bool(np.all(np.diff([frame["engine_frame"] for frame in frames]) == 1))
    gates[name + "_size"] = (manifest["width"], manifest["height"]) == (896, 512)
    arrays = []
    for frame in frames:
        file = manifest_path.parent / frame["file"]
        if manifest["output_format"] == "png":
            array = np.asarray(Image.open(file).convert("RGB"), dtype=np.float32) / 255
        else:
            assert manifest["format"] == 2
            data = file.read_bytes()
            array = np.frombuffer(data, "<f4", offset=len(data) - 896 * 512 * 16).reshape(512, 896, 4)[..., :3]
        arrays.append(array)
    array = np.stack(arrays)
    gates[name + "_finite"] = bool(np.isfinite(array).all())
    sequences[name] = {"frames": len(frames), "max": float(array.max()), "mean": float(array.mean())}
    if name == "reference_length":
        lengths = np.rint(np.where(array[..., 0] > 0, 2048 * (1 + array[..., 0]), 2048 * array[..., 1]))
        gates["progressive_reference"] = bool(np.all(lengths > 32) and np.all(np.diff(lengths, axis=0) == 1))
        sequences[name]["length_range"] = [float(lengths.min()), float(lengths.max())]
materials = json.loads((root / "materials.json").read_text())
glass = [mat for mat in materials if mat["name"].split("@")[0] == "Glass"]
lamps = [mat for mat in materials if mat["name"].split("@")[0] == "HeadlightGlass"]
gates["window_parameters"] = bool(glass) and all(mat["transmission"] == 1 and abs(mat["ior"] - 1.45) < 1e-5
                                                and mat["alpha_blend"] for mat in glass)
gates["lamp_parameters"] = bool(lamps) and all(mat["transmission"] == 1 and mat["ior"] == 1 for mat in lamps)
gates["driver_exit"] = json.loads((root / "run_summary.json").read_text())["exit_code"] == 0
gates["restored"] = all(item["restored"] for item in json.loads((root / "restoration.json").read_text()))
settings = json.loads((root / "settings.json").read_text())
gates["dxr_reference"] = settings["hardware_ray_tracing"] and settings["accumulation_mode"] == "reference"
result = {"gates": gates, "sequences": sequences,
          "glass_materials": glass + lamps,
          "limits": "Continuity/finite/parameters are numerical gates; window/lamp interior visibility requires visual review."}
(root / "glass_analysis.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
comparison = Image.new("RGB", (1260, 378), (28, 28, 32))
from PIL import ImageDraw
labels = ImageDraw.Draw(comparison)
for index, (name, label) in enumerate((("opaque_present", "Opaque control"),
                                      ("reference_present", "Glass transmission + coverage"))):
    file = next((root / name).rglob("*.png"))
    crop = Image.open(file).convert("RGB").crop((290, 155, 605, 330)).resize((630, 350))
    comparison.paste(crop, (index * 630, 28))
    labels.text((index * 630 + 12, 8), label, fill="white")
comparison.save(root / "glass_before_after.png")
print(json.dumps({"passed": sum(gates.values()), "total": len(gates), "sequences": sequences}, indent=2))
sys.exit(0 if all(gates.values()) else 1)
