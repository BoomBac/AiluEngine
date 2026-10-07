# TAA + RenderGraph ExecPlan Progress

## Purpose

This is the only mutable progress/status file for the TAA RG task package.

## Scope

Track current milestone, completed implementation steps, validation results, decisions, blockers, and discovered source facts.

## Non-goals

Do not place long implementation instructions here. Those belong in the current milestone file.

## Architecture Constraints

Do not mark a milestone complete until every acceptance item is actually validated in-engine.

## Baseline

Source: `AiluEngine_Source(20261002-064447).zip`.

## Target Outcome

All M0-M8 gates complete with TAA enabled in RenderGraph and no known matrix/history conflict with supported features.

## Relevant Files

See `README.md` and the current milestone file.

## Milestones

- [x] M0 Baseline and instrumentation
- [x] M1 Camera temporal contract
- [x] M2 MotionVector correctness (automated in-engine/image acceptance; interactive review remains separate)
- [x] M3 TAA core and history lifecycle (automated in-engine/image acceptance; interactive review remains separate)
- [x] M4 TAA RenderGraph integration (core/native RG automated acceptance)
- [x] M5 Transparent/Sprite policy (Policy A; automated production-RG/image retest passed; interactive review separate)
- [x] M6 Volumetric Clouds temporal compatibility (automated single-camera production-RG fixture; broader review separate)
- [x] M7 RayTraceGI temporal compatibility (automated single-camera production-RG/image acceptance)
- [ ] M8 Integration, cleanup, and performance

## Current Milestone

```text
M3-M7 automated acceptance passed within their recorded scopes. M8 final acceptance FAIL on 2026-10-04:
requested feature flags/all-feature passes and destroyed-camera history retirement fail. Actual matrix, continuous
three-minute capture, Editor resize/selection/main camera, GPU/CPU and barrier measurements are recorded in
m8-acceptance-20261004.md. Full perceptual/gizmo/disk-reload coverage is not claimed. The plan is not complete.
```

## Progress

### M8 - Expanded acceptance - 2026-10-04

- `[M8 FINAL FAIL]` Combined production fixture:1280 consecutive frames/5080 DDS, exact seed/resource-version gates
  pass, D3D12 errors0; requested feature flags and all-feature pass gates fail. Renderer forces SSAO/Fog on in the
  ordinary path and off under RTGI. Feature isolation and intended full coexistence remain unaccepted.
- `[M8 HISTORY RETIREMENT FAIL]` Continuous180-second sampling:1412 consecutive frames/5604 DDS, finite/transport
  pass, D3D12 errors0. Four temporary cameras are destroyed, but history count remains6 rather than2 through the end;
  tracked RenderTexture bytes retain2,304,000 extra bytes. Late fixed-size memory otherwise stays stable.
- `[M8 EDITOR SMOKE PASS]` Actual main-window resize gives768x384/960x512/1088x576;12 click/gizmo-key cycles include
  8 selected-entity observations; Play/Stop creates/destroys runtime Scene copy.144 frames pass integrity. Separately,
  real temporary CCamera AA2 and3 GameView/SceneView Play/Stop cycles capture112 frames and pass integrity.
  No perceptual/first-frame/gizmo-transform/disk-reload pass is claimed; Open Scene toolbar is not wired.
- `[M8 PERFORMANCE RECORDED]` Fixed1088x576, no image readback, Debug: TAA rolling GPU median.084282ms,
  CPU record median8.2315->8.4261ms, RG passes58->60. Four replayed D3D12 captures show barrier calls97->100 and
  barrier entries160->169, empty debug-message lists. Injected process crashes on shutdown in renderdoc.dll;
  accept completed frame inspection only. RG compile timing is unavailable because CPUProfileBlock bookkeeping
  is commented out. Normal native runs exit0. Final build-debug and diff check pass; wrapper files restore8/8.
- `[M8 REPORT]` See [m8-acceptance-20261004.md](m8-acceptance-20261004.md), which supersedes the earlier lack of
  runtime/continuous/performance evidence while retaining failed/intermediate runs. M8 remains unchecked.

### M8 - Integration, cleanup, and performance - 2026-10-03

- `[M8 ACCEPTANCE RETEST / PARTIAL]` Fresh Debug build and CameraTemporalHistorySmoke passed. Existing core-RG,
  real-scene controls, seven transparent/Sprite cases, Cloud and RTGI regressions all passed their original analyzers;
  five fixtures exited0, each D3D12 ERROR/CORRUPTION0 and restoration8/8. Actual NewScene Editor input/postprocess
  capture completed176 frames. Seven feature-toggle combinations completed42 sequences/672 frames, followed by
  184.532 seconds of idle/yaw/SSAO toggles with30 bursts/240 frames. All912 matrix/long-run captured frames are
  continuous within sessions and finite. This is Editor smoke/transport evidence: effective Surfel/DDGI contributions,
  paired combination-quality oracles and inter-burst flashes are not established. No separate main camera, Editor
  resize, selection/gizmo or scene reload acceptance. Source history-map retirement/occupancy remains unmeasured.
- `[M8 GPU / CORE INSPECTION]` 1088x576 Debug GPU rolling averages: TAA0.0772365-0.0799070ms,
  opaque snapshot0.0057090-0.0058740ms, StaticMotion0.0092550-0.0093840ms, DynamicMotion0.0074780-0.0076690ms;
  samples include intermittent capture load. Logical color+responsive history5.9765625MiB/camera, excluding
  allocation overhead. Fresh two-frame core-RG RenderDoc replay passed6/6 binding/alias/ping-pong gates;
  full combined-graph inspection, CPU recording cost, compile delta and barrier delta remain open.
- `[M8 EVIDENCE / RESTORATION]` See [m8-acceptance-20261003.md](m8-acceptance-20261003.md) for exact run directories,
  scripts, performance limits and remaining gates. Both Editor runs restore camera/features and8/8 backed-up files;
  scene assets were not saved and started processes are closed. M8 remains unchecked. The source/build-only notes
  below predate this acceptance run; their statement that no Editor/cost capture was available is superseded above.

- `[M8.2 SOURCE PASS]` Repository search confirms the legacy `TemporalAA::HaltonSequence`, `TAAPreparePass`, and old matrix/cbuffer candidates are absent. Removed the remaining unused `_motion_vector_depth` RG field/name/allocation; current MotionVector uses scene depth.
- `[M8.3 SOURCE PASS]` Added Debug assertions for TAA camera mode, finite current UV jitter, valid RG color/depth/motion inputs, distinct history read/write textures, matching history dimensions, and contiguous history frames before reuse. Existing guards still skip invalid pass inputs in non-assert builds.
- `[M8 BUILD PASS]` `cmake --build --preset build-debug` completed successfully, including Engine, Editor, and test targets. Repository-wide search finds no `_motion_vector_depth` or `kMotionVectorDepth` references.
- `[M8.1/M8.4/M8.5 OPEN]` No combined feature-matrix Editor run, multi-minute long-run sequence, PIX/RenderDoc inspection, or GPU/CPU cost capture was available in this turn. Existing M4-M7 single-camera production-RG fixtures are prerequisite evidence, not substitutes for the M8 matrix/performance gates. No live Editor process was running.
- `[M8 ACCEPTANCE OPEN]` M8 remains in progress. Keep this milestone unchecked until its matrix, long-run state, performance measurements, and final documentation gates are recorded.

### M6 - Volumetric Clouds temporal compatibility

- `[M6 FINAL REPAIR/ACCEPTANCE PASS]`2026-10-03 follow-up fixes Cloud's explicit environment sky-LUT dependency and
  captures record-time depth/LUT/motion handles. Missing inputs or skipped compute/reprojection prevent compose/commit.
  Compose loads scene color. TAAOpaqueSnapshot moves455->456 so Cloud455 is included before Sprite460/forward475;
  otherwise stable equal-event sorting misclassifies Cloud as blended responsiveness. Default Cloud remains opt-in.
- `[M6 FINAL BUILD/TRANSPORT PASS]`Full Debug build passes. Final sun-lit unsaved production scene captures384
  continuous observations/2110 DDS with no dimensions/nonfinite/frame-ID errors.383 enabled Cloud frames bind the
  explicit LUT;185 tiled reprojects consume earlier DynamicMotion's exact current id/version. A/B alternates once per
  enabled frame. Final process exit0 and D3D12 ERROR/CORRUPTION0. Four-way Cloud temporal/TAA controls remain unchanged.
- `[M6 FINAL NUMERICAL/IMAGE PASS]`11/11 gates pass. First/resize/back/off-on/20-unit-cut seeds are full CloudMain output
  with no reproject; all updated tile pixels match captured CloudMain exactly. Previous-GPU-frame bilinear oracle
  worst robust RGBA RMS0.00017724<0.001 and correct jitter sign beats opposite. Sampler fractions are1/256 texel with
  half-float output rounding;7200 aggregate pixels within1e-7 of UV0/1 are recorded as boundary-uncertain, with full
  error bounds retained. No interior error is hidden by this classification. Final Cloud reactive mask stays0.
- `[M6 FINAL COMBINED TAA PASS]`Best static integer alignment is[0,0] for both TAA combinations; quadratic offsets
  are(-0.02164,0.00990) and(0.02274,0.06018) pixels, below0.25. Adjacent static RGB RMS drops0.012388->0.005556
  (Cloud temporal off) and0.009046->0.005528 (on). Final four-way montage reviewed; captured Cloud radiance is nonzero.
- `[M6 M5 REGRESSION PASS]`Unchanged M5 thresholds/factor0.95:784 continuous observations,3136 DDS, seven
  material/projection cases and all original repair gates pass. Process exit0, D3D12 ERROR/CORRUPTION0.
- `[M6 FINAL EVIDENCE/BOUNDARY]`Select `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M6_Final_20261003/`
  and `TAA_M6_M5Regression_20261003/`. Both restore8/8 state entries, save no scene and leave no test process.
  RepairRetest lacked a sun; SunRetest used0.01 sun and preceded the snapshot repair; retain as intermediate evidence.
  Acceptance is scoped to this single-camera production-RG fixture, not interactive Editor review, multi-camera,
  GPU cost, extreme rotations or pose-unchanged Camera history-reset notification. M7/M8 remain unstarted.

### M7 - RayTraceGI temporal compatibility

- `[M7 FINAL REPAIR/RETEST PASS, 2026-10-03]` User requested repair and retest. GIPass now produces opaque color at455
  before snapshot456/Sprite460; denoise taps and clamp-neighbor center use one jitter-corrected history_position.
  Camera reset revision is independently observed by TAA/RTGI. Raw/history/debug RenderTextures and synchronous
  reservoir/surface allocation make the first resize dispatch ready. History validity, jitter and ping-pong rotation
  commit after actual dispatch; skipped primary work cannot commit denoise history. TAA factor remains0.95.
- `[M7 BUILD/SMOKE PASS]` `cmake --build --preset build-debug` passes with normal reflection generation.
  CameraTemporalHistorySmoke passes first/rebuild/skipped/duplicate/reset/custom-VP controls.
- `[M7 GPU/IMAGE PASS]` Five64-frame modes for each of Compute and DXR: baseline, jitter-only realtime, full TAA,
  reference and disabled control.640 consecutive observations/4992 DDS;103/103 gates pass; process exit0;
  D3D12 ERROR/CORRUPTION0. Captures have matching dimensions/frame IDs, finite values and complete transport.
  Slow translation/rotation, resize317x179 and return320x180, cut, FOV and unchanged-pose explicit reset execute.
  Required reset frames seed RTGI length1 with raw RGB; TAA seeds exact current packed RGB, including first resize frames.
- `[M7 ALIGNMENT/REFERENCE PASS]` Static motion is below0.01pixel; realtime/reference histories reach length32 despite
  32 distinct static jitters. Full TAA best integer shift[0,0]; quadratic offsets Compute[-0.01509,0.02228] and
  DXR[-0.01494,0.01477]pixels. Opaque reactive mask is0. Adjacent RMS Compute0.08930->0.03044,
  DXR0.08609->0.03229. Reference progressive mean replay max RMS3.81e-8 over16 robust pairs per backend,
  retaining the documented integer-boundary exclusion and original threshold; real movement resets length1.
- `[M7 RECONSTRUCTION PASS]` Measurement kernel calls production ComputeWorldSpacePosition/UnpackNormal with actual
  jittered CameraCB/depth/GBuffer. Max plane error2.34e-6, normal-dot error0 across all640 samples. Known geometry
  coverage is exact outside a1/256 reference-pixel boundary band. Original ideal-float oracle differed on96 pixels,
  all0.001366..0.001373pixels from a horizontal edge. Band/counts are recorded; plane/normal checks omit no pixels.
  Compute's active backend is stochastic PrimaryRay; measurement does not replace it with inactive GBuffer RayGen.
- `[M7 M5 REGRESSION PASS]` Seven-material production-RG transparent policy regression:784 observations/3136 DDS,
  current/departure protected RGB exact, masks and billboard bounds pass. Process exit0; D3D12 severe count0.
- `[M7 IMAGE/RESTORATION PASS]` Final montage reviewed; combined output is stable and aligned in captured fixture.
  Final M7 and M5 runs each restore8/8 backed-up config/log/session/graph entries with matching existence/hashes;
  no scene saved. Runner persists process exit before restoration and retries transient mapped-log copy failures.
- `[M7 FINAL EVIDENCE]` Select only F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M7_FinalRetest_20261003/
  and TAA_M7_M5Regression_20261003/. FinalAcceptance is the original failure; RepairRetest/RepairFinal/RepairVerified/
  RepairComplete are intermediate runs. The last contains valid GPU output but no independently persisted process exit.
  Final report, analysis, manifests, observations, graph declarations, validation, engine log and restoration are retained.
  Interactive Editor, multi-camera, ReSTIR execution, indirect-light-only boundaries and GPU cost remain unmeasured.
  M7 automated acceptance is closed within this scope; M8 is unstarted.


- `[M7 FINAL ACCEPTANCE FAIL, 2026-10-03]` Production Renderer RT mode, both Compute and DXR, unsaved procedural
  environment plus emissive high-contrast meshes. Five64-frame modes per backend: realtime/no-jitter, jitter-only,
  combined TAA, Reference/jitter and disabled accumulation control. Realtime modes include32 static,8 translation,
  8 rotation,317x179 resize/back,2m cut,FOV60->70 and same-pose Camera history-reset notification. No production fix.
- `[BUILD PASS / EXECUTION FAIL]` Full `cmake --build --preset build-debug` passes.640 planned samples/4224 DDS;
  512 observed samples/3304 completed DDS. Observed manifests have matching frame IDs/dimensions and finite pixels.
  Compute128-191 and DXR448-511 never execute; each remaining64-frame segment has consecutive engine-frame steps1.
  Eight first-resize/return frames lack raw/history/debug capture. D3D12 ERROR/CORRUPTION0 is separate from engine RG
  validation failure. Fixture exits1. Runtime33.06s including initialization, single render thread with capture load.
- `[V3 FAIL: RG COLOR DEPENDENCY]` Renderer RT mode schedules GBuffer/motion/depth without deferred lighting/sky color
  writes. TAAOpaqueSnapshot456 reads `_CameraColorAttachmentA` before Debug GI495 first writes it. RG validation rejects
  all128 combined frames with `resource=_CameraColorAttachmentA is read before its first write`. Thus no TAA/RTGI image
  combination or stable one-sample offset acceptance is available. This is an actual production scheduling failure.
- `[SOURCE NEIGHBOR FAIL]` Denoise primary four taps use corrected history_uv, but raytrace_gi.hlsl1055 still computes
  history_center from `pixel - temporal_motion.xy * screen_size`, omitting jitter. The earlier M7 source entry overstated
  coverage: ReSTIR neighbor mapping is corrected, the denoiser's later clamp-neighbor center is not.
- `[V1/V2 PARTIAL]` Both backends have32 static distinct jitter samples with zero geometric motion in jitter modes.
  Static history median grows1->32. No-jitter raw/history adjacent RGB RMS is0.072972/0.003965 for Compute and
  0.004325/0.000164 for DXR. Jitter-only values are0.093000/0.088842 and0.088259/0.088145. These are measured capture
  statistics, not full alignment/convergence acceptance. Compute actively selects PrimaryRay with independent random
  pixel sampling; the GBuffer RayGen reconstruction is not the active kernel. DXR primary/raster foreground agreement
  is>=99.98% for jittered static coverage; Compute is>=99.18%. Arbitrary reconstructed position/normal remains unverified.
- `[V4 PARTIAL / FAIL]` Valid-geometry2m cut and FOV change reset both backend histories to length1 with exact raw RGB.
  First ready frames after resize/back likewise have length1 and exact raw RGB, but the first resize frames are missing
  raw/history/debug transport because new textures are not ready. Same-pose MarkTemporalHistoryReset at local56 is not
  consumed by GIPass: history remains length3. Raw/history max difference is0.50667 in Compute and0.37999 in jittered DXR.
- `[V5 SCOPED PASS]` Reference retains median lengths1..32 across Camera jitter; a real0.2m camera move resets length1.
  Progressive-mean replay from preceding GPU history passes on16 robust pairs/backend: max RMS3.796e-8/3.821e-8 versus
  opposite-jitter mean RMS0.06998/0.07292. Nearest-pixel integer boundaries within1e-4 pixels are uncertain:504000 ROI
  pixel observations/backend excluded and explicitly counted. The full31-pair jitter sequence is not a bit-exact oracle.
  Thin-edge noise/quality, arbitrary path-traced surfaces and ReSTIR execution are not accepted by this mean check.
- `[RESTORATION / EVIDENCE]`8/8 config/log/session/graph entries restored by matching hash/existence; no scene saved,
  no test process left running. Final evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/
  TAA_M7_FinalAcceptance_20261003/` with run1/report.md,analysis.json,montage.png and parent engine.log/restoration.json.
  Acceptance and Runtime directories are intermediate harness runs. Reproduce using RunTaaTransparentAcceptance.ps1
  mode `--rtgi` and AnalyzeTaaRtgiAcceptance.py. Interactive review,multi-camera,GPU cost,fractional TAA offset and
  indirect-light-only boundaries remain unmeasured. M7 stays open; M8 is not started.

- `[M7 SOURCE IMPLEMENTED]` RTGI camera dirty detection now names `GetViewProjNoJitter()` explicitly. Camera identity changes
  join target recreation, history signature, accumulation transition, reference camera change, and large-cut invalidation.
  DXR dispatch uses the camera belonging to the current `RenderingData` view and fills its camera buffer with jittered
  VP/IVP, stable VP/previous VP, and Camera jitter values.
- `[M7 SOURCE IMPLEMENTED]` RTGI compute history receives previous/current top-left-origin UV jitter from Camera. ReSTIR
  temporal reuse and realtime denoise use `uv - motion + previous_jitter - current_jitter`; realtime tap coordinates and
  ReSTIR neighbor validation derive from that corrected history location. Reference accumulation remains separate from
  motion-vector reprojection, remaps jitter-only history, and retains its progressive mean across Halton samples.
- `[M7 BUILD PASS]` `cmake --build --preset build-debug` passed after reflection codegen and the const camera-pointer fix.
- `[M7 RUNTIME/IMAGE ACCEPTANCE OPEN]` No RTGI Editor capture or image sequence was run in this implementation turn.
  The V1-V5 temporal-only, jittered, combined TAA, cut/resize, and reference-mode gates remain unverified in-engine.

- `[M6 SOURCE IMPLEMENTED]` Cloud now records at named event `kAfterMotionVector` (455), before Sprite (460) and
  forward transparency (475), so its declared motion-vector read is recorded after `DynamicMotion`.
- `[M6 SOURCE IMPLEMENTED]` Cloud retains Camera's jittered render-ray corners from M1 and applies the existing Camera
  top-left UV convention in reprojection: `previous_uv = uv - motion + previous_uv_jitter - current_uv_jitter`.
- `[M6 SOURCE IMPLEMENTED]` Invalid history uses a full-resolution CloudMain seed frame without reading history; tiled
  update/reprojection resumes on the next valid frame. Resize, camera identity/cuts/projection changes, render gaps,
  and cloud setting changes invalidate history.
- `[M6 SOURCE IMPLEMENTED]` RG A/B state commits once in the compose callback only after CloudMain and, for tiled reuse,
  tile reprojection were dispatched. Legacy EndPass retains its fallback swap when the RG callback did not commit.
- `[M6 BUILD PASS]` Final `cmake --build --preset build-debug` passed after the execution guard and invalid-history seed
  changes. Reflection generation updated the new named render-pass event through the normal codegen flow.
- `[M6 ACCEPTANCE FAIL, 2026-10-03]` Final unsaved production-RG fixture adds a real procedural EnvironmentComponent
  and registers production VolumetricClouds through AddFeature. Renderer-owned TAA is tested in four temporal/TAA
  combinations,96 samples each: static32, slow translation32, rotation32; the last segment includes317x179 resize/back,
  one-frame Cloud off/on,20-unit teleport and Camera history reset. Wind speed/time are frozen; quality remains high.
- `[M6 BUILD/TRANSPORT PASS]` Full `cmake --build --preset build-debug` passes.384 observations,1918 completed native
  DDS, finite pixels, correct dimensions/manifest frame IDs, consecutive engine-frame steps=1. Process exit0 and D3D12
  ERROR/CORRUPTION0 are transport results, not successful Cloud execution.8/8 configuration/log/session/graph entries
  restore with matching hashes/existence; no test scene asset saved and no test process remains.
- `[M6 ROOT CAUSE]` EnvironmentSystem imports `_EnvironmentSkyViewLUT` into `_rg_handles._sky_view_lut`, and the real
  DrawSkyBox pass reads it. Cloud calls ReadGlobalTexture(`_TexSkyViewLUT`), but no producer publishes this RG-global
  slot. Every enabled Cloud frame logs the missing global; its compute callback resolves null and returns before
  CloudMain dispatch. The declared output nevertheless proceeds to capture/compose.
- `[M6 EXECUTION/IMAGE FAIL]`383 enabled frames contain0 Cloud LUT reads and0 TileReproject passes. Only CloudTexA
  appears as the output: no observed A/B rotation. Selected first/resize/restart/cut/reset seed captures are RGBA zero;
  four-way montage is black. Current-motion consumption, valid seeds, resize/cut recovery and combined jitter cannot
  be accepted from this sequence. Event455 and jitter-delta ownership/formula remain source-confirmed only.
- `[M6 FINAL EVIDENCE]` Use `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M6_ProductionEnvironment_20261003/`:
  run1/analysis.json, m6_graph.csv, m6_observations.csv, combinations.png, native capture manifests, engine.log and
  restoration.json. Test/TaaCloudAcceptance.h plus `RunTaaTransparentAcceptance.ps1 -Mode --cloud` reproduce the fixture;
  `AnalyzeTaaCloudAcceptance.py` exits1 on acceptance failure. The earlier TAA_M6_Acceptance run lacked an explicit
  environment and is preflight evidence only. No Editor interactive visual acceptance is claimed.
- `[M6 REPAIR DIRECTION]` Read the existing explicit `_rg_handles._sky_view_lut` and capture its record-time handle;
  prevent composition of an output when required Cloud compute did not execute. Rerun this same acceptance sequence
  before evaluating reprojection/jitter quality. This turn adds validation tooling and records failure; production
  repair and M7/M8 are outside this acceptance request.

### M0

- [x] Initialize `RenderFeature::_is_active` deterministically to `true`, matching `RenderPass`; explicit RayTraceGI/RTXDI disable calls remain after feature registration.
- [x] Add a SceneView `TAA Jitter` control that changes only the active SceneView camera's AA mode. TAA is still absent from `_features`, so the legacy resolve remains disabled.
- [x] Add an opt-in, eight-frame temporal-state log with frame, camera hash, AA mode, matrix/UV jitter, and jittered/no-jitter VP offset terms.
- [x] Add a SceneView `Motion Debug` view, default off. It reads the final RG motion-vector version and displays signed XY direction as color over the viewport. FrameDebugger captures remain available for raw texture inspection.
- [x] Record baseline screenshots/captures after building and launching the updated Editor. Artifacts are under `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Saved/TAA_M0_Baseline_20261002/`.
- [x] Re-run the source audit against the working tree; the package baseline remains accurate for the TAA, camera, MotionVector, Cloud, and RTGI findings. M0-only addition: the SceneView exposes Camera jitter without registering TAA. At M0 completion, `Renderer::PrepareCamera()` still put no-jitter data in `_MatrixVP` and `_MatrixIVP`; M1 now corrects that contract.

#### Fixed validation scene references

Use the existing AiluTestProj scenes; do not save test-only edits back to them.

- Static edges / camera pan: `Assets/DDGITest/08_thin_plane_occluder.almap`, camera `MainCamera` at `(0, 1.4, 2.3)`, facing `+Z`; thin divider at `(0, 1.5, 0)` with scale `(8.4, 1, 3)`, emitter at `(0, 2.8, -2)` with scale `(3, 0.06, 1.6)`, and `dark_side_ball` at `(1.8, 0.35, 1.2)`. Keep the camera stationary for jitter/culling checks, then pan horizontally for camera-motion vectors.
- Motion / occlusion: use the same scene. Move `dark_side_ball` from `(1.8, 0.35, 1.2)` through the divider along Z to `(1.8, 0.35, -1.2)` while keeping `MainCamera` fixed. The divider is the static foreground occluder; record the baseline visibility/depth result without repairing it in M0.
- Temporal feature reference: `Assets/VolumetricFogBaseline/VolumetricFogBaseline.almap`; use `FogCamera_Main_Shadow` at `(0, 2, -8)` and `FogCamera_NearOccluder` at `(0, 1.4, -2.2)`. Cloud and RTGI remain optional and are not configured in this scene.
- These existing scenes do not cover alpha-test, blended transparency, or Sprite in one fixed setup. That combined policy scene remains a follow-up before the M5 acceptance gate.

#### M0 implementation status

- `cmake --build --preset build-debug` passed. The first full rebuild completed; the later rebuild required closing the Editor because it held `Engine_d.pdb` open, then passed.
- Editor runtime smoke: `render.list_features` reports SSAO/Fog/DDGI/Surfel/Sprite active, RayTraceGI/RTXDI inactive, and no TAA feature. SceneView `TAA Jitter` toggled AA mode to `kTAA`, emitted eight temporal-state lines with changing jittered VP offsets and stable no-jitter offsets, then returned to `kNone`.
- A SceneView `Motion Debug` runtime check replaced the scene output with the signed-vector color view, and toggling it off restored the regular view. The display was black for the static baseline, as expected for zero motion.
- Static `NewScene` baseline: `CopyColor` output, 8 PNG frames at 1088x576, capture indices 0-7; capture directory `.../Saved/TAA_M0_Baseline_20261002/color/session_1/`.
- Static `NewScene` motion baseline: `DynamicMotion` output, 8 PNG frames at 1088x576; direct PNG readback appears black for near-zero signed values. The color-mapped live debug view is the useful in-Editor visualization.
- Camera-pan baseline on `NewScene`: controller moved from x=10.4144 to x=12.0144 while 16 consecutive native DDS frames were captured (engine frames 42405-42420, 1088x576, RGBA16F). XY motion stayed at or below `2.4e-7` absolute, so the current camera-pan producer path fails the expected coherent screen-wide motion check. This is recorded for M2; no MotionVector correction was made in M0.
- The active SceneView was `NewScene`; the fixed references above remain reproducible alternate scenes. No scene asset was edited or saved.
- Live Editor showed unrelated DX12 debug-layer warnings at startup. No shader compilation error was observed; the debug view rendered. These warnings were not attributed to the M0 changes.

### M1

- [x] Store explicit stable projection, jittered VP and previous stable VP camera state.
- [x] Keep culling on the no-jitter frustum and separate render-ray corners.
- [x] Update main and auxiliary CameraCB producers to use explicit camera matrix accessors.
- [x] Run M1 in-Editor acceptance checks V1-V6; see the acceptance retest record below.

M1 implementation and V1-V6 acceptance passed on the recorded retest; see evidence below.

### M2 implementation - 2026-10-02

- [x] RG `DynamicMotion` reads the current scene `_depth_target` as DSV and binds it for `LEqual` testing; it no longer writes or imports the isolated `_motion_vector_depth` resource.
- [x] The MotionVector material uses `ZWrite: Off`, so dynamic velocity cannot alter scene depth.
- [x] Legacy `MotionVectorPass::Execute()` draws camera motion without a DSV, then tests dynamic geometry against `_camera_depth_target_handle`.
- [x] Confirmed transform history initializes `_prev_render_world_matrix` from current on first history and render-interpolation reset paths; `Renderer` consumes that previous render matrix for primitive data.
- [x] Added `Camera::MarkTemporalHistoryReset()` so camera cuts/teleports can align previous stable VP to current before the next render view. No existing cut notification source was found, so callers must signal cuts explicitly.
- [x] `cmake --build --preset build-debug` passed after the MotionVector and Camera changes.
- [ ] M2 V1-V6 runtime/capture acceptance is still open. Editor capture RPC tools and `Tools/render_capture_client.mjs` were unavailable in this environment; no claim is made for camera motion vectors, occlusion pixels, or alpha-test visual parity.
- [ ] M2 acceptance criteria remain unchecked; do not advance to M3.

### M2 runtime acceptance - 2026-10-02

- **Overall: FAIL.** V2 camera translation and rotation produce zero motion despite actual camera movement. M2 remains open;
  M3 is not authorized by this acceptance run. No production rendering code was changed.
- **Build PASS:** `cmake --build --preset build-debug` completed before launching a fresh Debug Editor (PID 3992).
  Project: `AiluTestProj/empty.json`; scene: `NewScene`; output: 1088x576; no TAA resolve registered.
- **Capture/continuity PASS:** eight canonical native RGBA16F motion sequences, 784 frames total, all `capture_index` counts
  correct and `engine_frame` gaps exactly 1. Earlier repeated dynamic runs remain separate in their session directories.
  Use each `<case>_status.json` output directory to select the final evidence, not a directory-name sort.
- **V1/V6 PASS:** `DynamicMotion`, jitter enabled, 128 consecutive frames: XY maximum exactly 0. AA-off 16-frame control
  also exactly 0. Camera jitter observations change; transforms remain stationary.
- **V2 FAIL:** `DynamicMotion` pan and rotation, 64 frames each, XY maximum exactly 0. Pan observed x=12.0142 -> 12.2922;
  yaw observed 1779.5500 -> 1780.6240 degrees. Independent `StaticMotion` retests, 128 frames each, also exactly 0;
  observed x=12.0142 -> 12.3342 and yaw=1779.9501 -> 1781.5500. All sampled current/previous stable VPs are identical.
  This excludes DynamicMotion overwriting a correct camera-motion output as the explanation.
- **V3 PASS for rigid opaque geometry:** move cube right by 0.15 world units per automation edit at z=-3, camera at
  (0,6,-10), facing +Z. Four captured edit frames have median XY=(0.01057434,0), 5852-6138 active pixels.
  Front-face geometric prediction is 0.01058040 UV: difference 6.06e-6 UV (0.0066 pixel), below one half-float ULP.
  `prev_uv=uv-motion` therefore samples to the left for this right-moving object. Inter-edit frames are stationary.
- **V4 PASS for full occlusion:** same moving cube at z=3 behind an 8x6x1 opaque wall centered at (0,6,0), camera fixed.
  Five automation position edits during 128 consecutive captured frames; XY maximum exactly 0 throughout.
  Front/hidden color references and motion debug images were inspected. Partial occlusion and moving-camera occlusion
  were not measured; camera-motion failure prevents claiming the latter.
- **Scene-depth write rule SOURCE PASS:** RG reads scene depth as DSV, does not declare a depth write; shader is
  `ZWrite: Off`. Reverse-Z mapping of `LEqual` is confirmed in `Shader.cpp`. Native capture of DynamicMotion's DSV input
  fails with `unsupported depth-stencil texture capture`; no pixel-level before/after scene-depth equality claim.
- **V5 OPEN:** no project material asset enables `ALPHA_TEST`, and the current material automation adapter exposes no
  editable properties. Source audit found an additional coverage risk: GBuffer multiplies texture alpha by
  `_AlbedoValue.a`; MotionVector uses texture alpha alone when `_SamplerMask & 1`. Coverage with tinted alpha is not
  equivalent. This is a source finding, not a runtime alpha-test failure measurement.
- **First-frame/reset OPEN:** `MarkTemporalHistoryReset()` exists, but there is no reset automation input; no first-render
  frame or explicitly signaled cut was captured. Normal stationary zero motion does not establish reset correctness.
- **CPU_DEFORM OPEN:** this scene has no skinned/deforming test mesh. Rigid-object results do not validate previous vertices.
- **Legacy SOURCE PASS only:** Execute uses color-only fullscreen camera motion, then scene depth for object motion.
  Legacy runtime was not exercised; it is not marked deprecated or independently accepted.
- **Diagnosis:** Camera history production is the first repair target. `UpdatePreviousTemporalState()` copies the current
  stable VP when the graphics frame changes; it is invoked both during controller matrix rebuild and again from
  `PrepareRenderFrame()` after render synchronization. Graphics frame increment occurs in `D3DContext::Present()`.
  The sampled VP equality and zero captures establish lost camera history; the precise cross-thread overwrite timing
  still needs instrumentation. Preserve the VP of the previous rendered view rather than compensating in shader math.
- **Restoration:** both temporary cube transforms and the original camera pose/AA were restored through RPC. Structure
  revision stayed 39; edit revision increased from 40 to 80 because test transforms use undoable writes. No scene save
  was issued. The test Editor was stopped to discard that dirty test state; config/session restoration is in the report.
- Evidence/scripts/report: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M2_Acceptance_20261002/`.

### M2 camera-motion diagnosis - 2026-10-02

- **Cause localized:** camera history is advanced from both pre-synchronization controller updates and synchronized render
  preparation, using the asynchronous graphics frame counter. `UpdatePreviousTemporalState()` copies the current stable
  VP, not a saved previous-rendered VP. A previous frame's worker `PresentImpl()` can increment the counter between these
  two calls. The second call then replaces history with the already-updated current VP, and camera motion becomes zero.
- **A/B runtime evidence:** same Debug binary, project, scene, 1088x576 and jitter-on camera driver; only
  `isMultiThreadRender` changed. Single-thread `StaticMotion`: 128 pan frames, peak XY magnitude 0.00440412 UV, 13 nonzero
  frames; 128 rotation frames, peak 0.00800207 UV, 17 nonzero frames. Fresh multi-thread replay: both 128-frame sequences
  exactly zero. All four sequences have consecutive engine frames. Sparse RPC observations can miss the transient
  previous/current difference even in the passing single-thread control; those snapshots alone do not diagnose timing.
- **Source ordering:** UI/controller updates occur before `RenderPipeline::Render()` waits for the prior render worker;
  worker increments the graphics counter inside `PresentImpl()` before its completion notification. After that wait,
  Pipeline calls `Camera::PrepareRenderFrame()`, which calls the history-copy function again. CameraCB faithfully uploads
  the resulting current/previous matrices; shader subtraction is consistent with the zero output.
- **Repair direction, not implemented:** move history advancement to a single explicit render-view boundary after the
  existing wait; retain a per-camera last-rendered stable VP snapshot. Controller matrix rebuild and jitter/ray refresh
  must not advance history. First view/reset uses previous=current; repeated preparation of one render view must not
  advance twice. Keep no-jitter history and the M1 per-render jitter refresh contract.
- No production code changed or new build performed. Both test Editors were stopped, and EngineConfig, EditorConfig,
  dock layout, main widget and prior editor session match backups byte-for-byte. No scene entity edit/save in this run.
- Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M2_Diagnosis_20261002/`; single-thread evidence at the root,
  fresh multi-thread evidence in `multi_thread/`. M2 remains failed; wait for an explicit repair request.

### M2 camera-history repair and retest - 2026-10-02

- **Implemented the approved repair:** `PrepareRenderFrame(frame_index)` advances history only at the synchronized
  render-view boundary, reading previous VP from a per-camera last-rendered stable snapshot. Matrix rebuilding,
  `LookTo()` and `SetViewAndProj()` refresh matrices without advancing history. First preparation/reset aligns
  previous=current; repeated preparation at the same supplied frame index does not advance twice. Cubemap faces
  explicitly prepare their own histories before rendering. No MotionVector shader compensation was added.
- **Build PASS:** final `cmake --build --preset build-debug` completed. Camera header changes went through normal
  AiluHeadTool code generation. Rendering architecture documents the new boundary.
- **Native regression PASS:** `CameraTemporalHistorySmoke`, using the actual exported Camera class, tests perspective
  and orthographic first-frame alignment, multiple controller rebuilds, skipped/duplicate frame indices, reset,
  motion resumption, `LookTo()` and custom VP. These checks validate CPU Camera state, not GPU first-frame/reset output.
- **V2 defect retest PASS:** fresh single/multi-thread Editors, Camera jitter enabled, no TAA resolve; each pan and rotation
  case captures 128 consecutive `StaticMotion` RGBA16F frames. Perspective peak magnitudes: multi-thread pan 0.00417376 UV,
  rotation 0.00759986 UV; single-thread pan 0.00449814 UV, rotation 0.00799454 UV. Active-pixel median X is negative for
  rightward camera motion, consistent with `history_uv=uv-motion`. Driver timing differs, so equal peak values are not expected.
- **Orthographic motion retest:** width 40, immediate transform edits rather than the perspective-only interpolation
  controller. Both threading modes produce pan peak 0.00188371 UV and rotation peak 0.17297727 UV. Rotation peak includes
  far/sky reconstructed pixels and is not a foreground reprojection-error metric. Initial ineffective controller runs
  are retained separately; final case status files select the actual driven captures.
- **V1/V6 regression PASS:** 128-frame static-jitter sequences for each mode/projection. Maximum XY magnitude:
  perspective multi-thread 2.98e-7 UV, single-thread 1.69e-7 UV; orthographic exactly zero in both. Each sampled sequence
  has changing jitter. No active pixels above the analysis threshold 1e-5 UV.
- **V3/V4 regression PASS in prior rigid/full-occlusion setup:** front-object peak 0.01057434 UV; hidden object remains
  exactly zero over 128 frames. Both temporary entity transforms match their original snapshots after restoration.
- **Capture PASS:** 14 canonical motion sequences, 1792 frames, all capture indices correct and engine-frame gaps exactly 1.
  Sparse automation edits and capture overhead remain limitations; no mouse/UI, full TAA resolve or matched-timing
  performance acceptance claim. Representative signed-motion images were inspected.
- **Restoration PASS:** both test Editors stopped; EngineConfig, EditorConfig, dock layout, main widget and prior editor
  session match pre-run backups byte-for-byte. Camera states restored by driver finally blocks. No scene save issued;
  temporary undoable object edits were discarded when the multi-thread test Editor stopped.
- **M2 remains open:** alpha-test coverage mismatch, CPU_DEFORM, GPU first-frame/explicit-reset output and legacy runtime
  remain unmeasured. Partial/moving-camera occlusion is an additional evidence limitation. The repaired camera-motion
  defect no longer blocks progress, but remaining required gates prevent marking M2 complete or advancing to M3.
- Report/scripts/native captures: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M2_HistoryFix_20261002/`.

### M2 remaining acceptance - 2026-10-02

- **Overall: FAIL; do not enter M3.** This acceptance adds a standalone GPU measurement executable and its CMake target;
  no production rendering algorithm was changed. Existing camera-history repair and unrelated dirty changes are retained.
- Test host: `MotionVectorGpuAcceptance`, Debug Engine DLL, temporary scene/materials/texture, 320x180, perspective FOV
  60 degrees, single-thread submission, graphics jobs disabled. Temporary EngineConfig files live beside the exercised
  executable; project scenes/assets and Editor session were not saved. Camera warm-up uses a separate camera. First/reset
  captures are taken on the trigger frame, not after settling. All reported measurements use `canonical/` captures;
  earlier root/`run_all/` captures used an incorrect fixture FOV and are excluded.
- **GPU first-frame/reset PASS for this perspective fixture:** first submission, engine frame 89, max XY motion 0;
  explicit reset after teleport, frame 183, max XY motion 0. Both are finite and have visible geometry (3,844 / 3,906 pixels).
  The unreset teleport control, frame 180, produces max motion 0.292236328125 UV on 3,906 foreground pixels.
- **RG depth preservation PASS:** before/after R32Float copies of the actual scene DSV are byte-equivalent in first,
  static, teleport, reset, rigid and CPU_DEFORM cases. The rigid-motion case has 3,844 covered pixels, motion
  +0.0004870891571044922 UV horizontally, and zero changed depth pixels. This is GPU evidence, extending the prior
  source-only ZWrite Off check. It does not establish legacy depth preservation.
- **CPU_DEFORM FAIL:** manually enable the existing keyword and shift current vertices +0.2 world units in X while
  keeping previous positions unchanged, using the production SkeletonMesh layout. Expected motion is uniformly
  (+0.019485571585149873, 0) UV. Captured frame 178 instead has max magnitude 0.2042225748184079 UV; masked Y spans
  [-0.16943359375, +0.16943359375], and p95 vector error is 0.17304557443426788 UV. Shader `position_prev` consumes
  TEXCOORD0, while SkeletonMesh stores UV0 there and previous Float3 positions at TEXCOORD1. Additionally, repository
  search found no production keyword enabler and no per-frame producer updating `_previous_vertices`; the synthetic
  keyword-enabled fixture does not claim production skinned-animation support.
- **Legacy FAIL:** static and rigid motion captures complete and the rigid foreground velocity matches the RG control.
  However, both static and rigid cases change all 3,844 covered DSV pixels from depth 0.019019020721316338 to 0.
  `MotionVectorPass::Execute()` binds the scene depth with its default Clear load action; unlike the RG declaration,
  it does not preserve the attachment. ZWrite Off alone does not prevent this clear. Legacy is neither fully fixed nor
  explicitly deprecated, so its acceptance gate remains failed.
- **RG alpha-test FAIL / coverage measurement blocked by runtime error:** value-only, textured and tinted variants each
  fail before the requested capture. Exception diagnostics record D3D12 message 708: a root parameter declared CBV is
  set as a descriptor table, followed by an access violation in `nvwgf2umx.dll`. An A/B with CPU state batching and
  incremental graphics binding disabled reproduces the same error. This is an observed binding failure; its deeper
  RHI cause was not repaired or established in this acceptance task.
- Alpha source-contract failures remain confirmed independently: GBuffer takes UV from TEXCOORD0, MotionVector takes
  TEXCOORD1; textured GBuffer alpha is `_AlbedoValue.a * texture.a`, while MotionVector ignores the value alpha.
  The controlled alpha fixture supplies TEXCOORD1 explicitly to distinguish the binding failure from missing UV input;
  it still fails on the RG path. Thus tinted-alpha coverage parity is **not** claimed as measured. Legacy textured alpha
  produces zero GBuffer depth coverage yet 3,844 velocity pixels in this fixture; its depth-clearing defect and binding
  behavior prevent treating that capture as a clean test of alpha clipping alone.
- **Validation levels:** Debug compilation/linking passed; 27 native DDS readbacks completed (9 cases x motion/before/
  after). Image/numeric analysis passed for first/reset and RG depth preservation, and measured CPU_DEFORM/legacy failures.
  No full Editor visual acceptance was claimed. Completed isolated captures subsequently hit a Texture3D release
  `Allocator::Deallocate` assertion during Application shutdown; the executable as a whole is not reported as a passing
  smoke test. The shutdown assertion is recorded separately from the completed GPU measurements.
- Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M2_Remaining_20261002/`: `report.md`, `analysis.json`,
  `analyze.py`, `canonical/` DDS/manifests/runtime/exception logs, and `alpha_binding_control/` A/B evidence.

## Decision Log

### D001 - Camera owns jitter

No feature may generate an independent camera projection jitter sequence.

### D002 - Render/depth matrix pair

`_MatrixVP` and `_MatrixIVP` represent the current jittered raster camera.

### D003 - Stable motion

`_MatrixVP_NoJitter` and `_MatrixVP_Pre` are current/previous no-jitter matrices used for motion.

### D004 - Stable culling

`GetViewFrustum()` must be no-jitter.

### D005 - Explicit RG dependencies

TAA, Cloud, and RTGI temporal passes must bind/read RG handles explicitly instead of depending on legacy global textures.

### D006 - Keep GetViewProj compatibility

Do not silently change existing `Camera::GetViewProj()` from no-jitter to jittered. Add explicit accessors and migrate call sites
where required.

## Surprises & Discoveries

Initial audit:

1. `Renderer::PrepareCamera()` is inconsistent with `ViewEntity::FormCamera()`.
2. Orthographic `GetProjNoJitter()` is invalid under current implementation.
3. Culling frustum is currently jittered.
4. TAA owns a duplicate Halton sequence.
5. TAA first history read can be uninitialized.
6. TAA shader ignores `_HistoryFactor` and ignores `_Jitter` in reprojection.
7. Dynamic MotionVector depth test is isolated from scene depth.
8. Cloud RG pass can read an old motion-vector version.
9. RTGI has a duplicate CameraCB builder and temporal reprojection lacks jitter delta.
10. Transparent/Sprite are before CopyColor/TAA and lack a full temporal ownership policy.

Append new discoveries; do not delete historical entries.

## Validation

Use concise entries such as:

```text
[M1.3 PASS] Static camera, TAA jitter enabled: culling result count stable for 200 frames.
[M2.4 FAIL] Dynamic object behind wall still writes velocity. PIX shows scene depth not bound in DynamicMotion.
```

Current validation:

- `[M0 PASS]` `cmake --build --preset build-debug` completed successfully after the Editor released its PDB lock.
- `[M0 PASS]` Editor runtime confirmed jitter toggling and eight temporal-state log entries while TAA resolve remained absent.
- `[M0 PASS]` Editor runtime confirmed the motion-vector debug view toggles on/off; static output was near-black as expected.
- `[M0 BASELINE FAIL, M2 follow-up]` A 1.6-unit camera pan over 16 consecutive frames produced at most `2.4e-7` absolute XY motion-vector values.
- `[M1 SOURCE PASS]` Stable/jittered projection and VP accessors are implemented; both main CameraCB paths use jittered VP and inverse jittered VP, while culling reads the stable frustum.
- `[M1 BUILD PASS]` `cmake --build --preset build-debug` exited 0 after the final orthographic-jitter and projected-UV diagnostic changes; Engine, Editor, and test targets completed.
- `[M1 CAPTURE PASS, NO-JITTER ONLY]` In `AiluTestProj/NewScene`, `DeferredLightingPass` wrote four 1088x576 PNGs for consecutive engine frames 42568-42571 to `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M1_CameraContract_20261002/no_jitter/session_2/`. The selected output was visually inspected; it establishes capture transport and a stable no-jitter reference only.
- `[M1 RUNTIME PENDING]` The SceneView jitter control remained off during the runtime capture. This session's automation API has no setter for Camera AA mode and desktop input did not activate the button, so V1-V6 jitter-on checks remain open. The Editor was closed and the original `.ailu/editor_session.json` was restored byte-for-byte; the scene revision did not change and no scene asset was saved.

### M1 acceptance run - 2026-10-02

- `[M1 NOT ACCEPTED]` V5 failed: valid-width orthographic SceneView has aa=2 but a fixed jitter/render VP over 128 distinct rendering frames. `SceneView::ProcessCameraInput()` returns from its 2D branch before the controller's per-frame matrix rebuild. Keep M1 open and do not enter M2.
- `[M1 BUILD PASS]` Final `cmake --build --preset build-debug` passed. One intervening link was blocked by the hidden test Editor's PDB lock; terminating only that test Editor and rebuilding passed.
- `[M1 V1/V2 PERSPECTIVE]` 128 frame observations, 126 unique jitter values; UV delta max error 3.53e-8. Static edge shift correlation 0.9922 and RMS 0.0376 pixel against expected Camera UV jitter over 16 consecutive DeferredLightingPass captures. Stable projection unchanged; stable VP changed up to 1.91e-6 from stationary controller quaternion recomputation. Actual float IVP*VP identity error max 8.20e-4.
- `[M1 V3 NUMERICAL PASS, PERSPECTIVE ROI]` Reconstructing 4800 floor pixels per frame from 16 consecutive R32_FLOAT CopyDepthPass captures gives plane residual RMS 0.0271 mm using matching jittered VP, versus 7.231 mm using stable VP. CPU double inverse and selected floor ROI; no shader-wide world-position readback claimed.
- `[M1 V4 PARTIAL]` Offline box checks using the captured planes showed no inside/intersecting/outside toggles over 128 observations. Native Renderer visible/cull status collection with a placed box remains open; perspective planes had coefficient drift up to 0.04483 from controller floating-point variation.
- `[M1 V6 NUMERICAL PASS]` Render-corner re-projection max NDC error 3.34e-5 perspective / 3.26e-6 orthographic. Clouds remained disabled.
- `[M1 ADDITIONAL DISCOVERY]` Camera constructor leaves `_size` uninitialized. Initial orthographic captures were sky-only and invalid; explicit runtime width 40 produced valid geometry but still exposed the fixed-jitter failure. Production camera initialization was not changed during acceptance.
- `[M1 AUTOMATION]` Added validated runtime AA/projection/orthographic-width parameters to existing SceneView camera setter and temporal diagnostics to its getter. Camera is not a reflected class, so no reflection conversion was introduced.
- `[M1 RESTORATION]` Runtime pose/AA/projection restored in `finally`; scene revisions stayed 40/39 and no scene saved. Test Editor closed and original project editor_session.json restored byte-for-byte.
- Artifacts/report: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M1_Acceptance_20261002/report.md`. All selected color/depth capture runs have consecutive engine frames. Initial `orthographic_*` runs are invalid; use `orthographic_valid_*`.

### M1 fixes and retest - 2026-10-02

- `[M1 PASS]` Fixed V5 and default orthographic width; V1-V6 camera-contract checks now have in-engine sample/capture evidence. No M2 implementation performed.
- `[M1 FIX]` `Camera::PrepareRenderFrame()` refreshes Camera projection jitter/render VP/corners from the graphics frame seed. `RenderPipeline::Render()` calls it after frame synchronization/NewFrame, before view/culling preparation. Stable matrices/frustum stay owned by camera transform rebuild; custom VP retains zero jitter and previous stable VP keeps its once-per-frame guard. `_size` defaults to 10; default AA remains None.
- `[M1 BUILD PASS]` Final `cmake --build --preset build-debug` passed. An initial native diagnostic link error was resolved by routing the unexported `ViewFrustum::Conatin` call through Camera's AABB query. Final diagnostic text-only edits rebuilt successfully.
- `[M1 V1/V5 PASS]` Perspective and orthographic: 128 distinct frames AND 128 distinct jitter values each. Orthographic stable projection/VP/frustum exactly unchanged. Previous stable VP matches stationary current VP. AA-off tests (16 snapshots per projection) return exactly zero jitter and render VP equals stable VP. Default orthographic width check returns 10 without an override.
- `[M1 UV/V2 PASS]` Projected texture-UV jitter max error 3.61e-8 perspective / 1.48e-8 ortho. Static diagonal edge shift correlation 0.9886, RMS 0.0394 pixel over 16 consecutive DeferredLightingPass frames. Actual float inverse/VP identity error max 7.61e-4 perspective / 1.54e-5 ortho. Perspective stationary-controller float VP/plane variation remains recorded separately in the report.
- `[M1 V3 NUMERICAL PASS]` Native R32_FLOAT depth, 16 consecutive frames per projection: jittered world-plane residual RMS 0.0268 mm perspective floor (4800 pixels/frame) and 0.0280 mm orthographic facade (9000 pixels/frame). Stable-inverse negative controls are 7.268 mm / 4.037 mm. Selected-ROI CPU inverse of captured GPU depth; no full-screen GPU world-position readback claim.
- `[M1 V4 NATIVE PASS]` Fixed world-space inside/intersecting/outside AABBs tested with Engine's actual Renderer-equivalent culling predicate, 128 distinct jitter samples per projection: visible counts [128,128,0], zero toggles. No scene entities/assets mutated.
- `[M1 V6 NUMERICAL PASS]` Render-corner NDC error max 3.32e-5 perspective / 1.08e-5 ortho. Clouds remain disabled; Cloud temporal integration remains M6 scope.
- `[M1 CAPTURE PASS]` Eight capture sessions (16-frame color baseline/jitter and 4/16-frame depth baseline/jitter per projection), all engine-frame gaps exactly 1. The refreshed render camera now selects the Halton sample from the same engine-frame seed; independently confirmed by live snapshots, raster shift and depth shift.
- `[M1 RESTORATION PASS]` Original pose/AA/projection/width restored, scene revisions remain 40/39, no scene save. Test Editor closed; prior editor_session restored byte-for-byte, and all three Editor configuration files match their pre-run backups.
- Report/evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M1_Retest_20261002/report.md`; representative images inspected. Interactive mouse/UI acceptance and full TAA resolve quality are not claimed.

## Blockers

None external.

Hard implementation gate:

```text
Do not implement RG TAA resolve before M1-M3 pass.
```

## M2 Repair / Retest - 2026-10-02

- Fixed MotionVector UV/previous-position semantics and material-multiplied textured alpha.
- Fixed Material global binding re-resolution across variants and variant-aware frame constant blocks.
- Fixed DX12 input slots: reflection enumeration order differs from register IDs when SV_INSTANCEID is present.
- Added per-mesh/entity position-buffer history for CPU/Compute skinning, stationary/first/gap initialization, automatic
  draw-specific CPU_DEFORM selection, and GPU vertex-stream view refresh on rotation.
- Fixed legacy color/depth Load actions and shutdown window-event dispatch after LayerStack destruction.
- Final build-debug and CameraTemporalHistorySmoke passed. Final GPU executable: exit 0; D3D12 validation enabled,
  per-frame ERROR/CORRUPTION count 0. All 16 captures completed (48 DDS), analysis/gates PASS 38/38.
- RG / legacy textured alpha: 1922 depth pixels = 1922 moving pixels; tinted alpha clips both to 0. No outside motion.
- CPU_DEFORM +0.2 X: measured 0.0194854736328125 UV vs expected 0.019485571585149873; error < 1e-6.
- Actual CSkeletonMesh CPU and Compute skinning +0.005 X/frame: 0.0004870891571044922 UV; stopping yields exact zero.
- All 16 depth-before/depth-after arrays are byte-identical; first/static/reset motion is zero; unreset cut control is nonzero.
- Test validity corrections: RGBA8 SetPixel32 copied 16-byte Color and corrupted allocation metadata; test now uploads
  packed bytes. A tentative allocator change was reverted. Real skinning fixtures use CSkeletonMesh and row-vector
  translation; earlier static-component/column-translation results are excluded. General texture setter encoding is a
  separate follow-up, not a resolved production API issue.
- Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M2_RepairRetest_20261002/accepted/`:
  `report.md`, `run_all.log`, `analysis.json`, `gates.json`, `analyze.py`, `gates.py`, DDS/NPY/PNG.
- This is automated in-engine/capture/numerical acceptance. No new complete interactive Editor visual acceptance claim.
  M3 was not started; project scenes and Editor session were not saved.

## Next Step

M2 repair/retest passed its automated in-engine and image/numerical gates. The user explicitly requested M3 implementation.

### M3 implementation - 2026-10-02

- Removed the TAA-owned Halton sequence, unused `TAAPreparePass`, raster fallback matrices/material state, and the separate
  feature jitter scale. `TemporalAA::AddRenderPasses()` now reads the current Camera UV jitter and does not modify projection.
- Replaced the old TAA history record with per-camera A/B textures, explicit read/write side, history-valid state, previous UV
  jitter, rendered-frame index, dimensions, camera identity, and prior camera/projection snapshot.
- Added invalidation for first use, texture recreation/resize, TAA disabled frames, skipped render frames, camera lifetime
  identity changes, large translation/rotation cuts, projection type/FOV/near/far/aspect discontinuities.
- First use and invalid history now write current color only. Reprojection adds previous-current Camera UV jitter, and blend
  weight now uses `_HistoryFactor`. History side and snapshots advance only after command-buffer execution and `EndPass()`.
- Bound depth explicitly and added a dedicated motion-vector RTHandle input to `RenderingData`; the compute pass binds it when
  supplied and forces current-only output when it is unavailable. Current motion vectors are produced only as an RG handle,
  which cannot be consumed by the legacy Execute path. Supplying a legacy RTHandle producer or moving TAA to RG reads is still
  required before temporal accumulation and M3 validation can pass; the latter is M4 scope.
- `cmake --build --preset build-debug` passed, including reflection regeneration. No shader compile or runtime/image acceptance
  was run; the active Editor path still does not register TAA and the legacy Execute path has no motion-vector RTHandle source.

M3 remains open. V7 current-only/unnormalized-filter behavior is repaired and isolated GPU regressions pass.
Retain the live Editor/full off-on/static convergence gaps. Do not enter M4.

### M3 acceptance - 2026-10-02

- `[M3 NOT ACCEPTED]` V7 fails: valid-history `_HistoryFactor=0` returns filtered current instead of raw current. Nine samples
  fail; max RGB error 0.15625, sample 65 RMS 0.1037. `_FILTER_CENTER` uses an unnormalized Mitchell 3x3 weight sum
  1.1894952136. No production TAA code/shader modified during acceptance; M4 remains unstarted.
- `[M3 BUILD PASS]` Full `cmake --build --preset build-debug` passed; final acceptance-fixture adjustment rebuilt the
  TaaGpuAcceptance target with the same preset. TAA shader ran on GPU in the final captures.
- `[M3 GPU/CAPTURE PASS]` Independent legacy production Setup/Execute/EndPass fixture supplies explicit current/depth/motion
  RTHandles. 85 input/output pairs, 170 DDS R11G11B10_FLOAT captures completed; D3D12 ERROR/CORRUPTION=0; process exit 0.
  Static segment has 64 consecutive frames with 64 unique Camera UV jitters. No native RG TAA integration implemented.
- `[M3 HISTORY PASS, ISOLATED]` First use, six resize/recreate frames, explicit invalidation helper, translation/rotation cuts,
  FOV change and render gap resolve exactly to current RGB. Full feature off/on and the inactive Editor TAA path remain untested.
- `[M3 JITTER NUMERICAL PASS]` Independent shader-math reference using previous GPU output: correct previous-current jitter
  RMS 0.004529 vs reversed-sign RMS 0.246424, on the diagonal edge ROI. No GPU history-UV debug visualization claimed.
- `[M3 STATIC PARTIAL]` Analytic fixed-diagonal input: full-image adjacent RMS 0.020038 current vs 0.013887 resolved.
  Static scene rails/convergence and complete live Editor visual acceptance remain open. Defaults kept at factor 0.95/clamp 2.
- `[M3 FIXTURE VALIDITY]` Final input passes analytic/format-precision checks. run1 has async-upload black frames and a
  LookTo/Recalculate rotation fixture error; run2 has an unready first Blit. Exclude both from acceptance. Final uses ApplySync
  and eight input-only Blit warm-up frames before first TAA resolve. No history quality/weight workaround was applied.
- `[M3 RESTORATION]` Temporary scene only, no project/Editor-session save, all test processes exited. Repository log/imgui
  layout side effects restored to their initially clean tracked contents; original production dirty changes preserved.
- Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M3_Acceptance_20261002/final/report.md`, analysis.json,
  observations.csv, validation.txt, 170 capture manifests/DDS and representative PNGs. Reusable fixture/analyzer in Test/.

### M3 V7 repair / retest - 2026-10-02

- `[M3 V7 PASS]` Shader emits raw current color before temporal reads when factor <= 0; normal center filtering now divides
  by its accumulated Mitchell weight using float accumulation. No history/clamp parameter reduction or M4 integration.
- `[M3 BUILD PASS]` Full `cmake --build --preset build-debug` passed. Fresh independent GPU executable exits 0;
  D3D12 ERROR/CORRUPTION=0. All 170 DDS captures complete; analyzer exit 0, 15/15 isolated regression gates pass.
- `[M3 CURRENT-ONLY PASS]` All 9 factor=0 outputs equal current RGB bit-for-bit (max error 0 vs prior 0.15625). First use,
  invalidate helper, six resizes, translation/orientation cuts, FOV discontinuity and render gap retain exact current-only RGB.
- `[M3 FILTER PASS, FORMAT PRECISION]` Constant 11x11 input neighborhoods away from the diagonal have max RGB difference
  0.015625, one R11G11B10 blue-channel step. Initial cross-neighbor flat mask included edge-filtered pixels and was corrected;
  its false flat-region failure is not a production failure. Normal accumulated output is not claimed bit-identical.
- `[M3 JITTER/STATIC PASS, ISOLATED]` Correct/reversed jitter-reference RMS 0.003724/0.229586. Static adjacent full-image RMS
  current 0.020038 vs output 0.012993 (35.16% lower); 64 consecutive frames, 64 Camera UV jitters.
- `[M3 STILL OPEN]` Real-scene thin rails/static convergence and full TemporalAA feature off/on remain unverified. Keep M3
  open and M4 unstarted. The isolated helper toggle result does not establish complete Editor toggle acceptance.
- `[RESTORATION PASS]` Test exited; no scene/session save. Repository log/imgui restored byte-for-byte to pre-run backups;
  unrelated dirty work preserved. Evidence/report in
  `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M3_RepairRetest_20261002/run1/report.md`.

### M4 implementation - 2026-10-02

- `[M3 PREREQUISITE OPEN]` Continued into the explicitly requested M4 implementation while retaining M3's open real-scene
  static-convergence and complete feature-toggle acceptance gaps. M3 is not marked complete.
- Added native `TAAExecutePass::OnRecordRenderGraph()`. It captures the current CopyColor, copied depth, and current motion
  vector handles; imports persistent A/B history textures; declares SRV reads and an imported UAV write; resolves and binds
  those exact versions; and dispatches the existing TAA compute shader with camera dimensions and temporal parameters.
- Added `_taa_color` as the postprocess input slot so TAA does not mutate `_color_tex`, which CopyColor's execution callback
  reads. PostprocessStack now selects TAA output first. Registered TemporalAA in the Renderer feature list; per-camera AA
  mode still gates pass enqueueing.
- History textures now use the camera color target descriptor/format with random-access enabled. Added a compatibility
  `Setup()` overload for the existing legacy GPU acceptance fixture. RG execution commits the ping-pong side and camera/jitter
  snapshot only after resources resolve and the dispatch is recorded; RG does not rely on legacy `EndPass()`.
- `[BUILD PASS]` `cmake --build --preset build-debug` passed, including Engine, Editor, generated reflection, and the existing
  `TaaGpuAcceptance` target.
- `[RUNTIME OPEN]` A fresh Debug Editor process did not publish a reachable automation pipe and displayed a Microsoft Visual
  C++ Runtime Library window. The process started for this check was closed. The project session file still points to an old
  PID; no scene or editor-session file was changed. RenderGraph capture, binding/version inspection, history flip, and
  postprocess image acceptance remain unverified.

### M4 acceptance - 2026-10-02

- `[M4 CORE RG AUTOMATED PASS]` Accepted the current native RG integration on the tested Editor path plus isolated native
  GPU/RenderDoc fixtures. M3's real-scene thin-rail convergence and complete TemporalAA feature off/on acceptance remain
  explicitly open. No M5-M8 implementation or production/shader repair was performed.
- `[BUILD PASS]` Full `cmake --build --preset build-debug` exit 0. Final fixture additions rebuilt with
  `cmake --build --preset build-debug --target TaaGpuAcceptance`; final native and RenderDoc runs exit 0.
- `[EDITOR CAPTURE PASS]` Fresh Editor PID 49352 on NewScene, 1088x576: 12 sessions, 168 native DDS + 8 ToneMapping PNG.
  Every manifest completes with engine-frame gaps exactly 1 and finite pixels. TAA output static segment is 64 frames;
  current/depth/motion/history SRVs, CopyColor output, Bloom input, ToneMapping, AA off/on and 5-unit cut/return were captured.
- `[VERSION/POSTPROCESS PASS]` Actual TAA current-input texture matches CopyColor output, handle (477,1), rather than either
  TAA output. BloomDownsample_0 reads alternating `_TemporalAA_TargetA/B`. `_taa_color` does not replace `_color_tex` or
  `_color_target`; existing source pass ordering is retained. Static geometric motion UV is exactly zero.
- `[NATIVE RG RESET PASS]` 85 same-frame production `OnRecordRenderGraph()` input/output pairs, 170 DDS; four SRV declarations
  and one UAV declaration checked. First use, explicit invalidation helper, translation/orientation/FOV cuts, render gap,
  six resize frames and nine factor=0 samples are bit-identical to current RGB. RG never calls legacy Execute/EndPass.
  Resize covered in isolated fixture, not the Editor viewport. Isolated toggle is helper-only; actual Editor camera AA off/on
  smoke does not close complete feature activation/first-frame off/on acceptance.
- `[NATIVE TEMPORAL PASS]` 64 consecutive static frames, 64 Camera UV jitters. Correct/reversed jitter-reference RMS
  0.003724/0.229586; current/output adjacent RMS 0.020038/0.012993 (35.16% reduction). This analytic diagonal result does not
  close the prior real-scene thin-rail convergence gap. Editor separate-segment current/output RMS 0.008538/0.004243 is
  descriptive, not a matched-frame convergence gate.
- `[RENDERDOC BINDING/HAZARD PASS]` Replayed two captures at frame140/frame141, TAA event358, dispatch 40x23x1. Four SRVs
  bind `rt_M3Color`, prior A/B, `rt_M3Depth`, `rt_M3Motion`; UAV binds the opposite A/B. Resource IDs 3921/3922 swap
  history/output roles exactly. Read/write IDs are disjoint; debug messages empty. Six binding gates PASS.
- `[D3D12 PASS]` Native and RenderDoc fixture ERROR/CORRUPTION=0; Editor log contains zero severity 0/1 debug-layer messages.
  Baseline severity 2 warnings are present and are not represented as a clean-warning or general Editor acceptance claim.
- `[FIXTURE EXCLUSIONS]` Initial RG runs imported current color as reserved handle 0; TAA was not registered and those runs
  are excluded. Final fixture reserves the first handle to match existing Renderer preallocation. General RG handle-zero
  allocation is a separate follow-up. RenderDoc capture preloads the installed DLL only when explicitly selected by env var.
- `[RESTORATION PASS]` Camera, feature states and scene revisions 40/39 restored; no scene save. All test processes exited.
  Six original config/log/session backups restored byte-for-byte; graph dumps moved to evidence directory. Other dirty work
  retained. Evidence/report: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M4_Acceptance_20261002/`.

### M3 real-geometry acceptance - 2026-10-03

- **Overall NOT ACCEPTED: V2 thin-rail convergence fails.** Added a real mesh scene mode and matched-frame analyzer to
  the existing GPU fixture. Used Renderer-owned TemporalAA, production deferred passes, native RG and ImageCaptureService.
  No production C++/shader/default-factor changes. M5-M8 remain unstarted.
- `[BUILD/RUNTIME PASS]` Full `cmake --build --preset build-debug` passed. Final scene2 exits 0, D3D12 ERROR/CORRUPTION=0.
  Single-thread rendering and graphics jobs disabled; 320x180 except resize. This is in-engine/captured-image acceptance,
  not full interactive Editor review or new multi-camera GPU acceptance.
- `[TRANSPORT PASS]` 262 sequential observed engine frames, 770 completed DDS captures, matched frame/dimensions and finite
  pixels. Real emissive mesh scene: nominal 0.4/0.8/1.4/2.4/4.0-pixel slanted rails plus a broad diagonal. 128 static frames
  have 128 Camera jitters, XY motion=0, and factor=0.95. No scene save.
- `[LIFECYCLE PASS]` 21 same-frame RGB pairs are bit-identical: first, actual feature and AA reactivation, eight factor=0
  frames, translation, six 257x137/320x180 resizes, FOV, 90-degree rotation and orthographic switch. Feature-off and AA-off
  each last eight frames; all 16 omit TAA output. Emissive geometry changes during each off interval. Actual feature render
  gap resets history without calling the TAA helper. Full feature-toggle gap is closed.
- `[V2 FAIL]` Local output adjacent RMS falls 94.64% for the 0.4-pixel rail and 48.18% for the diagonal, but the thin rail
  retains only 10.14% of positive above-background brightness versus the 128-frame input mean and becomes nearly invisible.
  Other rail retention: 70.88%, 81.97%, 94.58%, 95.00%; diagonal 98.01%. Darkening/missing geometry cannot be accepted merely
  for low variance. Sampled input mean is not analytic ground truth. Filter/clipping causality remains undiagnosed.
- `[EXCLUSIONS]` Initial scene run retained but excluded from final exit/rotation acceptance. LookTo was overwritten by
  RecalculateMatrix; bright_material survived ResourceMgr shutdown. Fixture now uses quaternion rotation and releases its
  material ref before shutdown. Final run exits normally; no production Camera/Material lifetime repair was made.
- `[RESTORATION PASS]` Six config/log/session files restored with matching SHA256; all test processes exited. Existing
  dirty work retained. No scene/asset save. Evidence/report: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/
  TAA_M3_FullAcceptance_20261002/`; final measurements select `scene2/`. Inspect `thin_rail_detail.png` before a V2 pass claim.

### M3 V2 cause diagnosis - 2026-10-03

- `[CAUSE]` Current-frame 3x3 neighborhood clipping erases intermittent subpixel coverage. Sample1 pixel(65,146) has
  current/low/high=0.03857421875, correctly reprojected prior GPU history approximately 0.728452, clipped history=background,
  and actual GPU output exactly background. 28.15% of frame/pixel neighborhoods over the sampled thin-rail footprint contain
  only background. Later covered frames contribute only 5% new color; repeated history erasure prevents geometric convergence.
- `[OFFLINE A/B]` Factor remains 0.95. Scalar baseline positive retention 10.95%; actual GPU R-channel 10.42% (prior
  full-RGB metric 10.14%). Remove clipping alone: signed/net retention 99.78%. Remove center filtering, use float history,
  or use bilinear history: each still about 11%. Correct/opposite jitter one-step RMS 0.001245/0.069857 over the rails.
- `[LIMITS]` No new GPU shader A/B or build; replays use captured current R-channel/jitter and approximate R11 quantization.
  Full trajectory reference/GPU RMS 0.007909, so the emulator is not bit-exact. Actual captured background output agrees
  with the collapsed-bound case. This is diagnosis, not repair acceptance; M3 stays failed. No production changes.
- `[REPAIR DIRECTION]` Preserve credible subpixel coverage during history clipping while retaining genuine occlusion/color
  rejection. Global clip removal is a diagnostic counterfactual, not a proposed shipping fix. Do not lower history weight.
- Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M3_FullAcceptance_20261002/v2_diagnosis/`;
  reproducible analyzer `Test/DiagnoseTaaScene.py`. No runtime/config/scene changes in this diagnosis turn.

### M3 V2 repair and validation - 2026-10-03

- `[REPAIR]` Added a bounded current-color coverage guard for stationary (<0.5pixel motion), uniform 3x3 neighborhoods
  with rejected history: expand only when1-4 of16 outer 5x5 taps provide different colors. Filled edges/moving geometry
  retain original footprint. Variance bounds now intersect current min/max; old union admitted stale bright colors after
  material changes. History factor remains0.95; no extra history resource or globally disabled color rejection.
- `[BUILD PASS]` Full `cmake --build --preset build-debug` passed. Actual GPU runtime compiles/loads production taa.hlsl;
  final runs have no DXC errors. Final real scene and independent native RG fixture both exit0, ERROR/CORRUPTION=0.
- `[GPU V2 PASS]` Selected sparse_guard:334 consecutive frames,986 completed DDS,22 current-only pairs exact. Static128
  samples have128 jitters and motion0. 0.4-pixel signal retention10.14% before ->86.43% after; adjacent RMS0.115787 ->0.012104
  (89.55% reduction). Late-reference error0.034228 versus current0.070458. Other rail retention85.57/82.32/94.89/95.21%;
  diagonal97.97%, adjacent RMS0.038171 versus current0.074038. All local stability gates and captured detail review pass.
- `[REJECTION PASS]` Eight color-drop frames have zero stale bright excess; eight full-occlusion frames max error0.0009765625;
  eight real rigid-motion frames have zero positive trail outside5x5 current-color footprint. Motion fixture verified
  nonzero; disocclusion accumulates again. Tolerance.003 HDR RGB. No general filter-boundary ghost-free claim.
- `[JITTER/RESET PASS]` Final jitter_rg regression85 pairs/170DDS, bit-identical reset/factor0, flat-color pass and independent
  correct/opposite-jitter reference RMS0.003710/0.211595. Independent numerical oracle updated for intended clipping policy.
- `[EXCLUSIONS]` Initial scene shader compile failure excluded. Intermediate broad-guard runs failed normal-edge/reference
  and color-change checks. Accepted final sparse guard narrows coverage to sparse uniform-neighborhood cases and corrects
  variance intersection. Previous diagnosis/failed acceptance entries are historical and superseded by this retest.
- `[RESTORED]` Six config/log/session hashes match backups; graph dumps archived; test processes exited; no test scene save.
  Existing dirty work preserved. Evidence `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M3_Repair_20261003/`;
  select sparse_guard and jitter_rg. GPU cost/full interactive/multi-camera acceptance remain unmeasured. M5-M8 unstarted.

### M5 transparent and Sprite policy - 2026-10-03

- `[POLICY B SELECTED]` Keep Sprite and blended forward transparency before CopyColor/TAA. They contribute color without
  per-pixel object velocity or a reactive mask, so moving blended content has limited temporal support and may reuse
  unrelated opaque motion/history. No global history-factor change or pass reorder was made.
- `[CLASSIFICATION]` Opaque and alpha-test geometry remain in the core TAA path. The deferred shader and MotionVector pass
  both apply the alpha-test clip; alpha-test is not treated as blended transparency. Sprite and blended forward materials
  are explicitly classified as limited-support inputs. Billboard retains its existing stable-projection adjustment while
  its blended color remains pre-TAA and has no object velocity.
- `[SOURCE UPDATE]` Added policy comments at Sprite graph recording and billboard projection to make these temporal
  semantics visible at the implementation points. Existing 3x3 variance/neighborhood clipping and sparse static-coverage
  guard are unchanged.
- `[VALIDATION OPEN]` No Editor process or render-capture client is available in this checkout/session. Static and moving
  transparency, moving Sprite, billboard-under-jitter, and alpha-test image sequences have not been captured; severe-smear
  behavior is therefore unmeasured. M5 remains PARTIAL under the plan's Policy B gate.
- `[BUILD]` Not run: the changes are comments and ExecPlan status only; no executable behavior or shader expression changed.

### M5 acceptance - 2026-10-03

- `[BUILD PASS]` Full `cmake --build --preset build-debug` passed after adding `--transparent` to the existing GPU
  acceptance executable. Only test orchestration/capture/analysis and task documents changed; production TAA is unchanged.
- `[CAPTURE PASS]` Final unsaved production RenderGraph scene: 320x180 perspective, Camera jitter enabled, factor0.95,
  single-thread rendering, optional lighting/fog/GI features disabled. Four112-frame material segments:16 background,
  32 static,32 lateral-motion at3 pixels/frame,32 disappearance.448 consecutive engine frames89-536,1344 completed native
  DDS captures (same-frame current/resolved/motion), no duplicates/gaps or invalid values; exit0, D3D12 errors/corruption0.
  Wall runtime19.50s includes warm-up/drain; this is capture-load evidence, not interactive performance acceptance.
- `[CLASSIFICATION PASS]` Forward blend and production Sprite contribute color with captured motionXY exactly0. Alpha-test
  remains deferred/core TAA: production checker-alpha coverage, captured2.998 pixels/frame versus intended3;100% of
  red-covered pixels in sample399 have nonzero motion. Static ROI adjacent RMS decreases with TAA for all four cases.
- `[BILLBOARD SCOPED PASS]` Production billboard shader, correct per-object `DrawMesh` binding, pre-TAA test pass480.
  All32 static perspective-jitter frames have identical visible bounds[87,65,136,114]. Source audit found normal
  `GizmoPass` writes `FinalColorHandle()` after postprocess; its Editor icon billboards are not the same pre-TAA color path
  as Forward blended meshes. Do not generalize the earlier policy comment to all billboard uses. Orthographic/interactive
  billboard acceptance remains open. Grayscale billboard smear is not covered by the red-channel numerical gate.
- `[SPRITE FOUR VIEWS]` Matched current, MotionVector and TAA final; historyUV saved as CPU-derived
  `uv-motion+previous_jitter-current_jitter`, not a GPU debug output. For sample175 all velocityXY values are exactly0,
  so production closest-depth velocity dilation cannot change that UV. `sprite_four_views.png` and raw `.npy` retained.
- `[SMEAR FAIL]` Colored moving targets on grayscale thin rails retain history outside a5x5 current coverage footprint.
  Maximum positive red excess (R-G) blend0.3027/Sprite0.5000/cutout0.5234 HDR. All32 motion frames exceed0.03 in every
  colored case. Red centroid lags up to5.20/6.19/4.53 pixels. After disappearance, excess>0.03 persists24/31/at least32
  frames respectively. These are explicit fixture-local diagnostic thresholds; image review confirms colored rail trails.
  Full M5 acceptance fails; Policy B remains PARTIAL and has not received user shipping-limit acceptance.
- `[DISCOVERY]` Alpha-test velocity is valid yet colored history remains on the newly revealed grayscale background.
  Example sample424 pixel[227,81]: currentRGB[.03857,.03857,.03809], resolved[.4375,.23047,.19531]. Current3x3 envelope
  spans dark-to-bright neutral colors, admitting that stale RGB inside the per-channel AABB. RGB neighborhood clipping
  therefore does not guarantee chroma rejection on detailed backgrounds; this is broader than missing transparent velocity.
  No production repair or history-factor tuning was performed in this acceptance turn. Earlier M3 grayscale controls did
  not test this chromatic-disocclusion case; their scoped results remain historical evidence, not general robustness proof.
- `[EXCLUDED RUNS]` First run stopped before valid captures at ImGui viewport initialization assertion because the test
  changed rendering mode after initialization. Retest uses a separate single-thread initialization config. Initial billboard
  scene-mesh binding produced no billboard and is excluded; final run uses the production per-object binding convention.
- `[RESTORED PASS]` Eight configuration/log/session/graph entries restored with verified hashes/existence; no scene save,
  test processes exited, unrelated dirty work preserved. Evidence selects
  `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M5_Acceptance_20261003_final/`, not initial/retest artifacts.
  Driver `Test/RunTaaTransparentAcceptance.ps1`; analyzer `Test/AnalyzeTaaTransparentAcceptance.py`.

### M5 failure diagnosis - 2026-10-03

- `[DIAGNOSIS ONLY]` No production shader/pass changes, runtime/config changes or new GPU run. Added an offline analyzer
  for the existing final captures: `Test/DiagnoseTaaTransparentAcceptance.py`. Source and numerical evidence are distinct
  from repair acceptance. M5 remains PARTIAL with its failed image gate.
- `[CAUSE 1: COVERAGE/VELOCITY MISMATCH]` Sprite/Forward blended color is composed after MotionVector and before TAA.
  Neither writes object velocity or a reactive input; all captured motionXY is zero in these moving cases. HistoryUV
  therefore follows the static opaque background, not the moving blended layer. This is the explicit Policy B limitation.
- `[CAUSE 2: RGB CHROMA REJECTION]` Actual shader has YCoCg disabled. Per-channel RGB min/max plus variance intersection
  on mixed dark/bright neutral background still admits intermediate colored history. For cutout sample424 pixel[227,81],
  currentRGB[.03857,.03857,.03809], projected history[.45845,.23883,.20110], variance bounds approximately
  [.03857,.03857,.03809] to [.74505,.74505,.73766]. Every history component is inside the box, so ClipColor returns it
  unchanged although all current3x3 neighbors have R-G=0. GPU output[.4375,.23047,.19531] retains the old red layer.
  Corresponding blend/Sprite examples likewise keep history unchanged. This explains residuals beyond transparent velocity.
- `[CAUSE 3: NO DISOCCLUSION VALIDITY]` Alpha-test object motion is valid on current covered pixels. After an object leaves
  or disappears, the visible static background correctly has zero motion, but its previous color contains the object.
  Current depth only selects a velocity neighbor; TAA has no previous-depth comparison or per-pixel disocclusion/reactive
  rejection. Camera-level history remains valid. Failed chroma clipping then blends stale color at factor0.95 repeatedly.
- `[OFFLINE CAUSAL CONTROLS]` Replay uses actual preceding GPU output and recorded previous/current jitter, with all96
  disappearance frames' motionXY exactly0, so unknown depth-dilation choice cannot affect the sampled velocity. Production
  RGB one-step reference RMS is .00415 HDR versus .0615-.0623 with reversed jitter. Selected pixels agree within .00782;
  float CPU/packed-R11G11B10 replay is approximate, not a bit-exact GPU oracle. Recursive predictions are qualitative:
  removing coverage guard still leaves Sprite/cutout residual>0.03 through all32 frames; removing center filtering likewise.
  Bilinear history shortens but retains trails (15/18/19 frames). Correct single-conversion YCoCg clips color excess below
  0.03 from the first disappearance frame in all three offline cases. No claim of repaired moving-layer correspondence,
  thin-rail stability, or GPU acceptance follows from that counterfactual.
- `[GUARD IS NOT PRIMARY CAUSE]` All three selected residual pixels have nonuniform3x3 neighborhoods, so the sparse coverage
  guard cannot change their bounds. Its removal does not close the failure gate. Existing M3 sparse-coverage fix is not
  sufficient to explain or repair these chromatic residuals.
- `[YCOCG IMPLEMENTATION TRAP]` Do not simply uncomment TAA_YCOCG: GetCurColorSamples already transforms samples to working
  space; MinMax transforms them again, and CSMain transforms its filtered working-space center again. These are identity
  operations only with the current RGB mode. A color-space repair must first establish exactly one conversion at each
  input/output boundary and preserve current-only semantics and M3 thin-coverage regression.
- `[BILLBOARD OPEN ITEM]` The perspective stable raster gate passed, rather than failed. Orthographic remains unverified;
  source audit reveals a separate incompatibility: Camera orthographic jitter is CPU P[3][0/1] (shader P[0/1][3]), whereas
  billboard unconditionally subtracts shader P[0/1][2], the perspective positions. For orthographic view depth z, residual
  clip jitter is (1-z)*jitter; at z4 it is -3*jitter instead of0. This is a source/matrix diagnosis, not a captured GPU result.
- `[REPAIR DIRECTION]` First correct chroma rejection with a consistent working-space contract while preserving factor0.95
  and M3 subpixel coverage. Then implement explicit blended responsiveness/reactive coverage if full M5 support is wanted;
  its validation must cover both currently covered and newly uncovered background pixels. Retain alpha-test in core TAA.
  Repair billboard projection removal according to projection type and separately test perspective/orthographic paths.
- Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M5_Diagnosis_20261003/` with `diagnosis.json` and
  `counterfactuals.png`; no Editor/test process launched and no new build necessary for this diagnosis-only turn.

### M5 repair and final retest - 2026-10-03

- `[POLICY A]` Replace the failed Policy B limitation with explicit responsive coverage. Keep pass order and global
  factor0.95. New event455 `TAAOpaqueSnapshot` records/copies pre-Sprite/pre-forward color with immutable captured handles.
  Resolve compares composed color to snapshot (>0.001 HDR in any component), writes an R8_UNORM mask on every shader
  path, and rejects history for current coverage or expanded/reprojected previous coverage. Both mask textures belong
  to the existing per-camera history state, resize/reset with it, and rotate with color only after dispatch. RG declares
  snapshot/previous-mask reads and color/current-mask UAV writes. Legacy non-RG binds resources but disables responsiveness.
- `[CHROMA REPAIR]` Keep accepted RGB clipping and sparse coverage filtering; independently clamp history Co/Cg to the
  current3x3 chroma envelope. This rejects false historic color over gray contrast without changing the luminance filter.
  Also remove latent double working-space conversions in neighborhood/center filtering; raw-current early returns remain
  bit-identical. Direct whole-YCoCg clipping was tested and rejected: diagonal late-reference RMS0.063844 exceeded
  input0.045229, and independent jitter reference RMS0.009617 failed. Those intermediate runs are not acceptance evidence.
- `[BILLBOARD REPAIR]` Preserve intentional stable raster projection. Perspective jitter removal retains shader matrix
  [0][2]/[1][2]; orthographic now correctly removes [0][3]/[1][3]. Actual postprocess GizmoPass icons remain a separate path.
- `[BUILD PASS]` Full `cmake --build --preset build-debug` passed after final C++ edits. Production shaders compiled/loaded
  by the GPU tests; final processes exit0 and each reports D3D12 ERROR/CORRUPTION0. `git diff --check` passed.
- `[M5 CAPTURE PASS]` Unsaved production-RG scene,320x180,factor0.95:784 consecutive observed frames,3136 completed native
  DDS (current/resolved/motion/reactive per sample), no manifest/frame/dimension/nonfinite errors. Seven112-frame cases:
  colored blend, production Sprite, perspective billboard, alpha-test, gray blend, gray Sprite, orthographic billboard.
  Each contains16 background,32 static,32 lateral-motion and32 disappearance frames. Active motion is3pixels/frame.
- `[M5 IMAGE/NUMERICAL PASS]` Colored blend/Sprite/cutout red excess outside5x5 current footprint max0.00390625/
  0.0078125/0.0078125, below unchanged0.03 HDR threshold. No disappearance frame exceeds0.03 (previous failures24/31/
  at least32 frames). All six blended cases have nonzero active mask and exact current RGB on every covered pixel;
  first departure protected pixels also return exact current RGB. Cutout mask remains0, deferred motion2.998pixels,
  100% covered moving pixels. Background/disappearance current masks are0. Perspective/orthographic billboard bounds
  stay identical over all32 static samples, respectively[87,65,136,114] and[88,65,137,114]. Aggregate repair gate passes.
- `[SAMPLER CHECK]` Previous-mask CPU prediction includes the observed GPU's1/256-texel bilinear coordinate precision.
  An ideal float sampler falsely included14 gray-Sprite mismatch pixels with weights at most0.001828; these are outside
  actual sampled coverage. Final analysis records quantized protected coverage and ideal-sampler fringe counts; it also
  independently requires exact RGB on robust ideal support>1/256. Both checks pass. This sampler model is scoped to the
  captured device; it is not a claim of identical fractional precision on every GPU.
- `[M3 REGRESSION PASS]` Final production-RG scene controls:334 consecutive observations,986 DDS,22 current-only pairs
  exact; all static geometry/variance gates pass.0.4pixel rail retention86.94%, adjacent RMS0.115787 ->0.012142
  (89.51% reduction), late-reference RMS0.034148 versus current0.070458. Diagonal retention97.98%, late-reference
  RMS0.042134 versus current0.045229. Color-change, real rigid-motion, occlusion and disocclusion-growth controls pass.
- `[CORE RG REGRESSION PASS]` Independent85pairs/170DDS: reset/factor0 exact,64 static jitters/continuous frames,
  flat-region pass; correct/opposite jitter reference RMS0.003834/0.212697. Original oracle/thresholds retained.
- `[IMAGE REVIEW]` Reviewed final disappearance montage: no persistent colored blend/Sprite trail. This is captured
  image review, not an interactive Editor acceptance session. Alpha-test chroma rejection is measured; arbitrary opaque
  luminance disocclusion and previous-depth validation are not solved or claimed by this change.
- `[RESTORATION PASS]` Each final run restores8/8 config/log/session/graph entries with matching hash/existence.
  No scene asset saved; unrelated dirty changes retained; all test processes exited. Tests and reports are reproducible
  through `Test/RunTaaTransparentAcceptance.ps1` and the corresponding analyzers.
- `[FINAL EVIDENCE]` Select only `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M5_RepairFinal_20261003/`,
  `TAA_M5_M3Final_20261003/` and `TAA_M5_CoreFinal_20261003/`. ColorRepair, RepairRetest, M3Regression and CoreRegression
  directories contain intermediate experiments. M5 automated supported-policy acceptance passes; interactive review,
  broader/multi-camera material coverage and GPU-cost profiling remain separate. M6-M8 are unstarted.

## Known Follow-ups

See README. Generic RenderGraph resource allocation starts at 0 although `RGHandle::IsValid()` reserves 0 as invalid;
the standalone fixture exposed this independently of the normal TAA input allocation sequence. No repair in M4 acceptance.

## Outcomes

M0 and M1 passed. M2 camera-history repair and prior single/multi-thread perspective/orthographic, jitter and occlusion
regressions remain recorded. The 2026-10-02 repair/retest closed alpha coverage, CPU_DEFORM and legacy depth gates:
16 in-engine cases, 48 DDS, 38/38 numerical gates, D3D12 ERROR/CORRUPTION count 0, and normal process exit 0.
M2 automated acceptance passed; full interactive Editor visual review remains separate. Earlier failed/invalid fixture runs
are retained as historical evidence. M3 core/history source changes and Debug build are recorded above, but M3 acceptance
now passes automated acceptance after sparse coverage/variance-clipping repair. Full feature/AA toggle passes in the real
mesh fixture, V2 thin-rail retention is86.43% with factor0.95, and color/motion/occlusion controls pass. V7 is repaired and
isolated GPU regression passes
15/15 gates. M4 native RG core automated acceptance now passed using the real Editor resource/postprocess capture path,
isolated same-frame RG regression and replayed two-frame RenderDoc binding/hazard checks. M3's full feature off/on gap is
closed along with the real-scene thin-rail convergence gap; interactive visual acceptance is not claimed. M5 now uses
Policy A with explicit current/previous responsive coverage and independent chroma rejection. Final seven-case production-RG
image/numerical retest and M3/core-RG regressions passed. Earlier Policy B failures remain historical evidence.
Broader opaque luminance disocclusion, interactive review, multi-camera coverage and GPU cost remain unmeasured.
M6 automated single-camera production-RG acceptance now passes after sky-LUT dependency, execution guard and TAA
snapshot ordering repairs. The initial failed capture remains historical evidence; select Final/M5Regression evidence.
M7 repair/retest now passes automated single-camera production-RG acceptance:640 frames/4992 DDS,103 gates,
exact reset seeds, jitter-aware histories, depth/normal reconstruction and combination alignment. M5 regression passes.
The original failure remains historical evidence. Select FinalRetest/M5Regression evidence. M8 source work and the
2026-10-04 expanded acceptance is now recorded above; the final M8 gate FAILS feature-toggle/coexistence and retired
camera-history checks. See m8-acceptance-20261004.md. The complete plan is not finished.

## Resume Protocol

Load `README.md`, this file, and only the requested milestone file when resuming. M3 automated acceptance and M4 core/native
RG acceptance passed. M5 Policy A automated supported-policy acceptance passed after repair; read the final retest record.
Select M5 RepairFinal/M3Final/CoreFinal evidence, not intermediate whole-YCoCg experiments. Original M3 evidence selects
sparse_guard and jitter_rg. Do not infer full interactive/multi-camera/performance or universal transparent-velocity
acceptance. M6 automated acceptance passed; select Final/M5Regression evidence, retaining broader runtime limitations.
M7 automated acceptance passed after repair; select FinalRetest/M5Regression evidence and retain its scope limitations.
Continue only the explicitly
requested scope. M8 expanded acceptance was exercised on2026-10-04; retain its FAIL status and resume from
m8-acceptance-20261004.md. Return history retirement to M3, resolve the supported feature/backend boundary, and rerun
the failed gates. Do not equate numerical/transport smoke with perceptual/gizmo/disk-reload acceptance.
