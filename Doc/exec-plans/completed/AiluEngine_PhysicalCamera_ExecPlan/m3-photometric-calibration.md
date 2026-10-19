# M3 — Photometric Calibration & Scene Units

## Purpose
让物理相机中“相机单位”和“光源单位”实际可比，不以形式上的 EV API 替代物理验证。

## Scope
- 建立 `SceneLinearRgb -> Approx cd/m²` 的可测试标定合同；`ELightIntensityUnit` 的 Candela/Lumen/Lux、Watt、UnitlessLegacy 清晰区分。
- 基础物理测量场景：灰色 Lambertian 卡（rho=0.18）、已知 Lux 方向光、点光源距离照度、大小一致的环境/IBL。
- 检查 `sky_atmosphere.hlsl`, Environment/IBL、光探针、GI、Emissive、自发光、透明和云 HDR 贡献的尺度。
- 根据测试选择 `scene_luminance_to_cd_m2`（可独立配置，仅用于物理模式），不要盲目全局乘 683。处理与 GPU histogram 不同的 input domain。

## Non-goals
不追求严格光谱积分与相机 sensor spectral response；不批量改写旧场景的 unitless 参数。

## Architecture Constraints
- `GetLightRadiometricScale` 的 683 是 555 nm 的最大光谱光效，现有 RGB 系统的近似必须用实测 oracle 认证；不能视为所有光谱天然等价。
- Lambertian 灰卡在照度 `E_lux` 下理想亮度 `L_cd/m² = rho * E_lux / π`；例如 `E=1000 lux, rho=0.18 → ~57.30 cd/m²`（忽略环境与多次反弹）。
- 物理源、legacy 材质、自发光混合时须在 Editor 报告 unit mode；旧场景观感优先保真。

## Baseline
`Scene/LightUnits.h` 含光度/辐射转换；`sky_atmosphere.hlsl` 包含 PI 经验缩放；某些云 shader 使用自有 `_Exposure` 作为材质风格化参数。

## Target Outcome
在可控线性灰卡场景，物理曝光结果与光度基准一致；与 legacy 的转换是显式、可关闭、可回退的。

## Relevant Files
`Scene/LightUnits.h`, `Renderer.cpp`, `RayTracing/SceneRayTracingProxy.cpp`, `lighting.hlsli`, `sky_atmosphere.hlsl`, `Environment*`, `RayTraceGI.cpp`, `PostprocessSettings.h`, `AutoExposurePass`。

## Implementation
1. 记录每种光源单位在 GPU Shader 中最终的 color / radiance/illuminance 语义与世界距离基准（默认1 world unit 是否1m）。
2. 在 HDR 测试图中排除 Tonemapper 和环境源，以红/绿/蓝白灰卡分别测量；验证 Lambertian 光照亮度线性。
3. 引入物理模式独立单位换算逻辑或场景 calibrator；先不修改 legacy 的原始数值。把 `_scene_luminance_to_cd_m2` 加到 camera/exposure debug 输出。
4. 分别校正天空/IBL/Emissive 模型的物理默认预设或物理模式值。云特效已有 `_Exposure` 含非线性含义，不得简单绑定相机曝光。
5. 更新 M2 physical AE 的 luminance-to-EV 映射与校准参数；检查物理手动相机和 auto 的灰点是否在同一换算下重合。
6. 为 GPU 验收扩展 Editor SceneView camera automation，读写并回读 `PhysicalCameraSettings`；测试只改运行时相机，不保存场景或 Editor 配置。

## Acceptance Criteria & Validation
- [x] 1000 lux、0.18 反照率 Lambertian 灰卡，测得估算 `57.30 cd/m²`，标定误差目标 ±5%（若现有 BRDF/颜色模型不允许，则先修正或记录 blocker，不能标成 PASS）。GPU directional gray-card capture measured +4.77% at 1000/2000 lux.
- [x] Lux×2、亮度×2；Cd 点光强×2、radiance×2；点光距离×2、亮度≈1/4（忽略 range/window/近距渐变）。GPU captures verified exact 2x and 1/4 ratios; see the capture record for ROIs and range-window controls.
- [x] 物理相机改变 1 EV，灰卡 pre-tonemap scene value 恰按倍率变化；Legacy 不变。 GPU SceneView API read/write/readback; neutral ToneMapping R11G11B10 ROI median 0.087890625 -> 0.17578125 (2.0x), eight contiguous frames each.
- [ ] HDR sky/IBL/emissive path 在物理模式中有可解释的亮度合同；没有未经检查的硬编码额外曝相机值。
- [x] GPU 自动曝光的当前 Meter EV 与 M1 CPU oracle 匹配（指定灰卡测试输入）。 GPU history EV median 1.0271 vs ideal M1 CPU oracle 1.1015 (delta -0.0743 EV, within half-bin 0.2188 EV); the measured +4.77% card luminance predicts 1.0342 EV, leaving -0.0071 EV residual.

## Current Milestone
M3
## Progress
实现了 `_scene_luminance_to_cd_m2` 配置（默认值依据 authored photometric light 到 shader radiometric 值的 683 lm/W 近似反推），并同时用于 Manual Physical 与 GPU 自动曝光。CPU 灰卡 oracle 验证了 `1000 lux × 0.18 / π ≈ 57.30 cd/m²` 及手动/自动归一化一致。真实 raster/RT GPU 灰卡、距离衰减、天空/IBL/Emission/Legacy 的 scale 和 ±5% 标定仍待运行验收；该默认值不是全内容实测结果。 已尝试隐藏启动 Debug Editor，但 `SHGetFolderPath(CSIDL_PROFILE)` 指向不存在的 `C:/Users/CodexSandboxOffline/OneDrive/AiluEngine`，与工作区路径不同；Windows ACL 拒绝创建临时 junction。进程未发布 automation session，未产生 capture。一次性项目与启动日志保存在 `.artifacts/out/editor/bin/x64/debug/M3_PhotometricCalibration_20261017T121113Z/`，共享 Editor 配置均验证未变化。校准系数已作为反射配置字段提供；相机/曝光专用调试展示按 UI 计划留到 M6。
## Decision Log
真实光度单独量化验证，不依赖“变量叫 Lux”作为成功证据。当前 683 默认值来自 `LightUnits.h` 对物理光源输入的近似反向映射，仅用于 Physical 曝光且可由 Postprocess exposure settings 覆盖；不把这个值当作天空、IBL、自发光和 Legacy 内容的实测普适系数。
## Surprises & Discoveries
Sky/Cloud 特效与物理点灯亮度不一定共用同一量纲。
## Known Follow-ups
精确光谱/胶片/光学滤镜可以后续单独实现。
## Blockers
No blocker for SceneView camera automation or the gray-card exposure captures. HDR sky/IBL/emissive/UnitlessLegacy still need an appropriate controlled fixture and brightness oracle.
## Outcomes
`M3_PARTIALLY_GPU_ACCEPTED`。线性 HDR 物理光照灰卡与点光相对比值、Manual Physical 1 EV/Legacy 不变及 GPU Auto EV/CPU oracle 已完成；sky/IBL/emissive/UnitlessLegacy 亮度合同仍未验收。

### GPU capture update: point-light ratios

The running user-launched Editor is confirmed on the disposable `M3PhotometricGrayCard` fixture (4 entities, no scene camera entity). Directional gray-card tests at 1000/2000 lux pass the +/-5% luminance criterion at +4.77%, with exact 2x radiance response; see the capture record for frame manifests and visual PNG.

Point-light GPU captures also pass the relative checks. At a fixed 4 m distance, 1000 to 2000 cd doubled the center 8x8 median radiance from 0.005533854 to 0.011067708. With the smooth range window disabled, moving the 1000 cd point source from 4 m to 8 m changed the center median from 0.0055338541667 to 0.0013834635417, exactly 1/4. Each accepted ratio sequence contains 8 contiguous frames; the inverse-square sequences are byte-identical. The range-enabled 8 m result is diagnostic only. Point absolute luminance is +5.55% against the ideal Lambert estimate and is outside the explicitly scoped gray-card +/-5% gate.

Evidence directories are `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PointLight_20261017/session_9-11/` and `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PointLight_InverseSquare_20261017/session_12-13/`. The temporary light and SceneView camera settings were restored; no test scene was saved.

Still pending: HDR sky/IBL/emissive and UnitlessLegacy brightness-domain calibration. Physical exposure automation, Manual Physical 1 EV versus Legacy invariance, and Auto EV versus CPU oracle are now GPU-verified; M3 remains incomplete.
## Resume Protocol
通过全部验收再进入 M4；记录标定系数推导和 sample output。

### Follow-up: user-launched Editor transport smoke

The user started Editor under the normal Windows account. The automation pipe was reachable only with elevated access from this sandbox. A capture completed for `DeferredLightingPass` / `_CameraColorAttachmentA`: 8 native DDS frames at 896x512, engine frames 18804-18811. The live session reports project root `C:/Users/BoomBac/OneDrive/AiluTestProj`, not the M3 gray-card fixture, so this is transport evidence only and does not satisfy M3 GPU acceptance. DDS files and manifest: `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PhotometricGrayCard_20261017/session_1/`. No scene or runtime settings were changed. The intended fixture still needs to be opened for radiometric analysis.

### GPU capture update: controlled directional gray card

The expected `M3PhotometricGrayCard` fixture is now confirmed active in the user-launched Editor. A temporary correction to the test-only directional light transform was required: identity rotation points the engine default `(0,-1,0)` direction downward; the fixture's authored 180-degree X rotation pointed it upward. The original light rotation and SceneView camera were restored after capture; the scene was not saved.

At 1000 lux, uniform card RGB in R11G11B10_FLOAT is 0.087890625, which maps by the current 683 lm/W approximation to 60.0293 cd/m2 versus 57.2958 expected (+4.77%). At 2000 lux the captured value is 0.17578125 and 120.0586 cd/m2 versus 114.5916 expected (+4.77%); doubling illuminance doubled radiance exactly. Both 8-frame sequences are completed, contiguous, and internally byte-identical. The centered ToneMapping PNG shows the card. Raw captures, manifest, and visual PNG are under `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PhotometricGrayCard_20261017_{radiometric,visual}/`.

This satisfies the directional gray-card +/-5% and 2x illuminance criteria. Point light intensity/distance, manual/auto exposure, and sky/IBL/emissive/legacy remain pending; do not mark M3 complete.

### GPU retest: SceneView physical exposure automation

- Rebuilt Debug Editor was restarted by the user; PID 12648 resolved to `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe` and the active project was the disposable `M3PhotometricGrayCard` fixture (896x512, four entities, no scene camera entity).
- `editor.get_scene_camera` returned all ten `physical_settings` fields. Partial `editor.set_scene_camera` writes were read back with the requested sanitized values; an invalid `exposure_mode: 99` was rejected. The test did not add reflection or persist camera settings.
- Manual Physical mode 0 EV versus +1 EV, neutral ToneMapping pass: gray-card ROI median R11G11B10 red value 0.087890625 -> 0.17578125 (exact 2.0x). Each sequence contains eight contiguous engine frames; ROI values are stable across all eight frames.
- Legacy Manual 0 EV versus +1 EV: ROI median 0.0859375 -> 0.0859375 (exact 1.0x), also eight contiguous and stable frames.
- GPU Auto Exposure was captured after narrowing SceneView FOV so the gray card filled the metering image. `AutoExposureHistory` output EV ranged 1.0246-1.0293 (median 1.0271) over eight contiguous frames. The ideal M1 CPU oracle from 57.2958 cd/m² predicts 1.1015 EV; GPU differs by -0.0743 EV, within half a 64-bin histogram step (0.2188 EV). The measured GPU card luminance was +4.77% over ideal and predicts 1.0342 EV, leaving a -0.0071 EV residual. Auto ToneMapping card median was 0.171875 versus the 0.18 gray target (-4.5%); its eight captured frames were byte-identical.
- Capture artifacts and RPC/restoration records: `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PhysicalExposure_20261017/session_centered/` and `.../session_auto_cardfill/`. Final SceneView camera settings and directional-light rotation were read back at their original values; fixture was not saved.
- The first two Auto attempts used a mostly black or partially framed view and were excluded from acceptance. The accepted card-fill capture is the `session_auto_cardfill` result.
- Still untested in this pass: sky, IBL, emissive, and UnitlessLegacy brightness calibration.
