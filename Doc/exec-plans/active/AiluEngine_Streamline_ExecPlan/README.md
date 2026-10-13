# AiluEngine · NVIDIA Streamline / DLSS 最小侵入执行计划

> 基线：`AiluEngine_Source(20261012-111424).zip`（2026-10-12）。开发执行者应优先校验实际工作树与本基线的差异；不要把此快照当作最新仓库。
>
> SDK 基线：NVIDIA Streamline SDK **v2.14.1**（查阅官方发行页确认该版本及 API；实现时以安装的 `include/` 头文件为准）。
>
> 任务规模：**大型、跨 Renderer / RenderGraph / DX12 RHI / Camera / Postprocess、需要分阶段验收**。使用分拆式 ExecPlan；当前执行 M0，其余里程碑只读取其 README + 当前阶段文件 + progress.md。

## Purpose

以**最小侵入、功能隔离、后续扩展成本可控**的方式，在 AiluEngine DX12 后端接入 NVIDIA Streamline。首先验证 DLSS Super Resolution（SR），但 SDK 初始化、Device/SwapChain 对接、FrameToken、Viewport、资源 Tag 和命令执行入口应能够供未来 DLAA、Ray Reconstruction（RR）、Frame Generation（FG）、Reflex 复用。

## Scope

1. 单一 `StreamlineRuntime` 管理 SDK、插件、设备与 Feature capability；单一 `StreamlineDx12Bridge` 处理 DXGI/D3D12 对接、原生句柄与状态同步；`StreamlineSrPass` 负责 SR 特有资源及选项。
2. SDK 可选依赖：未安装 SDK、未开启编译宏、不支持的显卡均安全退回原有渲染管线；如果还要求**运行时 DLL 缺失也不阻止进程启动**，必须选择显式 `LoadLibrary`/`GetProcAddress` 或验证可用的 `/DELAYLOAD` + 失败处理。**普通非延迟导入 `sl.interposer.lib` 不具备这种回退能力**。
3. 优先采用官方 **Manual Hooking**；只对 Streamline 要求的 DXGI / Device 调用接入对应 proxy，其余引擎仍使用 native 接口。尤其不要将 SL proxy 传入第三方库。
4. 在现有 RenderGraph 中注册一个 SR Pass；通过 `CommandCustom` 在当前命令组的 DX12 命令列表上执行 `slEvaluateFeature`，不额外维护与 RG 不一致的独立渲染队列。
5. 封装最小通用 Feature 元数据、Viewport/Frame 上下文；未来新 DLSS Feature **新增特定模块**，不重复实现 SDK 初始化、DXGI Hook、frame tagging。
6. 保持 TAA/非 DLSS 路径、无 RenderGraph 路径和其他后端行为不变。

## Non-goals

- 当前计划**不实现** RR、FG、MFG、Reflex、FSR、XeSS、完整统一超分系统，也不重写 TAA。
- 不为 FG 提前重做 Present、SwapChain、UI 合成、RenderGraph 或并行录制架构。
- 不让 SL 侵入普通 Shader、材质、场景、资源基类，或将 `sl::*` 暴露到上层公开头文件。
- 不自行修改 NVIDIA SDK，不硬编码 DLSS 内部分辨率比例，不用模拟“成功”掩盖不支持的硬件。
- 不要求一次性兼容所有 Camera、离屏视口及非 RG 模式：第一版优先**DX12 + 单主 Camera + RenderGraph**，其余走安全 fallback；M3 再逐步扩展。

## Architecture Constraints

### 模块边界

```text
Renderer / Camera / Postprocess
          │ 仅查询请求、有效模式、render/output size、color handle
          ▼
StreamlineSrPass (feature-specific; RenderGraph)
          │ 只传递中立的 SR 描述与资源 handle；不在 Pass 内导出原生 API
          ▼
StreamlineDx12Bridge (唯一持有 sl::Resource/Tag、ID3D12Resource、native cmd list 的层)
          │                              ▲
          │                         CommandCustom
          ▼                              │
StreamlineRuntime (SDK init / capability / frame token / viewport / synchronization)
          │
          └───── SDK plugins + limited DXGI/Device hooking

New feature later:
  StreamlineRrPass or StreamlineFgCoordinator → same Runtime / Dx12Bridge
```

**扩展原则**：先 3 个明确模块，不创建为未知未来功能而设计的大型 `IFeature` 虚接口或跨后端通用 SDK 框架。只抽取 100% 已被 SR 使用且 RR/FG 明确共用的能力；以后扩展时可增加按 Feature 分支的薄适配器。

### 必须遵守

- `StreamlineRuntime` 与 `StreamlineDx12Bridge` 必须**禁止复制**、显式初始化/销毁、无显式外部 SDK 类型泄漏。
- Streamline 的初始化应在任何可能创建 DXGI/SwapChain 的引擎路径之前安排；Manual Hooking 的 DX12 SDK 文档允许设备在 `slInit` 前或后创建，但本工程优先使用更严格、容易审计的启动顺序。
- Manual Hooking **不是零 Hook**：需处理 SDK 列出的必要 Device/Factory/SwapChain 接口，且每帧验证 `presentCommon()` 被调用；含多 RenderWindow 的 Present/Resize 路径需要覆盖。
- SDK 版本、头文件、lib、运行时 DLL 和 plugin DLL 必须来自同一发行版本；按官方要求验证二进制签名与安全加载路径；不可搜索不可信 DLL 路径。选择纯动态加载时 Feature helper 的符号解析必须改用 SDK 规定的动态接口，不能继续直接假设导入库辅助函数可调用。
- **只在 GPU 录制线程调用** `slEvaluateFeature`；线程、生命周期、FrameToken、Viewport、纹理资源、CommandList、GPU Fence 必须对应；SDK 声明非线程安全的 API 由单一同步策略保证安全，禁止边录制边从别的线程修改该 Viewport 选项。
- SDK 调用造成的 CommandList state changes 必须恢复或确保下一条引擎命令完整重设。单独 `ResetGraphicsStateCache()` 只是逻辑失效，不自动重绑所有 D3D12 状态；需要用验证证明后续 Dispatch/Draw 正确。
- **RenderGraph 的状态跟踪不可与 Streamline 的内部资源转换各记各的**：必须明确进入/离开 SL 的可观测状态、barrier 归属和下一 Pass 的状态，使用 D3D12 Debug Layer + PIX 验证。不得仅以“官方说 SL 会转换资源”为由跳过自研 tracker 同步。
- 对外标记的原生纹理必须引用真实 DX12 资源，使用实际 extent/format/state；不得持有可能已被资源池复用的裸指针到下一帧。
- 失败路径必须具有**有效图像输出**，不能将未初始化 DLSS Output 送入 Bloom/ToneMapping；对运行时 evaluate 失败给出明确帧内兜底或安全中止该 Pass 的策略。
- 用户约定：变量 `lower_snake_case`，成员 `_lower_snake_case`，静态变量 `s_...`，常量 `kCamelCase`，类型/函数 `CamelCase`，代码宽度 ≤160 列；遵循工程既有类型命名习惯时不做无关大规模重命名。

## Baseline

已从所附源码直接核对的事实：

| 现有入口 | 具体状态 | 本计划处理 |
|---|---|---|
| `Engine/Inc/Render/GfxCommand.h` | 已定义 `CommandCustom`，携带 `std::function<void(RHICommandBuffer *)>` | **复用**命令类型；仅为 `CommandBuffer` 增加受控录制函数 |
| `Engine/Src/RHI/DX12/D3DContext.cpp` | `ProcessGpuCommand` 已能执行 `CommandCustom::_func(cmd_buffer)`；存在 `GpuCommandWorker::RecordCommandGroup` | 不额外增加一个 kStreamline 命令枚举；只添加必要的安全保护与桥接入口 |
| `Engine/Inc/RHI/DX12/D3DCommandBuffer.h` | `NativeCmdList()`、`GraphicsStateCache`、`ResetGraphicsStateCache()` 已有 | 需额外明确实际状态恢复策略 |
| `Engine/Inc/Render/Camera.h` / `Engine/Src/Render/Camera.cpp` | `RenderScale()`、`RenderSize()`、`OutputSize()` 已有；仅 TAA 才 `ProcessJitter()` | 只做一个“有效时域算法”及渲染尺寸协调点，避免覆盖用户 `RenderScale` |
| `Engine/Src/Render/Features/TemporalAA.cpp` | 通过 `_anti_aliasing == kTAA` 决定启用，拥有历史管理 | SR 运行时跳过 TAA；TAA 原实现尽量不动 |
| `Engine/Src/Render/Features/CommonPasses.cpp`、`Engine/Res/Shaders/hlsl/motion_vector.hlsl` | RG 路径有 Camera/动态物体 MV；Depth 使用单独 CopyDepth 到 `RFloat` | 校验 Depth 是否符合 Streamline 重投影约定、MV 符号/范围，**不是**无条件改 MotionVector Shader |
| `Engine/Inc/Render/RenderingData.h` | `_rg_handles._taa_color/_final_color` 已存在 | 最小改动增加一个 SR 输出句柄或单个有效重建输出句柄 |
| `Engine/Src/Render/Features/PostprocessStack.cpp` | 优先 `_taa_color`，否则 `_color_tex`；Postprocess 决定 `_final_color` | 单点加入 SR 优先路径，不散落判断 |
| `Engine/Src/Render/Features/BloomPass.cpp`、`ToneMappingPass.cpp` | 尺寸基于 `rendering_data._width/_height`（内部尺寸） | 仅适配输入纹理尺寸，保持 TAA/原尺寸兼容 |
| `Engine/Src/Render/Renderer.cpp` | `BeginScene()` 使用 `cam.RenderSize()` 分配 scene resources；`_presentation_texture` 按 `cam.OutputSize()`；`FinalBlit` 已处理两者差异 | SR 提前决定渲染尺寸，在 FinalBlit 前输出至 Postprocess |
| `Engine/Src/RHI/DX12/D3DContext.cpp::PresentImpl()` | 负责 RenderWindow、ImGui、Submit、SwapChain Present | M0 只设置必需 hook；FG 才重构帧生成协作路径 |

**快照限制**：压缩包以引擎/编辑器源码为主，不能推断本机编译脚本、SDK 安装路径、RTX 驱动、实际设备与运行图像表现。M0 先核实它们，再记录在 progress.md。RTX 不可用时只能完成能力检测与回退验证，不能宣称 SR 画质验收通过。

## Target Outcome

- `AILU_WITH_STREAMLINE=0`：原工程渲染路径无额外 SDK 运行依赖，零功能回归。
- `AILU_WITH_STREAMLINE=1`、无 DLL 或不支持设备：明确日志 + 原始 TAA/Blit 路径，不崩溃。
- 支持的 RTX/DX12 机器：Camera 可选择 DLSS Quality/Balanced/Performance（按 capability 实际显示），内部尺寸来自 `slDLSSGetOptimalSettings()`，正确输出至 `OutputSize()`，随后进入原有后处理。
- Future RR/FG 接入时：可复用 SDK 初始化、capability、frame/viewport、resource bridge 和 DXGI hooks；只在新增功能所特需的渲染资源与调度点扩展。
- 每个阶段单独编译、运行、记录证据、验收通过后才推进。

## Relevant Files

现有文件优先级：

```text
Engine/Inc/Render/GfxCommand.h
Engine/Inc/Render/CommandBuffer.h
Engine/Src/Render/CommandBuffer.cpp
Engine/Src/RHI/DX12/D3DContext.cpp
Engine/Inc/RHI/DX12/D3DCommandBuffer.h
Engine/Src/Render/RenderGraph/RenderGraph.cpp
Engine/Inc/Render/RenderGraph/RenderGraph.h
Engine/Inc/Render/Camera.h
Engine/Src/Render/Camera.cpp
Engine/Inc/Render/RenderingData.h
Engine/Src/Render/Renderer.cpp
Engine/Src/Render/Features/TemporalAA.cpp
Engine/Src/Render/Features/PostprocessStack.cpp
Engine/Src/Render/Features/BloomPass.cpp
Engine/Src/Render/Features/ToneMappingPass.cpp
Engine/Src/Render/Features/CommonPasses.cpp
Engine/Res/Shaders/hlsl/motion_vector.hlsl
```

建议**新增**（根据工程构建方式调整名字，不存在的文件不能被当作已存在）：

```text
Engine/Inc/Render/Features/Streamline/StreamlineRuntime.h
Engine/Src/Render/Features/Streamline/StreamlineRuntime.cpp
Engine/Inc/RHI/DX12/StreamlineDx12Bridge.h
Engine/Src/RHI/DX12/StreamlineDx12Bridge.cpp
Engine/Inc/Render/Features/Streamline/StreamlineSrPass.h
Engine/Src/Render/Features/Streamline/StreamlineSrPass.cpp
```

原则：SDK `<sl_*.h>` 只出现在模块实现文件或私有内部头，不放进高频公共 API/渲染数据结构。

## Milestones

| 阶段 | 独立可验收结果 | 详情 | 状态 |
|---|---|---|---|
| **M0 · SDK/Runtime 最小引导** | 构建可选、启动/关闭安全、capability 输出、Manual Hooking 路径验证 | `m0-sdk-runtime.md` | 本机修复复验通过；完整矩阵 Blocked，详见 progress |
| **M1 · 命令/资源桥接** | 不运行 DLSS，也能在 RG 的正确命令位置验证原生资源、状态/线程/生命周期 | `m1-command-resource-bridge.md` | 进行中 |
| **M2 · DLSS SR 画面闭环** | RTX 机器上能切换 None/TAA/DLSS SR，真实 render-size→output-size，后处理正确 | `m2-dlss-sr.md` | 实现中 |
| **M3 · 稳定性与扩展就绪** | Resize、Camera 切换、资源复用、失败回退、RenderDoc/PIX、未来 Feature 扩展文档 | `m3-hardening.md` | 未开始 |

## Current Milestone

**M2**，读取 `m2-dlss-sr.md`。用户于 2026-10-12 明确要求开始 M2；M1 验收仍未完成，相关缺口保留在 `progress.md`，不视为已通过。
M0 完整矩阵仍 Blocked，见 `progress.md`。

## Progress

详见 `progress.md`；首次执行把硬件、SDK 目录、编译/运行命令、测试结果、主要 commit 写进去。本文件保留稳定约束与阶段索引。

## Decision Log

- D001：**Manual Hooking + native 接口优先**；必要 Hook 不可省略；替代方案（全局 proxy）的兼容性风险更高。
- D002：沿用已有 `CommandCustom`，不发明专用 GPU 命令类型或独立 Queue。
- D003：SR-specific RenderPass 与 SL runtime 分离；“未来可扩展”不等于先实现所有 Feature。
- D004：`FrameToken` 与 `ViewportHandle` 单点管理；第一版局部 tag 跟随 `slEvaluateFeature`，为未来 FG 保留帧级 tag 能力；不混用旧版 `slSetTag`。
- D005：SR 输出进入现有 Postprocess 前；TAA 与 DLSS 互斥；UI 继续由已有 Present 路径合成。
- D006：从低风险单主 Camera 开始，其他路径明确 fallback；没有成功执行 SR 不修改原有截图/渲染输出语义。
- D007：Postprocess 按其 RG 输入的**真实尺寸**建纹理，不做全面渲染分辨率系统重构。

## Surprises & Discoveries

1. 已有 `CommandCustom` **没有**对外的 `CommandBuffer::RecordCustom(...)` 之类入口，需要极小补充（不要误判为已能由 Pass 直接调用）。
2. `D3DCommandBuffer::ResetGraphicsStateCache()` 仅使缓存失效，还要确保下一次 `Set*` 完整恢复真实 D3D12 所需状态。
3. 当前 `_depth_tex` 是从深度 attachment 拷贝到 `RFloat`，**尚未证明**为可直接用于 DLSS 的设备深度；优先验证，必要时使用原始 Depth 或转换资源。
4. Bloom/ToneMapping 默认仍使用 render-size；不修正会在 SR 后重新缩小输出。
5. 官方 v2.14.1 `slSetTag` 已 deprecated；`slSetTagForFrame` + `eUseFrameBasedResourceTagging` 或 evaluate-local tags 更适合多帧并行。
6. Manual Hooking 要求 `presentCommon()` 每帧执行；这属于 M0，而不是等到开发 FG 才考虑。

## Validation

**统一质量门禁**：

1. 修改前后记录 Release/RelWithDebInfo 构建命令与结果；不存在的编译环境不得声称“编译通过”。
2. `AILU_WITH_STREAMLINE=0` 编译/启动对比，TAA、Bloom、ToneMapping、ImGui/Present 与当前正常行为等价。
3. 启用 SL 后验证 DX12 Debug Layer 无新增严重错误、RG 不报资源 read/write/state 错误。
4. M0/M1 无 RTX 也能完成基础检查；M2 SR 成功只能在真正支持的 RTX 硬件上宣称。
5. 同一 milestone 的 Acceptance Criteria 必须全部通过，并在 progress.md 填入证据；失败则停留当前阶段。

## Known Follow-ups

- RR：增加 `sl::kFeatureDLSS_RR`、RR 的 specific options/tags、与 RT/PathTracer 降噪链路的接入点；**不是**对 SR 的简单开关，需单独 ExecPlan。
- FG / MFG：增加 `sl::kFeatureDLSS_G` 等所需能力，深入对接 SwapChain/Present、UI/HUD-less color、depth/MV frame tagging、Reflex、帧节奏；需独立设计和硬件矩阵，绝不假设 M0 hook 已完成 FG 的全部要求。
- DLAA：复用 SR 模块，但以实际 SDK 支持的 Mode/配置为准；关闭超分时的输入与输出尺寸严格一致。
- FSR/XeSS：若后续确需统一 upscaler 接口，再抽象跨供应商层；当前不做。
- 动态分辨率、立体渲染、离屏 Camera、多个 swapchain、计算/图形跨队列同步分别建后续验收。

## Outcomes

M0 已实现并修复验收发现的 COM ownership/native 路由/teardown 问题，本机 RTX hook 和销毁复验通过；
完整矩阵尚未完成。最新事实见 `progress.md` 与 `m0-repair-20261012.md`，初次记录见 `m0-acceptance-20261012.md`。
M2 正在实现；M1/M0 验收缺口仍保留，M3 未开始。

## Resume Protocol

1. 读取 `README.md` → `progress.md` → `Current Milestone` 对应 `mX-*.md`，不一次加载全部阶段文件。
2. 确认当前实际源码 SHA/版本、最近决策与未完成验收，查找未提交修改。
3. 只执行当前阶段；任何范围变化、SDK 文档冲突或工程事实偏差先追加 `Decision Log` 与 `Surprises & Discoveries`。
4. 完成子步骤立刻写 `progress.md`：变更文件、构建命令、运行记录、失败原因与下步。
5. 本阶段所有 Acceptance Criteria 满足后，更新 `Current Milestone`，提交/保存可恢复状态，再进入下一文件。
6. 一旦 Context Compaction，重新按 1～5 步恢复；不要根据旧聊天记忆猜测剩余任务。

## 官方参考

- Release: https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1
- General guide: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md
- Manual Hooking: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideManualHooking.md
- DLSS SR: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md
- DLSS FG: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md
- Sample: https://github.com/NVIDIA-RTX/Streamline_Sample
