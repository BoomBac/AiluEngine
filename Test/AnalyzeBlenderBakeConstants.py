"""Summarize repeated native timings and diagnostics without adding overlapping pipeline costs."""
import argparse
import json
import re
import statistics
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--baseline", nargs="+", default=["baseline", "baseline_repeat"])
parser.add_argument("--optimized", nargs="+", default=["validated", "validated_repeat"])
args = parser.parse_args()
result = {"baseline_stages": args.baseline, "optimized_stages": args.optimized, "scenes": {}}
reference = json.loads((args.output / args.baseline[0] / "summary.json").read_text())
for stage in args.baseline + args.optimized:
    summary = json.loads((args.output / stage / "summary.json").read_text())
    for key in ("editor_sha256", "engine_sha256", "plugin_sha256", "source_hashes", "configuration"):
        assert summary[key] == reference[key], (stage, key)


def Diagnostics(path):
    text = path.read_text(errors="replace")
    batches = [line for line in text.splitlines() if "Bake batch:" in line]
    phases = {}
    for line in batches:
        for name, value in re.findall(r"(prepare|graphs|bake|pixels_cleanup|save_publish)=([\d.]+)ms", line):
            phases[name] = phases.get(name, 0.0) + float(value)
    return {
        "batches": len(batches),
        "bake_calls": sum(int(re.search(r"passes=(\d+)", line)[1]) for line in batches),
        "constant_channels": sum(int(value) for value in re.findall(r"constant_channels=(\d+)", text)),
        "phases_ms": phases,
        "png_prepare_ms": sum(float(value) for value in re.findall(r"PNG save: prepare=([\d.]+)ms", text)),
        "png_encode_write_ms": sum(float(value) for value in re.findall(r"encode_write=([\d.]+)ms", text)),
        "source_buffers_released": sum(int(value) for value in re.findall(
            r"Source image buffers: released=(\d+)", text)),
        "source_buffer_scan_ms": sum(float(value) for value in re.findall(r"released=\d+ scan=([\d.]+)ms", text)),
        "source_buffer_free_ms": sum(float(value) for value in re.findall(r"free=([\d.]+)ms", text)),
    }


for scene in ("bmw", "classroom"):
    groups = {}
    for group, stages in (("baseline", args.baseline), ("optimized", args.optimized)):
        runs = []
        for stage in stages:
            summary = json.loads((args.output / stage / "summary.json").read_text())
            assert all(value["restored"] for value in summary["restoration"]), stage
            assert all(summary["source_unchanged"].values()), stage
            run = args.output / stage / f"{scene}_0"
            metrics = json.loads((run / "metrics.json").read_text())
            assert not metrics["apply_failures"] and not metrics["queue_overflow"], stage
            runs.append({"stage": stage, "overall_ms": metrics["send_to_apply_end_ms"],
                         "generation_ms": metrics["export"]["generation_ms"],
                         "generation_cpu_ms": metrics["export"]["generation_cpu_ms"],
                         "average_cpu_cores": metrics["export"]["average_cpu_cores"],
                         "engine_apply_ms": metrics["apply_ms"], "diagnostics": Diagnostics(run / "blender.log")})
        groups[group] = {"runs": runs}
        for metric in ("overall_ms", "generation_ms", "generation_cpu_ms", "engine_apply_ms"):
            values = [run[metric] for run in runs]
            groups[group][metric] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
    reduction = {metric: 100 * (1 - groups["optimized"][metric]["median"] / groups["baseline"][metric]["median"])
                 for metric in ("overall_ms", "generation_ms", "generation_cpu_ms")}
    result["scenes"][scene] = {**groups, "reduction_percent": reduction}
(args.output / "final-report.json").write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
