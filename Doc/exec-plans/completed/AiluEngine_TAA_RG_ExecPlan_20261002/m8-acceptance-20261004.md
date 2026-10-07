# M8 acceptance - 2026-10-04

## Outcome

**FAIL / final gate OPEN. The complete M0-M8 plan is not finished.** This run expands the earlier partial acceptance
with actual combined production features, continuous frame capture, camera retirement, real Editor window/input,
independent GameView camera and controlled performance measurements. Two final gates fail with runtime evidence:

1. Renderer overrides requested SSAO/Fog/RTGI feature flags every frame. The ordinary path forces SSAO/Fog on; the RTGI
   path disables both. The requested all-feature case has no SSAO or Fog GPU passes. This is an existing backend/support
   boundary, not an established TAA shader regression. Feature isolation/toggle acceptance and the intended full matrix
   cannot be signed off without resolving/documenting that boundary.
2. TAA retains histories for destroyed cameras. Four temporary cameras are rendered and destroyed; history count rises
   from 2 to 6 and remains 6 through the end of the three-minute run. Tracked RenderTexture payload rises by 2,304,000
   bytes and does not return after retirement. The map has no retirement path in current source. Return to the M3 owner
   for lifecycle repair; do not add an M8-only workaround.

M0-M7 keep their earlier scoped automated acceptance. They do not imply universal interactive/combination acceptance.
No renderer/shader fix was made in this acceptance run. The plan remains active and M8 remains unchecked.

## Current build and instrumentation

`cmake --build --preset build-debug` passed after the final source change. `git diff --check` passed. The capture skill
`.agents/skills/ailu-render-frame-validation/SKILL.md` was used. Existing unrelated dirty changes were retained.

Added production-RG test fixture `Test/TaaM8Acceptance.h`, matrix/continuous/Editor analyzers and Win32 routed-input
helpers. Existing runners and `TaaGpuAcceptance.cpp` gained M8 modes. Two small source additions provide observability
and control: read-only TAA history-map count, and AA mode in the existing CCamera automation property adapter. The latter
accepts integer modes 0-2 and requests a history reset. No new singleton, subsystem or generated reflection edits.

Evidence root for every directory below:
`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/`.
Native fixture CSV/manifests/analysis live inside each directory's `run1/`; Editor evidence lives at directory root.
The final build summary is retained as `TAA_M8_MainCameraEditor_20261004/build_debug.log`.

## Production combination matrix

Selected run: `TAA_M8_CombinedRetest_20261004`, process exit 0, 1280 consecutive engine frames, 5080 matched DDS files,
D3D12 ERROR/CORRUPTION 0, restoration 8/8. The analyzer exits 1 because the final acceptance gates fail; process exit
0 and successful capture transport are not an acceptance pass.

The fixture creates real opaque receiver/emitter/occluder/floor geometry, a sun/environment, DDGI volume, Surfel settings
and a runtime Sprite texture/asset. It uses production renderer-owned features and adds Cloud through feature registration.
RTGI uses the realtime Compute backend. Each case runs 128 frames covering static, pan, yaw, object/Sprite motion,
resize, cut, explicit reset, TAA/AA disable-enable, Cloud toggle, orthographic projection and a distinct main-camera switch.

| Case | Requested feature mask | Production graph result |
| --- | --- | --- |
| 0 | baseline | SSAO/Fog forced active, so not a clean feature-off reference |
| 1 | SSAO | SSAO passes present; Fog also forced active |
| 2 | Fog | Fog passes present; SSAO also forced active |
| 3 | Surfel | actual Surfel update/lighting producer passes present |
| 4 | DDGI | actual DDGI trace/update producer passes present |
| 5 | Fog + Surfel + DDGI | requested producer passes present; SSAO additionally forced active |
| 6 | Cloud | actual Cloud passes present |
| 7 | RTGI | actual RayTrace passes present; SSAO/Fog disabled |
| 8 | Sprite | actual Sprite draw present |
| 9 | all above | SSAO/Fog passes absent under RTGI; full-matrix gate FAIL |

`analysis.json` checks continuity, same-frame IDs, dimensions, finite values including raw R11G11B10 special encodings,
matching DynamicMotion/TAA handle versions, exact current-color RGB on expected seed frames, both camera identities
and both sizes. These checks pass. The two failing gates are `requested_feature_flags_respected` and `case_9_passes`.
Static TAA adjacent-frame RMS does not exceed current-color RMS by more than the recorded .001 tolerance in any case.

`combinations.png` was inspected: Surfel, DDGI, Cloud, RTGI and Sprite produce visibly different output from the first
case. Whole-image differences are supporting evidence only: baseline SSAO/Fog isolation is invalid, and stochastic
lighting differences alone cannot prove an individual feature contribution. The analyzer's per-case difference gates
must not be read as an independent Fog/SSAO quality acceptance. Full motion/occlusion/material quality across every
combination still lacks a paired perceptual/reference oracle.

## Continuous long run and history retirement

Selected run: `TAA_M8_ContinuousRetest_20261004`, process exit 0, total runtime 189.465 seconds including startup;
sampling stops at 180 seconds. Last captured elapsed time 179.949 seconds, 1412 consecutive engine frames, 5604 DDS,
D3D12 ERROR/CORRUPTION 0, restoration 8/8. All captured outputs are complete, frame-aligned and finite.

The native run uses all ordinary-path features including Cloud/Sprite, begins idle and then slowly orbits, and repeats
AA/feature toggles, resize, cuts and projection/camera changes. It renders four additional short-lived cameras at samples
300-303, destroys them at 305, and continues observing the two persistent views.

| Sample | Elapsed seconds | History entries | Tracked RenderTexture bytes |
| --- | --- | --- | --- |
| 299 | 35.878 | 2 | 132,467,960 |
| 304 | 36.462 | 6 | 134,771,960 |
| 306 | 36.700 | 6 | 134,771,960 |
| 1400 | 178.476 | 6 | 134,771,960 |

`continuous_analysis.json` fails `retired_camera_histories_released`; late fixed-size memory otherwise stays constant.
This demonstrates retained camera histories, not indefinitely rising memory for a stable two-camera workload. The
2,304,000-byte delta matches four cameras' logical 320x180 color A+B and responsive A+B payloads. Allocation overhead
and unrelated RHI pools are outside this counter.

Continuous transport closes the earlier gaps between capture bursts. It does not alone prove absence of perceptual
one-frame flashes. Stable-phase adjacent RMS is recorded (median .01988, max .06710); no flash threshold or full-scene
quality oracle is asserted. Editor selection/gizmo/reload are exercised separately rather than throughout this native run.

## Actual Editor window and camera checks

Selected input run: `TAA_M8_WindowEditorInputRetest_20261004`, driver exit 0, 18 sequences / 144 frames. Real Win32
resize changes SceneView output to 768x384, 960x512 and 1088x576. Twelve routed click/key cycles produce nonempty entity
selection in eight observations; W/E/R/Q are sent through the actual SceneView input path. Play/Stop enters
`NewScene_copy` and returns to `NewScene`. Captures are complete, continuous within each sequence and finite; original
scene identity/name/entity count return and files restore 8/8. `editor_analysis.json` passes its explicit smoke gates.
Gizmo transform movement and first-frame visual seed behavior are not asserted by this test.

Selected independent camera run: `TAA_M8_MainCameraEditor_20261004`, driver exit 0, seven sequences / 112 frames.
A temporary real `Ailu.ECS.CCamera` is configured through `scene.set_property` with `_camera._anti_aliasing=2`, confirmed
in `main_camera_component.json` and the runtime-copy inspections. Three actual toolbar Play/Stop cycles activate
GameView/main camera, capture TAA, then return to SceneView and capture again. All seven sequences pass transport,
continuity and finite-pixel checks. Commands are undone, entity count returns to 19, and files restore 8/8. Undo advances
runtime edit/structure revisions; revisions are not claimed unchanged. No scene asset is saved.

These tests cover real separate main/Editor cameras and runtime Scene instance creation/destruction. They do not claim
simultaneous-view rendering or disk scene reload. The current toolbar Open Scene action is explicitly not wired, as
shown by `EditorLayer::BuildEditorChrome()`. No alternate persistent asset rewrite was used to force a reload.

## Performance

Selected normal measurement: `TAA_M8_Performance_20261004`, process exit 0, D3D12 ERROR/CORRUPTION 0, restoration 8/8.
Fixed 1088x576 scene/camera/features, frozen scene time and object positions, alternating 256-frame AA-off/on blocks.
No image readbacks; skip the first 128 samples of each block. Debug single-thread render configuration. The test's one
observation pass and graph logging exist in both settings; the pass-count delta is attributable to TAA.

| Measurement | AA off | TAA on | Difference |
| --- | --- | --- | --- |
| Warm samples | 384 | 256 | - |
| Compiled RG passes | 58 | 60 | +2 |
| RG resources | 69 | 74 | +5 |
| Declared RG usages | 243 | 255 | +12; usages are not barriers |
| CPU record median ms | 8.2315 | 8.4261 | +0.1946 |
| CPU record P95 ms | 10.2073 | 9.7957 | noisy; not a speedup claim |
| StaticMotion GPU rolling median ms | .009446 | .009436 | - |
| DynamicMotion GPU rolling median ms | .0096065 | .009706 | - |
| TAA GPU rolling median ms | unavailable | .084282 | P95 .100835 |

GPU pass numbers are profiler rolling averages rather than isolated hardware-counter timings. TAA's AA-off profiler
value is stale and must be ignored. CPU delta is a controlled descriptive measurement, not a statistically established
production-performance target. `CPUProfileBlock` currently comments out native profiler registration/completion, so
the attempted RG compile field stays zero: compile time is **unavailable**, not zero-cost.

Selected barrier captures: `TAA_M8_PerformanceRenderDoc_20261004/run1/TAA_RG_frame{140,141,540,541}.rdc`.
Frames 140/141 are AA off; 540/541 are TAA on. Four completed captures replay successfully with empty debug-message lists.
`barrier_analysis.json`, produced by `Test/InspectTaaM8Barriers.py`, counts actual structured D3D12 ResourceBarrier calls:

| Setting | Calls/frame | Barrier array entries/frame |
| --- | --- | --- |
| AA off (both frames) | 97 | 160 |
| TAA on (both frames) | 100 | 169 |
| Delta | +3 | +9 |

Array entries include transition/UAV/other barrier types; do not relabel all nine as transitions. The RenderDoc-injected
fixture completed all 1280 measurements with D3D12 errors 0 but crashed on shutdown in `renderdoc.dll` (exit
-1073741819). PIX also reports swapchain capture warnings. This run is valid completed frame-level inspection evidence,
not a successful process/shutdown acceptance. Normal noninjected performance/matrix/long-run fixtures exited 0.

Logical TAA histories remain 5.9765625 MiB per 1088x576 active camera: color A+B plus responsive A+B. Opaque snapshot
logical transient payload is 2.390625 MiB. These are payload calculations, not driver-resident allocation measurements.

## Invalid/intermediate evidence

- `TAA_M8_Combined_20261004`: first fixture incorrectly selected RTGI for every case; excluded from matrix acceptance.
- `TAA_M8_Continuous_20261004`: test warmup accessed its null camera before Prepare; corrected by explicit started gate.
  It is a fixture defect, not a product crash. The continuous retest above supersedes it.
- `TAA_M8_WindowEditor_20261004`: selected an auxiliary window; input results invalid.
- `TAA_M8_WindowEditorRetest_20261004`: selected the main window and verified resize, but mouse button hit tests read
  system cursor coordinates; click checks invalid. The selected input retest synchronizes/restores system cursor position.

All directories retain their original results. Every wrapper restores its eight backed-up config/log/session/graph files.
All processes started for this task have been closed. No scene was saved, unrelated changes reset, or memory updated.

## Next acceptance boundary

Resolve history retirement in its owning lifecycle, establish the supported feature/backend matrix and its toggle
semantics, then rerun the same failing gates. Keep perceptual flash/ghosting and actual gizmo manipulation separate from
transport/numerical smoke. Disk reload remains untested; current toolbar support is unavailable. M8 cannot be closed or
the plan moved to completed on the evidence recorded here.
