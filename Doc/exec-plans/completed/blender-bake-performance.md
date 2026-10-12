# Blender bake performance

## Purpose and scope
Reduce native Blender Live Link bake cost; compare BMW27 and Classroom with unchanged resolution and samples.
Preserve source scenes, export suppression, bounded mesh/image lifetime, and Engine mesh-cache behavior.

## Architecture constraints and relevant files
`Doc/architecture/plugin-system.md`; `Tools/Blender/ailu_live_link/material_bake.py`;
`Test/BlenderLinkPerformance*.py`; `Test/BlenderLinkPerformanceCapture.mjs`.
Native Blender RNA and baking remain on Blender's main thread. Temporary context must restore on exceptions.

## Current milestone and progress
- M0: fresh native baseline, exact source backup, unchanged Editor binary. Complete.
- M1: one bake-scene context per material; scalar channels packed into one RGB emission bake;
  omit discarded roughness/metallic outputs for diffuse-only closures. Complete.
- M2: scalar and lifetime regressions, native scene timings and image comparisons, package and build. Complete.
- M3: repeat baseline and validate installed modules in fresh native Blender/Editor sessions. Complete.

## Decision log
Use red-channel extraction before packing to retain previous color-to-scalar semantics.
Keep separate coverage and normal bakes, sampling and resolution unchanged.

## Validation
Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderBakePerf_20261011_01`.
Baseline and optimized processes use factory startup and fresh Editor sessions; restore nine state files and
verify source blend hashes. Native images and scalar fixtures gate correctness independently of speed.

## Surprises and discoveries
Previous native Classroom export spent approximately 59 seconds on Blender generation; instance material
bakes contribute to the residual beyond the direct-material phase.
Shared material bake identities pick their first owner from an unordered object set; four Classroom material
names change only their owner suffix. Final texture matching uses the material name and its actual binding-owner
names, independent of random GUIDs and first-owner suffixes; it rejects ambiguous bindings.
Native mesh byte hashes vary between fresh exports
due to previously observed UV/tangent floating-point differences; GBuffer is an independent geometry check.

## Outcomes
First native pair: BMW generation 3807.97 -> 2916.83 ms, FullSync 4971.99 -> 4552.44 ms;
Classroom generation 59331.94 -> 36341.16 ms, FullSync 64916.91 -> 50737.11 ms.
Context-only Classroom generation: 38177.45 ms. Scene-context reuse accounts for most of its measured benefit.
BMW 51 textures match exactly; Classroom 911 textures match except one normal image with max 1/255,
RMS 0.00648/255. Scalar consumed channels, payload factors and transparency modes match.
Both GBuffer captures match exactly. Final RMS BMW 0.0168/255 and Classroom 0.0186/255, below baseline
adjacent-frame RMS 0.0172 and 0.0190 respectively; P99 zero and maximum 2/255. All captures have three
contiguous finite frames, 1152x640, AA disabled. This proves before/after equivalence for these views;
it does not claim Blender-render equivalence or fix pre-existing lighting/exposure behavior.
Native scalar comparison, scalar-map regression, Diffuse/Glossy regression and empty-instance/failure recovery pass.
`cmake --build --preset build-debug` passes. ZIP rebuilt and two changed installed modules hash-match source;
old installed modules and ZIP are backed up beside Editor. All nine state files restored and source blends unchanged.

Final paired native results (two old exports, two optimized exports; second optimized run loads installed modules):

| Scene | Blender generation median | Reduction | FullSync median | Reduction |
| --- | --- | --- | --- | --- |
| BMW27 | 3.722 -> 2.958 s | 20.54% | 4.727 -> 4.694 s | 0.69% |
| Classroom | 58.055 -> 36.392 s | 37.31% | 63.256 -> 51.085 s | 19.24% |

BMW end-to-end improvement is within observed run variation; do not infer a significant overall speedup.
Classroom generation ranges: old 56.78–59.33 s, optimized 36.34–36.44 s. Context-only result is one
native run and shows most of its benefit comes from fewer scene switches; packing adds a smaller benefit.
Installed modules load from Blender 5.0's user add-on directory; bake SHA256 matches final source and ZIP.
All five stages use identical Editor/Engine/plugin binary hashes and preserve source files and nine state files.
Installed native runs also pass 962 texture comparisons: same single normal-map 1/255 difference. GBuffer
matches exactly; final RMS BMW 0.01825/255 and Classroom 0.01869/255, P99 zero, max 2/255, consistent with
the stationary baseline's approximately 0.017–0.019/255 frame noise. This is numerical equivalence and
inspection of captured views, not interactive user visual acceptance of full-scene shading.
Remaining measured Classroom export cost includes direct-material 19.24 s and instance-material 13.49 s
in the first optimized run. Persistent bake caching or Engine texture-import optimization is a later task.
Evidence: `aggregate.json`, `optimized-comparison.json`, `installed-comparison.json`,
`optimized-texture-comparison.json`, `installed-texture-comparison.json`, `scalar-comparison.json`,
`build-debug.log`, `material_bake.patch`, `installation/`, `installation_final/`, and raw per-stage captures.
