# Wasp close-up detail preservation

## Purpose
Preserve repeated Object/BOX height-map detail at close range; the previous 2048px atlas repair does not
preserve source texel density (measured median source/atlas ratios 8.22 and 25.81 on Fiber_cloth parts).

## Scope / Non-goals
Support the existing Standard Lit Normal slot as native object-space BOX height input for a recognized,
single Principled/Bump branch. Unsupported graphs retain normal baking. Preserve explicit DIRECT and
original scenes. Do not claim general Blender BSDF or lighting equivalence.

## Architecture constraints
Reuse material texture ownership, sampler settings, and existing shader property serialization. Keep
frame material layout compatible; raster forward/deferred/voxel must decode the same normal mode.
Do not overwrite user Blender/Editor processes or configuration. Source scene remains read-only.

## Relevant files
Tools/Blender/ailu_live_link/material_sync.py, material_bake.py; Plugins/BlenderLiveLink/Src/LiveLinkSession.cpp;
Engine/Res/Shaders/hlsl/standard_lit_common.hlsli, input.hlsli, defered_standard_lit.hlsl, forwardlit.hlsl.

## Current milestone
Complete: original-height/RGB transport, shared raster evaluation, independent GPU regression and native close-up capture.

## Progress
- M1 complete: inspected actual exported mesh packets and measured source BOX texels per atlas texel.
- M2 implemented: guarded primary normal extraction, original linear/sRGB height import, shared raster BOX
  evaluation and surface-gradient bump. Competing normal graphs retain baking. Frame material stride remains 128 bytes.
- Native export smoke passed: original 2x2 height bypasses a 64px atlas; composed normals retain bake fallback.
- Real scene routing recognizes Fiber_cloth's 2000x1333 leather and Metal_parts' 1920x1200 scratch height images.
- GBuffer shader compiles with the engine's HLSL 2018 semantics; plugin Debug build passed.
- Independent Blender oracle covers 72 cases: nine directions, two color spaces, two distances and inversion.
- Replaced the initial Engine-lifecycle smoke with a standalone DX12 raster test of the production shared helper.
  The initial test infrastructure failures are preserved as diagnostic artifacts, not accepted rendering evidence.
- M3 complete: 72 normal and 72 distinct-RGB color comparisons passed; D3D12 debug layer clean.
- Actual near-chest captures exposed remaining atlas blur in base color after the normal repair. Recognized exact
  opaque single-closure RGB branches now reuse the original height image, including identity zero-factor RGB Mix.
  Other color operations retain baking; existing Compute/DXR consumers retain the baked albedo fallback.
- Final installed-addon Full Sync passed: 38 meshes, 40 material packets, 534 binding checks and eight capture checks.
  All ten shared-state entries restored and owned Blender/Editor/oracle processes exited successfully.
- Debug build and all seven affected deferred/forward/voxel shader entry points passed. Addon source/ZIP/installed
  hashes match for all 17 Python modules. The source wasp scene SHA256 remains unchanged.

## Decision Log
- Raising the global atlas dimension cannot preserve repeated texture density within bounded memory.
- Reuse the Normal texture slot with explicit mode/parameters instead of a separate global resource.
- BOX projection reference: Blender Cycles kernel/svm/image.h; surface-gradient bump follows the
  native derivative model. Match blend/orientation and color-space handling explicitly.

## Surprises & Discoveries
- Body_armour_11 uses ~0.262 UV area and median 25.81 source texels per 2048 atlas texel.
- Existing Compute/DXR material consumers do not currently sample the normal texture; do not interpret
  a height image as a tangent normal in any newly introduced consumer.
- User-owned process checks were preserved; final native runs used only isolated owned processes.
- Raising normal precision alone left blurry baked base color visible in GBuffer. Final acceptance must inspect
  Blender background render, GBuffer base color and Engine final output separately.

## Validation
Evidence under F:/AiluEngine/artifacts/out/editor/bin/x64/debug/wasp_closeup_density_20261018.
- Final distinct-RGB GPU oracle: maximum per-case mean normal error 0.56214 degrees, P95 1.65066 degrees;
  maximum per-case color MAE 0.00016016, P95 absolute error 0.00042611 (linear RGB).
- Regression files: Test/ObjectBoxBumpReference.py, ObjectBoxBumpSmoke.cpp/.hlsl, AnalyzeObjectBoxBump.py;
  Tools/Blender/tests/material_fidelity_smoke.py checks faithful bake routing and native-height/RGB guards.
- Final normal/color results beside ObjectBoxBumpSmoke.exe: wasp_box_bump_gpu_20261018_color_final and
  wasp_box_color_gpu_20261018. Build/export/deployment evidence remains beside Editor.exe under
  wasp_closeup_density_20261018.
- Final native stage: wasp_closeup_native_color/20261018-175641-8786c820. Three-image report:
  wasp_closeup_native_color/skill_report_wasp_chest_detail_20261018/wasp_chest_detail_link_validation_report.md.
- Inspected all three final images: chest pores and fine grain retained in both base color and final shading;
  prior stretched base-color blur is removed on the recognized cloth branch.

## Outcomes
Close-up raster detail repair implemented, built, deployed and visually checked. This is not whole-scene Blender
BSDF/lighting parity: complex head normals, nonidentity/layered base-color graphs and roughness remain baked;
Compute/DXR do not use the raster BOX bump mode. Native near-chest views are not pixel-aligned due to camera
handedness/heading differences. Reconnect/restart persistence was inspected in saved assets, not exercised by
a second reopened Editor session. Original source SHA256:
f2b5fa51681ad90c850ba709657d65a37cc22e2163a04bb902ce1c628fe5082a.
