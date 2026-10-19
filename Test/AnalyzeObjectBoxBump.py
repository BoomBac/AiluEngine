"""Compare actual GPU normals against independent native Blender OBJECT normal bakes."""
import argparse
import json
import struct
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("gpu", type=Path)
parser.add_argument("reference", type=Path)
parser.add_argument("--color", action="store_true")
args = parser.parse_args()
cases = json.loads((args.reference / "cases.json").read_text(encoding="utf-8"))
checks = []
for case in cases:
    files = list(args.gpu.glob(f"**/{case['name']}.bin")) or list(args.gpu.glob(f"**/{case['name']}_*.dds"))
    assert len(files) == 1, (case["name"], files)
    data = files[0].read_bytes()
    if files[0].suffix == ".dds":
        assert data[:4] == b"DDS " and data[84:88] == b"DX10"
        height, width = struct.unpack_from("<II", data, 12)
        assert struct.unpack_from("<I", data, 128)[0] == 2, "Expected uncompressed RGBA32F"
        gpu = np.frombuffer(data, dtype="<f4", offset=148).reshape(height, width, 4)
    else:
        gpu = np.frombuffer(data, dtype="<f4").reshape(256, 256, 4)
    # DDS rows start at the top, bpy image pixels start at the bottom.
    if args.color:
        gpu = np.flipud(gpu)[2:-2, 2:-2, :3]
        source_name = case["name"].replace("normal_", "color_")
        source = np.load(args.reference / (source_name + ".npy"))[2:-2, 2:-2, :3]
        error = np.abs(gpu - source)
        finite = bool(np.isfinite(gpu).all())
        checks.append({"case": source_name, "finite": finite, "mean_absolute_error": float(error.mean()),
                       "p95_absolute_error": float(np.percentile(error, 95)),
                       "status": "PASS" if finite and error.mean() < 0.005
                                 and np.percentile(error, 95) < 0.015 else "FAIL"})
        continue
    gpu = np.flipud(gpu)[2:-2, 2:-2, :3] * 2.0 - 1.0
    source = np.load(args.reference / (case["name"] + ".npy"))[2:-2, 2:-2, :3] * 2.0 - 1.0
    finite = bool(np.isfinite(gpu).all())
    assert finite and gpu.shape == source.shape
    gpu /= np.maximum(np.linalg.norm(gpu, axis=2, keepdims=True), 1e-8)
    source /= np.maximum(np.linalg.norm(source, axis=2, keepdims=True), 1e-8)
    angles = np.degrees(np.arccos(np.clip(np.sum(gpu * source, axis=2), -1.0, 1.0)))
    baseline = np.degrees(np.arccos(np.clip(np.sum(np.asarray(case["normal"]) * source, axis=2), -1.0, 1.0)))
    baseline_mean = float(baseline.mean())
    relative_error = float(angles.mean()) / max(baseline_mean, 1e-8)
    checks.append({"case": case["name"], "finite": finite, "mean_degrees": float(angles.mean()),
                   "p95_degrees": float(np.percentile(angles, 95)), "max_degrees": float(angles.max()),
                   "unperturbed_mean_degrees": baseline_mean, "relative_mean_error": relative_error,
                   "status": "PASS" if (angles.mean() < 1.5 and np.percentile(angles, 95) < 3.0
                                          and baseline_mean > 0.1 and relative_error < 0.25) else "FAIL"})
summary = {"status": "PASS" if all(check["status"] == "PASS" for check in checks) else "FAIL", "checks": checks}
(args.gpu / ("color_assertions.json" if args.color else "normal_assertions.json")).write_text(
    json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
raise SystemExit(0 if summary["status"] == "PASS" else 1)
