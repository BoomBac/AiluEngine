# AiluEngine TAA + RenderGraph Integration ExecPlan

> Source baseline: `AiluEngine_Source(20261002-064447).zip`
>
> This package is intentionally split by milestone. When implementing, load only `README.md`, `progress.md`, and the current
> `mX-*.md` file. `source-audit.md` is an optional reference and should not be loaded by default.

## Purpose

Restore Temporal Anti-Aliasing as a first-class RenderGraph feature in AiluEngine and make camera sub-pixel jitter consistent
with the modern rendering pipeline. The work must make TAA coexist with the current deferred renderer, MotionVector, SSAO,
Surfel GI, DDGI, Volumetric Fog, Volumetric Clouds, RayTraceGI, Sprite rendering, transparency, and post processing.

The primary objective is correctness of temporal state and matrix semantics. Do not start by tuning history weight or clamp
parameters. A visually acceptable result produced by compensating for an incorrect reprojection is not considered complete.

## Scope

- Define one authoritative camera jitter contract.
- Separate jittered render matrices from no-jitter culling/history matrices.
- Fix `Renderer::PrepareCamera()` so `_MatrixVP` and `_MatrixIVP` match the projection that actually rasterizes the frame.
- Keep MotionVector jitter-free and fix dynamic-object depth testing.
- Remove the legacy TAA-owned Halton sequence.
- Fix TAA reprojection, history initialization, resize/toggle/camera-cut invalidation, and per-camera state.
- Add a real `TAAExecutePass::OnRecordRenderGraph()` path.
- Make TAA feed the RG post-process chain through `_rg_handles._color_tex` / an explicit TAA output handle.
- Audit transparency/Sprite behavior and document the first supported policy.
- Fix Volumetric Clouds reading a stale motion-vector RG version and add jitter compensation if cloud rays remain jittered.
- Align RayTraceGI screen reconstruction and temporal history with the same camera contract.
- Run combination tests and remove obsolete legacy TAA paths.

## Non-goals

- TSR/TAAU or dynamic-resolution upscaling.
- DLSS/FSR/XeSS integration.
- A complete transparent-motion-vector system in the core TAA milestone.
- Replacing the current neighborhood clamp with a new research-grade TAA algorithm.
- Rewriting Surfel GI, DDGI, Volumetric Fog, Volumetric Clouds, or RayTraceGI algorithms unrelated to temporal compatibility.
- Performance optimization before correctness is established.
- Changing the engine matrix convention globally.

## Architecture Constraints

### Camera temporal contract

The target contract is:

```text
Camera projection state
    P_no_jitter
    P_jittered

Camera VP state
    VP_no_jitter_current
    VP_no_jitter_previous
    VP_jittered_current
    IVP_jittered_current

Jitter state
    jitter_matrix_current
    jitter_uv_current
    jitter_uv_previous OR per-feature previous jitter copied from Camera
```

Shader constant-buffer contract:

```text
_MatrixP            = P_jittered
_MatrixVP           = VP_jittered_current
_MatrixIVP          = inverse(VP_jittered_current)
_MatrixVP_NoJitter  = VP_no_jitter_current
_MatrixVP_Pre       = VP_no_jitter_previous
```

`_MatrixVP` is for rasterization. `_MatrixVP_NoJitter` and `_MatrixVP_Pre` are for geometric motion. `_MatrixIVP` must always
match the projection that produced the depth texture being reconstructed.

### Jitter ownership

Camera is the only generator of camera sub-pixel jitter. `TemporalAA` may cache a previous Camera jitter value, but it must not
own a second Halton sequence or independently modify projection matrices.

### Motion-vector contract

MotionVector does not contain TAA jitter:

```text
motion_uv = current_no_jitter_uv - previous_no_jitter_uv
```

Temporal consumers that operate on jittered frame images must add the sample-pattern displacement themselves:

```text
history_uv = current_uv - motion_uv + previous_jitter_uv - current_jitter_uv
```

The exact UV Y sign must be established once in Camera's UV-jitter convention. Do not scatter ad-hoc Y negations across TAA,
Cloud, and RayTraceGI.

### Culling contract

TAA jitter must not change CPU visibility, LOD selection, or culling. `Camera::GetViewFrustum()` must be based on the no-jitter
projection.

Screen-ray data used by rendering features is a separate concept from the culling frustum. If `_LT/_LB/_RT/_RB` is kept as
jittered render rays for Volumetric Clouds, do not derive the culling planes from those same jittered corners.

### RenderGraph history ownership

TAA history is persistent across frames and therefore owned by `TemporalAA`/`TAAExecutePass`, not allocated as a transient RG
resource each frame. Each frame imports the current history read/write textures into RG and declares all reads/writes explicitly.

### Validation gates

Every milestone has independent Acceptance Criteria. Do not enter the next milestone until the current one passes. In
particular:

```text
M1 Camera contract must pass before M2 MotionVector.
M2 MotionVector must pass before M3 TAA reprojection/history.
M3 must pass before M4 RenderGraph integration.
```

Do not lower `_HistoryFactor`, disable jitter, or increase neighborhood clamp merely to make a failed gate appear stable.

## Baseline

Verified against the 2026-10-02 source snapshot:

1. `TemporalAA` is constructed but not added to `_features` in `Renderer::Renderer()`.
2. `RenderFeature::_is_active` is not initialized in the base constructor. `TemporalAA` also does not initialize it.
3. `Camera::RecalculateMatrix()` builds `_no_jitter_view_proj_matrix` before `ProcessJitter()`, then calls
   `CalculateFrustum()` after jitter has modified `_proj_matrix`.
4. `Camera::GetProjNoJitter()` attempts to reconstruct a no-jitter matrix by subtracting `_jitter.x/y`; this only matches the
   perspective path and is invalid for the current orthographic jitter path.
5. `Renderer::PrepareCamera()` currently sets both `_MatrixVP` and `_MatrixVP_NoJitter` to `cam.GetViewProj()`, so both are
   no-jitter. `_MatrixIVP` is therefore also no-jitter.
6. `ViewEntity::FormCamera()` already uses the intended render-side form: `_MatrixVP = V * P`, where `P` is current jittered
   projection, and `_MatrixVP_NoJitter = cam.GetViewProj()`.
7. `common.hlsli` and `primitive.hlsli` already use `_MatrixVP` in normal clip-space helpers and expose explicit `NoJitter`
   helpers. No shader-helper rewrite is required for the normal raster path.
8. `MotionVectorVSMain` rasterizes with jittered `TransformPrimitiveToClipSpace()` but computes current/previous velocity from
   `_MatrixVP_NoJitter` and `_MatrixVP_Pre`, which is the desired direction.
9. RG `DynamicMotion` binds a separate `_motion_vector_depth` instead of scene depth. The MotionVector material uses `ZTest:
   LEqual` and `ZWrite: On`, so the dynamic pass cannot reliably reject dynamic geometry hidden behind static foreground.
10. `TemporalAA::AddRenderPasses()` still owns a second `HaltonSequence`; `TAAPreparePass` is no longer enqueued.
11. `TAAExecutePass` has no RG recording implementation.
12. `TAAInfo` creates A/B history, copies the current frame only into the current write target, then samples the opposite
    uninitialized target as history on the first resolve.
13. Compute TAA defines `_JitterPrev/_JitterCur` but uses only `prev_uv = uv - mv`.
14. Compute TAA uploads `_HistoryFactor` but blends with hard-coded `0.95`.
15. Compute TAA depends on depth and motion vector but the legacy execute path does not bind them explicitly; it relies on
    global state.
16. `CopyColorPass` writes RG `_color_tex` at event 500. `PostprocessStack` reads `_color_tex` and starts at event 575.
17. `VolumetricCloudsPass` is event 400 but its tile reprojection reads `_motion_vector_tex`; MotionVector is event 450. In
    RG this records a read of an older motion-vector version.
18. Volumetric Clouds flip their persistent A/B history inside the RG execute callback, not `EndPass()`. TAA should follow the
    same RG-safe principle.
19. Volumetric Fog intentionally uses `GetProjNoJitter()` for froxel/history logic and should remain stable after the Camera
    contract is fixed.
20. RayTraceGI has an additional DXR CameraCB construction path that currently repeats the same no-jitter `_MatrixVP` problem.
21. RayTraceGI temporal reprojection uses `history_uv = uv - motion` and currently has no camera-jitter delta.
22. Sprite pass is event 460; Forward transparent pass is event 475; both execute before `CopyColor` and therefore before the
    intended TAA event 550.

## Target Outcome

```text
Camera update
    -> build stable no-jitter matrices
    -> generate one jitter sample
    -> build jittered render matrices
    -> build stable culling frustum
    -> expose jittered screen-ray corners separately if required

Opaque raster / depth
    -> jittered VP

Depth reconstruction
    -> jittered IVP

MotionVector
    -> current no-jitter VP versus previous no-jitter VP

Scene effects
    -> consume consistent depth/matrix semantics

TAA RG pass
    -> read scene color + depth + motion + history
    -> reproject using motion + jitter delta
    -> reject invalid history
    -> write persistent current history / TAA output

Postprocess
    -> Bloom
    -> ToneMapping

Editor overlays
    -> after postprocess where appropriate
```

## Relevant Files

Primary implementation files:

```text
Engine/Inc/Render/Camera.h
Engine/Src/Render/Camera.cpp
Engine/Src/Render/Renderer.cpp
Engine/Src/Render/RenderPipeline.cpp
Engine/Inc/Render/ShaderInterop.h
Engine/Inc/Render/RenderingData.h
Engine/Inc/Render/Features/RenderFeature.h

Engine/Inc/Render/Features/TemporalAA.h
Engine/Src/Render/Features/TemporalAA.cpp
Engine/Res/Shaders/hlsl/Compute/taa.hlsl

Engine/Src/Render/Features/CommonPasses.cpp
Engine/Res/Shaders/hlsl/motion_vector.hlsl
Engine/Res/Shaders/hlsl/defered_standard_lit.hlsl

Engine/Src/Render/Features/VolumetricClouds.cpp
Engine/Res/Shaders/hlsl/Compute/volumetric_cloud.hlsl

Engine/Src/Render/Features/VolumetricFog.cpp
Engine/Src/Render/Features/RayTraceGI.cpp
Engine/Res/Shaders/hlsl/ray_trace/raytrace_gi.hlsl

Engine/Src/Render/2D/SpriteRenderFeature.cpp
Engine/Src/Render/2D/SpriteRenderPass.cpp
```

## Milestones

| Milestone | Purpose | Gate |
|---|---|---|
| M0 | Baseline, instrumentation, and small safety fixes | Required |
| M1 | Camera temporal/matrix contract | Required before M2 |
| M2 | MotionVector correctness | Required before M3 |
| M3 | TAA core reprojection and history lifecycle | Required before M4 |
| M4 | Native RenderGraph TAA integration | Core TAA complete |
| M5 | Transparent/Sprite compatibility policy | Required before calling TAA generally robust |
| M6 | Volumetric Clouds temporal compatibility | Required if Clouds are enabled |
| M7 | RayTraceGI temporal compatibility | Required if RTGI is enabled with TAA |
| M8 | Combination regression, cleanup, and performance capture | Final gate |

## Current Milestone

`M8 - Integration, cleanup, and performance`

## Progress

See `progress.md`.

## Decision Log

- D001: Camera is the only camera-jitter generator.
- D002: `_MatrixVP` and `_MatrixIVP` are jittered render matrices.
- D003: Motion vectors exclude TAA jitter.
- D004: Culling uses no-jitter projection.
- D005: Persistent temporal history is imported into RG each frame.
- D006: Correctness gates precede TAA quality tuning.
- D007: Keep `Camera::GetViewProj()` no-jitter semantics for compatibility; add/use explicit jittered/no-jitter accessors rather
  than silently changing its meaning across the engine.

## Surprises & Discoveries

Record new cross-subsystem findings in `progress.md`. If a discovery changes a global matrix or history contract, stop the
current implementation step and document the decision before patching multiple systems.

## Validation

Maintain these reusable scenes/tests:

```text
A. Static high-contrast geometry, thin rails, diagonal edges.
B. Slow camera translation.
C. Slow camera rotation.
D. One moving opaque object.
E. Moving object fully/partially hidden by static foreground.
F. Alpha-test geometry.
G. Blended transparent object and moving Sprite.
H. Volumetric Fog.
I. Volumetric Clouds tile rendering.
J. Surfel GI + DDGI.
K. RayTraceGI realtime temporal mode.
L. Orthographic camera.
M. Camera cut / teleport / FOV change / resize / TAA toggle.
```

## Known Follow-ups

After this plan:

- Reactive mask and transparent velocity.
- Velocity dilation / disocclusion mask.
- Catmull-Rom or improved history sampling.
- Better variance clipping and anti-flicker heuristics.
- TAA sharpening.
- Unified temporal-history registry.
- TAAU / dynamic resolution.

## Outcomes

The final result must allow TAA to remain enabled as a normal RG feature without relying on stale globals, duplicate jitter
state, invalid first-frame history, or accidental matrix mismatches.

## Resume Protocol

1. Read `README.md`.
2. Read `progress.md`.
3. Read only the current `mX-*.md`.
4. Execute the first unchecked task in that milestone.
5. Run the validation immediately after the corresponding implementation step.
6. Update `progress.md` with changes, validation, discoveries, and blockers.
7. Do not load the next milestone until all current Acceptance Criteria pass.
8. Load `source-audit.md` only if a symbol/path needs re-confirmation.
