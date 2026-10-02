# AiluEngine Volumetric Fog — Progress

> Mutable state file.
>
> This is the file that should be updated most often.
>
> Before implementation:
>
> 1. Read `00_Architecture_Overview.md`.
> 2. Read `01_Milestone_Index.md`, then open only the current file under `Milestones/`.
> 3. Read this file.
>
> After implementation:
>
> 1. Update checkboxes.
> 2. Record validation.
> 3. Record discoveries/decisions only if they affect future work.
> 4. Set the next task.

# Current Milestone

```text
Milestone 6 acceptance passed for captured NewScene single-SceneView path
```

Current result:

```text
M4 environment in-scattering implementation and captured NewScene acceptance passed.
M5 implementation, Debug build and targeted glass/quad/alpha-card/background acceptance passed after fixing
SpriteRenderPass attachment preservation. Interactive/temporal-on and other transparent shader paths remain unverified.
M6 now derives XY froxel dimensions from viewport/tile size, uses texture depth as the shader's Z slice count, and
recreates accumulation/light/history textures when the layout changes. Debug build, aspect-ratio and post-resize
captures, continuous quality switching and GPU profiling passed for the captured NewScene single-SceneView path.
M1-02 motion-pop validation and M2 multi-camera acceptance remain open.
```

M1-02 near-clipping motion validation, M1 acceptance, and M2 acceptance remain open. M3 implementation proceeded at
the user's explicit request before those acceptance gates were closed; this does not mark M1 or M2 accepted.

# Progress

## Milestone 0 — Baseline

- [x] 保存 baseline screenshots
- [x] 建立 Wide FOV 测试镜头
- [x] 建立 Directional Shadow 测试镜头
- [x] 建立 No ShadowMap 测试
- [x] 建立 No Directional Light 测试
- [x] 建立 Multi-Camera 测试场景与相机实体
- [x] 记录 baseline GPU timing
- [x] Milestone 0 acceptance passed

## Milestone 1 — Correctness

- [x] M1-01 修复 ray thickness
- [x] M1-01 validation passed
- [x] M1-02 修复 accumulation Z mapping
- [ ] M1-02 validation passed
- [x] M1-03 限制 jitter 到 single slice
- [x] M1-03 validation passed
- [x] M1-04 确认并修复 HG phase direction
- [x] M1-04 validation passed
- [x] M1-05 ShadowMap 改为 optional
- [x] M1-05 validation passed
- [x] M1-06 No Main Light safety
- [x] M1-06 validation passed
- [x] 保存 Milestone 1 A/B screenshots
- [x] 记录 Milestone 1 GPU timing
- [ ] Milestone 1 acceptance passed

## Milestone 2 — Temporal

- [x] M2-01 新增 per-camera history state
- [x] M2-02 per-camera history keyed by Camera::HashCode
- [x] M2-03 history invalidation
- [x] M2-04 previous clip validity
- [x] M2-05 luminance rejection
- [ ] M2-06 optional neighborhood clamp
- [ ] M2-07 temporal parameter tuning
- [x] static-camera validation
- [x] moving-camera validation
- [x] camera-cut validation
- [ ] multi-camera validation
- [x] 记录 Milestone 2 GPU timing
- [ ] Milestone 2 acceptance passed

## Milestone 3 — Medium / Height Fog

- [x] M3-01 `_VolumetricLight.a` 改为 extinction
- [x] M3-02 `EvaluateFogMedium`
- [x] M3-03 Height Fog
- [x] M3-04 Uniform fallback
- [ ] M3-05 optional low-frequency noise
- [x] density/extinction debug view
- [x] height movement validation
- [x] temporal stability validation
- [x] 记录 Milestone 3 GPU timing
- [x] Milestone 3 acceptance passed

## Milestone 4 — Environment Scattering

- [x] M4-01 environment source (diffuse SH L0)
- [x] M4-02 independent `_ambient_intensity`
- [x] M4-03 add environment source before extinction/integration
- [x] shadow-region validation
- [x] ambient=0 regression
- [x] opaque depth relation retained
- [x] 记录 Milestone 4 GPU timing
- [x] Milestone 4 acceptance passed for the captured NewScene path

## Milestone 5 — Transparent Integration

- [x] M5-01 global fog resource
- [x] M5-02 common fog sampling include
- [x] M5-03 transparent apply
- [x] M5-04 opaque composite remains before transparency; no later fog composite found in graph ordering
- [x] glass validation (captured straight-alpha planar glass/quad path)
- [x] particle validation (captured straight-alpha sprite cards and depth occlusion)
- [x] Milestone 5 acceptance passed for captured straight-alpha planar glass/quad/sprite paths

M5 implementation is present for forward-lit materials, water, billboards, and sprites. Shader variants for
`forwardlit`, `water`, `billboard`, and `default_sprite` compiled successfully during Editor startup. RenderGraph
capture transport completed for both `TransparentPass` and `VolumetricFog_Composite` at 1280x704 (four contiguous
frames per pass). The active `NewScene` has no glass, particle, or sprite test renderables, so these captures verify
the pass path only; the M5 visual acceptance cases remain open.

Build:

```text
cmake --build --preset build-debug passed; Engine, Editor, and Test targets linked. One rebuild while the Editor was
running failed with LNK1201 on Engine_d.pdb; after closing that Editor process, the final build passed (exit code 0).
```

Validation:

```text
`git diff --check` passed. Editor startup compiled the touched transparent shader variants without shader errors.
`TransparentPass/session_1` captured engine frames 34702-34705; `VolumetricFogComposite/session_2` captured frames
40451-40454, under `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/M5Acceptance_20261001/`. Both manifests show
four contiguous frames. The captured NewScene view contains opaque environment
geometry only; glass plane, alpha particle, transparent quad, opaque-behind-transparent, and visual double-fog
checks remain open.
```

## Milestone 6 — Dynamic Froxel Resolution

- [x] M6-01 viewport/quality based XY resolution
- [x] M6-02 configurable Z slices
- [x] M6-03 resource recreation/history invalidation
- [x] 16:9 validation
- [x] 16:10 validation
- [x] ultrawide validation
- [x] resize validation
- [x] quality-switch validation
- [x] profiling
- [x] Milestone 6 acceptance passed

## Optional Follow-up

- [ ] Milestone 7 Local Fog Volume
- [ ] Milestone 8 Point Light
- [ ] Milestone 8 Spot Light
- [ ] clustered/tiled light/fog culling

# Validation Records

## Milestone 6 — Runtime Acceptance (2026-10-02)

Result: passed for captured NewScene single-SceneView path. No M7/M8 work performed.

- Fresh Debug Editor on AiluTestProj/project://scene/NewScene.almap, stationary FOV-60 camera, RTX 5070.
- build-debug and git diff --check passed. Added `render.volumetric_fog_settings` to existing Editor automation for
  runtime tile/Z/temporal switching and actual light/accum dimension observation; no scene save or source-default change.
- 1280x720, 1280x800, 1680x720, 1277x719 all match ceil(viewport/tile), Z=96. Five post-resize 24-frame segments
  cover those dimensions plus 640x360 and return to 1280x720; both 3D resources match requested layout.
- Continuous quality capture: 320 frames, engine_frame 20698-21017, no gaps, all six changes observed in the same
  session. Low16/64, Medium12/96, High8/96 and High8/128 all rendered without crash or black frame.
- 616 accepted PNG frames total, with manifests/files/continuity checked. Worst quality pair RMS 0.34258/255, P99 1/255.
- Layout-reset temporal-on versus temporal-off mean images: RMS 0.03874/255, P99 0.15625/255. First captured reset
  frame RMS 0.13139/255; reviewed frames show no persistent contamination, consistent with source history clearing.
- Profiling (12 settled rolling-average samples each, no active readback): Low inject/integrate/composite
  0.01626/0.02088/0.01222 ms; Medium 0.03595/0.03559/0.01214; High96 0.07116/0.03927/0.01227;
  High128 0.09306/0.05150/0.01224. Injection+integration scales with count plus overhead (four-point fit R2=0.9898).
  Retain 8/96 default; 8/128 costs ~31% more compute with no obvious benefit in this captured view.
- Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/M6Acceptance_20261002_01/REPORT.md`, `analysis.json`,
  `observations.json`, `quality_events.json`, `resize_segment_events.json`, `comparison.png`, `worst_pairs.png`.
- Capture load: continuous quality PNG session was about 16 fps at 1280x720; this is not a normal-frame-rate benchmark.
- Transport limitation: changing output dimensions within one capture fails with `source descriptor changed during
  capture`; replaced with contiguous post-resize segments. First GPU transition frame and uninterrupted mouse resize
  are not proven. 3D DDS export failed (0x80004005), so full-volume finite/alpha/monotonicity checks remain open.
- Auxiliary fog-disabled controls discarded: Renderer::Prepare re-enables fog in deferred mode. Not an M6 fix.
- Startup debug severity-2 warnings and asset enum errors were logged before tests; no barrier/state/device-removal
  diagnostic appeared during switching. No new shader compile error found; this is not a warning-free-log claim.
- Runtime parameters restored; camera and scene revisions unchanged; Editor started by this run closed. EditorConfig,
  dock_layout, main_widget and .ailu/editor_session.json match exact pre-run hashes. Unrelated dirty work preserved.
- Captured image inspection passed; user live visual approval, interactive resize and multi-camera stress not claimed.
  M1-02 and M2 gaps remain open independently. Await user review before proceeding.

## Milestone 6 — Implementation

Implementation:

```text
VolumetricFog exposes _xy_tile_size (default 8 pixels) and _z_slice_count (default 96). Each renderer viewport maps to
ceil(width / tile) x ceil(height / tile) froxels. The compute shader obtains the slice count from the bound 3D texture
and uses it for logarithmic slice distances and integration, removing the fixed shader-side 96 source. A layout change
recreates accumulation, cubemap scratch, and per-camera history textures and clears temporal history.
```

Build:

```text
cmake --build --preset build-debug passed; AiluHeadTool regenerated VolumetricFog reflection code and Engine,
Editor, and Test targets linked.
```

Validation:

```text
Source inspection and git diff --check passed. Runtime 16:9, 16:10, ultrawide, resize, quality-switch, and GPU profiling
acceptance remain pending; the Milestone 6 acceptance gate is not yet passed.
```

## Baseline

Build:

```text
No source changes in Milestone 0; build not run.
```

GPU:

```text
LightInject:      0.0519 ms
LightIntegration: 0.0216 ms
Composite:        0.0118 ms
Source: Editor GPU panel rolling average, 1280x704, VolumetricFogBaseline scene.
```

Captures / Screenshots:

```text
F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Saved/VolumetricFog/
  M00_BaselineScene_Static/session_1 (4 frames, engine_frame 2074-2077)
  M00_WideFov/session_2
  M00_NearGeometry/session_3
  M00_OppositeView/session_4
  M00_NoShadowMap/session_5
  M00_NoDirectionalLight/session_6
```

Scene asset:

```text
C:/Users/BoomBac/OneDrive/AiluTestProj/Assets/VolumetricFogBaseline/VolumetricFogBaseline.almap
```

Validation notes:

```text
- Static baseline capture transport completed; four frames are contiguous at 1280x704.
- No Directional Light produces a black frame in the current baseline.
- Wide FOV, near-geometry, opposite-view, and no-shadow captures completed.
- Four camera entities are prepared; alternating multi-camera rendering in Play Mode is not yet verified.
- Existing voxel debug path was confirmed in source, but the live voxel overlay was not toggled in this run.
- Fog shader variants loaded from cache without shader compiler diagnostics. DX12 startup emitted graphics PSO
  debug-layer warnings (IDs 679, 680, 974, and 677); these are recorded as baseline findings.
```

Known visual artifacts:

```text
- M1 corrections for ray thickness, accumulation sampling, jitter range, and HG direction are implemented.
- Static captures show no obvious new artifact; moving near-clipping geometry remains unverified.
- M1 GPU integration cost increased by about 0.0189 ms at 1280x704; attributed to per-froxel ray reconstruction.
```

---

## Milestone 1

Status:

```text
Implementation complete; M1-02 moving near-clip validation and user acceptance pending.
```

Build:

```text
Final `cmake --build --preset build-debug` passed after all M1 code changes.
The hidden Editor was restarted from the resulting binary; its log confirms fresh compilation of
`volumetric_light.hlsl` kernels 0 and 1 with `reason=SourceChanged`, without fog shader errors.
```

GPU:

```text
LightInject:      0.05226 ms
LightIntegration: 0.04050 ms
Composite:        0.01175 ms
Source: Editor GPU panel rolling average, 1280x704, VolumetricFogBaseline scene, final M1 binary.
Compared with M0: injection and composite are effectively unchanged; integration is +0.0189 ms,
consistent with per-froxel view-ray reconstruction for ray-space thickness.
```

Validation summary:

```text
M1-01: source uses view-space ray thickness. Final wide-FOV capture `M01_Final/M101_Wide/session_2`
contains 4 contiguous 1280x704 frames (engine_frame 6476-6479); center and edge show no obvious
thickness discontinuity. Integration is 0.04050 ms versus 0.0216 ms at M0 (+0.0189 ms), an explainable
ray reconstruction cost at this resolution. A DDS export of the 3D accumulator failed; the PNG fallback
captured only its first near slice, so finite-volume analysis is source-backed, not a full-volume readback.

M1-02: implementation maps boundary indices to texture-center coordinates and returns the identity
volume at/before near. `M01_Final/M102_Near/session_1` contains 4 contiguous Composite frames
(4918-4921), with no premature near-plane fog in the static near-occluder comparison. SceneView near
clip is 0.0098 m. The 120-frame moving-geometry capture did not record the requested transform updates
(adjacent RGB mean about 0.020, P99=1 LSB, max=3). A subsequent one-frame capture at the moved position
visibly differs from the stationary frame, confirming the geometry transform took effect, but it does
not show the object crossing the near plane or establish whether fog pops. The required motion-pop check
is therefore still open. The attempted DDS export of the 3D accumulator failed, so no full-volume
numerical comparison is available.

M1-03: source constrains jitter to +/-0.4 slice. Final `M01_Final/M103_Jitter/session_3` contains 8
contiguous frames (7875-7882); adjacent-frame RGB delta averages 0.0145, P99=1 LSB, max=2. This static
baseline-density scene does not expose shadow-boundary detail strongly, so the source bound and temporal
sequence are verified, while a high-density shadow-edge visibility comparison is not claimed.

M1-04: source tracing confirmed the directional component and shadow system store light-travel direction;
surface BRDF negates it to obtain the vector toward the light. HG now uses explicit light-travel and
sample-to-camera directions. A temporary diagnostic varied g across -0.7/0/+0.7 at 4x density and was
then fully reverted. Center ROI mean luma rose monotonically 62.564/74.109/80.752 when viewing along
the forward direction, and fell 125.873/115.534/95.592 from the opposite view (8 frames per value).
Captures: `M01_GDiagnostic/Front/session_1` and `M01_GDiagnostic/Opposite/session_2`.

M1-05: RenderGraph reads/binds the shadow map only when its handle is valid; the shader defaults to
unshadowed injection and samples the map only when enabled. `M01_Final/M105_NoShadow/session_4` and
`M105_ShadowEnabled/session_6` each contain 4 contiguous frames; both are non-black and no invalid-read,
resource-state, or fog-shader diagnostic appeared. The no-shadow image differs from the M0 no-shadow
reference (mean absolute RGB delta 0.8817, P99=6, max=56). The isolated scene comparison is not a
quantitative measure of fog illumination, but both modes render without a black frame.

M1-06: shader explicitly checks active directional-light count, emits zero injection with no directionals,
and skips temporal history reuse. `M01_Final/M106_NoMain/session_5` has 4 contiguous frames
(16344-16347); this directional-light-only scene is all black as expected when its sole light is changed
to a point light. Source inspection establishes the finite zero branch; an 8-bit black capture alone
cannot prove absence of NaNs.

Final A/B and restored-state captures are under `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Saved/VolumetricFog/M01_Final/`.
The Editor configuration and test-project editor-session file were restored after capture; the baseline
scene asset was not saved. GPU differences are small or explainable; no new obvious artifact was found
in the reviewed frames. The M1 acceptance gate remains open because M1-02 motion-pop validation is not
yet demonstrated and user acceptance has not been given.
```

---

## Milestone 2

Status:

```text
Implementation complete; multi-camera validation, blend-weight sweep, and M2 acceptance remain open.
```

Validation summary:

```text
Build: `cmake --build --preset build-debug` passed with temporal default restored to false. A temporary
default-on Debug build was used only for runtime captures and was reverted; the final Editor binary was rebuilt.
The Editor was launched from the rebuilt binary and closed afterward. Its `.ailu/editor_session.json` was restored
byte-for-byte from the pre-run backup. No test scene was saved.

Runtime path: `VolumetricFog_Composite`, NewScene, 1280x704. The modified `volumetric_light.hlsl` kernels 0 and 1
compiled from source with `reason=SourceChanged`, with no fog shader compilation error. Temporal was enabled by a
temporary default value for this diagnostic because the available Editor automation can list/activate features but
cannot set a VolumetricFog property. Source default remains false.

Static: temporal-off baseline contains 8 contiguous frames (engine_frame 7302-7309), mean adjacent RGB delta
0.04325, P99=1, max=4. Temporal-on capture contains 32 contiguous frames (3397-3428), mean 0.04290, P99=1,
max=4. The reviewed static images are visually equivalent and stable; this low-noise scene does not demonstrate a
meaningful quality gain.

Moving camera: one continuous 180-frame capture is contiguous (engine_frame 9135-9314) at 1280x704. The camera
excursion requested up to +0.3 m / +10 degrees and returned to its original pose; automation observations show
the applied pose lagged the requested pose under capture load. The last 30 frames at the restored view differ from
the stationary temporal-on frame by mean 0.0431-0.0453, P99=1, max=3-4, consistent with stationary frame noise.
The whole-frame movement sequence includes large expected pixel changes at geometry edges, so those deltas are not
used as ghosting metrics. Capture write duration was about 11.7 s (~15.4 fps), limiting this result to the loaded
capture setup and slow scripted motion.

Camera cut: an 8 m translation and 90-degree yaw snap produced 8 contiguous frames (engine_frame 9447-9454);
the first frame visibly shows the new view, and the sequence is stable. This checks the cut path in that test scene.

GPU panel snapshot while temporal was enabled: LightInject 0.05373 ms, LightIntegration 0.04048 ms,
Composite 0.01200 ms. This is a single rolling snapshot, not a controlled before/after benchmark.

Not verified: alternating multiple stable camera identities in one capture; temporal-current-weight sweep across
0.05/0.10/0.15/0.25. Neighborhood clamp was not added because these captures did not demonstrate persistent
ghosting that would justify the extra sampling. M2 acceptance remains open until multi-camera behavior and the
remaining tuning decision are reviewed.
```

---

## Milestone 3

Status:

```text
Core M3 acceptance passed on 2026-10-01 for the captured NewScene path (uniform / height fog / temporal).
M3-05 noise remains optional and unimplemented. M1 and M2 acceptance remain independent open follow-ups.
This is capture/image/numerical acceptance, not a human interactive Editor acceptance or the complete shared
cross-milestone regression matrix.
```

Validation summary:

```text
Build: initial and final `cmake --build --preset build-debug` passed. A temporary temporal-default-on Debug binary
was used to exercise actual C++ per-camera history; off cases disabled shader history reuse. All diagnostic parameter
and shader overrides were restored byte-for-byte, and the final binary was rebuilt with temporal default false.
Final restarted Editor compiled volumetric_light.hlsl kernels 0/1 and volumetric_fog_composite from source with
reason=SourceChanged. No fog shader compiler warnings/errors were found.

Setup: AiluTestProj, project://scene/NewScene.almap, VolumetricFog_Composite, 1280x704, 170x90x96 froxels,
near=0.0098 m, fog_distance=50 m, density=1, fog_height=0, g=0, intensity=1, temporal current weight=0.5.
Starting camera position=(22.5865,5.0254,-9.7637), pitch=1.3863 degrees, yaw=320.1321 degrees, FOV=60 degrees.
Positive-falloff stress comparison used 0.2; the unmodified default 0.02 was also captured with temporal on/off.

Uniform fallback: 16 frames each for falloff=0 and a diagnostic integration control using the pre-M3 uniform
extinction expression (0.01 * density). RGB mean absolute difference=0.04009 LSB, RMS=0.20763, P99=1, max=4.
The control deliberately shares current geometry/composition fixes; this isolates M3 medium storage rather than
claiming equivalence to the known-incorrect original M0 pipeline. No visual discrepancy was found.

Height: static captures at camera Y=1.0254 / 5.0254 / 21.0254 show a smooth low-to-high reduction of fog.
An independent diagnostic composite exported all 96 accumulation boundaries along the center ray in DDS, carrying
transmittance and two scattering channels. At falloff=0.2 the final transmittance is 0.6328125 / 0.8125 / 0.984375
at those heights. All sampled values are finite, scattering is nonnegative, and T is in [0,1] and monotonically
nonincreasing with depth. Uniform T is height-independent (final 0.6015625; analytic 0.6065901), with maximum
absolute boundary error 0.0078034 through R11G11B10_FLOAT readback. That error fits the output quantization near 1.
This is a center-ray boundary check, not a complete 3D-volume finite-value readback.

Motion: temporal off/on each produced 180 contiguous frames (engine_frame 969-1148). One continuous capture per
mode drove a smooth +4 m vertical excursion and returned to the starting pose. Actual maximum Y was 9.0131 off /
8.9665 on. Both cameras were restored. Capture plus polling took 12.58 / 12.70 s (about 14.3 / 14.2 fps), with
12 applied-pose observations per 5.5 s trajectory. Representative frames and worst pairs show no obvious slice
banding or persistent trail. Geometry-edge motion accounts for the large whole-frame deltas and is not treated as
fog instability. Fixed-direction sky ROI (x=16..199,y=16..159, no geometry) worst adjacent RMS=0.5901 off /
0.5767 on, P99=1 LSB. Temporal-on final 30 frames versus stationary reference have mean RGB delta=0.03144..0.03470,
P99=1. Post-motion stationary capture versus pre-motion reference: RMS=0.18239, P99=1, max=4, consistent with noise.
This establishes stability under the loaded scripted capture setup, not normal-frame-rate mouse interaction.

Noise off: source inspection confirms EvaluateFogMedium uses only arithmetic/exp; no medium-noise texture or
extra noise sample was added. The existing blue-noise jitter sample remains unchanged.

GPU rolling snapshots, milliseconds (not a controlled performance benchmark):
                              Inject    Integration    Composite
falloff=0, temporal off        0.06333   0.04242        0.01444
falloff=0.2, temporal off      0.08295   0.04231        0.01466
falloff=0.2, temporal on       0.09611   0.02409        0.01468
falloff=0.02, temporal on      0.08581   0.04253        0.01468
final default, temporal off   0.07544   0.02383        0.01468
Historical M1: 0.05226 / 0.04050 / 0.01175; M2: 0.05373 / 0.04048 / 0.01200. Different runs and snapshot
variability prevent assigning the whole cost delta to height fog; no controlled performance-regression claim is made.

Logs: no resource-state barrier mismatch, undeclared binding, or read-before-write diagnostics were found.
D3D startup message IDs 677/679/680/974/1328 also occur in this session's uniform control; they remain baseline
findings and are not presented as clean debug-layer acceptance.

Restoration: header, both shaders, three dirty Editor config/layout files, and project editor_session.json all match
pre-run bytes. No scene was saved. Only Editors launched by this run were closed. Final-default capture contains
16 contiguous frames from the rebuilt binary. Existing unrelated changes were preserved.

Evidence directory:
F:/AiluEngine/artifacts/out/editor/bin/x64/debug/M3Acceptance_20261001_1530/
Key artifacts: analysis.json, numeric_analysis.json, motion_roi_analysis.json, comparison.png,
height_on_motion_filmstrip.png, motion_viewer.html, final_restoration.json, build_final.log, final_runtime.log.
```

---

## Milestone 4

Status:

```text
Implementation and the targeted NewScene acceptance gate passed on 2026-10-01. This is capture/image acceptance
for the tested path, not the full shared cross-milestone visual regression matrix or interactive Editor acceptance.
M1-02 and M2 acceptance remain open independently.
```

Validation summary:

```text
Build: `cmake --build --preset build-debug` passed with `_ambient_intensity = 1.0` restored. Reflection generation
included the new APROPERTY. A temporary `_ambient_intensity = 0.0` default build was used only for the regression
control, then the default was restored and the final Debug build passed again.

Setup: AiluTestProj, project://scene/NewScene.almap, VolumetricFog_Composite, 1280x704. Default ambient and zero
ambient each produced 16 frames. After sorting the zero-ambient manifest by capture_index, both sequences are
contiguous: default engine_frame 467-482, zero engine_frame 451-466. Runtime logs show both volumetric_light.hlsl
kernels ready from the shader artifact cache, with no fog shader diagnostics, undeclared binding, barrier mismatch,
or read-before-write report. Existing D3D startup warnings remain baseline findings.

Ambient=0 control versus the prior M3 final-default frame 8: mean absolute RGB delta 0.03598 LSB, RMS 0.19759,
P99 1 LSB, max 8, consistent with capture noise and confirming the directional-only output is retained. Default
ambient versus zero at aligned frame 8 lifts the shadow-ground ROI (x=430..899, y=450..619) by a mean absolute
RGB delta of 8.77 LSB (P99 20); the wall-shadow ROI delta is 14.89 LSB (P99 30). Visual review shows shadowed
surfaces lifted without a distinct self-emission appearance. The composition/depth lookup was not modified, and
ambient=0 matches the M3 capture, so opaque-depth alignment remains unchanged for this path.

GPU rolling snapshot, milliseconds (not a controlled benchmark): LightInject 0.11423, LightIntegration 0.02513,
Composite 0.01423. These are single-frame rolling values and are not a controlled before/after performance claim.

The Editor used for capture was restarted from the final build and closed afterward. The test scene was not saved;
the project editor_session.json was restored byte-for-byte. Evidence is under
F:/AiluEngine/artifacts/out/editor/bin/x64/debug/M4Acceptance_20261001/ (captures, manifests, logs, GPU snapshot).
The pass/depth, shadow-boundary, temporal-on, no-directional-light, and alternate-environment-source cases from
the full shared matrix were not repeated in this M4 session.
```

## Milestone 5 — Acceptance session 2026-10-01

Status: **not accepted**. Glass/transparent-quad captures passed with SpriteRenderFeature disabled; alpha particle-card
rendering fails because SpriteRenderPass clears the existing fogged opaque/sky background and depth. No engine repair
was made during this acceptance-only session.

Fresh prior-M5 Debug binaries were used (Editor 21:26:48 / Engine_d.dll 21:26:47); no persistent C++ change or rebuild.
Separate temporary scene: near/far straight-alpha glass/quad slabs at Z=6/25, opaque background Z=32, alpha sprite
cards at Z=6/25. Static camera, 1280x704, default fog, temporal off. 35 sessions / 185 frames including native DDS;
all per-session manifests have contiguous engine_frame indices and existing files. Forwardlit shader bypass and
restoration compiled at fresh startup with reason=DependencyChanged.

Alpha=0 versus opaque composite linear RMS=0.001176; alpha=0.5 versus endpoint interpolation local RMS=0.00237..0.00566.
CopyColor versus TransparentPass RMS=0.001195; no later fullscreen double-fog found for the tested layers/background.
Far slabs visibly approach fog color; sampling-bypass versus normal RMS=0.010211 confirms transparent fog sampling.
Values include R11G11B10 quantization and separate stochastic frames, not exact physical error. The runtime fog-disable
control was excluded because Renderer re-enables fog every normal view. Refractive glass, water, actual emitter
simulation, temporal-on, and interactive UI paths were not accepted by this run.

Blocking diagnosis: SpriteRenderPass calls SpriteBatcher::Render without color/depth Load actions; CommandBuffer falls
back to RenderTexture's default kClear and clears both targets. Its event 460 precedes ForwardPass 475. The final image
loses sky/opaque backdrop; disabling SpriteRenderFeature restores it. Fix attachment preservation and repeat particle
acceptance before marking M5 complete.

GPU rolling snapshot: inject 0.06262 ms, integration 0.02059 ms, composite 0.01245 ms (not a controlled benchmark).
No shader compile error, undeclared binding, barrier mismatch or read-before-write found; existing startup warnings
remain. All temporary shader/config/database changes were restored byte-for-byte, original NewScene untouched, no
scene saved, temporary test assets removed, and only run-owned Editors closed.

Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/M5Validation_20261001_2200/`
(`REPORT.md`, `comparison.png`, `analysis.json`, `sequence_summary.json`, `restoration.json`, scripts/test assets/logs).
M1-02 and M2 multi-camera acceptance remain open independently.

---

## Milestone 5 — Sprite fix and re-acceptance 2026-10-01

Status: targeted captured M5 acceptance passed; this supersedes the particle failure in the preceding acceptance record.

SpriteRenderPass now declares the color read needed for blending and sets color/depth Load in RenderGraph and legacy
Execute before binding the attachments. Pass order, fog sampling and blend equations are unchanged. Debug build passed
with `cmake --build --preset build-debug`; Engine_d.dll rebuilt at 22:31:13 and every Editor capture instance restarted.

Ten-entity isolated test scene, default fog / temporal off / 1280x704. 16 sessions / 124 frames; all files exist and
engine_frame values are contiguous within each session. Captures preserve sky, opaque backdrop, glass/quad and near/far
alpha cards. Added particle at Z=40 behind opaque Z=32 is occluded; moving it to Z=30 makes it visible and restoring
Z=40 occludes it again. Linear central ROI RMS: behind-versus-sprites-disabled 0.001151, front-versus-behind 0.154444,
restored-versus-behind 0.000972. Unaffected backdrop with sprites on/off RMS=0.001179. CopyColor-versus-sprite output
outside transparent slabs RMS=0.001172 (P99=0.0078125), finding no second fog application.

Verified shader-sampling bypass / restored controls give near-card RMS=0.011331 and far-card RMS=0.043787; sky control
RMS=0.000731. Reviewed far cards visibly approach fog color. All sampled native DDS values finite. Quantization and
independent stochastic frames limit numerical precision. No shader compile error, undeclared binding, barrier mismatch
or read-before-write found; existing startup warnings remain. GPU snapshot inject/integrate/composite/sprite:
0.05604 / 0.02052 / 0.01225 / 0.00520 ms (not controlled benchmark).

All temporary shader/config/database changes restored byte-for-byte, test assets removed, no scene saved, run-owned
Editors closed. No new engine changes beyond the sprite attachment fix. Evidence:
`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/M5SpriteFix_20261001/REPORT.md`, `comparison.png`, `occlusion.png`,
`analysis.json`, `restoration.json`, `build.log`, captured manifests/logs and reproducible test assets.

Scope: straight-alpha planar glass/quad and sprite particle-card paths accepted. Actual emitter simulation,
refractive water, premultiplied materials, temporal-on, multi-camera and interactive UI acceptance not claimed.
M1-02 and M2 gaps remain open independently. M6 has not started.

---

# Decision Log

Only add decisions discovered during implementation that are not already stable architecture decisions.

## Runtime Decisions

None yet.

# Surprises & Discoveries

Only add new findings from implementation/profiling.

Current known items are documented in `00_Architecture_Overview.md`.

## New Discoveries

None yet.

# Known Follow-ups

Non-blocking:

1. MaxZ / depth-aware early integration optimization.
2. Local Fog Volume.
3. Point / Spot volumetric lights.
4. Clustered volumetric light culling.
5. Multiple scattering approximation.
6. Physically based scattering albedo.
7. Colored absorption.
8. Volumetric emissive medium.
9. DDGI / Surfel GI indirect contribution.
10. Replace hard-coded blue-noise `1024` dimensions with runtime dimensions.
11. Higher-quality temporal variance clipping.
12. Froxel checkerboard/interleaved update.
13. Packed/half format bandwidth optimization.
14. Async compute feasibility.
15. Dynamic-resolution integration.
16. Fog / VolumetricCloud atmospheric lighting unification.

# Outcomes

## Delivery A

```text
Milestone 0 + 1
```

Status:

```text
Not complete
```

## Delivery B — Recommended Target

```text
Milestone 0 ~ 4
```

Status:

```text
Not complete
```

## Delivery C

```text
Milestone 0 ~ 6
```

Status:

```text
Not complete
```

# Resume Protocol

## 1. Read Stable Context

Read:

```text
00_Architecture_Overview.md
```

Do not redesign the system before reading it.

## 2. Read Current Progress

Read:

```text
02_Progress.md
```

Find:

```text
Current Milestone
first unchecked task
last passed validation
```

## 3. Read Only Relevant Milestone

Use `01_Milestone_Index.md` to locate and open only the current file under:

```text
Milestones/
```

Avoid loading all milestone implementation details unless necessary.

## 4. Check Git Diff

Focus on:

```text
Engine/Src/Render/Features/VolumetricFog.cpp
Engine/Inc/Render/Features/VolumetricFog.h
Engine/Res/Shaders/hlsl/Compute/volumetric_light.hlsl
Engine/Res/Shaders/hlsl/volumetric_fog_composite.hlsl
Engine/Res/Shaders/hlsl/froxel_common.hlsli
```

If current diff modifies unrelated systems, determine whether it is intentional before continuing.

## 5. Continue From First Unchecked Task

Do not repeat tasks that already have:

```text
implementation checked
+
validation checked
```

## 6. Validation Is Required

“Code written” does not mean task complete.

Only mark a milestone complete when:

```text
Implementation complete
+
Validation complete
+
No blocking regression
+
Progress updated
```

## 7. Stop on Broken Assumptions

If a baseline assumption is wrong:

- stop adding features;
- update the relevant architecture/decision note;
- re-run current correctness validation.

## 8. Avoid Cross-Milestone Changes

Especially while Milestone 1 is incomplete, do not implement:

```text
Height Fog
Noise
Local Fog
Point Light
Spot Light
```

## 9. End-of-Session Update

Before ending an implementation session, record:

```text
Current task
Completed task
Build status
Validation performed
GPU timing if available
Capture/screenshot name
Known artifact
New discovery
Next task
```

## 10. Next Task

Current next task:

```text
M6 acceptance passed for captured NewScene single-SceneView path. Await user review; do not start optional M7/M8.
M1-02 near-clipping and M2 multi-camera acceptance remain independent open gaps.
```
