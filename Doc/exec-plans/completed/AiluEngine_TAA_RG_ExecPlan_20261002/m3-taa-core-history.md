# M3 - TAA Core Reprojection and History Lifecycle

## Purpose

Fix TAA itself while still keeping RG integration separate. After this milestone, TAA math/history ownership should be correct
independently of how the pass is scheduled.

## Scope

- Remove duplicate TAA Halton generator.
- Feed Camera jitter into TAA.
- Apply jitter delta in history reprojection.
- Make `_HistoryFactor` real.
- Explicitly bind depth and motion inputs.
- Redesign per-camera history state and invalidation.
- Fix first-frame/resize/toggle/camera-cut behavior.
- Remove obsolete prepare-pass projection ownership.

## Non-goals

- No RG scheduling yet.
- No transparent reactive mask.
- No Cloud/RTGI temporal changes.

## Architecture Constraints

TAA consumes Camera state; it does not mutate Camera projection.

History must never be sampled before it is valid.

## Baseline

`TemporalAA::AddRenderPasses()` owns `Map<u64, HaltonSequence>` and computes another jitter matrix even though
`Camera::ProcessJitter()` already exists. `_prepare_pass` is not enqueued, so the extra projection math no longer even controls
rasterization.

`TAAInfo` currently contains:

```cpp
Matrix4x4f _pre_matrix;
bool _first_tick;
Ref<RenderTexture> _target_a;
Ref<RenderTexture> _target_b;
bool _is_cur_a;
```

On first allocation it copies scene color to the selected current target, then resolves using the opposite texture as history.
That opposite texture has never been initialized.

Compute shader:

```hlsl
float2 prev_uv = uv - mv;
half4 final_color = lerp(cur_color, clamped_history, 0.95);
```

`_Jitter` and `_HistoryFactor` are uploaded but unused for these operations.

## Target Outcome

Per camera:

```text
history A/B allocated
history_valid explicit
read side explicit
write side explicit
previous Camera UV jitter explicit
last rendered frame explicit
resolution explicit
```

Resolve:

```text
if history invalid:
    output current frame
    seed history
else:
    history_uv = uv - motion + previous_jitter_uv - current_jitter_uv
    validate bounds/depth neighborhood as existing algorithm supports
    sample/clamp history
    blend with configurable history factor
```

## Relevant Files

```text
Engine/Inc/Render/Features/TemporalAA.h
Engine/Src/Render/Features/TemporalAA.cpp
Engine/Res/Shaders/hlsl/Compute/taa.hlsl
Engine/Inc/Render/Camera.h
Engine/Inc/Render/RenderingData.h
```

## Milestones

### M3.1 - Remove TAA-owned Halton sequence

Delete or stop using:

```text
TemporalAA::_halton_sequence
TemporalAA::HaltonSequence projection-jitter generation
TAAPreparePass as a camera-jitter owner
```

`TemporalAA::AddRenderPasses()` should only gather TAA parameters and Camera state, then enqueue/setup the execute pass.

The only current jitter comes from:

```cpp
const Vector4f &camera_jitter = rendering_data._camera->GetJitter();
```

If `TAAPreparePass` becomes unused, remove it after repository-wide search rather than leaving dead temporal code.

### M3.2 - Replace TAAInfo with explicit history state

Recommended shape:

```cpp
struct TAAHistoryState
{
    Ref<RenderTexture> _target_a;
    Ref<RenderTexture> _target_b;
    Vector2f _previous_uv_jitter = Vector2f::kZero;
    u32 _width = 0u;
    u32 _height = 0u;
    u64 _last_render_frame = 0u;
    bool _write_a = true;
    bool _history_valid = false;
};
```

Do not rely on aggregate defaulting of `_is_cur_a`/`_first_tick`.

Prefer map key `u64 camera_hash` as currently used, but ensure state is removed/invalidated if camera lifetime/hash reuse can
occur.

### M3.3 - Centralize history invalidation

Add a helper such as:

```cpp
void InvalidateHistory(u64 camera_hash);
void InvalidateAllHistory();
```

At minimum invalidate on:

```text
first use
history texture recreation
resolution change
TAA disabled -> enabled
camera cut/teleport
projection type change
large FOV change
large near/far discontinuity
render gap where history no longer represents previous rendered frame
```

Do not infer all of these only inside the shader. CPU knows when history is structurally invalid.

For camera cut, if the engine does not yet expose one, use a conservative detector in TAA state similar to RTGI:

```text
large camera position delta
large forward/up dot-product change
projection discontinuity
```

Keep thresholds in one place and document them.

### M3.4 - Make first-frame path explicit

Do not initialize only one history and then sample the other.

Two valid designs:

A. shader branch:

```text
_HistoryValid = 0
CurrentTarget = CurrentColor
```

B. CPU/RG copy path:

```text
copy current color -> write history
use write history as output
skip temporal sampling
```

For eventual M4 RG integration, design A is usually simpler because the same compute pass can seed history without a special
copy pass. Add an integer/float `_HistoryValid` constant if needed.

After the pass executes successfully:

```text
history_valid = true
previous_uv_jitter = current camera uv jitter
flip read/write side
```

Do not flip state during setup/recording; flip only when the resolve actually executes.

### M3.5 - Fix jitter reprojection

Camera UV jitter must already have been validated in M1. In compute TAA:

```hlsl
float2 jitter_delta = _JitterPrev - _JitterCur;
float2 prev_uv = uv - mv + jitter_delta;
```

Where `_JitterPrev` and `_JitterCur` are actual texture-UV displacements.

Do not add another Y negation in this shader. If the sign is wrong, fix Camera's UV-jitter semantic and re-run M1 V1/V2.

### M3.6 - Use `_HistoryFactor`

Replace hard-coded `0.95`:

```hlsl
half history_factor = saturate((half)_HistoryFactor);
half4 final_color = lerp(cur_color, clamped_history, history_factor);
```

Keep default behavior equivalent by leaving `_history_factor = 0.95f` in the feature.

Do not tune this value yet.

### M3.7 - Explicitly bind all compute inputs

Legacy `Execute()` currently sets current/history/output but not depth/motion explicitly. Bind:

```text
_CurrentColor
_HistoryColor
_CurrentTarget
_CameraDepthTexture
_MotionVectorTexture
```

Do not rely on `ComputeShader::SetGlobalTexture()` from other passes for core TAA inputs. M4 will convert these to explicit RG
reads.

### M3.8 - Remove unused legacy matrices from TAA

The compute path does not need `_pre_matrix`, `_cur_vp_matrix`, or `_origin_camera_cbuf` for reprojection once MotionVector and
Camera jitter are correct. Remove them if no raster TAA path still uses them.

If retaining the old material/raster TAA fallback, isolate it clearly behind a debug/deprecated path. Do not let its state drive
the compute implementation.

### M3.9 - Handle frame continuity

`_last_render_frame` should detect whether the camera actually rendered last frame. If the same camera skips one or more frames
while history remains allocated, invalidate rather than interpreting old history as frame N-1.

Use renderer/GFX frame index already available in the engine.

## Current Milestone

M3.

## Progress

Update `progress.md` after each task and test.

## Decision Log

- TAA caches previous Camera jitter value but does not generate jitter.
- History flips only after successful execution.
- Invalid history never gets sampled.

## Surprises & Discoveries

If the current working tree already has camera-cut/history infrastructure, reuse it instead of creating a TAA-only duplicate.

## Validation

### V1 - First frame

Enable TAA in a static scene. First resolved frame must exactly match current color apart from format precision. No black/garbage
flash.

### V2 - Static jitter convergence

Current-frame debug should jitter. TAA output should converge and remain spatially stable.

### V3 - Jitter sign

Temporarily visualize `history_uv - uv`. For static geometry with zero MotionVector it should equal previous-current jitter.
History edges must align, not oscillate one sample in the opposite direction.

### V4 - Resize

Repeated resize/recreate: first frame after each resize uses current only; no stale image from previous dimensions.

### V5 - Toggle

TAA off for multiple frames, then on. Old pre-disable history must not appear.

### V6 - Camera cut

Teleport/rotate camera discontinuously. Cut frame uses current only, then accumulation restarts.

### V7 - History factor

Set `_history_factor` to 0: output current frame. Set to 0.95: normal accumulation. This proves the shader no longer hardcodes
0.95.

## Acceptance Criteria

- [x] TAA no longer owns/generates camera Halton jitter.
- [x] Obsolete prepare-pass jitter path removed or clearly deprecated.
- [x] Explicit per-camera history state exists.
- [x] Invalid history is never sampled.
- [x] First frame is clean.
- [x] Resize invalidates history.
- [x] TAA off/on invalidates history.
- [x] Camera cut invalidates history.
- [x] Render-gap invalidation handled.
- [x] Reprojection includes previous-current Camera UV jitter.
- [x] `_HistoryFactor` controls blending.
- [x] Depth and motion textures are bound explicitly.
- [x] Static jitter converges without reducing history weight as a workaround.

Do not enter M4 until all pass in the non-RG/isolated path available for testing.

## Known Follow-ups

Depth disocclusion/reprojection rejection can be improved later; it is not a substitute for the above correctness.

## Outcomes

Document final history struct and invalidation triggers in `progress.md`.

### 2026-10-03 acceptance: NOT ACCEPTED (V2 detail loss)

Prior isolated GPU/reference evidence is retained. New `TaaGpuAcceptance --scene` uses real deferred meshes and the
Renderer-owned TemporalAA feature through the normal native RG path. Final `scene2/` exits 0, with 262 matched engine
frames, 770 completed native DDS captures, and D3D12 ERROR/CORRUPTION=0. Full Debug build passed. No production algorithm
or default history weight changed. Automated in-engine/image review is distinct from interactive Editor acceptance.

V1/V4/V5/V6/V7 pass: 21 first/reset/factor=0 outputs exactly equal same-frame current RGB. Feature-off and camera-AA-off
each last eight frames and omit TAA output; each actual reactivation's immediate first output equals current, including
changed emissive geometry. Six alternating 257x137/320x180 resizes, translation, real 90-degree rotation, FOV and projection
changes reset correctly. This closes the complete feature-toggle gap. Prior isolated numerical reference retains V3 sign
evidence; the new run does not add a jitter diagnostic shader or multi-camera GPU test. Camera-only jitter/per-camera state
are source-verified, and invalid-history shader branches return before reading history.

V2 remains failed: 128 consecutive static frames have 128 Camera jitters, XY motion exactly zero, and factor 0.95 throughout.
Adjacent-frame RMS decreases by 94.64% for the 0.4-pixel rail and 48.18% for the diagonal. However, the thin rail retains only
10.14% of positive above-background brightness versus its 128-frame current-color mean and visually becomes nearly
invisible. Other rail retention: 70.88%, 81.97%, 94.58%, 95.00%; diagonal 98.01%. Noise reduction alone does not establish
correct geometric convergence. The input-frame mean is a sampled reference, not analytic ground truth. Filtering/clipping
causality remains undiagnosed; do not reduce history weight to hide this failure.

Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M3_FullAcceptance_20261002/report.md`, final `scene2/` captures,
`analysis.json`, `static_comparison.png`, and `thin_rail_detail.png`. Initial `scene/` is excluded from final exit/rotation
acceptance: LookTo was overwritten by matrix recalculation and a held material outlived ResourceMgr. Both fixture issues
were fixed without production changes; the final process exits normally. Six config/log/session files restored with
matching SHA256; no scene/asset save. M3 remains incomplete.

### 2026-10-03 V2 diagnosis: current-neighborhood clipping destroys subpixel coverage

Offline one-factor replays use the captured real scene's current R channel, observed Camera jitter and actual previous GPU
history. At sample1, pixel(65,146), current 3x3 samples are all background 0.03857421875; MinMax gives the same low/high.
Correctly reprojected previous GPU history is approximately 0.728452, but ClipColor forces it to background; actual GPU
output is exactly 0.03857421875. This is static uncovered raster sampling, not a measured motion/disocclusion event. Across
196 pixels in the thin rail's sampled footprint, 28.15% of frame/pixel neighborhoods contain only background. Such frames
erase the historical subpixel signal; later visible frames add only 5% new color before the next erase.

The scalar reference reproduces the loss: baseline positive signal retention 10.95% versus actual R-channel GPU 10.42%
(full RGB acceptance metric remains 10.14%). Removing only history clipping restores signed/net signal to 99.78% with
factor 0.95 unchanged. Removing current filtering, retaining float history, or using bilinear history still retains only
about 11%. Correct/reversed jitter one-step prediction RMS is 0.001245/0.069857 over the five-rail ROI. This rules out a
jitter-sign reversal as the dominant failure mechanism and points to current-only color bounds treating intermittent
subpixel coverage as invalid history. Raising variance scale cannot widen an exactly zero-variance background neighborhood.

These are offline counterfactuals, not new GPU shader A/B or an accepted repair. Scalar/R11 round-to-nearest emulation is
approximate: full sequence five-rail RMS versus GPU is 0.007909; vector clipping, shader arithmetic and texture conversion
are not bit-exact. Direct actual-GPU output agreement at the collapsed neighborhood and the selective replay effect support
the diagnosis. Repair should preserve credible subpixel history while retaining true occlusion/color-change rejection;
globally disabling clipping or lowering history factor is not accepted. No production code/config changed or new build/run.
Evidence: `TAA_M3_FullAcceptance_20261002/v2_diagnosis/diagnosis.json`, `ablations.png`, `report.md`, and
`Test/DiagnoseTaaScene.py`.

### 2026-10-03 repair/retest: AUTOMATED M3 PASS

Repaired `taa.hlsl` while keeping factor=0.95 and the existing history/motion/jitter/RG contracts. A stationary pixel
(geometric motion below 0.5 pixel) with an entirely uniform current 3x3 and an out-of-bounds history color may expand its
color envelope from the outer 5x5 ring only when 1-4 of those 16 taps differ. This sparse coverage guard preserves a missed
thin feature; filled edges and moving geometry retain normal rejection. History is still clipped to current colors.
Variance limits now intersect, rather than expand, current min/max: the color-drop control exposed stale bright values
admitted by the previous union. No new history textures, globally disabled clip or weight workaround.

Selected final real GPU run: `TAA_M3_Repair_20261003/sparse_guard/`. Process exit0; 334 consecutive frames, 986 completed
DDS; ERROR/CORRUPTION=0. First/reset/factor0 checks 22/22 bit-identical RGB. Static 128-frame input matches the original
fixture setup; factor stays0.95, 128 jitters, motion0. Narrowest rail signal retention rises from10.14% to86.43%; adjacent
RMS 0.115787 input /0.012104 output (89.55% reduction), late reference RMS0.034228 versus current0.070458. The 0.8/1.4/2.4/4.0
rail retention is85.57/82.32/94.89/95.21%; diagonal97.97%, adjacent RMS0.038171 versus input0.074038. All local stability
checks pass and captured crops preserve the narrow rail. Sampled current mean is not analytic ground truth; residual blur
remains and this is not a claim of exact radiance reconstruction.

New eight-frame color-drop control has zero stale bright excess; full occlusion error <=0.0009765625 and no rail remnant;
rigid motion fixture is nonzero and has zero positive trail outside its 5x5 footprint. Disocclusion accumulates again.
The .003 HDR-RGB tolerance is explicit in the control analyzer; boundary pixels inside the filter footprint are not claimed
ghost-free. Independent `jitter_rg/` regression:85 pairs/170 DDS, resets exact, flat-region and jitter-reference gates pass;
correct/opposite reference RMS0.003710/0.211595. Both final runs exit0, D3D12 ERROR/CORRUPTION=0. Full build-debug passed;
runtime logs verify successful production shader compilation/cache load without DXC errors.

Earlier `scene/` has a DXC vector-logical condition compile error and black output, and is excluded. Intermediate `controls/`
and `guard_flat/` runs recovered thin signal but failed normal-edge/reference and color-drop checks; only sparse_guard is
accepted. Final changes are the bounded sparse guard plus variance intersection; all experiments/evidence retained.
Six config/log/session files restored with matching SHA256; generated graph dumps archived. No test scene save, no remaining
test process. Existing dirty work retained. GPU overhead (up to16 extra taps in the guarded case), full interactive Editor,
multi-camera and general scene quality remain outside this targeted automated acceptance. M5-M8 unstarted.
Report: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M3_Repair_20261003/report.md`.

## Resume Protocol

If ghosting is observed, debug in this order: motion -> jitter delta -> history validity -> UV bounds -> clamp. Do not start by
changing blend weight.
