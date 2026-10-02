# M1 - Camera Temporal and Matrix Contract

## Purpose

Make Camera expose an unambiguous jittered render transform and stable no-jitter transform, then make every CameraCB producer
follow the same contract.

## Scope

- Store no-jitter projection explicitly.
- Build/store current jittered VP explicitly.
- Fix orthographic jitter bookkeeping.
- Separate culling frustum from render-ray corners.
- Fix `Renderer::PrepareCamera()`.
- Align `ViewEntity::FormCamera()` with the same helpers.
- Preserve stable `GetViewProj()` compatibility while adding explicit accessors.
- Audit screen/world helpers that should or should not use jitter.

## Non-goals

- Do not change MotionVector formulas yet.
- Do not modify TAA history or RG.
- Do not change Volumetric Fog temporal logic.
- Do not tune Cloud/RTGI temporal accumulation yet.

## Architecture Constraints

Do not change `GetViewProj()` to suddenly mean jittered VP. Existing code uses it as a stable no-jitter matrix for camera-change
detection and history. Add explicit APIs instead.

Recommended public API shape:

```cpp
const Matrix4x4f &GetProj() const;
const Matrix4x4f &GetProjNoJitter() const;
const Matrix4x4f &GetViewProj() const;             // compatibility: no-jitter
const Matrix4x4f &GetViewProjNoJitter() const;
const Matrix4x4f &GetViewProjJittered() const;
const Matrix4x4f &GetPrevViewProj() const;         // previous no-jitter
const Vector4f &GetJitter() const;
```

Keep the existing code-style conventions.

## Baseline

Current `Camera` stores only `_proj_matrix`. `GetProjNoJitter()` tries to reconstruct a stable projection by subtracting
perspective jitter fields. That cannot restore the current orthographic path, which modifies `[0][3]/[1][3]`.

Current `Renderer::PrepareCamera()` is wrong for TAA:

```cpp
cam_cb_data->_MatrixP = cam.GetProj();
cam_cb_data->_MatrixVP = cam.GetViewProj();
cam_cb_data->_MatrixVP_NoJitter = cam.GetViewProj();
cam_cb_data->_MatrixIVP = MatrixInverse(cam_cb_data->_MatrixVP);
```

`ViewEntity::FormCamera()` already demonstrates the intended main relationship:

```cpp
_MatrixP = cam.GetProj();
_MatrixVP = _MatrixV * _MatrixP;
_MatrixVP_NoJitter = cam.GetViewProj();
_MatrixIVP = MatrixInverse(_MatrixVP);
```

`Camera::CalculateFrustum()` currently consumes jittered `_proj_matrix`, so culling is jittered.

## Target Outcome

```text
P_no_jitter                stable for a fixed camera
P_jittered                  changes with Halton sample
VP_no_jitter_current        stable for a fixed camera
VP_no_jitter_previous       previous real camera transform
VP_jittered_current         used by raster
IVP_jittered_current        reconstructs current jittered depth
culling frustum             built from no-jitter VP
render ray corners          may remain jittered for screen-ray effects
```

## Relevant Files

```text
Engine/Inc/Render/Camera.h
Engine/Src/Render/Camera.cpp
Engine/Src/Render/Renderer.cpp
Engine/Src/Render/RenderPipeline.cpp
Engine/Inc/Render/ShaderInterop.h
Engine/Res/Shaders/hlsl/common.hlsli
Engine/Res/Shaders/hlsl/primitive.hlsli
Engine/Res/Shaders/hlsl/billboard.hlsl
```

## Milestones

### M1.1 - Store stable projection state

Add at least:

```cpp
Matrix4x4f _proj_matrix_no_jitter{};
Matrix4x4f _jittered_view_proj_matrix{};
```

Keep `_proj_matrix` as the current jittered render projection to minimize call-site churn.

Change `GetProjNoJitter()` from reconstructing a matrix to returning the stored matrix:

```cpp
const Matrix4x4f &GetProjNoJitter() const { return _proj_matrix_no_jitter; }
```

Add explicit VP accessors:

```cpp
const Matrix4x4f &GetViewProjNoJitter() const { return _no_jitter_view_proj_matrix; }
const Matrix4x4f &GetViewProjJittered() const { return _jittered_view_proj_matrix; }
```

Leave `GetViewProj()` returning `_no_jitter_view_proj_matrix` for compatibility.

### M1.2 - Rebuild RecalculateMatrix in a deterministic order

Target sequence:

```text
1. Save previous no-jitter VP once per frame.
2. Build P_no_jitter.
3. Copy P_no_jitter -> P_jittered.
4. Build view.
5. Build VP_no_jitter.
6. Reset current jitter to zero.
7. If TAA, apply one Camera jitter to P_jittered.
8. Build VP_jittered.
9. Build stable culling frustum from VP_no_jitter.
10. Build jittered render-ray corners separately if those corners are consumed by screen-ray effects.
```

Pseudo-code shape:

```cpp
void Camera::RecalculateMatrix(bool force)
{
    if (!force && !_is_dirty)
        return;

    UpdatePreviousTemporalStateOncePerFrame();
    BuildNoJitterProjection(_proj_matrix_no_jitter);

    _proj_matrix = _proj_matrix_no_jitter;
    BuildViewMatrixLookToLH(_view_matrix, _position, _forward, _up);
    _no_jitter_view_proj_matrix = _view_matrix * _proj_matrix_no_jitter;

    _jitter = Vector4f::kZero;
    if (_anti_aliasing == EAntiAliasing::kTAA)
        ProcessJitter();

    _jittered_view_proj_matrix = _view_matrix * _proj_matrix;
    CalculateCullingFrustum();
    CalculateRenderRayCorners();
}
```

Do not copy this literally if helper names differ; preserve engine conventions.

### M1.3 - Define jitter semantics once

Target meaning of `Camera::_jitter`:

```text
xy = projection-matrix/NDC jitter representation used by render projection
zw = actual texture-UV displacement for temporal reprojection
```

`zw` must use texture UV coordinates, including the engine's Y direction. The purpose is that every temporal shader can use:

```text
previous_uv_jitter - current_uv_jitter
```

without feature-specific Y fixes.

Perspective path currently computes matrix jitter correctly enough for the existing projection, but UV Y sign must be
validated against the actual projected pixel shift. Orthographic must also populate `_jitter.x/y`; it currently changes the
matrix without doing so.

Recommended validation rather than guessing sign:

1. Select a fixed world-space point near screen center.
2. Project it through `VP_no_jitter` to texture UV.
3. Project it through `VP_jittered` to texture UV.
4. Verify `GetJitter().zw == jittered_uv - no_jitter_uv` within epsilon.
5. If not, fix Camera's UV-jitter conversion. Do not compensate later in TAA.

### M1.4 - Split culling frustum and render-ray corners

Current `CalculateFrustum()` both generates corners and planes from the jittered projection. That couples culling and cloud
ray generation.

Refactor into two concepts, for example:

```cpp
void CalculateCullingFrustum();
void CalculateRenderRayCorners();
```

`CalculateCullingFrustum()` must invert `VP_no_jitter` and update `_vf` only.

`CalculateRenderRayCorners()` should invert `VP_jittered` if `_LT/_LB/_RT/_RB` are intended to represent the current raster
sample rays. Store dedicated render corners if necessary rather than reusing culling-plane corner members.

Reason: later Cloud TAA compatibility is much cleaner if cloud rays match the current jittered screen sample, while CPU
visibility remains stable.

### M1.5 - Fix Renderer::PrepareCamera

Use explicit accessors:

```cpp
cam_cb_data->_MatrixV = cam.GetView();
cam_cb_data->_MatrixP = cam.GetProj();
cam_cb_data->_MatrixVP = cam.GetViewProjJittered();
cam_cb_data->_MatrixVP_NoJitter = cam.GetViewProjNoJitter();
cam_cb_data->_MatrixIVP = MatrixInverse(cam_cb_data->_MatrixVP);
cam_cb_data->_MatrixVP_Pre = cam.GetPrevViewProj();
```

Prefer `cam.GetPrevViewProj()` over reading `_prev_fs->_MatrixVP_NoJitter` so `Renderer::PrepareCamera()` and
`ViewEntity::FormCamera()` use the same camera temporal source. If a multi-camera render reveals a reason to retain FrameStore
state, document it in `progress.md` before diverging.

Continue writing jitter fields from `cam.GetJitter()`.

### M1.6 - Align ViewEntity::FormCamera

Replace raw multiplication with explicit Camera helpers so both CameraCB producers are visibly identical in meaning:

```cpp
_MatrixP = cam.GetProj();
_MatrixVP = cam.GetViewProjJittered();
_MatrixVP_NoJitter = cam.GetViewProjNoJitter();
_MatrixIVP = MatrixInverse(_MatrixVP);
_MatrixVP_Pre = cam.GetPrevViewProj();
```

### M1.7 - Handle SetViewAndProj/custom cameras

`SetViewAndProj()` currently assigns `_view_matrix/_proj_matrix` and calls `CalculateFrustum()` without updating the no-jitter
VP. For custom/shadow/probe cameras, default to stable/no-jitter semantics unless there is an explicit need for TAA jitter:

```text
P_no_jitter = supplied P
P_jittered = supplied P
VP_no_jitter = V * P
VP_jittered = V * P
jitter = 0
```

Do not accidentally jitter shadow cameras or cubemap probe cameras.

### M1.8 - Audit screen conversion helpers

`ScreenToWorld()` and `WorldToScreen()` currently use `_proj_matrix` for perspective. Decide based on call purpose:

- render/depth reconstruction -> jittered is correct;
- editor picking/UI/camera tools -> usually stable no-jitter is preferable.

Do not change all call sites blindly. Add explicit variants if both behaviors are required.

### M1.9 - Audit billboard special case

`billboard.hlsl` manually removes matrix jitter from `_MatrixP`. Do not change it in M1 unless validation proves it violates the
chosen transparent policy. Record it as a known M5 item.

## Current Milestone

M1.

## Progress

### M1 implementation

- [x] Store stable no-jitter projection and explicit jittered/no-jitter current VP matrices; keep `GetViewProj()` as the no-jitter compatibility accessor.
- [x] Rebuild both projection paths deterministically and clear jitter before each build. Perspective and orthographic paths now populate the same matrix/UV jitter contract.
- [x] Build the culling frustum from stable no-jitter VP and keep jittered render-ray corners in separate fields used by CameraCB cloud-ray inputs.
- [x] Align `Renderer::PrepareCamera()` and `ViewEntity::FormCamera()` with the explicit Camera accessors and previous stable VP source.
- [x] Make `SetViewAndProj()` a zero-jitter custom-camera path; route `LookTo()` through the shared matrix rebuild sequence.
- [x] Make perspective `ScreenToWorld()` / `WorldToScreen()` use the stable projection for editor interaction.
- [x] Align the RayTraceGI and DXR sample CameraCB producers with the Camera matrix/jitter contract, without changing their accumulation logic.

Runtime camera-contract acceptance passed after the V5/default-width fixes; see `progress.md` for V1-V6 evidence. M2 requires an explicit implementation request.

## Decision Log

- Keep `GetViewProj()` no-jitter for compatibility.
- Add explicit jittered/no-jitter accessors.
- Separate stable culling data from render ray data.

## Surprises & Discoveries

If any shader uses `_MatrixP` directly, classify whether it wants jittered raster behavior or a stable projection before
changing it.

## Validation

### V1 - Matrix contract

For static camera with TAA jitter enabled:

```text
GetProjNoJitter()                constant
GetViewProjNoJitter()            constant
GetViewProjJittered()            changes each jitter sample
_MatrixIVP * _MatrixVP           approximately identity
```

### V2 - Raster shift

A thin static edge must move by subpixel amounts across samples when viewed in an unaccumulated current-frame debug view.

### V3 - Depth reconstruction

Use Deferred Lighting/SSAO world-position reconstruction. With jitter enabled, reconstructed world positions must remain
attached to geometry and not slide.

### V4 - Culling stability

Place a box at a frustum edge and collect visible/cull status for at least 128 jitter samples. Jitter alone must not toggle it.

### V5 - Orthographic

Repeat matrix and culling tests using orthographic camera. `GetProjNoJitter()` must be exactly the base ortho projection.

### V6 - Cloud ray corner semantic

If render corners remain jittered, verify a projected corner/ray matches the jittered raster sample. If Clouds are disabled,
record the result without enabling the feature yet.

## Acceptance Criteria

- [x] Stable no-jitter projection stored explicitly.
- [x] Jittered VP stored/exposed explicitly.
- [x] `_MatrixVP` is jittered in every CameraCB producer that uses a `Camera`.
- [x] `_MatrixIVP` is inverse of jittered `_MatrixVP` in those producers.
- [x] `_MatrixVP_NoJitter` is stable current VP.
- [x] `_MatrixVP_Pre` is previous stable VP.
- [x] Orthographic no-jitter recovery no longer relies on subtracting perspective jitter fields; orthographic jitter modifies the row-vector projection translation terms.
- [x] Camera UV jitter semantic is validated in texture UV coordinates.
- [x] Culling frustum is no-jitter.
- [x] Render-ray corner semantics are explicitly separated from culling.
- [x] Static depth reconstruction remains correct with jitter enabled (captured native-depth planar ROI checks).
- [x] Custom/shadow cameras use supplied matrices with zero Camera jitter; cubemap cameras use the non-TAA default Camera path.

- [x] Idle orthographic camera advances jitter/render VP on each rendered frame (128 distinct samples on the 2026-10-02 retest).

Do not enter M2 until all pass.

## Known Follow-ups

Billboard/transparent special projection handling is deferred to M5.

### Acceptance run 2026-10-02

M1 NOT ACCEPTED: V5 found a fixed jitter/render VP in the idle orthographic SceneView over 128 different rendered frames.
The 2D input branch returns before per-frame Camera matrix recalculation. Perspective raster/UV and selected-ROI raw-depth
checks passed; native V4 visible/cull-status evidence remains open. Full details and artifacts are recorded in `progress.md`.
The initial orthographic width was uninitialized; only the explicit-width-40 rerun is valid evidence.

## Outcomes

M1 camera-contract acceptance passed on the 2026-10-02 retest. `Camera::PrepareRenderFrame()` refreshes render jitter at
frame/view preparation, fixing the idle orthographic path, and orthographic width defaults to 10. Both projections passed
128 distinct jitter samples, native frustum AABB checks, projected UV/corner checks and selected-ROI raw-depth reconstruction.
See `progress.md` and `TAA_M1_Retest_20261002/report.md` beside Editor.exe for evidence and numerical limits. Earlier failed
runs above remain historical records. No full TAA resolve-quality or interactive mouse/UI acceptance is claimed.

## Resume Protocol

When resuming, first verify the current CameraCB values with the debug view before editing any consumer.
