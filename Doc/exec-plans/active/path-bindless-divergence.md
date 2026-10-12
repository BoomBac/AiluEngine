# Path tracer fireflies and divergent resource reads

## Purpose
Investigate persistent bright points in DXR output after the Reference accumulation repair.
The user confirms the latest screenshot uses Disabled accumulation; it displays raw stochastic samples.

## Scope / Non-goals
Repair confirmed missing nonuniform resource-index annotations and solar NEE variance in path tracing.
Validate multiple resources and the live BMW scene in production DXR passes. No energy clamp or BRDF change.

## Architecture constraints
Keep current SceneRayTracingProxy ownership, light lookup, alias sampling and temporal modes.
Use local resource access annotations without global bindless macro changes.

## Relevant files
Engine/Res/Shaders/hlsl/ray_trace/{path_tracer_material,path_tracer_light_common,path_tracer_dxr_backend,hit,ray_surface}.hlsli
Test/TaaGpuAcceptance.cpp, Test/RunTaaTransparentAcceptance.ps1, Test/AnalyzeRtgiBindless.py

## Current milestone
Dense solar NEE artifact repaired and verified; localized remaining bright paths are unclassified.

## Progress
- [x] Confirm per-ray resource indices require NonUniformResourceIndex.
- [x] Annotate divergent mesh and material reads; eight shader variants and Debug build pass.
- [x] Verify GPU texture fixture and ordinary temporal regression with the limits recorded below.
- [x] Reproduce all three accumulation modes in a background Editor with the real Blender BMW session.
- [x] Isolate the solar light with a temporary component removal and undo restoration.
- [x] Independent directional NEE and three-technique MIS; eight shader variants and Debug build pass.
- [x] Verify the repaired BMW scene and rerun temporal regression.
- [ ] Classify the few localized car-light/floor bright paths before claiming all fireflies eliminated.

## Decision Log
Disabled output has no historical averaging. This screenshot alone cannot establish a convergence failure.
Nonuniform access is a correctness repair; its contribution to the reported bright points is unproven.
Solar NEE must not compete only through one unified draw with thousands of triangle emitters.
Add an independent directional technique and update both NEE contributions and BSDF misses consistently.

## Surprises & Discoveries
Doc/exec-plans/TEMPLATE.md is absent. Use the required plan sections directly.

## Validation
Eight Compute/DXR shader variants compile; emitted DXIL includes nonuniform annotations.
cmake --build --preset build-debug passes.
The red/blue fixture uses black perfect-metal surfaces to isolate primary emission from indirect stochastic energy.
- PathBindless_20261010_Textured_1: 640 contiguous frames, finite data, zero D3D12 ERROR/CORRUPTION,
  normal exit and 8/8 restored files. DXR baseline/Disabled known-emission gates pass (max RGB errors
  0.00021856 / 0.00022198). Compute fails both known-emission gates, with max error 1; not accepted.
  Exact cause of the Compute discrepancy is unverified. Representative raw images reviewed.
- PathBindless_20261010_Regression_1: 103/105 gates; two pre-existing TAA reset-seed gates still fail.
  Normal exit, finite contiguous captures and restored configuration. No new temporal failure observed.
- PathBindless_20261010_Car_4: actual Blender BMW27 scene auto-connects to the background Editor.
  Full Sync completion is observed before capture. Three modes reproduce bright points at 896x512,
  with debug target 0. Reference lengths 974..981 at every pixel prove accumulation is active.
  Disabled raw peak 26722.49, adjacent RMS 23.217; Realtime history peak 13026.33;
  Reference late peak 350.67 and adjacent RMS 0.013045. This baseline fails visual acceptance.
- PathBindless_20261010_CarSun_1: same camera/materials, sole LightComponent temporarily removed and restored via undo.
  Sun raw: peak 26722.54, 3073 pixels above 1 over 64 frames, RMS 20.68775, mean 0.07748248.
  No-sun raw: peak 3.35626, 2 pixels above 1 over 64 frames, RMS 0.00102872.
  This isolates the analytic sun contribution; removing sunlight is not the proposed repair.
- Failed startup/capture attempts are kept: Car_1 exceeded 120-second readiness; Car_2's replay client
  competed with the already running Blender client; Car_3 selected a skipped TransparentPass.
  All were cleaned up. The driver now uses live Blender sync, FinalBlit and a GPU warm-up period.
- PathSunNee_20261010_Car_1: 217 frames across 14 sequences, 30/30 transport/continuity/finite/settings/history/
  restoration gates. Three mode presentation images reviewed. Reference lengths 990..997 everywhere.
  Wall adjacent RMS 0.00840059 -> 0.0000356575 (99.58% lower), floor 0.00462329 -> 0.0000134488 (99.71%).
  Reference pixels above channel 10: 259.53125 -> 4 per frame. Mean RGB 0.07087167 -> 0.07182082 (+1.34%).
  No radiance clamp. Brightest raw pixel remains near car-light coordinates (529,254); a few floor spots remain.
  Actual Editor D3D12 debug layer was disabled; the separate production GPU regression enables it.
- PathSunNee_20261010_Regression_1: 103/105 gates, with only the same Compute/DXR TAA reset-seed failures.
  640 observations/4992 captures, normal exit, finite data, zero D3D12 ERROR/CORRUPTION and 8/8 restored files.
  Eight final shader variants and build-debug pass. No new PIX timing measurement.
  Evidence report: artifacts/path_sun_nee_20261010/result.md.

## Outcomes
Independent sun NEE with consistent MIS removes dense diffuse-wall/floor bright points in the live BMW DXR scene.
The bindless annotation is separately verified for DXR resource correctness; its contribution to this artifact is unproven.
This plan stays active for classification of the remaining localized bright paths. Do not claim all-firefly acceptance.
