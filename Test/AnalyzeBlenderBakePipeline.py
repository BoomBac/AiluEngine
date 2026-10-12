"""Inspect isolated native bake factors, phase coverage and restoration evidence."""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--stages", nargs="+", required=True)
args = parser.parse_args()
result = {}
reference = json.loads((args.output / "baseline/summary.json").read_text())
for stage in args.stages:
    summary = json.loads((args.output / stage / "summary.json").read_text())
    for key in ("editor_sha256", "engine_sha256", "plugin_sha256", "source_hashes", "configuration"):
        assert summary[key] == reference[key], (stage, key)
    assert all(item["restored"] for item in summary["restoration"]), stage
    assert all(summary["source_unchanged"].values()), stage
    values = {}
    for scene in ("bmw", "classroom"):
        run = args.output / stage / f"{scene}_0"
        metrics = json.loads((run / "metrics.json").read_text())
        assert not metrics["apply_failures"] and not metrics["queue_overflow"], (stage, scene)
        text = (run / "blender.log").read_text(errors="replace")
        batch_phases = {}
        for line in text.splitlines():
            if "Bake batch:" in line:
                for name, elapsed in re.findall(r"(prepare|graphs|bake|pixels_cleanup|save_publish)=([\d.]+)ms", line):
                    batch_phases[name] = batch_phases.get(name, 0.0) + float(elapsed)
        session = next((line for line in text.splitlines() if "Bake session:" in line), "")
        session_phases = {name: float(elapsed) for name, elapsed in re.findall(r"(\w+)=([\d.]+)ms", session)}
        worker = next((line for line in text.splitlines() if "PNG worker:" in line), "")
        values[scene] = {"generation_ms": metrics["export"]["generation_ms"],
                         "overall_ms": metrics["send_to_apply_end_ms"], "apply_ms": metrics["apply_ms"],
                         "batch_phases_ms": batch_phases, "session_phases_ms": session_phases,
                         "worker": worker, "experiment": summary.get("bake_experiment", {})}
    result[stage] = values
(args.output / "pipeline-report.json").write_text(json.dumps(result, indent=2))
for stage, scenes in result.items():
    print(stage, {name: {key: value for key, value in values.items() if key in ("generation_ms", "overall_ms")}
                  for name, values in scenes.items()})
