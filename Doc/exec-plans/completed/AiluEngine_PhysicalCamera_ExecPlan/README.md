# AiluEngine Physical Camera & Exposure Pipeline — ExecPlan

> 基线：用户提供 `AiluEngine_Source(20261016-165730).zip`，2026-10-16 源码快照。**这是实施计划，不是已经修改/构建的代码。**
> 任务规模：大型；跨 Camera / ECS & Serialization / Postprocess / RenderGraph / Shader & Lighting / TAA / DLSS & FSR / Editor / Tests。分拆为 README、progress 与 M0–M6。

## Purpose
一次引入可工作的物理相机**曝光体系**：镜头/传感器几何、ISO/光圈/快门、EV100、手动/自动/优先级曝光、GPU Histogram、Eye Adaptation、Pre-Exposure，并接入已有 ACES/AgX/Bloom、TAA 与超分链。完整功能目标一步确定，实施上逐里程碑验收。

## Scope
- 相机参数：sensor width/height mm、focal length mm、aperture f-number、shutter seconds、ISO、exposure compensation EV、焦距/FOV 双向编辑策略、物理/Legacy 模式。
- 曝光：Manual Physical、Auto Meter、Aperture Priority（调整 shutter）、Shutter Priority（调整 aperture）、Auto ISO（可选在优先模式中调整 ISO）；曝光/计量单位统一。
- GPU：对曝光前、Tonemap 前的线性 HDR 进行低分辨率直方图计量；log luminance 64 bin、百分位、时域平滑、1×1 R32_FLOAT 历史 ping-pong，全 GPU 无 readback。
- Pre-Exposure：场景预乘 `P_frame`，曝光统计反归一化；TAA/后处理/超分历史和光照生产者使用明确空间合同；与 Display-linear 输出分离。
- 光照验证：已有 `ELightIntensityUnit` 的真实场景 HDR 亮度校准（重点：Sky、IBL、legacy、Emissive、GI），标注近似性质；不擅自改变旧资产结果。
- 向后兼容：原 `_exposure`（线性倍率）与旧 `_fov_h` 场景保持观感；没有显式启用 Physical 模式时走旧行为。
- 编辑器与自动验证：Camera Inspector、Postprocess Volume、图像/数值 oracle、截图和 GPU 性能测量。

## Non-goals
- 本次不实现光学景深 DOF、散景 Bokeh、真实快门积分运动模糊、畸变、镜头透镜追迹、光谱颜色模拟。留数据接口，**不能因为存在光圈/快门字段而宣称这些光学效果已实现**。
- 不为真实光照强行修改所有旧场景的 `UnitlessLegacy` 强度；使用可校准的单位策略和场景兼容标志。
- 不在第一步导入真实硬件镜头标定，也不以常量 683 lm/W 自动宣称整个 RGB 渲染管线严格光谱正确。
- 不在曝光改造中重构并行 RenderGraph 或替换当前 Tonemapper。

## Architecture Constraints
1. SceneColor 的物理/场景线性亮度 `C_scene` 与预曝光值 `C_pre = C_scene * P_frame` 属于不同域，所有 Pass 必须声明消费/生产域；ToneMapping 输入不能被默认为未预曝光。
2. `P_frame` 每相机、每渲染帧固定，推荐上一有效帧曝光；最终 `ToneInput = C_pre * E_display / P_frame`。曝光直方图须先求 `L_scene = L_pre / P_frame`。HDR readback 禁止成为主运行时路径。
3. GPU 历史读写必须是不同的持久资源；遵守 Import、Read(SRV)/Write(UAV) 和帧内/跨帧 fence 生命周期。每相机单独 History（camera identity + reset revision + frame index）。
4. 先记录原版画面基线；所有新行为通过显式模式开启。CameraCut、缩放/算法切换、暂停恢复与 HDR 配置切换须重置或合理重标定历史。
5. `ToneMapping None` 在本快照 **仍会执行 ToneMappingPass**，仅不应用 ACES/AgX/Standard 曲线；保留该约定。
6. `kDefaultHDR` 为 R11G11B10_FLOAT，需验证极值、无负数、TAA 精度与 Pre-Exposure 的真实收益。
7. 一帧曝光必须与 Bloom、透明、天空、体积效果、TAA、DLSS/FSR 输入一致；Gizmo / Editor UI 不参与自动测光也不被重复曝光。
8. 类/函数 CamelCase、成员 `_lower_snake_case`、其他变量 lower_snake_case、静态 `s_`、常量 `kCamelCase`、代码行长 ≤160。
9. 未通过一个 Milestone 的所有 Acceptance Criteria，不进入下一 Milestone；各阶段保留运行开关、可回退。

## Baseline
- `Engine/Inc/Render/Camera.h` / `Engine/Src/Render/Camera.cpp`：仅 FOV `_fov_h`、无物理镜头参数；Camera 读写存在手工 `_fovh` 序列化，需迁移。`_fov_h` 命名虽似 horizontal，但 `BuildPerspectiveFovLHMatrix` 的数学实现将输入作为**垂直 FOV**（P[1][1]=cot(fov/2)，P[0][0] 再除以 aspect）。
- `Engine/Inc/Scene/LightUnits.h`：Lux/Lumen/Candela/功率 + `UnitlessLegacy`，以 683 lm/W 进行近似光度/辐射换算。`sky_atmosphere.hlsl` 仍使用 `PI * _EnvironmentIntensity`；无法预设场景值即 cd/m²。
- `Engine/Inc/Render/PostprocessSettings.h`：`ToneMappingSettings::_exposure` 为倍率（1.0）、`_gamma`，模式 None/ACES/AgX/Standard。
- `Engine/Src/Render/Features/PostprocessStack.cpp`：优先 `_upscaled_color` → `_taa_color` → `_color_tex` → `_color_target`，Bloom 后始终 ToneMapping。`_final_color` 不改 `_color_target`。
- `Engine/Src/Render/Features/ToneMappingPass.cpp` + `Engine/Res/Shaders/hlsl/PostProcess/tone_mapping.hlsl`：在 ToneMapping 中乘 `_Exposure`，保持 display-linear 输出合同。
- `Engine/Src/Render/Renderer.cpp`：为 Camera 使用 R11G11B10_FLOAT HDR；`CBufferPerCameraData` 在 `ShaderInterop.h`，CameraCB 按 HashCode 取资源。
- `Engine/Src/Render/Features/TemporalAA.cpp`：独立 per-camera temporal history，已有 reset revision / 跳帧处理。
- `Engine/Src/RHI/DX12/FsrRuntime.cpp`：FSR dispatch 接收 color domain 对应的 P；`Engine/Src/RHI/DX12/StreamlineDx12Bridge.cpp`：DLSS 保持 `useAutoExposure = true` 并传递 SR frame data 的 P。
- `Engine/Src/Render/PostprocessSettings.cpp`：只有 `Global` Volume 最高优先级选择，无 Volume 时关闭 Bloom 且 ToneMapping None。保持旧行为。
- `Editor/Src/Inspector/ComponentEditors/{CameraComponentEditor,PostprocessVolumeComponentEditor}.cpp`、`Editor/Src/Inspector/ComponentInspectorFields.cpp`：两套编辑入口均须同步。

## Target Outcome
- 旧场景无需修改，关闭新功能时逐像素/误差阈值与基线一致。
- 物理相机：在已标定光度场景，ISO/快门/光圈按每档严格 2 倍曝光变化，焦距/传感器可控制 FOV 且兼容原存档。
- 自动曝光：曝光目标可计算、对 HDR 极亮像素稳定、响应速度可调、帧率无关；多个相机、断帧、切换算法不会串历史。
- Pre-Exposure：场景、Bloom、TAA、DLSS、FSR 的颜色空间合同一致且无闪烁；禁用状态可回退。
- HDR/SDR + ACES/AgX/None 的最终显示变换只有一次。

## Relevant Files
- Camera/ECS: `Engine/Inc/Render/Camera.h`, `Engine/Src/Render/Camera.cpp`, `Engine/Inc/Scene/Component.h`, `Engine/Src/Framework/Math/Projection.cpp`。
- CameraCB/Renderer: `Engine/Inc/Render/ShaderInterop.h`, `Engine/Src/Render/Renderer.cpp`, `Engine/Inc/Render/RenderingData.h`。
- Postprocess: `Engine/Inc/Render/PostprocessSettings.h`, `Engine/Src/Render/PostprocessSettings.cpp`, `Engine/Src/Render/Features/{PostprocessStack,ToneMappingPass,BloomPass}.cpp`, `Engine/Res/Shaders/hlsl/PostProcess/{tone_mapping,bloom}.hlsl`。
- RenderGraph/GPU: `Engine/Inc/Render/RenderGraph/RenderGraph.h`, `Engine/Src/Render/Features/TemporalAA.cpp`, `Engine/Res/Shaders/hlsl/Compute/taa.hlsl`。
- Light/Sky: `Engine/Inc/Scene/LightUnits.h`, `Engine/Res/Shaders/hlsl/{lighting.hlsli,sky_atmosphere.hlsl,global_volumetric_cloud.hlsl}`, `Engine/Src/Render/RayTracing/SceneRayTracingProxy.cpp`。
- Upscaler: `Engine/Src/RHI/DX12/{FsrRuntime,StreamlineDx12Bridge}.cpp`，各自的 RenderFeature。
- UI/Automation: `Editor/Src/Inspector/ComponentEditors/{CameraComponentEditor,PostprocessVolumeComponentEditor}.cpp`, `Editor/Src/Inspector/ComponentInspectorFields.cpp`, `Editor/Src/Automation/AutomationCaptureService.cpp`。

## Exposure & Units Contract
### 摄影 EV100
- `EV100 = log2((N * N / t_seconds) * (100 / ISO))`，N 为 f-number；t 为曝光时间（秒）。
- **本项目约定** `exposure_compensation_ev > 0` = 更亮。`E_display = calibration * 2^(-EV100 + compensation_ev)`。
- 使用反射式测光常数 `K=12.5` 和物理亮度 `L_cd_m2` 时：`EV_meter = log2(L_cd_m2 * 100 / K)`。若希望测得中灰在 Tonemapper 前恰好是 0.18，选 `calibration = 0.18 * 100 / K = 1.44`。这是**项目选定的灰点归一化标定**，不要将其混同于某相机厂商的 sensor saturation 标定。自动曝光因此满足 `L_meter * E_target = 0.18 * 2^compensation_ev`。
- 将内部线性 HDR 数值换算到 `L_cd_m2` 的 `scene_luminance_to_cd_m2` 为**单独待校准**的系数，不允许默认假定为 1。针对已使用 683 的光源路径测试后选定可追溯策略；legacy 模式保留旧倍率。
- 在物理模式中，亮度单位/出图变换分开；镜头光圈调曝光**不会产生景深**，快门调曝光**不会产生运动模糊**。

### Pre-Exposure 合同
1. 本帧渲染开始从上次有效曝光确定 `P_frame`（例如 last_display_exposure，经限制），所有 SceneColor 写入统一采用它；首帧手动由物理相机计算，自动模式使用合理 seed。
2. 归一化计量：`unexposed_hdr = preexposed_hdr / max(P_frame, epsilon)`。
3. 本帧 GPU Histogram 产出并平滑新的 `E_display`。ToneMapping 输入 `C_pre * E_display / P_frame`，然后运行 ACES/AgX/Standard（None 不运行曲线），最后走已有显示输出编码。
4. TAA 历史颜色若包含上一帧预曝光 `P_history`，读入时乘 `P_frame / P_history`，或按约定规范化到统一域。运动矢量、depth、reactive mask 和不携带亮度的缓存不能误乘。
5. Vendor SR：FSR API `preExposure` 更新为真实 P；DLSS 内置 auto exposure 行为与输入/输出域必须经 SDK 核对并做抓帧验收；无法证明一致时，先为 vendor 输入输出加入受控的去预曝光/再预曝光转换，不允许直接带着 `1.0` 和任意 P 上线。

## Milestone Overview
| 阶段 | 文件 | 主要成果 | 关卡 |
|---|---|---|---|
| M0 | `m0-contract-and-baseline.md` | 单位/颜色空间/资产/截帧基线 & 灰卡 oracle | 无新效果、基线准确 |
| M1 | `m1-physical-camera-model.md` | 镜头 FOV + 可序列化物理参数 + Manual EV | 数学测试、兼容旧场景 |
| M2 | `m2-gpu-auto-exposure.md` | Histogram + Eye Adaptation + GPU Exposure History | 画面可测光，无 CPU 回读 |
| M3 | `m3-photometric-calibration.md` | 光源/天空/Emissive 光度审计与校准 | 标准灯+灰卡的亮度匹配 |
| M4 | `m4-pre-exposure.md` | CameraCB / SceneColor / Bloom / TAA 的全链预曝光 | 无亮度跳变与历史伪影 |
| M5 | `m5-upscaler-integration.md` | DLSS / FSR 与非 RG fallback 域适配 | 不同 AA/SR 模式比较通过 |
| M6 | `m6-editor-and-regression.md` | 编辑器操作、Volume、自动化与性能 | 用户已标记完成；未独立复核 |
| M7 | `m7-depth-of-field.md` | 物理景深与多边形散景 | 默认关闭、可控 CoC 与镜头参数 |

## Current Milestone
M7。用户于 2026-10-18 标记 M6 完成；本次不重新审查 M6 的验收材料。M7 实现物理景深与多边形散景，最终 Editor 视觉验收由用户完成。继续时只阅读 README、progress 和当前 milestone。

## Progress
M0–M3 的实现和验收状态见 `progress.md`。M4 源码已实现并通过 Debug build；shader 运行编译、RenderGraph capture 和 GPU/image acceptance 由用户后续验收。

## Decision Log
- D1：优先完成**物理相机曝光系统**；M7 将景深/散景作为独立、默认关闭的后处理扩展。
- D2：第一版保留旧 `_exposure` 线性倍率，Physical 模式显式开启。
- D3：默认全 GPU；每相机持久 exposure history，采用 ping-pong。
- D4：先建立单位验证与 FOV 纵向语义契约，再施加 Pre-Exposure。
- D5：绝不让 DLSS/FSR 隐性独立曝光和 Display Exposure 发生无法解释的叠加。

## Surprises & Discoveries
- 当前源码的 FOV `_fov_h` 名称与实际投影纵向公式不符，不能仅按名称做 horizontal focal length 计算。
- AgX、Gamma 已存在，旧 GitHub 审查快照不代表上传源码。
- `kDefaultHDR` 是 R11G11B10_FLOAT，可能有曝光前极值精度问题。
- FSR 与 DLSS 的调用均需明确输入/输出颜色域；RenderGraph 自动曝光的 P 仅能由 GPU history 获取，不能为 vendor 参数增加同步 readback。
- `Compute/volumetric_cloud.hlsl` 使用自有 `_Exposure` 进行 pow 着色；这是云材质参数，不能与物理相机曝光同名混用。

## Validation
详细阶段 oracle 见 mX。必须涵盖：EV 档位、18% 灰卡 + Lux、单峰/高光分布统计、禁用时图像基线、5/30/60/120 FPS 变步长、自适应方向、多相机、camera cut、动态分辨率、SR/TAA、极亮源、SDR/HDR 出图、Bloom 阈值、探针捕获与 UI overlay。

## Known Follow-ups
- Motion blur、T-stop/transmittance、rolling shutter、lens distortion。
- 精准光谱/色温校准、与 DCC 统一景物尺度；局部曝光（Local Exposure）作为独立特效。

## Outcomes
M0–M5 与 M6 当前实现范围见 `progress.md`；GPU/image/runtime acceptance 和性能矩阵仍有缺口。整个 Physical Camera Exposure Pipeline 尚未完成，不能仅以源码或 Debug build 宣称最终交付。

## Resume Protocol
1. 读取 `README.md`、`progress.md` 和 `progress.md` 标明的当前 mX；只在需要特定代码时阅读其他 mX。
2. 核对仓库 HEAD 与快照差异；若已有类似修改先记录 Decision Log，不重复实现。
3. 完成当前 mX 的代码、自动测试与 GPU 实测并记录 Validation、截图路径/数值、关键哈希、失败点。
4. 更新 `progress.md` 的当前阶段、下一原子操作；仅在全部 Acceptance Criteria 通过时推进。
5. context compaction 后只用 README + progress + 当前 mX 恢复。
