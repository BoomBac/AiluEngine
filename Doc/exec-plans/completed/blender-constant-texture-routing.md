# Blender Live Link constant texture and routing optimization

## Purpose

Reduce constant baked texture files and avoid baking directly exportable materials that share an object with a complex material. Re-run the background Blender/Editor baseline for Classroom and BMW27.

## Scope / Non-goals

- Omit baked channel images when their consumed channel is constant and representable by existing material factors. Skip normal baking when the active shader closure has no linked Normal input; retain it when a normal-affecting graph is connected.
- Select bake/direct export per material while preserving object-specific bake identity for channels that require baking.
- Preserve Blender scene files, transport protocol, resolution, and unsupported graph behavior.
- Do not change Engine material APIs or bake resolution/sampling.

## Architecture constraints

- Blender RNA and Cycles calls remain on the main thread.
- Preserve the existing Blender Live Link packet fields and Engine factor semantics.
- Keep spatial and unsupported shader graphs on the native bake path.
- Preserve unrelated worktree changes.

## Relevant files

- `Tools/Blender/ailu_live_link/material_bake.py`
- `Tools/Blender/ailu_live_link/material_sync.py`
- `Test/BlenderConstantTextureRoutingSmoke.py`
- `Test/BlenderLinkPerformance.py` and its export helper for fresh scene measurements.

## Current milestone

Implement channel omission and material-slot-level routing, then verify unit fixtures and fresh background exports.

## Progress

- [x] Read Blender Live Link architecture and current implementation; record current baseline artifacts.
- [x] Add safe constant channel omission and per-material bake routing.
- [x] Run Blender regression fixtures and `build-debug`.
- [x] Re-export Classroom and BMW27 in background and compare counts, timings, texture outputs, Engine Apply, and scene integrity.
- [x] Review the task diff and record final outcomes.

## Decision Log

- Omit base color when RGBA is constant, emission when RGB is constant, and roughness/metallic maps when the consumed red channel is constant; preserve byte-equivalent factor values.
- If a normal-affecting graph is connected, keep its normal map even when uniform: PNG byte 128 decodes to a small positive tangent X/Y value rather than exact 0.5. A controlled replay changing only 28 exact-flat normal bindings confirmed the resulting small normal perturbation is amplified by Classroom's specular response. Exact float `(0.5,0.5,1.0)` would map to the interpolated vertex normal through TBN, but this PNG pipeline cannot represent 0.5 exactly in 8-bit channels.
- For an unlinked closure Normal input, use the mesh shading normal and omit the normal atlas entirely. This avoids introducing the PNG quantization deviation in materials that do not modify normals.
- Use existing base/emission/scalar material factors when omitting their map; retain all unsupported/nonconstant cases.
- Determine bake routing and bake GUID per material. An object's other direct materials retain their material GUID and direct export path.

## Surprises & Discoveries

- Constant outputs had been written unconditionally, including emission and scalar channels that were uniform across the atlas. The publisher now detects byte-level uniformity after the same color encoding/quantization used by PNG output and carries the decoded value in material factors.
- Bake routing was object-wide. It is now decided for each material slot; materials that need spatial evaluation still bake, while direct-compatible constant materials on mixed objects retain the direct path. Explicit `DIRECT` overrides are preserved.
- No source `.blend` files changed. The background harness restored Editor/project state and reported source hashes unchanged for both scenes.

## Validation

- Blender 5.0.1 background smoke: `CONSTANT_TEXTURE_ROUTING PASS` (constant channels, nonconstant textures, mixed-object routing, explicit direct override, and non-finite input guard).
- Follow-up Blender 5.0.1 background smoke: `CONSTANT_TEXTURE_ROUTING PASS`; confirms an unlinked Principled Normal is omitted from material payloads and a connected Bump output requires a normal bake.
- Fresh background export and Editor replay after normal-input filtering: BMW27 generation 5.73s, Classroom 38.64s; 0 apply failures and no queue overflow. Normal-map paths fell from 9 to 1 (BMW27) and 173 to 122 (Classroom), while normal bake passes fell by 8 and 49. Since this is one run per scene, wall-clock throughput is inconclusive: generation was 14.1% slower for BMW27 and 2.8% slower for Classroom than the prior single run; bake-pass time changed by +0.22s and -0.12s respectively.
- Both captures contain three contiguous frames at 1152x640. Geometry buffers match the prior capture pixel-for-pixel. Final images show small changes from normal-map removal: Classroom changed 0.76-0.77% of pixels, RMS 0.491/255 over RGBA, max 104 in a localized specular highlight; BMW27 max difference 2/255, P99 1. These are frame comparisons against the prior map-bound capture, not direct pixel comparison against Blender's viewport.
- `cmake --build --preset build-debug`: passed before the final Python-only normal-map adjustment; no C++ files changed afterward.
- Fresh background FullSync + packet replay (one replay each), current Debug Editor: BMW export 5.02s / Engine Apply 3.23s; Classroom export 37.59s / Engine Apply 17.04s. Both had zero Apply failures and no queue overflow.
- Versus recorded baseline: BMW export 17.58s -> 5.02s; Classroom 42.51s -> 37.59s. Material packets: 26 -> 25 and 214 -> 197. Unique channel-map paths: 961 baseline -> 562 final (41.5% fewer); normal maps are retained. These timing samples are single runs and environment-sensitive.
- Geometry and final-image captures each contain three contiguous frames. Relative to baseline replay captures, geometry RMS was 0/255 for BMW and 0.0124/255 for Classroom; final-image RMS was 0.0187/255 and 0.0192/255 respectively, P99 was 0 in all cases, maximum channel differences were 2 (BMW) and 4 (Classroom). Classroom final image was reviewed.
- `git diff --check` passed. The two input `.blend` hashes were unchanged and all harness restoration checks passed.
- Follow-up controlled A/B reused one Classroom packet stream, changing only `normal_path` for the 28 PNGs whose every pixel was `(128,128,255)`. Two keep-map runs were pixel-identical (A/A final image delta 0); Base Color GBuffer was also identical between A/B. Decoded GBuffer0 normals changed by RMS 0.118 degrees, P99 0.358 degrees, maximum 0.493 degrees. Final image changed at 19,880 of 737,280 pixels in frame 0; RMS 0.352/255, P99 1, maximum 145 at `(461,479)`. The bright outliers are consistent with a small normal perturbation moving sharp specular response; the controlled packet and A/A runs establish the omission as the cause.

## Outcomes

- Implemented constant-map elision, per-material bake routing, and normal-channel omission when the active shader closure has no linked Normal input. The initial baseline evidence is in `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderLinkOptimization_20261012_06/optimized`; the normal-input retest is in `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderNormalSkipRetest_20261012_01/optimized`.
