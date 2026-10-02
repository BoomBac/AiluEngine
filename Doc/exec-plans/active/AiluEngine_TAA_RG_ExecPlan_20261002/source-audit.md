# Optional Source Audit - 2026-10-02 Snapshot

## Purpose

Provide source-level traceability for the plan. This file is optional and is not part of the normal resume context.

## Scope

Facts verified directly in `AiluEngine_Source(20261002-064447).zip`.

## Non-goals

This is not the implementation plan. Use milestone files for changes.

## Architecture Constraints

If the working tree differs from this snapshot, prefer the working tree and update `progress.md` with the delta.

## Baseline

### Camera

`Engine/Inc/Render/Camera.h`

- `GetProj()` returns `_proj_matrix` and is documented as including jitter.
- `GetProjNoJitter()` copies `_proj_matrix` and subtracts `_jitter.x/y` from `[2][0]/[2][1]`.
- `GetViewProj()` returns `_no_jitter_view_proj_matrix`.
- `GetPrevViewProj()` returns `_prev_view_proj_matrix`.
- One `_proj_matrix` is stored; there is no persistent `_proj_matrix_no_jitter`.

`Engine/Src/Render/Camera.cpp`

- `RecalculateMatrix()` builds `_proj_matrix`, computes `_no_jitter_view_proj_matrix = V * P`, then calls `ProcessJitter()`.
- `ProcessJitter()` edits `[2][0]/[2][1]` for perspective and `[0][3]/[1][3]` for orthographic.
- `CalculateFrustum()` executes after jitter and uses `_view_matrix * _proj_matrix`, so culling planes/corners jitter.
- `Scene::UpdateCameras()` calls `RecalculateMatrix(true)` every scene update, so jitter updates every frame.

### Camera constant buffer

`Engine/Src/Render/Renderer.cpp::PrepareCamera()`:

```text
_MatrixP           = cam.GetProj()       // jittered
_MatrixVP          = cam.GetViewProj()   // no-jitter, incorrect for raster contract
_MatrixVP_NoJitter = cam.GetViewProj()   // no-jitter
_MatrixIVP         = inverse(_MatrixVP)   // no-jitter
_MatrixVP_Pre      = previous frame's _MatrixVP_NoJitter
```

`Engine/Src/Render/RenderPipeline.cpp::ViewEntity::FormCamera()`:

```text
_MatrixP           = cam.GetProj()
_MatrixVP          = V * P               // jittered
_MatrixVP_NoJitter = cam.GetViewProj()
_MatrixIVP         = inverse(_MatrixVP)
_MatrixVP_Pre      = cam.GetPrevViewProj()
```

The two producers have diverged.

### Shader transform helpers

`common.hlsli` and `primitive.hlsli` normal clip helpers already use `_MatrixVP`. Explicit `NoJitter` helpers use
`_MatrixVP_NoJitter`. This is already the desired split.

`billboard.hlsl` copies `_MatrixP` then explicitly attempts to remove matrix jitter before rendering. This is a special-case
transparent path and must be re-audited under M5 rather than silently changed during M1.

### MotionVector

`MotionVectorVSMain`:

- `SV_POSITION` uses jittered normal raster helper.
- velocity uses `_MatrixVP_NoJitter` versus `_MatrixVP_Pre`.

`CameraMotionVectorPSMain`:

- reconstructs world position through `_MatrixIVP`.
- then compares no-jitter current and previous VP.

Therefore fixing `_MatrixIVP` to jittered is required for correct world reconstruction once raster jitter is real.

RG `DynamicMotion` writes `_motion_vector_tex` and a separate `_motion_vector_depth`. The material's MotionVector pass has
`ZTest: LEqual`, `ZWrite: On`.

### TAA

`Renderer::Renderer()` constructs `TemporalAA` but both `_features.push_back(_taa)` lines are commented.

`TemporalAA::AddRenderPasses()`:

- generates its own `HaltonSequence` per camera.
- computes another jittered projection.
- does not enqueue `TAAPreparePass`.
- enqueues `TAAExecutePass`.

`TAAExecutePass`:

- legacy `Execute()` only; no RG implementation.
- owns per-camera `TAAInfo` with A/B persistent textures.
- on first allocation copies current color to the current write target and then reads the other target as history.
- flips history in `EndPass()`, which is not called by the RG execution path.

`Compute/taa.hlsl`:

- defines `_JitterPrev` and `_JitterCur`.
- computes `prev_uv = uv - mv` and never applies jitter delta.
- defines `_HistoryFactor`, but final blend is hard-coded to `0.95`.

### RG scene-color chain

- MotionVector event: 450.
- Sprite event: 460.
- Forward transparent event: 475.
- CopyColor event: 500 and writes `_rg_handles._color_tex`.
- Intended TAA event: 550.
- PostprocessStack event: 575 and starts from `_color_tex` if valid.

`RenderingData` warns that `_color_target` should not be overwritten by postprocess registration because earlier execute
callbacks may reference `data._rg_handles._color_target` at execution time.

### Volumetric Clouds

Cloud pass event: 400. Tile reprojection reads `_motion_vector_tex`. Because MotionVector records later at 450, Cloud currently
captures an older version of that RG resource.

Cloud A/B history is flipped inside the RG compose execute callback. This is a useful pattern for TAA's RG ping-pong state.

### RayTraceGI

- Camera-dirty checks use `Camera::GetViewProj()`, currently stable no-jitter, which is desirable for detecting real camera
  changes.
- DXR path builds a duplicate `CBufferPerCameraData` and currently sets `_MatrixVP = cam.GetViewProj()`.
- Compute primary surface reconstruction uses `_MatrixIVP`.
- Realtime temporal path uses `history_uv = uv - motion` and does not apply jitter delta.

## Target Outcome

Use this audit only to verify that the working tree still resembles the baseline.

## Relevant Files

See README.

## Milestones

M0-M8.

## Current Milestone

M0.

## Progress

Audit complete for baseline snapshot.

## Decision Log

No additional decisions.

## Surprises & Discoveries

The shader helper layer is already correctly split between jittered and no-jitter helpers; the main mismatch is the CameraCB
producer.

## Validation

Audit was static source inspection only.

## Known Follow-ups

Re-audit source after each major milestone if the tree changes significantly.

## Outcomes

Source baseline captured.

## Resume Protocol

Do not load this file unless a milestone references a symbol that no longer matches the working tree.
