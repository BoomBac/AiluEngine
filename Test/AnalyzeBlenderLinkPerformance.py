"""Compare fresh-process replay medians and capture continuity/image deltas."""
import argparse
import hashlib
import json
import statistics
import struct
from pathlib import Path

import numpy as np
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("output")
parser.add_argument("--stage", default="cache")
parser.add_argument("--baseline-stage", default="baseline")
parser.add_argument("--native", action="store_true")
args = parser.parse_args()
output = Path(args.output)
result = {}

def geometry_hashes(stream):
    data = stream.read_bytes()
    offset = 0
    values = []
    while offset < len(data):
        _, version, kind, size, _ = struct.unpack_from("<IHHIQ", data, offset)
        payload = data[offset + 20:offset + 20 + size]
        if version != 1 or len(payload) != size:
            raise RuntimeError("Invalid recorded packet")
        if kind == 20:
            values.append(hashlib.sha256(payload[44:]).hexdigest())
        offset += 20 + size
    return sorted(values)

for name in ("bmw", "classroom"):
    trials = (0,) if args.native else (1, 2)
    before = [json.loads((output / args.baseline_stage / f"{name}_{trial}" / "metrics.json").read_text())
        for trial in trials]
    after = [json.loads((output / args.stage / f"{name}_{trial}" / "metrics.json").read_text()) for trial in trials]
    source_packets = output / args.baseline_stage / f"{name}_0" / "packets.bin"
    packet_hash = (hashlib.sha256(source_packets.read_bytes()).hexdigest() if source_packets.exists()
                   else before[0]["packet_sha256"])
    metrics = {}
    for key in ("total_ms", "apply_ms", "bvh_ms", "bvh_count"):
        baseline = statistics.median(row[key] for row in before)
        optimized = statistics.median(row[key] for row in after)
        metrics[key] = {"before": baseline, "after": optimized, "reduction_percent": (1 - optimized / baseline) * 100}
    images = {}
    for kind in ("geometry", "final"):
        groups = []
        integrity = []
        baseline_trial = 0 if (output / args.baseline_stage / f"{name}_0" / kind).exists() else 1
        for stage, trial in ((args.baseline_stage, baseline_trial), (args.stage, 0 if args.native else 1)):
            manifests = list((output / stage / f"{name}_{trial}" / kind).rglob("capture.json"))
            if len(manifests) != 1:
                raise RuntimeError("Missing or ambiguous capture")
            manifest_path = manifests[0]
            manifest = json.loads(manifest_path.read_text())
            frames = sorted(manifest["frames"], key=lambda frame: frame["capture_index"])
            arrays = [np.asarray(Image.open(manifest_path.parent / frame["file"]).convert("RGB"), dtype=np.float32)
                for frame in frames]
            frame_ids = [frame["engine_frame"] for frame in frames]
            integrity.append({"stage": stage, "frames": frame_ids,
                "contiguous": len(frames) == 3 and all(b - a == 1 for a, b in zip(frame_ids, frame_ids[1:])),
                "finite": all(np.isfinite(array).all() for array in arrays), "shape": arrays[0].shape})
            groups.append(arrays)
        delta = np.abs(groups[0][0] - groups[1][0])
        images[kind] = {"integrity": integrity, "rms_8bit": float(np.sqrt(np.mean(delta * delta))),
            "p99_8bit": float(np.percentile(delta, 99)), "max_8bit": float(delta.max()),
            "changed_pixel_fraction": float(np.mean(np.any(delta > 0, axis=2))),
            "baseline_adjacent_rms": float(np.sqrt(np.mean((groups[0][0] - groups[0][1]) ** 2)))}
    result[name] = {"metrics": metrics, "images": images,
        "identical_packet_stream": (None if args.native else
            all(row["packet_sha256"] == packet_hash for row in before + after)),
        "import_pass": all(row["apply_failures"] == 0 and not row["queue_overflow"] for row in before + after)}
    if args.native:
        result[name]["equivalent_geometry_stream"] = geometry_hashes(source_packets) == geometry_hashes(
            output / args.stage / f"{name}_0" / "packets.bin")
        result[name]["export_ms"] = {"before": before[0]["export"]["generation_ms"],
            "after": after[0]["export"]["generation_ms"]}
(output / f"{args.stage}-comparison.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(result, indent=2))
