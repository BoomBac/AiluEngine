"""Compare two identical Full Sync packet streams and their deterministic GPU captures."""
import json
import hashlib
import re
import sys
from pathlib import Path

import numpy as np


first_dir, second_dir, output_dir = map(Path, sys.argv[1:4])
first = json.loads((first_dir / "summary.json").read_text(encoding="utf-8"))
second = json.loads((second_dir / "summary.json").read_text(encoding="utf-8"))
failures = []
metrics = {}


def decode(path, fmt, width, height):
    data = path.read_bytes()
    if fmt == 34:
        return np.frombuffer(data, dtype="<f2", offset=len(data) - width * height * 4).reshape(height, width, 2).astype(np.float32)
    if fmt == 10:
        return np.frombuffer(data, dtype="<f2", offset=len(data) - width * height * 8).reshape(height, width, 4).astype(np.float32)
    if fmt == 26:
        packed = np.frombuffer(data, dtype="<u4", offset=len(data) - width * height * 4).reshape(height, width)
        channels = []
        for shift, bits in ((0, 6), (11, 6), (22, 5)):
            value = (packed >> shift) & ((1 << (bits + 5)) - 1)
            mantissa, exponent = value & ((1 << bits) - 1), value >> bits
            channels.append(np.where(exponent == 0, mantissa * 2.0 ** (-14 - bits),
                                     (1.0 + mantissa / 2.0 ** bits) * 2.0 ** (exponent.astype(float) - 15)))
        return np.stack(channels, axis=-1).astype(np.float32)
    if fmt in (28, 29):
        return np.frombuffer(data, dtype=np.uint8, offset=len(data) - width * height * 4).reshape(height, width, 4).astype(np.float32) / 255.0
    if fmt == 87:
        return np.frombuffer(data, dtype=np.uint8, offset=len(data) - width * height * 4).reshape(height, width, 4)[..., [2, 1, 0, 3]].astype(np.float32) / 255.0
    raise ValueError(f"unsupported replay comparison format {fmt}")


if first.get("status") != "PASS" or second.get("status") != "PASS":
    failures.append({"field": "runs.status", "expected": ["PASS", "PASS"],
                     "actual": [first.get("status"), second.get("status")]})
first_packets = (first_dir / "fixture/packets.bin").read_bytes()
second_packets = (second_dir / "fixture/packets.bin").read_bytes()
first_canonical = re.sub(rb"ailu_live_link_[A-Za-z0-9_-]+", b"ailu_live_link_<temp>", first_packets)
second_canonical = re.sub(rb"ailu_live_link_[A-Za-z0-9_-]+", b"ailu_live_link_<temp>", second_packets)
if first_canonical != second_canonical:
    failures.append({"field": "packets.canonical_sha256",
                     "expected": hashlib.sha256(first_canonical).hexdigest(),
                     "actual": hashlib.sha256(second_canonical).hexdigest(),
                     "reason": "Full Sync streams differ after normalizing the per-process bake temp directory"})
else:
    metrics["packet_sha256"] = {"run_a": first.get("packet_sha256"), "run_b": second.get("packet_sha256"),
                                "canonical_sha256": hashlib.sha256(first_canonical).hexdigest(),
                                "normalized_field": "per-process Blender material-bake temp directory"}

for name, tolerance in (("gbuffer_normal", 0.002), ("gbuffer_albedo", 0.005), ("final_color", 0.02)):
    first_manifest_path, = first_dir.glob(f"captures/{name}/session_*/capture.json")
    second_manifest_path, = second_dir.glob(f"captures/{name}/session_*/capture.json")
    first_manifest = json.loads(first_manifest_path.read_text(encoding="utf-8"))
    second_manifest = json.loads(second_manifest_path.read_text(encoding="utf-8"))
    first_frames = sorted(first_manifest["frames"], key=lambda frame: frame["capture_index"])
    second_frames = sorted(second_manifest["frames"], key=lambda frame: frame["capture_index"])
    if (first_manifest.get("state") != "completed" or second_manifest.get("state") != "completed"
            or len(first_frames) != 3 or len(second_frames) != 3
            or [frame["capture_index"] for frame in first_frames] != [frame["capture_index"] for frame in second_frames]):
        failures.append({"field": f"captures.{name}.frames", "expected": "three ordered frames per run",
                         "actual": [len(first_frames), len(second_frames)]})
        continue
    dims = (first_manifest["width"], first_manifest["height"], first_manifest["format"])
    other_dims = (second_manifest["width"], second_manifest["height"], second_manifest["format"])
    if dims != other_dims:
        failures.append({"field": f"captures.{name}.dimensions_format", "expected": dims, "actual": other_dims})
        continue
    first_image = decode(first_manifest_path.parent / first_frames[-1]["file"], first_manifest["format"],
                         first_manifest["width"], first_manifest["height"])
    second_image = decode(second_manifest_path.parent / second_frames[-1]["file"], second_manifest["format"],
                          second_manifest["width"], second_manifest["height"])
    difference = np.abs(first_image - second_image)
    rms = float(np.sqrt(np.mean(difference.astype(np.float64) ** 2)))
    p99 = float(np.percentile(difference, 99))
    metrics[name] = {"rms": rms, "p99_abs": p99, "tolerance": tolerance,
                     "engine_frames": [[frame["engine_frame"] for frame in first_frames],
                                       [frame["engine_frame"] for frame in second_frames]]}
    if not np.isfinite(difference).all() or rms > tolerance:
        failures.append({"field": f"captures.{name}.replay_image_error", "expected": f"RMS <= {tolerance}",
                         "actual": metrics[name]})

result = {"status": "PASS" if not failures else "FAIL", "metrics": metrics,
          "failure_count": len(failures), "failures": failures,
          "limits": "Same workstation/build only; compares final samples from identical packet streams."}
output_dir.mkdir(parents=True, exist_ok=True)
(output_dir / "m2_replay_comparison.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(result, indent=2))
sys.exit(0 if not failures else 1)
