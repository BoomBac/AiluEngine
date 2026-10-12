# Blender Live Link BMW27 / Classroom performance

## Purpose
Measure the current checkout on the two user-supplied scenes before optimizing, then compare identical inputs.

## Scope / Non-goals
BMW27: `I:/Downloads/BMW27.blend/BMW27.blend`; Classroom: `I:/Downloads/classroom/classroom/classroom.blend`.
Preserve the blend files, existing dirty work and Editor configuration. No unrelated rendering or material changes.

## Architecture constraints
Blender API/export remains on Blender's main thread; Scene mutation and GPU upload remain on Engine's owner thread.
Protocol v1 and FullSync Begin/End reconciliation remain compatible. Shared geometry must retain per-entity materials.

## Relevant files
Plugins/BlenderLiveLink/Inc/LiveLinkMeshCache.h; LiveLinkSession.h/.cpp; BlenderLiveLinkPlugin.cpp;
Test/BlenderMeshCacheSmoke.cpp; Test/BlenderLinkPerformance*.py; Test/BlenderLinkPerformanceCapture.mjs;
Test/AnalyzeBlenderLinkPerformance.py; Doc/architecture/plugin-system.md.

## Current milestone
Complete: geometry reuse, focused smoke and same-scene comparison. No Blender production source changes.

## Progress
- [x] Read current logs, source, plugin/threading architecture and existing task state.
- [x] Build current checkout with `cmake --build --preset build-debug` (exit 0).
- [x] M1: record both scene baselines and image evidence before optimization builds.
- [x] M2: targeted optimization, smoke/regression, Debug build, same-scene comparison.
- [x] Review diffs, restore runtime state and report measured scope.

## Decision Log
- Fresh process per run; distinguish export-to-Editor measurements from recorded-packet import-only replays.
- Retain input packet streams to compare exactly identical geometry/materials across builds.
- Evidence belongs beside the exercised Debug Editor.exe, in a unique run directory.
- No user Editor/Blender process was running at initial process inspection.
- First change: session-local, weak Mesh cache, 128 MiB canonical byte budget; sampled bucket key followed by
  complete byte equality, including attributes, bounds, indices and material-slot mapping. Protocol unchanged.

## Surprises & Discoveries
- Existing checkout contains extensive unrelated dirty work; preserve it.
- The old root Editor log ends October 10; latest Classroom evidence is export to an independent TCP receiver.
- Repository TEMPLATE.md is absent; this plan includes its required sections explicitly.
- BMW native FullSync 18.406 s; replay 13.138 / 12.867 s; 110 mesh packets, 36 unique geometry bodies.
- Classroom native FullSync 272.889 s, Blender generation 258.310 s; replay 96.783 / 101.090 s;
  838 mesh packets, 230 unique bodies. Replay Apply totals 10.052 / 10.197 s, including materials 7.125 / 7.236 s.
- Both native captures show imported geometry. Classroom baseline final output is very bright; this is preexisting.
- Initial native export script purged installed modules without unregistering their depsgraph handler. This
  inflated native generation time (BMW 11.638 s / Classroom 258.310 s). Those native timing results are excluded
  from the final comparison. Recorded-packet Engine replay results are unaffected. Corrected runs use factory
  startup, source addon only, and a cache-disabled control on the same binary.
- Cache replay: BMW 4.534 / 4.837 s (36 BVHs); Classroom 43.948 / 44.422 s (230 BVHs).
- Both GBuffer image comparisons are exact; final RGB RMS about 0.018 on 0-255 scale, P99=0,
  at/below baseline adjacent-frame noise. Captures contain three contiguous engine frames.

## Validation
Source validation, Debug build, transport, packet/geometry checks, captured images and live visual review are distinct.
Baseline evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderLinkPerf_20261011_01/baseline/summary.json`.
All six runs completed without Apply failures or queue overflow. Both source hashes unchanged; state backups restored.

- Final `cmake --build --preset build-debug`: exit 0; final-build.log retained beside Editor.
- BlenderMeshCacheSmoke: PASS (identical content, deliberately colliding sample keys, material slots,
  weak lifetime, byte-budget eviction, oversized/empty input). No GPU/window required.
- Script Python compilation, Node syntax check and scoped `git diff --check`: PASS.
- Exact recorded-packet imports: two fresh-process replay trials per scene/build; packet SHA256 identical.
- Corrected native control/optimized: one fresh Blender and Editor per scene/setting, factory startup;
  cache disabled/enabled through the process-local diagnostic flag. Same Debug Engine and scene/configuration.
- Both corrected native and exact-replay GBuffer comparisons are pixel-identical (1152 x 640, three frames).
  Final RGB RMS 0.018-0.019 / 255, P99=0, maximum channel delta=2; consistent with baseline temporal noise.
  Every capture has finite PNG pixels and contiguous engine_frame values.
- Fresh native exports are not byte-identical: BMW three meshes / Classroom two meshes have UV differences
  up to 1.79e-7 / 1.19e-7 and derived tangent differences up to 0.0348 / 7.28e-5. Positions, normals, headers
  and indices are identical. See native-geometry-deltas.json. Exact-packet replay is the strict geometry control.
- All four stage summaries verify restoration of nine preexisting runtime files and unchanged blend hashes.
  All owned test processes closed. User's unrelated dirty work retained; no blend, test scene or preferences saved.
- Interactive user Editor acceptance and arbitrary scenes/configurations are outside this bounded benchmark.

## Outcomes

| Scene / measurement | Before | After | Time reduction |
| --- | ---: | ---: | ---: |
| BMW27 native Full Sync | 13.072 s | 4.587 s | 64.9% |
| Classroom native Full Sync | 114.570 s | 64.424 s | 43.8% |
| BMW27 identical-packet replay median (2 runs) | 13.002 s | 4.685 s | 64.0% |
| Classroom identical-packet replay median (2 runs) | 98.937 s | 44.185 s | 55.3% |

Replay BVH builds drop from 110 to 36 (BMW) and 838 to 230 (Classroom). Sharing also avoids repeated vertex
decoding and GPU buffer creation; faster packet application consumes more packets within the existing 4ms Tick
budget, reducing inter-frame waits. No budget increase, protocol change, worker/Scene lifetime change or material
conversion change was needed. Cache is enabled by default in the built Debug plugin.

Remaining measured cost: Classroom Blender generation remains 56.329 / 58.823 s in the corrected native runs;
material import remains about 7.7 / 7.9 s. This optimization does not reduce bake work or wire bytes.
Future work can separately measure material baking/import and geometry-reference transport.

Evidence directory: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderLinkPerf_20261011_01/`.
`cache-comparison.json` contains repeated identical-packet timing/image results;
`native_optimized-comparison.json` contains corrected full-sync timing/image results;
each stage stores binary/source hashes, PID, stdout, engine log, captures, metrics and restoration evidence.
