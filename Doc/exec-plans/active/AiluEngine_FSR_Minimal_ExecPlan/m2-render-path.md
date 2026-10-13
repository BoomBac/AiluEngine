# M2 — Camera Resolution / RenderGraph / DX12 Dispatch

## Purpose

在**只影响主相机/Scene Camera**、不改其他渲染特性的前提下，跑通 FSR 3.1.5 Quality 的最小完整链路，并在 Scene View 相机面板提供最小 FSR 选择与状态反馈。

## Scope

- 让 FSR 参与 `RenderPipeline::Render()` 的同帧解析：RenderSize → Jitter → 实际 RenderGraph。
- 在现有 `StreamlineSrPass` 基础上实现公共 `SuperResolutionPass`，按已解析模式分派 DLSS 或 FSR DX12 backend。
- 接通 Color、Depth、MV、输出 Texture、Reset、帧时间、Jitter，并建立 Camera 对应的 FSR Context。
- 处理 RG read/write 状态、NativeCmdList、DX12 dispatch 失败后下一帧安全回退。
- 保证 FSR 和 TAA 互斥；FSR inactive 时保留既有 TAA/Spatial 路径。
- Scene View Upsampling selector 提供 FSR；质量行和 active/inactive 尺寸状态按 FSR/DLSS 当前选择显示。

## Non-goals

不创建 Reactive/T&C Mask、不重写 `motion_vector.hlsl`、不扩展到非 Scene/Main Camera、不改 DLSS 的 SDK 实现、不做多相机 Context LRU/完整性能优化。Scene View 只提供 FSR Quality；额外质量档位与更完整的 UX 留待 M3。

## Architecture Constraints

- **最少抽象**：首选 `Render::SuperResolution` 静态/命名空间入口 `ResolveCamera()`、`Evaluate()`，后端分发到原 `StreamlineSR::Evaluate()` 和 `FsrDx12Backend::Evaluate()`。不要引入通用接口继承树。
- Camera 最终渲染模式与用户选择分开：`EResolvedTemporalMode::{kSpatial,kTAA,kDlssSR,kFsrSR}`。用户选择可由一个 `ESuperResolutionMethod{kOff,kDlss,kFsr}` + 一个质量枚举控制，或者保留已有 `DlssMode()` 添加 FSR 设置（但必须强制互斥）；优先选择改动最少、无二义性的实现。
- 保留旧 `DlssMode()` 公共 API 和原 SceneView 逻辑，避免不必要破坏；如果用新通用选择字段，需要提供明确兼容映射并避免双配置状态。
- `Camera::RenderSize()` 必须对 **FSR 和 DLSS** 使用 resolved size；`_anti_aliasing` 字段用于 fallback，不能决定已激活的 FSR Jitter/运行 TAA。
- 只在 `SuperResolution::ResolveCamera()` 返回且 `Camera::PrepareRenderFrame()` 开始前确定模式；不允许下一帧才改 `_resolved_temporal_mode` 却在本帧绘制过程中更改 Camera 尺寸。

## Baseline

- `RenderPipeline.cpp` 当前逐相机：`StreamlineSR::ResolveCamera(*camera)` → `camera->PrepareRenderFrame(...)`。
- `Camera.h` 的 `RenderSize()` 仅识别 `kDlssSR`，`RefreshRenderMatrices()` 仅识别 TAA/DLSS。
- `StreamlineSrPass.cpp` 在 `kBeforePostprocess + 10`，`PostprocessStack.cpp` 在 `+25`。
- 已有两段 RG Pass：先空间 Blit 到 RGBAHalf fallback，再外部 NativeCallback 运行 DLSS，成功覆盖 output，结果 `_upscaled_color`。
- `TemporalAA.cpp` 目前只对 DLSS 排他，`Renderer::PrepareCamera()` 的 Jitter 验证也未识别 FSR。
- MV shader：`cur_ndc - pre_ndc`，UV 范围，Y 取反，无 Jitter。FSR 当前帧 → 上帧，要求负尺度。
- `RenderingData.h` 已有 `_rg_handles._color_tex/_depth_tex/_motion_vector_tex/_upscaled_color`；`Renderer.cpp` 的 MV 纹理为 RGBA16F。

## Target Outcome

运行时通过临时开关/已有调试接口把 Scene Camera 设为 FSR Quality；1920×1080 输出时低分辨率约 1280×720（根据 FSR Quality ratio 与像素舍入），经过 `_upscaled_color` 走既有 Bloom / Tonemap / FinalBlit。Camera/MV/历史重置正确，不破坏 DLSS/TAA。

## Relevant Files

1. `Engine/Inc/Render/Camera.h`, `Engine/Src/Render/Camera.cpp`。
2. `Engine/Src/Render/RenderPipeline.cpp`。
3. `Engine/Inc/Render/Features/StreamlineSrPass.h`, `Engine/Src/Render/Features/StreamlineSrPass.cpp`（改名为公共 Pass 或在本阶段逐步迁移）。
4. `Engine/Src/Render/Features/TemporalAA.cpp`；`Engine/Src/Render/Renderer.cpp` 与 `Renderer.h`。
5. `Engine/Inc/Render/StreamlineSR.h`, `Engine/Src/RHI/DX12/StreamlineDx12Bridge.cpp`（移动/复用现有 Resolve 逻辑，**尽可能不改 Evaluate**）。
6. `Engine/Inc/Render/RenderingData.h`、`Engine/Src/Render/Features/PostprocessStack.cpp`（核对，预计不用改）。
7. `Engine/Src/RHI/DX12/FsrRuntime.cpp`, `FsrDx12Backend.cpp` 等 M1 新文件。
8. `Engine/Src/RHI/DX12/D3DCommandBuffer.cpp`、`Engine/Src/Render/RenderGraph/RenderGraph.cpp`（状态行为核对，不默认修改）。

## Milestones

M2；M1 已通过之后执行。

## Current Milestone

**M2**。

## Implementation Steps

### S1. 相机请求与解析（先实现，勿先造 Pass）

1. 明确一个用户请求模式（kOff/kDlss/kFsr），互斥而不共存。对 `DlssMode()` 保留兼容；FSR 初期只支持 Quality，可用默认质量字段。
2. 扩展 `EResolvedTemporalMode` 添加 `kFsrSR`，以及 `IsTemporalUpscalingActive()`、`IsFsrSRActive()` 这样的简单判断；`RenderSize()` 在 `kDlssSR` 和 `kFsrSR` 返回 `_resolved_render_size`。
3. 将 `StreamlineSR::ResolveCamera()` 当前的资格条件抽到 `SuperResolution::ResolveCamera()` 的适当位置：`_is_scene_camera`/Main/Scene、无外部 TargetTexture、`_postprocess_enabled`、无 custom VP、输出尺寸有效、DX12 Device 可用。DLSS 原有逻辑/失败 Map 可留在专属 helper，避免搬运过多代码。
4. 对 FSR Quality 从 SDK `ffxQuery` 的质量/ratio query 得到 render size，SDK 查询不可用可显式使用该版本文档 Quality 1.5x 计算并校验边界，**不得叠加** `Camera::RenderScale()`；奇数尺寸/最小尺寸按 SDK 要求收敛。FSR 内部创建 Context 的 maxRenderSize/maxUpscaleSize 要匹配当前尺寸。
5. 所有失败/资格不满足按原 AA 选择 `kTAA` 或 `kSpatial`，resolved size 使用 `ConfiguredRenderSize()`；`ResolveTemporalMode` 在实际模式、尺寸、可用性或质量改变时触发历史 reset revision。
6. `RenderPipeline.cpp` 用新解析入口**替换**旧 `StreamlineSR::ResolveCamera(*camera)`，仍在 `PrepareRenderFrame()` 前。不能额外保留旧解析，避免“FSR 解析之后被 DLSS 覆盖”。

### S2. Camera Jitter + TAA 互斥

1. `Camera.cpp::RefreshRenderMatrices()` 使用 `(_anti_aliasing == kTAA && resolved == kTAA) || IsTemporalUpscalingActive()` 判断。确认 Spatial（包括回退路径）不产生超分 Jitter。
2. `Renderer.cpp::PrepareCamera()` 的调试 Jitter 判断同样统一。
3. `TemporalAA::AddRenderPasses()` 若 `kDlssSR` 或 `kFsrSR`，不 Enqueue TAA 的 prepare/execute pass，并失效 TAA 历史（沿用现有 DLSS 分支）。
4. FSR 不使用已 TAA 过的颜色；输入 `_color_tex` 必须是 Tonemap 之前的 Linear HDR 场景颜色。
5. 不改现有 `ProcessJitter()` 序列也可先验收，但必须确认非零抖动、Y 轴方向和实际渲染投影相符。若 Quality 时鬼影明显，再按官方 `ffxQueryDescUpscaleGetJitterPhaseCount/GetJitterOffset` 优化（不得在本阶段展开与渲染无关重构）。

### S3. 共用 RG Pass

1. 将 `StreamlineSrFeature`/`StreamlineSrPass` 调整成轻量 `SuperResolutionFeature`/`SuperResolutionPass`，注册位置仍为原 SR Feature，**只保留一个** SR Feature；旧类是否留兼容 alias 依据真实引用量决定。
2. 在 `OnRecordRenderGraph` 使用 `_color_tex/_depth_tex/_motion_vector_tex`，输出仍为 output size 的 RGBAHalf + UAV。
3. 保留已存在空间 Blit fallback Pass，再加 `SuperResolutionEvaluate` Native Callback（读取 color/depth/motion 并写 output），保证 `PostprocessStack` 最终仍收到 `_rg_handles._upscaled_color`。
4. RG 声明 `builder.Read(..., kReadSRV)`、`builder.Write(output, kWriteUAV)`，切勿把 SDK 所需 barrier 隐藏成“不申报 RG 的原生读写”。确认 `Write` 后用于下游的 RG handle 版本/实际 Texture 正确。
5. Snapshot 当前帧的 render_size/output_size/frame_index/reset_revision/jitter/delta_ms 等参数后再捕获到 callback；可复用动态资源指针，但要遵循 RG resource keep-alive 约定。
6. DLSS 的 Evaluate 原样分派。FFX 不继承/不调用 Streamline 代理逻辑，直接使用 Native DX12 Resource 与 `D3DCommandBuffer::NativeCmdList()`。

### S4. FSR DX12 Context 与 Dispatch

**创建 Context（每相机 / 输出尺寸 / 最大渲染尺寸 / 影响 Context 的 flags）。**

- 把 SDK 版本扩展 descriptor + 选定 provider override + DX12 backend descriptor + Upscale create descriptor 正确链入；最小 flags：HDR / Auto Exposure / Reverse-Z（仅实际反向深度时），**不要**添加 Depth Infinite（当前 Camera 有有限 near/far）。
- 用当前 native Device 创建；Context 生命周期由 Runtime/Backend 管理，首帧创建而不是每帧创建；`Camera::HashCode()` 只是身份候选，若存在复用/碰撞或 hash 生命周期风险，应结合代际 token。
- Context 尺寸或 flags 改变重新创建，但执行 GPU fence 安全释放；错误时记住当前相机/模式失败，下一帧退回 TAA/Spatial，避免每帧重新失败。

**每帧 Dispatch 最小必需输入：**

| FSR `ffxDispatchDescUpscale` | AiluEngine 来源 / 约定 |
| --- | --- |
| `color` | `_color_tex`，render resolution，Linear HDR，Tonemap 前 |
| `depth` | `_depth_tex` 的 R32F depth copy，按 Camera 实际 depth convention |
| `motionVectors` | `_motion_vector_tex`，当前 render resolution，UV current − previous；**Scale = (-render_width, -render_height)**（需验收） |
| `output` | OutputSize 的 RGBA16F UAV |
| `jitterOffset` | `GetJitter().zw * render_size`，像素偏移；与实际 jitter 投影方向验证 |
| `motionVectorScale` | 本引擎输入的 `(-render_width, -render_height)`，不是 SDK 默认 +1，不能沿用 DLSS 的 `mvecScale=(1,1)` |
| `renderSize` | 当前 Camera resolved render size |
| `frameTimeDelta` | 引擎当前帧间隔换算毫秒（具体 TimeMgr API 实测） |
| `reset` | 首帧 / `GetTemporalHistoryResetRevision()` 改变 / 相机切换、尺寸改变后第一帧 true |
| `cameraNear`, `cameraFar`, `cameraFovAngleVertical` 等 | 按锁定 SDK 的结构和相机投影填写；`FovH()` 命名代表水平 FOV，若 SDK 需要**垂直 FOV**须按 Aspect 转换，不可直接照抄 |
| `preExposure` | 无 pre-exposure 时 1.0f；根据 SDK 当前结构确认 |
| `exposure` / `reactive` / `transparencyAndComposition` | 第一阶段可省略/空资源（使用自动曝光） |

**资源状态与 Native Callback：**

- 各输入可作为 `NON_PIXEL_SHADER_RESOURCE`（或符合 SDK 要求的读取状态），output 为 `UNORDERED_ACCESS`；验证通过 RG 录制的 resource state 与 SDK Dispatch 的真实 resource state 一致。
- 用 SDK 所规定的 DX12 `FfxApiResource` 构造 helper，把 Resource 格式/width/height/mip 等传对；Native handle 获取可以参考 `StreamlineDx12Bridge::GetNativeResource`，但不要直接依赖 Streamline 类型。
- SDK 自行录制的 resource transitions、UAV barrier 必须计入状态校验；如果 SDK 的外部资源出口状态与 RG 假定不一致，则用**明确且唯一**的边界修复方案更新实际/跟踪状态，不重复转换。确认 SDK Dispatch 是否影响 DX12 descriptor heaps、root signature 等命令列表缓存，并在后续 Draw/Dispatch 恢复必要状态。
- 对 CommandList、Device、Resource 的引用计数/生命周期做清晰约束。dispatch 结果在 callback 中记录明确错误码；错误不能导致继续使用缺失的 `_upscaled_color`。

### S5. 成功、失败和下一帧切换

1. FSR 未准备好时，在 Camera 解析阶段就回退；不 enqueue FSR Evaluate。
2. FSR Dispatch 在帧内返回失败时，记录 camera/mode/error，下一帧的 `ResolveCamera()` 选 Spatial/TAA 并 reset 历史；限制同一错误的日志频率。
3. 现有 fallback Pass 会先填充 output；但 SDK **在写入中途失败**时可能产生部分像素覆盖，不能保证同帧全正确，允许首版仅保证下一帧稳定回退。不要为极端情况引入额外完整帧复制/双缓冲过度设计。
4. 若输出为0/输入纹理尺寸不符/格式不支持，直接禁用当前 FSR 尝试；不 crash/assert 退出正式版。

## Acceptance Criteria

- [x] 通过 M1；M1 smoke 确认 provider 为 **FSR 3.1.5**，非 FSR4。本阶段 FSR SDK-on dispatch 尚未运行。
- [ ] Scene View Upsampling selector 的 FSR 选项及质量/状态行已实现并编译；仍需 Editor 交互确认切换、fallback 与 active 尺寸显示。
- [ ] 可以用一个开发期开关激活 Scene/Main Camera 的 FSR Quality；输出分辨率正确，render width/height 比 output 小。
- [ ] FSR 与 TAA 不同时运行；Spatial/TAA/DLSS 可以恢复，Camera Jitter 不跨模式污染。
- [ ] 静止场景无持续明显闪烁、相机左右旋转不出现整屏拖影/方向相反的鬼影；移动物体有正确 MV 方向（必要时输出日志/截图验证 MV 解码）。
- [ ] 断言、DX12 Debug Layer、资源状态验证无外部资源状态错误；后续 Bloom/Tonemap/FinalBlit 正常。
- [ ] Camera Cut/`MarkTemporalHistoryReset()` 使 FSR reset 一次且只在该次突变帧生效。
- [ ] 取消 FSR 或故意制造不可用情况，不再提交对应 SDK Dispatch，并正确回退。
- [ ] 完成 `progress.md` 中的详细设备/运行/截图/日志/PIX 记录。

## Progress

M2 implementation and the SDK-enabled full Debug preset build are present. The local Editor loaded the SDK and found
FSR 3.1.5 provider `17700776140660019205`, then crashed on its first rendered frame in
`Register::View<PostprocessVolumeComponent>()` before FSR dispatch. Image/GPU acceptance remains pending; see
`progress.md` for evidence and the blocker.

## Decision Log

M2 使用较薄的非多态分发入口；M3 再增加 UI 和多档位管理。为兼容旧代码，DLSS 动态桥接尽量原样保留。

## Surprises & Discoveries

待填：真实 `ffxDispatchDescUpscale` 结构和 RG barrier 协议必须通过 SDK headers 与 GPU capture 核验，不能用文档推测行为。

## Validation

- 构建：引擎 DX12 Debug/Release，SDK 宏开/关；DLL 缺失。
- 场景：静态室内、转动相机、移动物体、Camera Cut。
- 图形：用 RenderDoc / PIX 检查颜色/深度/MV 的实际尺寸、格式、Barrier/资源状态、输出是否正确进入后处理。
- 记录：在 `progress.md` 写明每个测试结果，截图/日志路径和 FPS 只是观察值，勿在无数据时声明性能收益。

本次补充验证：`AILU_WITH_FSR=ON` 的 `cmake --build --preset build-debug` 全量构建通过；Editor 启动日志确认 SDK 2.3.0 与 FSR 3.1.5 provider 可用。首帧在 `Register::View<PostprocessVolumeComponent>()` 崩溃，故 FSR dispatch、画面、资源状态、MV 方向与历史 reset 尚未验证。

## Known Follow-ups

多相机同时启用、大规模 Resize/context 池、编辑器 UI 与三档 Quality、Reactive Mask 由 M3 或后续处理。

## Outcomes

SDK-on 编译和 provider 初始化通过；首帧 ECS 崩溃阻止 FSR dispatch；FSR 场景与 GPU 验收待完成。

## Resume Protocol

阅读 README + progress + 本文件；按 S1–S5 实施；每个 Acceptance 项都拿到依据后才允许进入 M3。
