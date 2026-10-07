# M0 - Baseline, Instrumentation, and Safety Fixes

## Purpose

Make the TAA work reproducible and remove small undefined-state hazards before changing camera math.

## Scope

- Deterministic `RenderFeature::_is_active`.
- A controlled way to enable TAA later without enabling it during baseline capture.
- Fixed validation scenes.
- Debug output for jitter, matrices, motion vectors, and history state.
- Confirm current CameraCB producers/temporal consumers in the working tree.

## Non-goals

- No matrix semantic changes.
- No TAA shader changes.
- No RG TAA implementation.
- No temporal-quality tuning.

## Architecture Constraints

Instrumentation must not alter render results when disabled.

## Baseline

`RenderFeature(const String &name): Object(name){}` leaves `_is_active` uninitialized. `TemporalAA` also does not initialize
it. Several features set `_is_active` explicitly, but not all.

TAA is currently not present in `_features`, so it does not execute.

## Target Outcome

A light model can run the same scene repeatedly and answer whether a later change fixed matrix/reprojection correctness rather
than relying on subjective screenshots.

## Relevant Files

```text
Engine/Inc/Render/Features/RenderFeature.h
Engine/Src/Render/Renderer.cpp
Engine/Inc/Render/Camera.h
Engine/Src/Render/Camera.cpp
Engine/Inc/Render/ShaderInterop.h
Engine/Src/Render/Features/CommonPasses.cpp
```

## Milestones

This file is M0.

## Current Milestone

M0.

## Progress

### Task M0.1 - Initialize RenderFeature state

Change the base constructor to match `RenderPass` behavior:

```cpp
RenderFeature(const String &name) : Object(name), _is_active(true) {}
```

Do not initialize it to false: the renderer currently adds normal features such as SSAO/Fog/Sprite expecting them to operate,
then explicitly disables features such as RayTraceGI/RTXDI with `SetActive(false)`.

Validation:

- Run existing renderer with TAA still absent from `_features`.
- Confirm no new feature unexpectedly changes state relative to the normal build.
- Explicit `SetActive(false)` for RayTraceGI/RTXDI must still win.

### Task M0.2 - Add a controlled TAA activation point

Do not uncomment `_features.push_back(_taa)` yet unless there is an existing engine/editor setting suitable for feature
selection. Prefer one explicit boolean/config path such as renderer debug/quality settings. The implementation must permit M1-M3
to turn Camera TAA jitter on for tests while keeping the legacy `TAAExecutePass` disabled until M3.

Important distinction:

```text
Camera anti-aliasing mode == TAA
    -> enables camera jitter for matrix tests.

TemporalAA feature active
    -> enables temporal resolve.
```

During M1/M2, only the first is required.

### Task M0.3 - Add debug visibility for Camera temporal state

At minimum provide debug log/overlay values per active camera:

```text
frame
camera hash
anti_aliasing mode
matrix jitter x/y
uv jitter x/y
MatrixVP[relevant offset terms]
MatrixVP_NoJitter[relevant offset terms]
```

Do not dump full matrices every frame by default. Use a debug toggle and print only a few frames or expose in ImGui.

### Task M0.4 - Confirm MotionVector visualization

Use the existing motion-vector texture and expose a debug view if there is not already one. Required test expectations before
any fixes:

```text
static camera + static scene  -> near zero
camera pan                    -> coherent screen-wide motion
moving object                 -> local object motion region
```

The occlusion case may be wrong at baseline and should be recorded, not fixed in M0.

### Task M0.5 - Create fixed validation scenes

Use at least:

```text
TAA_StaticEdges
    thin geometry / diagonal rails / high contrast

TAA_Motion
    static foreground wall
    moving opaque object that passes behind it
    one alpha-test object
    one blended transparent object
    one Sprite

TAA_TemporalFeatures
    Volumetric Fog
    optional Cloud
    optional RayTraceGI
```

If creating engine assets is expensive, use an existing scene and document exact camera/object positions instead.

### Task M0.6 - Re-run source audit against working tree

Search these symbols and record any delta from the baseline package in `progress.md`:

```text
ProcessJitter
GetProjNoJitter
GetViewProj
PrepareCamera
FormCamera
_MatrixVP
_MatrixIVP
_MatrixVP_NoJitter
_MatrixVP_Pre
TemporalAA
TAAExecutePass
MotionVectorPass
VolumetricCloudsPass
RayTraceGI
```

## Decision Log

M0 must not make visual TAA corrections.

## Surprises & Discoveries

If the source has already changed from the uploaded snapshot, record exact symbols and use the current source as authority.

## Validation

- Existing non-TAA rendering unchanged.
- Feature active states deterministic.
- Debug jitter/motion information available.
- Validation scene reproducible.

## Acceptance Criteria

- [x] `RenderFeature::_is_active` deterministic.
- [x] TAA can be enabled later through one controlled activation point.
- [x] Camera jitter can be tested independently of TAA resolve.
- [x] MotionVector debug is available; camera-pan baseline was captured and its near-zero result is recorded for M2.
- [x] Fixed validation scenes are referenced with camera/object positions; no scene asset changes were needed.
- [x] Baseline screenshot/capture saved.
- [x] Working-tree audit matches or differences are recorded.

Do not enter M1 until all items pass.

## Known Follow-ups

- Camera-pan motion vectors remain near zero despite the camera moving 1.6 world units. Diagnose/fix in M2.
- The existing validation references do not combine alpha-test, blended transparency, and Sprite; add that coverage before M5.

## Outcomes

M0 is complete. The working tree builds, jitter is independently controllable while temporal resolve remains disabled, temporal state can be inspected for eight frames, and the motion-vector debug view is available. The baseline run exposed the camera-pan motion-vector failure recorded in `progress.md`; no camera matrix semantics or motion-vector production were changed in M0.

## Resume Protocol

M0 is complete. Do not modify Camera matrix semantics until M1 is explicitly started.
