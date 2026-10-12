# Path tracer emissive hit optimization

## Purpose
Remove the per-path-ray scan of all emissive triangles in NewCapture_2026-10-10_205128.wpix.

## Scope / Non-goals
Reuse scene tracing for mesh light hits, preserve alias NEE and MIS, index analytic/directional lights separately.
Do not change shader optimization mode, shadow tracing, path depth, or introduce ReSTIR/light BVH.

## Architecture constraints
SceneRayTracingProxy owns lookup CPU/GPU lifetime. Use Renderer ScenePrimitive indices, not ECS traversal ordinals.
Register lookup reads in RenderGraph; accelerate DXR and preserve the Compute compatibility path. No serialized/reflected layout changes.

## Relevant files
SceneRayTracingProxy.{h,cpp}, RayTraceGI.cpp, path_tracer_* shaders, rendering.md.

## Current milestone
Completed bounded DXR optimization; performance and original-scene acceptance remain unmeasured.

## Progress
- [x] Inspect capture and scene/BLAS primitive ownership.
- [x] M1 implement lookup and accelerated hits; compile all eight Compute/DXR keyword combinations.
- [x] M2 Debug build, production rendering checks, record limitations, review diff.

## Decision Log
- Use a proxy-owned lookup buffer instead of expanding persistent PrimitiveData.
- Preserve DXR surface reflection and weight emission with the NEE competition PDF.
- Preserve full finite-light intersection in Compute: removing it lost emissive rail coverage in the real fixture.

## Surprises & Discoveries
- Existing mesh light indices were inferred from ECS order; Renderer indices are authoritative.
- Doc/exec-plans/TEMPLATE.md and historical render_capture_client.mjs are absent; no Editor is running.

## Validation
- Debug build passed, including the targeted lookup test entry. A simultaneous test process initially locked
  TaaGpuAcceptance.pdb; rebuilding after that process ended succeeded.
- Offline DXC: DXR lib_6_3 and Compute PrimaryRay cs_6_6, uniform/power x MNEE off/on all passed.
- First GPU fixture reached its 240-second limit during cold shader compilation, before capture; state restoration passed.
- The first lookup fixture exposed a test setup error (Scene mutation before RenderPipeline registration);
  corrected the fixture lifecycle, rebuilt and reran in a unique directory with a bounded 900-second timeout.
- Lookup_4: all 18 mapping checks passed, normal exit, zero D3D12 ERROR/CORRUPTION, all state restoration checks passed.
- RTGI_2: 640 contiguous observations / 4992 captures. Compute lost emissive rail coverage after replacing
  its full light scan, so restored that compatibility path and reran the same fixture (RTGI_3).
- Baseline_1: unmodified shader control passed 101/103 gates; both TAA reset seed checks already fail.
- RTGI_3: 640 contiguous frames / 4992 captures; finite pixels, normal exit, zero D3D12 errors,
  all state restoration checks passed. Final and baseline both pass 101/103 gates with the same two existing
  TAA reset failures. Compute raw output is bit-exact across 320 frames; DXR raw comparison RMS=0.0020608.
- Final montage reviewed; emissive rails present in both backends. No interactive/original-scene acceptance
  or measured GPU speedup is claimed. Evidence: artifacts/path_light_optimization_20261010/result.md.

## Outcomes
DXR uses its hardware scene hit and indexed light metadata instead of an emissive-triangle scan.
Compute keeps its validated compatibility fallback. Eight shader variants and the Debug build pass.
Known TAA reset failures and unmeasured original-scene GPU performance remain explicit acceptance boundaries.
