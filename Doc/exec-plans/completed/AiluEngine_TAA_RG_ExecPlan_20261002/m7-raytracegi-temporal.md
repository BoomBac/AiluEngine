# M7 - RayTraceGI Camera and Temporal Compatibility

## Purpose

Make RayTraceGI use the same raster/depth camera sample and the same jitter-aware history mapping as TAA.

## Scope

- Remove duplicate/inconsistent DXR CameraCB semantics.
- Ensure current surface reconstruction uses jittered IVP.
- Keep camera-change detection on stable no-jitter matrices.
- Add jitter delta to realtime RTGI temporal reprojection where required.
- Coordinate RTGI history reset with camera cut/resize.

## Non-goals

- No path-tracer integrator rewrite.
- No reservoir algorithm changes unrelated to camera temporal state.
- No denoiser retuning before reprojection is correct.

## Architecture Constraints

TAA owns camera projection jitter. RayTraceGI may own stochastic path/ray sampling, but it must not generate a competing camera
projection sequence.

## Baseline

RTGI currently uses `Camera::GetViewProj()` for camera-dirty detection. Since this is no-jitter, that is desirable and should
remain stable.

DXR path builds a private CameraCB and currently does:

```cpp
g_camData._MatrixP = cam.GetProj();
g_camData._MatrixVP = cam.GetViewProj();
g_camData._MatrixVP_NoJitter = cam.GetViewProj();
g_camData._MatrixIVP = MatrixInverse(g_camData._MatrixVP);
```

This repeats the pre-M1 main Renderer mismatch.

Compute RTGI reconstructs current surface world position from depth with `_MatrixIVP`, and realtime temporal uses:

```hlsl
float2 history_uv = uv - motion;
```

without jitter delta.

## Target Outcome

- Current GBuffer/depth sample and RTGI surface reconstruction refer to the same jittered camera sample.
- Camera dirty/history signature ignores subpixel Halton changes.
- RTGI realtime history maps current jittered pixels to previous jittered pixels using no-jitter motion plus jitter delta.
- Camera cut resets both TAA and RTGI history as appropriate.

## Relevant Files

```text
Engine/Src/Render/Features/RayTraceGI.cpp
Engine/Inc/Render/Features/RayTraceGI.h
Engine/Res/Shaders/hlsl/ray_trace/raytrace_gi.hlsl
Engine/Res/Shaders/hlsl/ray_trace/path_tracer_dxr_backend.hlsli
Engine/Inc/Render/ShaderInterop.h
```

## Milestones

### M7.1 - Keep dirty detection no-jitter

Do not replace:

```cpp
rendering_data._camera->GetViewProj()
```

with jittered VP in camera-change/reference accumulation checks. Halton jitter is not a real camera movement and must not reset
reference accumulation every frame.

If explicit M1 accessors exist, prefer `GetViewProjNoJitter()` for clarity.

### M7.2 - Fix duplicate DXR CameraCB builder

Best option: reuse a helper that builds `CBufferPerCameraData` using the same contract as `Renderer::PrepareCamera()`.

If direct reuse is not practical, at minimum set:

```cpp
g_camData._MatrixV = cam.GetView();
g_camData._MatrixP = cam.GetProj();
g_camData._MatrixVP = cam.GetViewProjJittered();
g_camData._MatrixVP_NoJitter = cam.GetViewProjNoJitter();
g_camData._MatrixIVP = MatrixInverse(g_camData._MatrixVP);
g_camData._MatrixVP_Pre = cam.GetPrevViewProj();
```

Also populate jitter fields if DXR shaders consume the same camera buffer layout.

Do not leave a second semantic definition of CameraCB in RTGI.

### M7.3 - Verify current surface reconstruction

Compute `RayGen` samples GBuffer/depth at pixel center and calls:

```hlsl
ComputeWorldSpacePosition(uv, depth, _MatrixIVP)
```

After M1 this should reconstruct the exact world surface that generated the jittered depth. Validate by visualizing position or
normal agreement. Do not add a no-jitter correction here.

### M7.4 - Pass previous/current Camera UV jitter to RTGI temporal

RTGI realtime history currently reprojects with motion only. Add the same Camera jitter delta used by TAA:

```hlsl
float2 history_uv = uv - motion + previous_jitter_uv - current_jitter_uv;
```

Update all related pixel-space conversions consistently, including the later `history_center` calculation used for neighboring
history samples. Do not fix only one history lookup while leaving neighbor validation centered on the old position.

A useful normalized flow:

```text
history_uv
    -> history_position = history_uv * screen_size - 0.5
    -> history_base/history_fraction
    -> history_center for neighbor validation
```

Prefer deriving all history pixel coordinates from the same `history_uv` rather than repeating `pixel - motion * size` in
separate locations.

### M7.5 - Reference accumulation mode

Reference mode may intentionally reset on any real camera matrix change and may not use motion reprojection the same way as
realtime mode. Keep its semantics separate. Camera jitter must not count as camera movement because dirty detection is based on
no-jitter VP.

### M7.6 - Coordinate history invalidation

RTGI already detects:

```text
history signature changes
camera cut
accumulation mode transition
target recreation
reference camera change
```

Ensure the same cut/resize event that invalidates TAA also invalidates RTGI when appropriate. Reuse shared camera-cut state if
M3 introduced one; avoid separate thresholds drifting over time.

### M7.7 - DXR primary-ray path audit

`path_tracer_dxr_backend.hlsli` uses `g_perCamData._MatrixIVP` for ray unprojection. After DXR CameraCB is fixed this becomes
jittered. Confirm this is desired for current raster/GBuffer-coupled modes.

If standalone reference path tracing should use a stable camera sample plus independent pixel jitter, separate that mode
explicitly rather than making the shared CameraCB ambiguous.

## Current Milestone

M7.

## Progress

M7 automated single-camera production-RG acceptance passed after repair/retest on 2026-10-03. Compute and DXR
execute all five modes; 640 continuous observations and 4992 DDS pass all 103 gates. Interactive use, multi-camera,
ReSTIR execution, indirect-light-only boundaries and GPU cost remain separate. See progress.md for final evidence.

## Decision Log

- Camera dirty detection is no-jitter.
- Current surface reconstruction is jittered.
- Realtime history uses no-jitter motion plus camera jitter delta.

## Surprises & Discoveries

RTGI already has significant temporal logic and history validation. Do not rewrite it wholesale. Limit changes to camera sample
alignment and shared reset semantics unless validation proves another issue.

## Validation

### V1 - RTGI temporal only

TAA off, Camera jitter off: preserve existing RTGI behavior.

### V2 - Camera jitter + RTGI temporal, final TAA off

Enable Camera jitter intentionally for test. Surface history should remain aligned after jitter compensation.

### V3 - Full combination

RTGI temporal on + TAA on. Look at high-contrast indirect-light boundaries and moving camera.

### V4 - Camera cut

No stale GI history after teleport/large rotation.

### V5 - Reference mode

Halton camera jitter must not reset reference accumulation every frame. If reference mode should disable raster TAA jitter
entirely, enforce that as a clear mode policy.

## Acceptance Criteria

- [x] RTGI dirty detection explicitly uses no-jitter VP.
- [x] DXR CameraCB follows the main Camera contract (source confirmed; captured static foreground agrees with raster).
- [x] Current jittered depth/normal reconstruction validated with production CameraCB/helper measurement kernel.
- [x] Realtime RTGI history includes camera jitter delta (primary taps and neighbor center; ReSTIR source audited).
- [x] All history-neighbor coordinates derive from the corrected history location.
- [x] Camera cut/resize/FOV/explicit reset seed RTGI immediately; full TAA seeds exact current RGB.
- [x] Reference mode behavior is explicit and not accidentally reset by Halton jitter (captured32-frame static sequence).
- [x] TAA + RTGI temporal has best integer offset[0,0]; quadratic subpixel estimates are below0.023pixels.

## Known Follow-ups

Interactive/multi-camera acceptance, ReSTIR execution, indirect-light-only scenes and GPU-cost profiling are unmeasured.
The current compute backend dispatches stochastic PrimaryRay, not the inactive GBuffer RayGen; the depth/normal measurement
uses the same production reconstruction helpers and camera constants without replacing the production kernel.

## Outcomes

M7 automated acceptance passes in the bounded procedural single-camera scene. Fixed GI/snapshot ordering, denoise
neighbor jitter, shared explicit-reset observation, first-resize resource readiness and execution-time history commits.
Debug build and CameraTemporalHistorySmoke pass; M5 seven-material regression passes784 frames/3136 DDS. M7 capture
process exits0, D3D12 ERROR/CORRUPTION count0, and all8 backed-up files restore with matching existence/hashes.

Final evidence: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M7_FinalRetest_20261003/run1/report.md.
The original failure and intermediate measurement/driver failures remain historical records. Geometry coverage excludes
one1/256 reference-pixel boundary band: the ideal-float oracle differed on96 pixels within0.001373pixels of a horizontal
edge. Coverage outside the band is exact; plane/normal checks include every pixel. Reference replay keeps its original
threshold and documented robust-coordinate subset. No universal visual, indirect-light-only or performance claim is made.

## Resume Protocol

Use the FinalRetest and M5Regression evidence. Continue only the requested scope; M8 remains unstarted.
