# AiluEngine — FSR 3.1.5 Minimal Integration ExecPlan

> 基线：`AiluEngine_Source(20261013-031318).zip`，核对日期 2026-10-13。
> 使用方法：执行者先阅读本文件和 `progress.md`，然后**只加载 Current Milestone 对应的 `mX-*.md`**。每个 Milestone 完成、通过验收并更新 `progress.md` 后才允许进入下一阶段。

## Purpose

以最小侵入方式在 AiluEngine（DX12）接入 **AMD FSR Super Resolution 3.1.5（非 ML 时域超分，仅 SR）**，和现有 DLSS Streamline 并存，复用 RenderGraph、Color/Depth/Motion Vector、Camera Jitter、时域历史重置及 `_upscaled_color` 后处理入口。

## Scope

- 引入 FidelityFX SDK 2.x 的 DX12 **Upscale API**，运行时加载 SDK 官方签名 DLL，明确选择 FSR 3.1.5 provider。
- 在已有相机准备时机统一解析普通空间放大 / TAA / DLSS / FSR；分辨率和 Jitter 同帧一致。
- RenderGraph 使用现有 SR 节点位置接入 FSR；新 backend 不污染通用 RHI 和 RG API。
- Quality / Balanced / Performance；保留 DLSS，编辑器可选择并显示实际激活结果。
- 处理缺失 SDK、功能不支持、Dispatch 失败、窗口变化、Camera Cut、历史资源和 Context 的生命周期。

## Non-goals

- 不接入 FSR Frame Generation、FSR 4 ML、Ray Regeneration、Streamline FSR 适配或 Vulkan。
- 不修改 DX12 Device / DXGI Swapchain 为 FSR Proxy；不重写 RenderGraph、TAA 或 Motion Vector shader。
- 首轮不实现 Reactive/Transparency & Composition Mask、Mip LOD Bias、透明物体 MV 补绘及细致画质优化（仅留追踪项）。
- 不为只含 SR 的工作引入大型超分插件系统、接口工厂、多级多态/抽象层。

## Architecture Constraints

1. **SDK 隔离**：SDK 头文件只进入 `Engine/Src/RHI/DX12/Fsr*.cpp`（及必要的私有头）；通用 Camera/RenderFeature 不依赖 FFX 类型。
2. **不改已有 DLSS 路径的行为**：`StreamlineRuntime`、Device/Factory/Swapchain 现有 hook 保持原状；将 `StreamlineSR::ResolveCamera()` 的相机解析迁到公共入口后，不得再重复解析一遍。
3. **单一实际模式**：一帧仅选择 kSpatial / kTAA / kDlssSR / kFsrSR 之一；SDK 不可用时根据 Camera 原有 AA 设置回退到 TAA 或 Spatial；不能出现“设置 FSR、却没有 Jitter 或误运行 TAA”的半激活状态。
4. **先解析、后刷新矩阵**：`RenderPipeline::Render()` 已在 `Camera::PrepareRenderFrame()` 之前调用 `StreamlineSR::ResolveCamera()`；新 `SuperResolution::ResolveCamera()` 必须替代这个位置，**不是**只在 RG Pass 末端选择 FSR。
5. **RHI 状态真相**：用 RG 声明 color/depth/MV 的 SRV 读、output 的 UAV 写；外部 SDK 可能录制内部资源屏障，必须验证 Dispatch 前后真实状态与 RG 状态跟踪一致。禁止靠额外隐式 barrier“碰运气”。
6. **时间与线程**：Native callback 延后执行，尽量快照本帧参数而非长期持有可变 Camera 指针；Context 必须与相机身份、尺寸/质量、GPU fence 生命周期匹配。禁止 SDK Context 每帧创建/销毁。
7. **最小依赖**：使用签名的 `amd_fidelityfx_loader_dx12.dll` + `amd_fidelityfx_upscaler_dx12.dll`，不得把 SDK 示例工程、Cauldron、FG 依赖打包到引擎。动态加载 `ffxCreateContext/ffxDestroyContext/ffxDispatch/ffxQuery/ffxConfigure`；**不要混用**需要 `.lib` 直接链接的 C++ wrapper 调用。
8. **编码风格**：本工程现有代码风格优先；新增变量小写下划线、成员 `_lower_snake_case`、静态变量 `s_...`、常量 `kCamelCase`、类型与函数 PascalCase，尽量按 160 列格式化。

## Baseline

以下为本次压缩包内已验证事实（不要靠旧任务包猜测）：

- `Engine/Src/Render/RenderPipeline.cpp`：每个 Camera 在每帧 `StreamlineSR::ResolveCamera(*camera)`，然后 `camera->PrepareRenderFrame(...)`。
- `Engine/Inc/Render/Camera.h`：`EDlssMode`、`EResolvedTemporalMode{kSpatial,kTAA,kDlssSR}`、`ResolveTemporalMode(...)`、`RenderSize()`；`RenderSize()` 当前仅在 DLSS 激活时使用 `_resolved_render_size`。
- `Engine/Src/Render/Camera.cpp`：`RefreshRenderMatrices()` 仅在 TAA/DLSS 时生成 Jitter；`ProcessJitter()` 的 `GetJitter().zw` 是 UV 偏移，MV shader 使用无 Jitter VP。
- `Engine/Src/Render/Renderer.cpp`：独立创建 `_color_tex`、`_depth_tex`（RFloat）、`_motion_vector_tex`（R16G16B16A16_FLOAT）；注册 `TemporalAA` 与 `StreamlineSrFeature`；`PrepareCamera()` 的诊断判断也仅识别 TAA/DLSS。
- `Engine/Src/Render/Features/StreamlineSrPass.cpp`：事件为 `kBeforePostprocess + 10`，创建 RGBAHalf UAV 输出；通过 `RecordNativeCallback` 交给 DX12；把输出写入 `_rg_handles._upscaled_color`。已有前置空间 Blit fallback，改造时保持行为。
- `Engine/Src/Render/Features/PostprocessStack.cpp`：优先读取 `_upscaled_color`，事件 `kBeforePostprocess + 25`；不需改后处理链。
- `Engine/Src/Render/Features/TemporalAA.cpp`：仅在 DLSS 激活时跳过 TAA，FSR 激活也必须跳过/失效 TAA 历史。
- `Engine/Src/RHI/DX12/StreamlineDx12Bridge.cpp`：含 DLSS 资格与分辨率解析、SDK Evaluate、资源转换、失败模式禁用；**不要把 FSR 错误混入其 DLSS failed mode Map**。
- `Engine/Res/Shaders/hlsl/motion_vector.hlsl`：MV `cur_ndc - pre_ndc`，`y` 取反，再乘 `0.5`，即 UV 范围 current - previous。FSR 需要 **previous - current**，可在 Dispatch 用 `(-render_width, -render_height)` 作为 `motionVectorScale`，无需改 shader；仍需运动测试确认 Y 轴。
- `Editor/Src/Widgets/RenderView.cpp`：SceneView Upsampling 下拉只有 Bilinear / Point / DLSS；DLSS Quality 单独选项。普通 Camera Inspector 另见 `Editor/Src/Inspector/ComponentEditors/CameraComponentEditor.cpp`。
- **源码压缩包只有 Engine / Editor 源文件，未包含完整工程生成脚本、SDK 二进制或可执行工程**；此环境无法替代 Windows DX12 编译和 GPU 验收。

## Target Outcome

- 主场景相机选择 FSR Quality/Balanced/Performance 后，HDR Linear Color 从低分辨率经过 FSR 输出到 presentation 分辨率；不影响现有 Bloom/Tonemap/FinalBlit。
- 相机旋转、静态几何、动态物体 MV 和窗口变化时输出稳定；既有 DLSS/TAA/Spatial 正常。
- 缺少 DLL 或设备不支持时明确记录原因、显示 inactive、自动安全退回，不影响程序启动。
- 通过最小路径提交，没有无关大重构；留下可复验的构建、GPU debug 和截图/录屏结果路径。

## Relevant Files

| 文件 | 作用 |
| --- | --- |
| `Engine/Inc/Render/Camera.h`, `Engine/Src/Render/Camera.cpp` | 选择、实际时域模式、Jitter/RenderSize/Reset |
| `Engine/Src/Render/RenderPipeline.cpp` | **模式解析时机** |
| `Engine/Src/Render/Renderer.cpp`, `Engine/Inc/Render/Renderer.h` | Feature 注册、资源规格和调试 |
| `Engine/Inc/Render/Features/StreamlineSrPass.h`, `Engine/Src/Render/Features/StreamlineSrPass.cpp` | SR RG Pass 模板，目标为轻量 `SuperResolutionPass` |
| `Engine/Src/Render/Features/TemporalAA.cpp` | TAA 互斥 |
| `Engine/Src/Render/Features/PostprocessStack.cpp` | 参考（预计无需改） |
| `Engine/Inc/Render/RenderingData.h` | `_upscaled_color` 及 RG handles |
| `Engine/Inc/Render/StreamlineSR.h`, `Engine/Src/RHI/DX12/StreamlineDx12Bridge.cpp` | DLSS 现有解析/评估，需无行为退化 |
| `Engine/Inc/RHI/DX12/D3DContext.h`, `Engine/Src/RHI/DX12/D3DContext.cpp` | DX12 Device、Runtime 生命周期 |
| `Engine/Inc/RHI/DX12/D3DCommandBuffer.h`, `Engine/Src/RHI/DX12/D3DCommandBuffer.cpp` | NativeCmdList / barrier |
| `Engine/Res/Shaders/hlsl/motion_vector.hlsl` | 约定参考（原则上不修改） |
| `Editor/Src/Widgets/RenderView.cpp`, `Editor/Inc/Widgets/RenderView.h` | 场景相机 SR 选项 |
| 新增 `Engine/Inc/Render/SuperResolution.h`, `Engine/Src/Render/SuperResolution.cpp`（按工程实情决定是否单独需要） | 后端无关 Resolve/Dispatch 入口 |
| 新增 `Engine/Inc/RHI/DX12/FsrRuntime.h`, `Engine/Src/RHI/DX12/FsrRuntime.cpp` | SDK API 动态加载、版本/能力、Context |
| 新增 `Engine/Inc/RHI/DX12/FsrDx12Backend.h`, `Engine/Src/RHI/DX12/FsrDx12Backend.cpp` | FFX resource 和 DX12 dispatch 桥接；如果很短可并入 Runtime |

文件名只是预期最小变更面，执行时以实际工程规则和既有布局为准，**不要为凑文件名增加空壳**。

## Milestones

| Milestone | 文件 | 独立交付/验收 |
| --- | --- | --- |
| **M1 SDK Bootstrap** | `m1-sdk-bootstrap.md` | SDK 动态加载、准确 FSR 3.1.5 provider 探测、失败不阻止正常渲染；编译检查 |
| **M2 SR Render Path** | `m2-render-path.md` | Main/Scene Camera FSR Quality 出图，RGB HDR/Depth/MV/Jitter/Reset 正确，TAA 互斥、RG 状态安全 |
| **M3 UX & Robustness** | `m3-stability-and-editor.md` | 三档质量、Editor、尺寸/模式切换、Context 回收、DLSS/TAA 回归通过 |

## Current Milestone

**M2 SR Render Path**。M1 SDK Bootstrap 已完成；详见 `progress.md` 与 `m2-render-path.md`。

## Progress

总状态：`NOT STARTED`。唯一进度真相为 `progress.md`，本文件不重复保存逐条完成进度。

## Decision Log

- D1：选择 SDK 2.x 公共 API + **FSR 3.1.5 provider**，而不是把默认 provider 当成 FSR 3（默认可能为 FSR 4）。
- D2：**不以 Streamline 包装 FSR**；两者共享最薄 RG Pass 和 Camera 解析，后端独立。
- D3：先执行 SDK/Capability 的独立验收，再打通最小场景，然后编辑器与稳定性；Reactive Mask 延后。
- D4：Quality 缩放按官方 Query / Ratio 计算（典型 Quality 1.5x、Balanced 1.7x、Performance 2.0x），不复制 DLSS optimal size；用户手动 RenderScale 不应同时叠乘 FSR 比例。
- D5：SDK 未接通时保留已知可用的空间放大路径；若已有 TAA 设置则按现有 TAA 配置回退。

## Surprises & Discoveries

- 最关键的 Camera 解析入口不在 SR Pass，而在 `RenderPipeline::Render()`。
- 现有 `PostprocessStack` 已兼容输出分辨率不同于渲染分辨率，改造无需新颜色通路。
- 现有 `StreamlineSREvaluate` 在 RG 中和 fallback Pass 顺序写同一输出；FSR 可以复用这一策略，但 SDK 已开始写 UAV 后报错可能留部分错误像素，因此**无法保证当帧无瑕 fallback**，必须下一帧禁用故障路径并重置历史。
- SDK 2.x 官方 `ffx_api.h` 允许 DLL 动态加载，但 C++ helper API 通常要求 `.lib` 链接，不能直接复制其 `ffx::CreateContext(...)` 示例到纯动态加载路径。
- 当前相机配置的 DLSS 状态尚未进入一般 Scene Camera 序列化记录（该源码仅看到 RenderScale/UpsamplingFilter 的写入）。本任务仅要求编辑器即时切换；持久化另记 follow-up。

## Validation

每个里程碑的编译、运行和 GPU 验证按单独文件执行，需填入 `progress.md`：命令、配置、设备、驱动、结果、截图/日志/RenderDoc或PIX 证据。无法实测时一律标明 `NOT RUN`，不能宣称通过。

全局回归基准：无 SDK/有 SDK；Spatial / TAA / DLSS；FSR Quality；动态物体、相机旋转、Camera Cut、不同窗口分辨率；DX12 Debug Layer；native callback / SDK barrier；切换后无持续性内存增长。

## Known Follow-ups

- FSR Reactive Mask/T&C Mask；透明粒子/水体 Motion Vector；Mip LOD Bias；sharpness 策略。
- 质量升级至 FSR 4 或 FG，单独开计划；不在本包顺手接入。
- 相机/场景配置序列化 SR 选择与质量档位，待工程格式确定后再单独评估兼容性。
- 更多 Camera 同时超分时的 Context 上限和长期淘汰策略；本轮首先确保 Main/Scene 稳定。

## Outcomes

交付结果以实际实施为准：代码变更、编译与运行记录、稳定性测试；当前尚未实施，禁止提前填写“完成”。

## Resume Protocol

1. 读取 `README.md` + `progress.md`；定位 `Current Milestone`，**只**加载对应 `mX-*.md`。
2. 对照实际仓库确认 Baseline 是否变化；若文件/接口变动，在 `progress.md` 记录新发现和决策；不要修改过去验收事实。
3. 实现当前 Milestone；优先编译、后端最小实测；运行 Acceptance Criteria，不允许以“代码已写”代替验收。
4. 更新 `progress.md` 的 Progress、Decision Log、Surprises & Discoveries、Validation、Blockers、Next Step、Outcomes。
5. 当前里程碑全部通过后，**才**切换到下一文件；遇到 SDK/构建缺失记录阻塞，不虚报完成。

### 官方资料（固定阅读入口）

- FidelityFX SDK 仓库：<https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>
- SDK 2.x 动态库/API：<https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/getting-started/ffx-api.md>
- FSR 3.1.5 的 Color/MV/Depth/Jitter/Reset 输入约定：<https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/>
- 实施 SDK 中的函数、descriptor、flags 均以**实际锁定 SDK 版本的头文件**为准，不混用旧版示例；在 `progress.md` 记录 SDK 版本/包名与提供的 signed DLL 哈希。
