# M8 acceptance - 2026-10-03

## Outcome

**PARTIAL / final gate OPEN.** No regression was found by the existing GPU fixtures or the captured Editor smoke
sequences. This run does not accept the complete M8 combination, long-run state, or performance requirements.
No production renderer/shader changes were made. Existing dirty changes were preserved.
The legacy non-RG TAA execute path is deliberately retained because non-RG Renderer configuration is still supported;
its responsive-mask option remains disabled without the RG opaque snapshot. This run does not newly accept that path.

## Build and prerequisite regression

`cmake --build --preset build-debug` passed for Engine, Editor and test targets. `CameraTemporalHistorySmoke` passed
first-frame, rebuild, skipped/duplicate frame, reset and custom-VP controls. Full Engine/Editor/Test search found no
`TemporalAA::HaltonSequence`, `TAAPreparePass`, `_origin_camera_cbuf`, `_cur_vp_matrix`, `_motion_vector_depth` or
`kMotionVectorDepth`; the TAA source/header has no `_pre_matrix` candidate. Camera's jitter generator remains.

Evidence root: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/`.
The current runner and analyzers were used without relaxing thresholds or changing the default history factor.

| Run directory | Evidence | Result |
| --- | --- | --- |
| `TAA_M8_Acceptance_20261003_rg_225128` | 85 current/resolved pairs, 170 DDS, jitter/seed/factor-zero gates | PASS |
| `TAA_M8_Acceptance_20261003_scene-controls_225140` | 334 observations, 986 DDS; static geometry plus color, rigid-motion, occlusion and disocclusion controls | PASS |
| `TAA_M8_Acceptance_20261003_transparent_225155` | 784 observations, 3136 DDS; seven supported blend/Sprite/cutout/billboard cases | PASS |
| `TAA_M8_Acceptance_20261003_cloud_225220` | 384 observations, 2110 DDS; tile/full, TAA off/on, motion, cut and resize | PASS |
| `TAA_M8_Acceptance_20261003_rtgi_225249` | 640 observations, 4992 DDS; 103 image/graph/reconstruction gates | PASS |

All five fixture processes exited 0, reported D3D12 ERROR/CORRUPTION=0, and restored 8/8 backed-up files with matching
hash/existence. Inspect each `run1/analysis.json`, `run1/validation.txt`, `run_summary.json` and `restoration.json`.
Scene controls additionally use `run1/controls_analysis.json`. These remain feature-specific prerequisite regressions.

## Actual Editor capture

Selected executable: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe`, from this Debug build.
Project: `C:/Users/BoomBac/OneDrive/AiluTestProj/rg_acceptance.json`; actual loaded scene: `NewScene`, 19 entities,
GUID `2fc40882-e46d-4cf3-9bf7-1ae226c395fd`. SceneView output is 1088x576. Runs use single-thread submission.
The main game camera is not independently exercised.

`TAA_M8_Editor_20261003_225438` contains the existing Editor driver run: 176 captured frames across TAA output,
current/depth/motion/history inputs, CopyColor, Bloom input, ToneMapping, AA off/on and cuts. All sequences completed,
were continuous and finite; recorded D3D12 ERROR/CORRUPTION count is 0. `comparison.png` was reviewed: scene geometry
is present and ToneMapping receives rendered color. Sequential input/output captures are not same-frame quality pairs.

`TAA_M8_MatrixLongRun_20261003` contains seven combinations:

1. TAA + SSAO.
2. TAA + VolumetricFog.
3. TAA + SurfelGI.
4. TAA + DDGI.
5. TAA + Fog + SurfelGI + DDGI.
6. TAA + SpriteRenderFeature.
7. TAA + SSAO + Fog + SurfelGI + DDGI + SpriteRenderFeature.

Each combination captured 16 frames for static, interpolated pan, interpolated rotation, camera cut, AA off/on and
orthographic phases: 42 sequences / 672 frames. A further 184.532-second run captured 30 eight-frame bursts / 240 frames,
with an initial idle period, slow yaw changes and repeated SSAO toggles. Total: **912 frames**, all complete, consecutive
within each capture, and finite, including raw packed R11G11B10 special-exponent checks. Matrix montage was reviewed.
`cases.json` records requested phases, returned camera states and feature flags; `longrun.json` records observations.
`matrix_analysis.json` reports transport/pixel validity, not a temporal-quality pass.

The seven static previews are very similar. Feature-active flags alone do not establish effective GI/fog/Sprite
contribution or that every owning subsystem's GPU passes ran. GPU timing snapshots do not contain Surfel/DDGI pass
entries. Therefore these runs establish Editor toggle/camera smoke coverage, **not full M8 feature-combination acceptance**.
No moving Sprite/opaque entity was scripted in this Editor scene; those controls are only covered by the fixtures above.
Pan/rotation requests were logged at phase entry, without per-captured-frame pose alignment or an image-quality oracle.

The long run uses intermittent bursts, so gaps between bursts cannot rule out one-frame flashes. No memory/history-map
occupancy trend was measured. `TAAExecutePass::_infos` has no retirement/erase path in current source; a single stable
camera run does not test stale camera entries. Selection/gizmo, window resize and scene reload were not exercised.

Both Editor runs restored their original runtime camera/features and 8/8 config/log/session/graph files. Scene GUID,
entity count, structure/edit revisions and selection were preserved; no scene was saved. All started processes were closed.

## RenderDoc

`TAA_M8_RenderDoc_20261003` contains fresh core-RG captures `TAA_RG_frame140.rdc` and `TAA_RG_frame141.rdc`.
Replay through `Test/InspectTaaRenderDoc.py` passed all six gates: two captures, dispatch groups [40,23,1], correct
320x180 binding dimensions, no color read/write alias, empty debug messages and A/B history exchange.
See `binding_gates.json` and `renderdoc_analysis.json`. This is core fixture inspection, not the combined Editor graph.
The PIX late-load warning is not treated as PIX capture evidence.

## Performance evidence

`gpu_timing_samples.json` records three `render.gpu_timings` snapshots during the Editor long run. These are Debug GPU
panel rolling averages, which may lag and include intermittent capture workload. Resolution: 1088x576. They are not
isolated dispatch timings or normal-frame-rate performance targets.

| GPU pass | Observed rolling-average range (ms) |
| --- | --- |
| TemporalAA | 0.0772365-0.0799070 |
| TAAOpaqueSnapshot | 0.0057090-0.0058740 |
| StaticMotion | 0.0092550-0.0093840 |
| DynamicMotion | 0.0074780-0.0076690 |

Source-derived cost, explicitly not measured allocation/barrier deltas: TAA adds `TAAOpaqueSnapshot` and `TemporalAA`.
For 1088x576 R11G11B10 color, A+B color history is 5,013,504 bytes (4.78125 MiB) per active camera. Two R8 responsive
histories add 1,253,376 bytes (1.1953125 MiB): 5.9765625 MiB total logical persistent texture payload, excluding alignment,
resource metadata and driver allocation. The opaque snapshot adds 2.390625 MiB logical transient color payload.

CPU record cost, RG compile delta and additional barrier/transition counts were not captured. Current
`render.submission_stats` does not expose the source's `CpuRenderRecordingTimeMs`. M8.5 remains PARTIAL.

## Open final gates

- Prove effective contribution and inspect combined resource versions for Fog/Surfel/DDGI and all supported features.
- Include Cloud and realtime RTGI in the full combination, not only isolated feature fixtures.
- Validate dynamic object/occlusion/material controls across combinations, with paired references and quality metrics.
- Validate separate main game and Editor cameras, resize, frequent selection/gizmo and scene reload where supported.
- Use continuous long-run frame evidence plus history/resource occupancy measurements to close flashes/leaks/stale entries.
- Record CPU record cost and barrier deltas; measure RG compile delta if possible.

Keep M8 unchecked and this plan active. Earlier source-cleanup and Debug assertion gates remain implemented.

## Reproduction

Use `Test/RunTaaTransparentAcceptance.ps1` with each recorded mode and a fresh output directory, then the corresponding
GPU, scene, controls, transparent, Cloud or RTGI analyzer. For Editor smoke coverage:

```powershell
./Test/RunTaaM8EditorAcceptance.ps1 -EditorDirectory '<actual Editor directory>' `
    -ProjectFile '<project.json>' -OutputDirectory '<fresh directory beside Editor.exe>'
python Test/AnalyzeTaaCombinationEditorAcceptance.py '<same output directory>'
```

`Test/TaaM8GpuTimingSnapshot.mjs` is read-only; run against the same live Editor/project during an idle interval between
driver captures. It records rolling-average data and its limits. Do not run concurrent capture drivers or mutate scene
state from a second client. Preserve all evidence directories; do not select older milestone experiments as this run.
