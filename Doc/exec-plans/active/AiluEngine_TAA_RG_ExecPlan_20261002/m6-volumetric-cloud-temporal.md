# M6 - Volumetric Clouds Temporal Compatibility

## Purpose

Make tiled Volumetric Clouds read current-frame motion and use the same jitter convention as TAA.

## Scope

- Fix RG pass ordering/version dependency.
- Preserve cloud A/B history.
- Add camera jitter delta to Cloud reprojection if cloud render rays are jittered.
- Add cloud history invalidation for resize/cut/enable transitions.

## Non-goals

- No cloud raymarch quality rewrite.
- No new denoiser.

## Architecture Constraints

Cloud tile history and final TAA are two separate temporal systems. Each must correctly reproject its own history.

## Baseline

`VolumetricCloudsPass` event is `kAfterSkybox` (400). `MotionVectorPass` is `kBeforeTransparent` (450). During Cloud RG
recording, tile reprojection calls:

```cpp
builder.Read(rendering_data._rg_handles._motion_vector_tex);
```

At that time MotionVector has not produced the current frame version yet.

Shader currently uses:

```hlsl
float2 mv = _MotionVectorTexture[pixel_index].xy;
float2 prev_uv = uv - mv;
```

Cloud A/B state is already flipped inside the RG compose execute callback, which is a good RG-safe pattern.

## Target Outcome

```text
current MotionVector -> Cloud tile reprojection
current Camera jitter -> Cloud ray sample
previous/current jitter delta -> Cloud history UV
Cloud compose -> scene color -> final TAA
```

## Relevant Files

```text
Engine/Src/Render/Features/VolumetricClouds.cpp
Engine/Res/Shaders/hlsl/Compute/volumetric_cloud.hlsl
Engine/Inc/Render/Features/VolumetricClouds.h
Engine/Src/Render/Camera.cpp
```

## Milestones

### M6.1 - Move Cloud recording after MotionVector but before transparency

Do not only rely on RG magically reordering a stale version. Resource version is chosen during recording.

Set Cloud event to an explicit value after MotionVector and before Sprite/Forward, for example a new enum/event around 455 if
consistent with engine style:

```text
MotionVector 450
Cloud       455
Sprite      460
Forward     475
```

Prefer adding a named event such as `kAfterMotionVector` if pass ordering is becoming important across features.

After the change, inspect RG recording: Cloud `builder.Read(_motion_vector_tex)` must receive the version written by
DynamicMotion, not the initial version created in `BeginScene()`.

### M6.2 - Preserve jittered render-ray semantics from M1

If M1 split culling and render corners as recommended, `_LT/_LB/_RT/_RB` should represent jittered current render rays while
`GetViewFrustum()` remains stable.

Verify `CloudMain` uses those corners. It currently interpolates them to obtain `cam_dir`.

If the implementation instead made Cloud rays no-jitter, document that alternative and re-evaluate whether final TAA should
apply jitter delta to Cloud pixels. The recommended path is to keep render rays jittered for consistency with the current frame
sample.

### M6.3 - Add Cloud jitter delta

Pass previous/current UV jitter to Cloud reprojection. Reuse the Camera UV convention from M1; do not create another Halton
sequence.

Shader target:

```hlsl
float2 prev_uv = uv - mv + (_JitterPrev - _JitterCur);
```

The uniform can be a small Cloud temporal constant or shared camera fields if both current/previous values are available there.

### M6.4 - Add Cloud history validity

`EnsureRenderTargets()` recreates history on resolution change but does not explicitly prevent the first reprojection from
sampling an uninitialized history side.

Add cloud state similar to TAA:

```text
history_valid
previous_uv_jitter
last_render_frame
```

For invalid history/tile pixels, do not sample the previous cloud texture. Either fully evaluate current cloud for the seed
frame or clear/copy a valid seed according to the tile algorithm.

Invalidate on:

```text
resize
Cloud off/on
camera cut
large cloud parameter discontinuity if it invalidates history
render gap
```

### M6.5 - Keep ping-pong commit execution-safe

Current RG compose callback flips `_is_cur_a`. Keep this principle. Ensure the flip happens exactly once per frame and only if
Cloud's history-producing work actually executed.

Do not also flip in `EndPass()` for RG.

### M6.6 - Validate with and without final TAA

Four combinations:

```text
Cloud temporal off, TAA off
Cloud temporal on,  TAA off
Cloud temporal off, TAA on
Cloud temporal on,  TAA on
```

This isolates whether artifacts originate in Cloud's history or final TAA.

## Current Milestone

M6.

## Progress

- Source implementation now uses `kAfterMotionVector = 455`; Cloud remains before Sprite 460 and Forward 475.
- Cloud keeps M1's jittered render rays and supplies previous/current Camera UV jitter to tile reprojection.
- Invalid history gets a full-resolution seed dispatch; history invalidates on resize, camera identity/cuts/projection
  changes, skipped render frames, and changes to Cloud settings. RG history commits only after its required dispatches.
- The initial 2026-10-03 acceptance failed on an unbound Cloud sky-LUT global; preserve that evidence below.
- After repair, the production-RG four-combination and transition fixture passes automated acceptance:384 continuous
  frames,2110 DDS,185 current-motion reprojection frames,11/11 numerical/execution gates. M5 regression also passes.
  Scope is the captured single-camera fixture; interactive/multi-camera acceptance remains separate.

## Decision Log

Recommended: stable culling frustum + jittered Cloud render rays + jitter-compensated Cloud history.

## Surprises & Discoveries

If Cloud motion should be based on its own world-space advection rather than only camera/object MotionVector, record that as a
future Cloud temporal improvement. It does not change the need for camera jitter compensation.

## Validation

- 2026-10-03 automated production-RG acceptance: **FAIL**, blocked before valid Cloud history exists.
- Final evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M6_ProductionEnvironment_20261003/run1/`.
  `analysis.json`, `m6_graph.csv`, `m6_observations.csv`, and `combinations.png` distinguish transport from acceptance.
- Fixture uses an unsaved scene with a real procedural `EnvironmentComponent`, production `VolumetricClouds`
  registered via `Renderer::AddFeature`, and Renderer-owned TAA. Default feature registration and production source
  remain unchanged. Four combinations each have 96 samples: 32 static, 32 slow translation, and 32 rotation/transition
  samples; the final combination also exercises 317x179 resize/back, one-frame Cloud off/on, a 20-unit cut and explicit
  Camera history reset. Wind speed and runtime animation time are frozen for this test; high quality remains enabled.
- Full Debug build passes; D3D12 ERROR/CORRUPTION=0; process exit=0; all 1918 manifests complete with correct dimensions,
  finite pixels, matching frame indices and consecutive engine-frame steps=1. These are transport/build results only.
- All 383 enabled Cloud frames lack a Cloud sky-LUT read; no TileReproject pass exists and only CloudTexA is written.
  Every selected seed capture is RGBA zero. Four-combination image review is black; it cannot establish jitter stability.
- `EnvironmentSystem::RecordRenderGraph` imports `_EnvironmentSkyViewLUT` into `_rg_handles._sky_view_lut`.
  `DrawSkyBox` reads that valid handle, but nothing publishes `_TexSkyViewLUT` with `SetGlobalTextureAfterPass`.
  Cloud instead calls `ReadGlobalTexture` for this absent slot, then returns on null `sky_view_lut`. The source-declared
  UAV output is still captured/composed even though no CloudMain dispatch populated it.
- Current-motion version consumption, valid first history, resize/off-on/cut recovery, execution-safe A/B rotation and
  combined TAA jitter alignment remain unaccepted behind that failure. Event455 and jitter formula are source-confirmed.
- Reproduce with `Test/RunTaaTransparentAcceptance.ps1 -Mode --cloud` and analyze with
  `Test/AnalyzeTaaCloudAcceptance.py`; analysis exits1 on this failed gate. Final run restored8/8 state entries and saved
  no scene assets. The earlier `TAA_M6_Acceptance_20261003` run lacked an explicit environment component and is preflight
  evidence only; use the ProductionEnvironment run for the final failure finding.

- Static camera: cloud details do not crawl with Halton pattern.
- Slow pan: no systematic one-pixel history offset.
- Fast rotation: history exits screen cleanly.
- Resize: no uninitialized cloud history.
- Camera cut: no old cloud layer ghost.
- Combined TAA: no doubled periodic jitter or stable offset.

## Acceptance Criteria

- [x] Cloud records after current MotionVector version exists.
- [x] RG capture proves Cloud reads current MotionVector.
- [x] Cloud render-ray jitter ownership is explicit.
- [x] Cloud history reprojection includes required jitter delta.
- [x] First Cloud history frame is valid.
- [x] Resize/off-on/cut invalidates Cloud history (captured resize, render-gap/off-on and20-unit cut).
- [x] A/B flips once after execution (captured normal execution).
- [x] Cloud temporal + final TAA has no systematic jitter offset (fixture-local static alignment).

### Repair and final retest - 2026-10-03

- Cloud uses the explicit environment `_rg_handles._sky_view_lut` dependency, captures depth/LUT/motion handles at
  recording, rejects missing inputs, and skips reprojection/compose when preceding work was not dispatched.
  Composition loads scene color. No new global publisher and no default Cloud enablement were added.
- TAAOpaqueSnapshot moves455->456 to include Cloud455 before Sprite460/forward475. Stable equal-event ordering
  previously let the TAA snapshot precede Cloud, incorrectly classifying Cloud as blended responsiveness.
- Final evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M6_Final_20261003/`.
  The fixture now includes a directional sun with unit RGB/intensity so Cloud radiance is nonzero. The earlier
  RepairRetest run established the dependency repair but had no sun; SunRetest used0.01 sun intensity and preceded
  the snapshot fix. Neither is selected as final acceptance evidence.
- Debug build passes; process exit0; D3D12 ERROR/CORRUPTION0.384 continuous observations,2110 completed DDS, all
  dimensions/frame IDs/finite checks pass.383 enabled frames bind the explicit LUT,185 TileReproject passes read the
  same id/version written by earlier DynamicMotion. Both A/B textures alternate once per enabled frame.
- Full seed dispatches at first frames0/96/192/288, resize368, resize-back370, off/on373 and teleport376 have no
  TileReproject and match their captured full CloudMain output exactly. Updated tiled pixels match CloudMain exactly.
- Independent previous-GPU-frame bilinear oracle validates reprojection across185 frames; worst robust RGBA RMS
  is0.00017724, below0.001. Correct jitter sign beats opposite sign. The oracle models this device's1/256-texel
  sampler fractions and half-float stores. It separately records7200 UV-boundary-uncertain pixels in aggregate
  (within1e-7 of0/1), rather than treating float-rounding-sensitive inside/outside decisions as temporal errors.
  Full-frame large-error counts/bounds remain in the report; all interior comparisons and all current-tile checks pass.
- Final reactive mask is0 in every captured Cloud-only TAA frame. Both TAA combinations have best static integer
  alignment[0,0]; quadratic offsets are(-0.02164,0.00990) pixels without Cloud temporal and(0.02274,0.06018) with it,
  below the0.25-pixel gate. Adjacent static RGB RMS falls0.012388->0.005556 and0.009046->0.005528 respectively.
  Reviewed the final four-way image montage: Cloud signal and final TAA filtering are visible.
- M5 regression uses unchanged thresholds and factor0.95:784 observations,3136 DDS, seven materials/projection
  cases, all responsive/chroma/raster gates pass; process exit0 and D3D12 ERROR/CORRUPTION0. Evidence is
  `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M6_M5Regression_20261003/`.
- Both final runs restore8/8 config/log/session/graph entries, save no scene and leave no test process running.
  This does not claim interactive Editor review, multi-camera histories, GPU cost, extreme rotation paths or explicit
  Camera reset notification without a qualifying pose/projection discontinuity. Existing threshold-based cut behavior
  remains unchanged; the extra pose-unchanged reset sample is not a reset-invalidation gate.

## Known Follow-ups

Cloud-specific disocclusion and wind/advection reprojection can be improved later.

## Outcomes

M6 automated single-camera production-RG acceptance passes after explicit sky-LUT dependency, failed-work composition
guard and Cloud-before-TAA-snapshot repairs. Original failure evidence remains above. M7/M8 remain untouched.

## Resume Protocol

When debugging, first disable final TAA and validate Cloud temporal independently.
