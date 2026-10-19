# M0 — Baseline & Physical/Color Contract

## Purpose
不改变渲染效果，冻结各阶段依赖的数值定义、颜色域及测试基线。

## Scope
- 画 SceneColor 的数据流图：deferred、sky、transparent、volumetric、GI、TAA、FSR/DLSS、Bloom、ToneMapping、FinalBlit、Editor overlay、probe capture。
- 每个 RGB target 标注 domain：RawSceneLinear / PreExposedSceneLinear / DisplayLinear；depth/motion/normal/opacity 无曝光。
- 查 RenderGraph Create/Import、CameraCB 填充时机、多个相机的 RenderGraph 记录/执行与 GPU fence。
- 以源码和实际投影矩阵确认 `_fov_h` 的**垂直 FOV**语义；不要在本阶段重命名所有调用点。
- 建立常量测试：1×/2×/4×灰阶 HDR、0.18 卡、极亮小面积灯、完整光源/天空场景；抓取 ToneMapping 前后 DDS/PNG。

## Non-goals
不修改 AE/Tonemapper，不提交 Pre-Exposure，不调整旧场景光强。

## Architecture Constraints
如果捕获/测试不可复现，不允许修改生产色彩路径；Color Domain 必须与真实 shader 中的乘法/采样位置匹配。

## Baseline
`Camera.cpp` 投影使用 `BuildPerspectiveFovLHMatrix`，矩阵纵向 `P[1][1]=cot(fov/2)`；HDR R11G11B10；Tonemap 的 `_Exposure` 线性倍率。

## Target Outcome
拥有可复现、跨多个相机和 AA/SR 模式的基线与颜色域表，并验证序列化加载旧 Camera 的行为。

## Relevant Files
`Camera.cpp`, `Projection.cpp`, `Renderer.cpp`, `RenderingData.h`, `ShaderInterop.h`, `PostprocessStack.cpp`, `ToneMappingPass.cpp`, `PostprocessSettings.cpp`, `AutomationCaptureService.cpp`。

## Milestones
当前 M0；完成再进入 M1。

## Implementation
1. 建全链资源语义表（texture format、producers、consumers、viewport/dynamic resolution、是否 temporal、是否 HDR）。
2. 快照默认 SceneView / GameView / AA-off / TAA / 可用的 DLSS / FSR；固定光源与相机参数。
3. 记录 Current Exposure=1、Gamma=1、AgX/ACES/None 等校验图；记录 RGB 各 channel min/max、平均/百分位值。
4. 建支持脚本复验的 CPU EV Oracle、FOV Oracle，保持本阶段不修改 runtime 行为。
5. 确认 `PostprocessSettings` Volume 解析、默认缺省、脚本修改 Camera、probe camera 与相机反序列化；标出风险。

## Acceptance Criteria & Validation
- [ ] 可原样复现基线且同设置同帧容差可测。
- [x] 证明 vertical FOV 实际矩阵语义在 aspect 非 1 时成立（Camera smoke 覆盖 16:9、1:1、9:16；焦距换算尚未覆盖）。
- [ ] 颜色域追踪覆盖所有 SceneColor 生产者，不遗漏天空、云、IBL、raytrace 及透明。
- [ ] 默认开启前/后的非 RG/RenderGraph 使用合同已记录。
- [ ] 基线场景数据、参数、抓帧路径/命令、机器环境和输出哈希写入 progress。

## Current Milestone
M0
## Progress
源码审计和 CPU EV/FOV smoke 已完成；Debug smoke 通过。HDR GPU 图像基线与全链数值验证尚未完成。
## Decision Log
约定 FOV 为 vertical；旧资源默认 Legacy。
## Surprises & Discoveries
`_fov_h` 字段名误导，避免误做水平 sensor fit。
## Known Follow-ups
测试扩展到头显/离屏 capture 可后续独立进行。
## Outcomes
待验收。

## 2026-10-17 Execution Record

### Source Audit

| Resource / stage | Current source contract | M0 classification |
|---|---|---|
| Camera scene color | `Renderer.cpp` creates `_CameraColorAttachmentA` and `_CameraColorTex` at render resolution as `kDefaultHDR`; the format maps to R11G11B10_FLOAT. | Scene-linear HDR; no pre-exposure is present. |
| Deferred, sky, clouds, GI, transparency | These producers share the scene-color HDR targets. The inspected code does not carry an explicit color-domain tag on the target or handle. | Treat as scene-linear by current unexposed pipeline behavior; producer-wide numeric validation is still pending. |
| TAA | Reads scene HDR and uses per-camera temporal color history. The TAA setup uses `kDefaultHDR`; no exposure normalization is performed. | Scene-linear HDR; temporal history. |
| FSR / DLSS | FSR runtime currently supplies `preExposure = 1.0f`; Streamline currently enables SDK auto exposure. | No shared exposure contract; capture is pending. |
| Bloom | Mip targets and composition output use `kDefaultHDR`; dimensions are derived from the input handle. | Scene-linear HDR; reduced-resolution intermediates. |
| Tone mapping | `PostprocessStack` chooses upscaled, TAA, camera color, then color target; Bloom is optional and ToneMapping is always recorded. Shader multiplies by `_Exposure`, applies the selected curve, and returns display-linear values. | Input is scene-linear HDR; output is display-linear. `None` skips the curve but still executes the pass. |
| FinalBlit / overlays | RenderGraph imports the presentation target and blits final color; Editor overlays are scheduled after postprocessing. | Output encoding and UI color interaction require live capture; not inferred from this source audit. |

Camera projection source confirms `FovH()` is stored in degrees, converted to radians, and passed to `BuildPerspectiveFovLHMatrix`. The matrix uses `P[1][1] = cot(vertical_fov / 2)` and derives `P[0][0]` from aspect. Camera text archives still write/read `_fovh`; the legacy key is intentionally retained.

`ResolvePostprocessSettings` selects the enabled global volume with the highest priority. With no global volume it disables Bloom and selects ToneMapping None. Authored `ToneMappingSettings` defaults remain ACES and exposure 1.0. These defaults differ by absence of an authored volume and must be held constant in capture fixtures.

### Implemented Oracle

`Test/CameraTemporalHistorySmoke.cpp` now checks EV100 stop direction, the K=12.5 gray-point calibration and positive compensation direction; it checks vertical FOV matrix semantics at landscape, square, and portrait aspects. The runtime implementation was not changed. Camera archive source retains `_fovh`, but an executable archive roundtrip is not covered by this target because the free Camera archive operators are not exported from Engine DLL.

### Validation Status

- [x] Source-level camera/FOV, HDR format, postprocess ordering/default, and Camera archive contracts inspected.
- [x] CPU oracle added to the existing camera smoke target.
- [x] `cmake --build --preset build-debug --target CameraTemporalHistorySmoke` and `CameraTemporalHistorySmoke.exe` passed.
- [ ] ToneMapping GPU image smoke: target built, but runtime stopped at initialization with ImGui assertion `Could not load font file!`; no captures were produced. Run output: `Temp/physical_camera_m0_tonemap_20261017_01`.
- [ ] SceneView / GameView baseline captures with fixed camera, exposure 1, Gamma 1, Bloom off, and ACES/AgX/None outputs.
- [ ] RGB channel min/max/percentile manifest and capture hashes recorded.
- [ ] Scene, prefab, and probe-camera legacy asset load/save verified at runtime.

The current environment has no running Editor process and no connected render capture automation. The standalone ToneMapping smoke also cannot initialize its font in this run. No PNG, GPU readback, or live visual result is claimed by this record. The M0 acceptance gate remains open until capture and asset roundtrip evidence is collected.

## Resume Protocol
仅在验收全部通过后进入 M1；更新 progress 的具体执行证据。
