# M8 - Integration, Cleanup, and Performance

## Purpose

Run the full feature matrix, remove obsolete temporal code, and leave TAA maintainable as a normal renderer option.

## Scope

- Combination validation.
- Dead-code cleanup.
- Assertions/debug checks.
- GPU/CPU cost capture.
- Final documentation/progress outcomes.

## Non-goals

- No new TAA quality feature unless required to fix a correctness regression.

## Architecture Constraints

M8 begins only after M1-M4 pass. M5 may be PARTIAL only if the user explicitly accepts the documented transparent limitation.
M6/M7 are required only for features intended to be enabled together with TAA.

## Baseline

Core TAA and feature-specific temporal fixes should already be independently validated.

## Target Outcome

TAA can be enabled through normal renderer settings and survive feature toggles, resize, camera cuts, and long-running sessions.

## Relevant Files

All files changed by M0-M7.

## Milestones

### M8.1 - Combination matrix

Run at least:

```text
TAA + SSAO
TAA + Volumetric Fog
TAA + Surfel GI
TAA + DDGI
TAA + Fog + Surfel + DDGI
TAA + Cloud (if enabled)
TAA + RayTraceGI realtime (if enabled)
TAA + transparent/Sprite supported policy
TAA + all supported features
```

Test static, pan, rotate, object motion, resize, camera cut, and feature off/on.

### M8.2 - Remove dead temporal code

Candidates after repository-wide search:

```text
TemporalAA::HaltonSequence
TAAPreparePass
legacy _pre_matrix/_cur_vp_matrix if compute path no longer needs them
_origin_camera_cbuf if unused
legacy RG-global texture assumptions
unused _motion_vector_depth resource
```

Do not delete legacy non-RG support if `_is_use_render_graph == false` remains a supported runtime configuration. Either keep it
working or clearly deprecate it.

### M8.3 - Add debug assertions

Useful debug-only checks:

```text
history read texture != history write texture
TAA history dimensions == current render dimensions
TAA runs only when Camera AA mode is TAA
_MatrixIVP approximately inverse(_MatrixVP) when built
current/previous jitter finite
history frame continuity valid before sampling
```

For RG, log/assert if TAA input handles are invalid.

### M8.4 - Long-run temporal state test

Run several minutes with:

```text
camera idle
slow orbit
frequent editor selection/gizmo
feature toggles
window resize
scene reload if supported
```

Watch for history pointer/state leaks, stale camera hash entries, and one-frame flashes.

### M8.5 - Performance capture

Record at representative resolution:

```text
TAA GPU dispatch ms
MotionVector GPU ms
additional RG pass count
additional barriers/transitions
TAA history memory (A+B per active camera)
CPU record cost
RG compile delta if measurable
```

Do not optimize before capturing the correct implementation.

### M8.6 - Final documentation

Update `progress.md` Outcomes with:

```text
implemented architecture
supported camera modes
supported feature combinations
known transparent limitations
known follow-ups
performance cost
```

## Current Milestone

M8.

## Progress

Complete final checklist in `progress.md`.

## Decision Log

No new architectural work should be introduced in M8 without opening a follow-up task.

## Surprises & Discoveries

If a combination exposes a new subsystem-level bug, return to the owning milestone rather than adding an M8-only workaround.

## Validation

Final required checks:

- Perspective.
- Orthographic.
- Main game camera.
- Scene/editor camera if applicable.
- Resize.
- TAA toggle.
- Camera cut.
- Dynamic opaque object.
- Occlusion case.
- Alpha-test.
- Supported transparency/Sprite policy.
- Supported temporal features.
- PIX/RenderDoc pass/resource inspection.

## Acceptance Criteria

- [x] M0-M4 PASS within the recorded automated acceptance scopes.
- [x] M5 PASS for the supported Policy A fixture scope.
- [x] M6 PASS for the recorded single-camera Cloud/TAA fixture scope.
- [x] M7 PASS for the recorded single-camera RTGI/TAA fixture scope.
- [x] No duplicate camera-jitter generator remains.
- [ ] No uninitialized history path remains.
- [ ] No known stale RG resource version in supported temporal features.
- [ ] Core culling is stable under jitter.
- [ ] Depth reconstruction remains attached under jitter.
- [ ] MotionVector occlusion correctness passes.
- [ ] Performance numbers recorded.
- [ ] Dead code cleaned or deliberately retained with comments.
- [ ] `progress.md` Outcomes complete.

## Known Follow-ups

- Transparent reactive mask/velocity if M5 remained partial.
- TemporalHistoryRegistry.
- TAA quality improvements.
- TAAU/dynamic resolution.

## Outcomes

2026-10-03 acceptance: **PARTIAL / final gate OPEN**. Fresh Debug build, camera smoke, five GPU/image prerequisite
regressions and two-frame core-RG RenderDoc binding inspection passed. Actual Editor seven-combination toggle/camera
smoke plus184.532-second intermittent-capture long run completed912 finite, continuous-within-session frames.
Debug GPU rolling averages and logical history payload are recorded. Effective GI/fog/Sprite combination contribution,
full Cloud/RTGI combination, separate main camera, resize/gizmo/reload, continuous flash/leak evidence and CPU/barrier
costs remain open. See [the acceptance report](m8-acceptance-20261003.md) and progress.md. Do not mark M8 complete.

## Resume Protocol

For any regression, identify the owning milestone and re-run its Acceptance Criteria. Do not patch around it in M8.
