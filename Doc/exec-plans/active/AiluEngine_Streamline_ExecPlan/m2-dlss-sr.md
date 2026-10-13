# M2 · DLSS Super Resolution 画面闭环

## Purpose

在 M0/M1 的底层已通过验收基础上，用最少 Renderer/Camera/Postprocess 修改把 `DLSS SR` 真正接进 RenderGraph：低分辨率场景输入、正确 Depth/MV/Jitter/FrameToken、按官方建议尺寸执行 SR、全分辨率后处理和 FinalBlit。保持普通 TAA 完整有效。

## Scope

### A. 每个 Camera 的单一“有效时域模式”决策（低侵入）

先建立一个轻量的每 Camera `ResolvedTemporalMode` 或 `ViewUpscaleConfig`（概念名，不要求额外复杂 class），由渲染准备阶段**一次性**决策：

- 用户请求的 Upscaler 模式（Off、DLSS SR Quality/Balanced/Performance；DLAA 如果 SDK 同一路线允许才添加）。
- SL Runtime/Device 对该 Feature 的支持、Cam 是否可用、是否 DX12+RG、是否存在合适 output target。
- 最终有效模式：`TAA` / `DLSS SR` / `spatial fallback`，以及 `render_size/output_size` 和 `reset_history`。
- 失效时按原用户设置退回 TAA 或原 Blit，**有效模式必须在 Camera projection jitter 与 Renderer::BeginScene 分配纹理之前确定**。

实现建议：

1. 不直接把 DLSS 塞进 `EAntiAliasing::kTAA` 内部；可在 Camera 上增加最小 `EUpscaleMode` 或少量 DLSS request 字段，和原 TAA 参数互不污染。
2. Camera 里的 `RenderScale()` 表示用户配置；DLSS 的 SDK optimal render size 应作为**当前帧有效渲染尺寸覆盖**，不能不可逆地把用户保存的 RenderScale 写回。
3. 修改 `Camera::RefreshRenderMatrices()` 的判定，让“有效 TAA 或有效 DLSS”生成 jitter；无效 DLSS 不允许残留 jitter。
4. 修改 `TemporalAA::AddRenderPasses()` 的调度判断，而不是拆解 TAA compute 内部；DLSS 生效时**不得**再登记 TAA 的 opaque snapshot / execute pass。
5. Main/Scene Camera 初版启用，非 RG、Custom VP、反射/烘焙 Camera 暂时强制 fallback 并明确日志；已支持的多 Camera 不共享 SR history。

### B. DLSS 分辨率与输出

- 构造目标 `sl::DLSSOptions`，调用 `slDLSSGetOptimalSettings()` 查询实际 `renderWidth/renderHeight`；在 Camera matrix jitter、GBuffer 和颜色/depth/MV 分配前传播该尺寸。
- `output_size = cam.OutputSize()`，来自真实目标纹理或主窗口。需验证整数边界、Resize 期间最小 1×1、防止 0 大小。
- 输出图建议通过 `RenderGraph::CreateResource(TextureDesc...)` 创建 output-size HDR UAV/SRV 资源：format/usage 按 SDK 校验，不直接假设所有 HDR RT 格式都允许 UAV。
- 不单独维护一个临时已分配又被 RG 管理的 output pool；先复用 RG 的实际资源分配/保活。
- DLAA 同分辨率模式在后续扩展，不强行绑定到 1.0 RenderScale 的偶然行为。

### C. 接口与 Pass（Feature-specific）

新增 `StreamlineSrPass`，通过现有 Renderer 的 RenderFeature/Pass 体系注册在 `CopyColor` 后、`PostprocessStack` 前的**确定事件点**；主动检查场景色已包含必须参与 SR 的透明/体积效果、无 UI。

- RG setup：`Read` 最终 HDR color（优先经 CopyColor 的 `_color_tex`，须核对实际依赖）、depth、motion、可选 exposure；`Write` output UAV，并声明后续 Postprocess 将读取它。
- RG execute：只调用 CommandBuffer 的 `RecordNativeCallback()`；一切实际 `sl::Resource` / `sl::ResourceTag`、`sl::Constants`、`slEvaluateFeature()` 构造和 native 操作都在 `StreamlineDx12Bridge`。
- 使用 `sl::kFeatureDLSS`、同一 FrameToken/Viewport/Options；逐帧传入有效 color input/output、depth、mvec tags，优先使用 **evaluate-local tags**（便于资源生命周期与实际命令列表状态一致）。Runtime 已启用 `eUseFrameBasedResourceTagging`；未来 FG 的帧级资源在单独模块调用 `slSetTagForFrame`，禁止回到 deprecated `slSetTag`。
- 若 `slDLSSSetOptions`/`slEvaluateFeature` 属于 SDK 非线程安全操作，必须与其它 SL API 调用串行化，且同一 Viewport 同一 FrameToken 按录制/提交顺序发生；局部 tags 的存储至少存活到 Evaluate 返回。
- Streamline `sl::Constants` 必须对应**当前摄像机的真实** depth/mvec/jitter/exposure 约定；无效参数拒绝调用 Evaluate。
- 输入输出尺寸不同但要保持适当 ResourceTag extents 和实际资源尺寸相符；所有 tags 包括真实 native state。
- SDK 可能修改 D3D12 CommandList state，桥接层使用 M1 已验收的恢复策略；紧跟 SR Pass 的 Draw/Dispatch 不得误用旧 PSO/root/descriptor cache。

**不变式**：只有执行成功并有定义的有效输出，才把 SR handle 作为后处理输入。调用结果失败不能将未写入的 output 当成功；选择有可验证的帧内 fallback：例如 pass 在 Evaluate 失败时用已有命令路径写一张 output-size fallback（需确保运行时 command state 已恢复），或在帧图构建阶段已准备可选择的有效替代输入；下一帧强制关闭 SR 并复原 jitter/尺寸。不要仅在 CPU 标记“下帧关 DLSS”却让当前帧显示垃圾。

### D. 核对实际 MV / Depth / Jitter（修改前必须独立验证）

#### Motion Vector

源码 `Engine/Res/Shaders/hlsl/motion_vector.hlsl::CalculateNdcMotionFormClip()` 当前计算：

```hlsl
float2 velocity = cur_ndc - pre_ndc;
velocity.y = -velocity.y;
velocity *= 0.5;
```

可见输出是“当前位置 − 前一位置”的归一化屏幕 UV 位移，Z 存当前/上一帧 linear depth 差；这一点**不能直接等同**于“Streamline 的所有 motion 参数已经正确”。

- 核对 SDK `mvecScale` 和 motion vector 方向：在官方规定的 DLSS `sl::Constants` 中明确声明单位/包含的相机运动/jitter 约定；只使用 `.xy`，不要误把 `.z` 当 motion depth。
- 检查 `_MatrixVP_NoJitter` / `_MatrixVP_Pre` 的矩阵时间语义；每帧 Jitter 投影应与“不带抖动 MV”约定匹配。
- 测试静态旋转相机、平移相机、运动 mesh、CPU/GPU 蒙皮、alpha-tested 物体、遮挡边界；MotionVector 的空白像素与 Camera MV 覆盖也需核对。
- **不要无依据修改 MV 正负号**；用可视化/重投影预期（`history_uv = current_uv - motion`) 比较后再定是否转换。

#### Depth

- `CopyDepthPass` 将 `_depth_target` 复制/Blit 到 `_depth_tex`（`RFloat`），需要通过 shader/blit 实际代码和像素采样验证内容究竟是 DSV 设备深度还是线性值。
- 对照 SDK `depthInverted`、`cameraNear/cameraFar`、`clipToPrevClip`、投影矩阵坐标系；不可直接因为源引擎有 `_REVERSED_Z` 就假设数据与标记匹配。
- 如果 `_depth_tex` 不是 SDK 预期的表示，优先复用真实 `_depth_target` 的可读 view 或**局部转换**，不要修改所有场景深度消费者的语义。

#### Jitter

- 当前 `Camera::ProcessJitter()` 生成投影/NDC 偏移，并存入 `Vector4f _jitter` 的不同分量；务必根据 SDK 期望转换为**输入渲染像素单位**和正确 Y 方向，不能直接原样传 `_jitter.x/y`。
- 验证第一帧、camera cut、resize、切换质量档位、从 TAA 到 SR、SR 到 TAA 历史重置条件；清除错误历史导致的鬼影。

#### Exposure / HDR

- 场景 Color 要是 ToneMapping **之前**的线性 HDR；与 SDK 选项中的 `colorBuffersHDR/useAutoExposure` 对应。
- 初版优先使用 SDK auto exposure（如果符合该版配置），不要捏造 1×1 exposure texture；未来需要稳定曝光再提供相应资源。
- 注意透明、粒子、云层、体积雾的组合边界；如有 reactive/transparency/composition hint，先预留可选 Tag，M2 非必须。

### E. 最小 Postprocess 适配

- `RenderingData::_rg_handles` 增加一个 `_upscaled_color`（或等价专用 SR 输出）有效句柄，`Reset()` 中清零；TAA 的 `_taa_color` 原路径保留。
- `PostprocessStack::OnRecordRenderGraph()` 仅更改**一次输入选择**：有效 SR Output → 有效 TAA Color → `_color_tex` → `_color_target`；严禁让后处理重写 Scene 的 `_color_target` 指向别处。
- Bloom、ToneMapping 输出尺寸应从 RG 输入 texture 的 `TextureDesc` 或 RG 已知 desc 推导，而不是始终 `RenderingData::_width/_height`。保证现有 TAA 和非 SR 处理行为一致。留意 Bloom 每级下采样的采样步长也必须对应输入尺寸。
- 如果 `cam._postprocess_enabled=false`，初版允许 SR 明确不适用并安全 fallback；不要为了首版覆盖这个分支而扩大修改范围。
- `FinalBlit` 保留现有行为；不再在输出纹理经 DLSS 达到最终尺寸后额外强制降采样。

### F. Feature 切换与失败策略

- UI/API 模式必须表达 `requested` / `available` / `effective`；不可在非 RTX 显示“已启用 DLSS”而实际只是普通 blit。
- `Off/TAA/DLSS` 切换只在 frame boundary 生效；SR 失败立即记录原因并保护有效画面，下帧退回 TAA/Blit。
- 将 `ViewportHandle` 稳定绑定主 Camera/RenderView，而不是每帧生成新 ID；resize/设备变更时通过正确的 GPU fence 和官方 API 释放旧 Feature 资源。

## Non-goals

- 不实现 Frame Generation/Reflex/RR，不改变现有 FG 所需的 ImGui/Present 时序。
- 不重写原 MotionVectorPass、TemporalAA 算法和 Camera 序列化体系；必要时仅增加很小的配置与插入点。
- 不在这一阶段实现自动动态分辨率、按 GPU 时间调整渲染比例、多 Viewport 同时运行 SR。

## Architecture Constraints

- 单独的 SR Pass 仅处理 SR 业务；SDK 对应资源和函数仅在私有桥接中。
- 读取/写入 RG 资源必须有 builder 依赖；SDK 操作的真实 resource state 与 RG tracker 一致。
- 按官方 SDK optimal size 调整当前帧 Scene 渲染分辨率，用户 RenderScale 配置不被覆盖。
- SSR、AO、volumetric、GI 等依旧保持输入的 scene render-size；只有 temporal upscaler 之后的链变为 output-size。
- 同帧 TAA 与 DLSS SR 互斥，不允许重复 jitter。

## Baseline

见 README 的文件表和静态查验结果；M2 开始时应重新检索当前工作树，优先检查 `Renderer::BeginScene()`、`TemporalAA::AddRenderPasses()`、`Camera::PrepareRenderFrame()`、`PostprocessStack`、`CopyColor`/`CopyDepth`、`MotionVectorPass` 的真实事件顺序。

## Target Outcome

支持 RTX 的 DX12/RG 主视口能以 SDK 建议 render-size 生成真实 DLSS SR 输出，输出分辨率为 Camera OutputSize，输入正确，Bloom/Tonemap 能在输出尺寸完成，Editor UI 正常。关闭 DLSS 后 TAA/普通渲染恢复且不残留无效历史。

## Relevant Files

- `Engine/Inc/Render/Camera.h`、`Engine/Src/Render/Camera.cpp`
- `Engine/Inc/Render/RenderingData.h`、`Engine/Src/Render/Renderer.cpp`
- `Engine/Src/Render/Features/TemporalAA.cpp`、`PostprocessStack.cpp`
- `Engine/Src/Render/Features/BloomPass.cpp`、`ToneMappingPass.cpp`
- `Engine/Src/Render/Features/CommonPasses.cpp`
- `Engine/Res/Shaders/hlsl/motion_vector.hlsl`
- 新增 `StreamlineSrPass.{h,cpp}`、扩展 `StreamlineDx12Bridge.cpp`

## Milestones

仅 M2；M2 验收通过才能做 M3。

## Current Milestone

M2（用户于 2026-10-12 明确要求开始实现；M0/M1 的未完成验收继续保留，不视为通过）。

## Progress

- [x] EffectiveTemporalMode 在创建 Camera 矩阵/资源之前得出
- [x] 分辨率使用 SDK optimal settings，不覆盖用户 RenderScale
- [x] Camera Jitter 与 SR/TAA 调度互斥已接入；像素 convention 仍待实测
- [ ] Depth/MV/Color/Exposure 约定逐一验证
- [x] `StreamlineSrPass` RG 读写、输出 UAV 与 Evaluate 调用已接入；资源状态仍待 PIX/Debug Layer 校验
- [x] Postprocess 根据 RG input 实际分辨率建立纹理
- [x] Evaluate 前写入 output-size spatial fallback；Evaluate 失败后保留该 fallback 并下帧退回
- [x] Camera C++ API 可读写 requested/available/effective；Inspector UI 尚未接入
- [ ] Debug Layer/PIX 和真实 RTX 运行验证

## Decision Log

SDK optimal sizes、MV 坐标约定、Depth 取用方案、状态 table 和 fallback 实现须记到 progress。任何时域 Pass 大规模重构都应先证明当前局部修补不可行。

- 2026-10-12：Camera `RenderScale` 保持为用户配置；effective DLSS render size 只保存在当帧解析状态。SR output 使用 RGBA16F，以满足 output UAV 的格式需求。
- 2026-10-12：Evaluate 之前执行 output-size spatial fallback；SL API 与 frame token 通过同一 SDK API mutex 串行化。M1 的 native callback state restore 尚未 GPU 验收，因此当前实现不能据 build 结果宣称状态闭环通过。

## Surprises & Discoveries

- MV `.xy` 是 current−previous UV displacement；其 `.z` 另存线性深度差，不可盲拷 SDK 的矢量尺度范例。
- Postprocess 当前默认是内部渲染尺寸：必须在 DLSS SR 之后局部改用输出尺寸。
- Camera Jitter 的启用判定现在仅识别 TAA，SR 需在视口准备阶段提前决策。

## Validation

**必测组合**：

| 场景 | 期望结果 |
|---|---|
| DLSS Off + TAA On + RenderScale=1.0 | 和基线同等渲染、历史有效 |
| DLSS Off + TAA Off | 原始 Blit、Jitter=0 |
| DLSS Quality / Balanced / Performance | 实际内部尺寸来自 SDK；输出尺寸固定；颜色正常 |
| 平移/旋转相机、移动/蒙皮 Mesh | 无严重 ghosting、motion 方向反转、明显拖影 |
| Alpha test、透明/体积物体、UI | 场景参与 SR，UI 不被错误时域处理；按当前 Pass 合成实际行为记录 |
| 1080p→其他尺寸 Resize | 无纹理错配、旧 history 污染或资源生命周期错误 |
| TAA→SR→TAA→Off（重复） | 输入尺寸、Jitter、历史重置正确 |
| 不支持 RTX/缺 DLL 或 `slEvaluateFeature` 失败 | 可见、合法的 fallback 画面且日志指明原因 |

必须使用 Debug Layer / PIX 核验 raw `ID3D12Resource` state、SL tag、RG barrier；提供每种 SR 模式的源/输出真实尺寸及至少一组 GPU pass 记录。建议同一固定场景记录 TAA 对照截图及性能，并观察 DLSS 加载是否引入额外资源同步阻塞。

### Acceptance Criteria

- [ ] 真实支持的 RTX 设备上 `slEvaluateFeature(sl::kFeatureDLSS, ...)` 返回成功并显示符合预期的不同分辨率输出。
- [ ] Off/TAA fallback 可用，SR 失败不出现未初始化输出。
- [ ] SDK 调用前后 D3D12 state/descriptor/root signature、RG 状态和后续 Draw/Dispatch 无验证错误。
- [ ] MV/Depth/Jitter 输入 convention 实测确认，日志和结果入 progress。
- [ ] Postprocess 输出尺寸正确，TAA 旧路径无回归。

## Known Follow-ups

M3 将覆盖长时间运行、多 Camera、Window Resize、资源持有与 FPS 测试；RR/FG 必须独立阶段重新设计其具体输入。

## Outcomes

SDK options 和 pass 已接入；每种模式实际输入/输出尺寸、D3D12 validation、失败回退图像、截图路径及性能数字待 RTX 实机验收。

### 2026-10-12 · 开启失败修复

- 用户日志：SDK/DLSS SR/plugin 支持检测通过；frame 1140 在 `slAllocateResources` 返回 25
  (`eErrorInvalidParameter`)，随后触发 SR 禁用和 spatial fallback。
- SDK v2.14.1 `sl.dlss/dlssEntry.cpp` 的显式分配使用 `EventData{viewport, 0}`，调用
  `dlssBeginEvent` 后仅检查是否已创建 handle，未传播 begin 返回值。桥接已按实际 FrameToken
  设置 constants/tags，因此显式分配的 frame 0 与当前帧不匹配。
- 移除显式分配调用及两个分配缓存，保留当前帧 constants/tags，让 `slEvaluateFeature` 自动初始化和重建。
  官方 ProgrammingGuideDLSS §8 明确支持 evaluate 时初始化；SDK begin callback 处理 mode/size 变化。
- 验证：`cmake --build --preset build-debug` 退出码 0；`StreamlineDx12BridgeSmoke` 和
  `CameraTemporalHistorySmoke` 退出码 0。Smoke 不覆盖实际 DLSS evaluate 成功或输出质量。
- 本次无运行中的 Editor，未执行 DLSS 开启/Resize/连续帧抓取；M2 实机验收仍 Pending。
- SDK 依据：https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/source/plugins/sl.dlss/dlssEntry.cpp
  和 https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS.md 。

### 2026-10-12 · render scale 0.7 开关 DLSS 后叠加偏移

- 用户截图：场景 cube 与 Editor 网格/灯光图标相对位置明显变化；Quality 截图状态文本仍为 Off，
  因此截图本身不能证明所用 binary 的 evaluate 状态。
- 源码确认：全尺寸 SR/postprocess color 与低尺寸 scene DSV 一起绑定，DX12 `kSetTarget` 分支
  用所有附件的最小尺寸裁剪 viewport/scissor。EditorDebug 还显式设置输入分辨率 viewport。
- 修复：PostprocessStack 在颜色/深度尺寸不一致时创建独立 `PostprocessOverlayDepth`，使用新增 Blit
  depth pass 最近点采样写入 DSV；Gizmo、Pick、EditorDebug 改用 `FinalDepthHandle()`，EditorDebug/Gizmo
  使用 final color 尺寸。场景输入深度、配置 render scale 和 SDK optimal size 决策保持原有职责。
- Shader：`dxc -T ps_6_0 -E PSMainCopyDepth` 编译通过。深度使用原场景深度；时域 jitter 引起的
  亚像素遮挡边缘及实际开关画面仍需实机连续帧验证。
- 验证结果见 progress；本次无运行中的 Editor，尚无修复后的 live capture。

### 2026-10-12 · DLSS Off 黑天空复现与修复

- 用户授权后台启动 Editor；使用实际 `Editor.exe` 的新目录 `DlssSkyRegression_20261012` 保存隔离场景、
  captures、analysis.json 和配置备份，场景来自 `AiluTestProj/Assets/scene/empty.almap` 的副本。
- 首轮纠正本次深度拷贝接入错误：共享 Blit 新增 pass 在 metadata 的 `info end` 之后，未被引擎解析。
  恢复公共 Blit，改为独立 `postprocess_overlay_depth.hlsl`，PostprocessStack 持有 Shader/Material 并使用 pass 0。
  修复后仍能复现黑天空，故该接入错误不能当作天空根因。
- 分层捕获确认：DrawSkyBox 已黑；透射率 LUT 有值（RGB mean=101.242），SkyView RGBA 全 0；
  CopyColor/FinalBlit 传递了黑天空。截图对应的天空问题发生在后处理/Editor overlay 之前。
- 临时每帧 SkyView 更新探针恢复天空；常量快照 frame-upload 假设未通过图像验证，已完整撤回。
  单线程对照未完成启动，无有效结果。
- 最终仅将 Environment.cpp `_sky_lut_dirty=false` 从注册 pass 后移入 execute callback 的 dispatch 后，
  保留原有按需更新。每帧探针已撤回，Shader.cpp 与公共 Blit 没有本次遗留修改。
- 最终 `build-debug` 退出码 0；独立 depth PS DXC 编译通过。正常多线程/graphics-job 配置下，最终
  SkyView 连续 8 帧（3165-3172）RGB mean=42.417、alpha=255；FinalBlit 连续 8 帧（3207-3214）
  896x512、天空上方 ROI mean=152.264，最大相邻像素差=2；实际检查图像确认天空恢复。
- 后台进程已退出，EditorConfig/EngineConfig/dock_layout/main_widget 配置逐字节恢复并校验。
  此次覆盖 DLSS Off 的天空回归，未将 DLSS Quality/MV/Depth/完整 M2 验收标为通过。

### 2026-10-13 · 黑天空的具体触发链复核

- 用户要求定位本次改动的具体触发原因，不能以改变 SkyView dirty 时机代替根因诊断。
  新证据目录：`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/SkyRootCause_20261013_001/`。
- 恢复旧 dirty 行为并加入临时日志：frame 0 注册 `EnvironmentSkyLUT` 后清除 dirty，但整图编译失败，
  没有 SkyView execute 日志。明确错误为 `PickBuffer / PostprocessOverlayDepth is read before its first write`。
- 首帧 Postprocess 输入日志：SR 和 TAA handle 都是 3452816845 (`0xCDCDCDCD`)，实际 scene color 为 6.1。
  错选未初始化 SR handle 后，GetTextureDesc 回退默认 4x4，最终 color=4x4、scene depth=896x512，
  因此 DLSS Off 首帧也进入新增的深度补齐分支。frame 1/2 Reset 后 SR/TAA=0，尺寸恢复 896x512。
- 深度补齐分支忽略 `builder.Write` 返回的新版本，并将创建时的 v0 交给 PickBuffer；v0 没有 producer，
  validation 中止整图。旧 dirty 已清除，后续帧不再生成 SkyView，导致持续黑天空。
- 这是本次新增路径引入的组合回归。原有 SkyView dirty 逻辑不是本次新增的触发点。
- 确定性 regression：在 0xCD 填充存储上构造 RenderingData，修复前 CameraTemporalHistorySmoke 退出码 1。
- 实际修复：value-initialize `_rg_handles`；将 `PostprocessOverlayDepth` 的最新已写入版本传给后续 overlay。
  SkyView dirty 行为恢复原样，Environment.cpp 最终无本任务 diff。此前帧内常量快照假设已撤回，不能视为根因。
- 冷启动对照保持旧 SkyView dirty 行为：frame 0 SR/TAA=0、color/depth=896x512、图编译成功，
  有 `EnvironmentSkyLUT` execute 日志。首帧没有 Read-invalid / read-before-first-write 错误。
- 修复后 SkyView 连续 8 帧（531-538），192x192，RGB mean=42.417、alpha=255；FinalBlit 连续 8 帧
  （576-583），896x512，RGB mean=92.110，最大相邻差=2；图像检查确认天空及 cube 环境光均恢复。
  上轮只推迟 dirty 的补偿仍留下 cube 环境光黑暗，因为首次被中止的还有 cubemap/SH 生成。
- CameraTemporalHistorySmoke 的复用内存 regression 修复前退出码 1、修复后退出码 0。
  本次不以之前长期运行正常为否定新增路径回归的依据；直接触发点来自本次句柄选择和新增深度 pass。
- 临时日志已移除；测试 Editor 已退出；四项 Editor 配置逐字节恢复，见 restoration.json。

## Resume Protocol

阅读 README、progress、M2；确认 M1 通过。只推进 M2 内子步骤，失败则先修正并验证。M2 所有 Acceptance Criteria 通过后才能更新到 M3。

## References

- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md
- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md
