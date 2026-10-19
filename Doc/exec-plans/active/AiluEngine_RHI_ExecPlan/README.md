# AiluEngine RHI 正确性与架构收敛 ExecPlan

> 状态：**规划完成 / 尚未实施**　|　版本：2026-10-19 v2（轻量模型可实施版）　|　实施方式：顺序 Milestone、逐阶段独立验收
>
> **给 Codex / Claude 的入口：** 每次恢复先读本文件及 `progress.md`，之后**只读当前** `mX-*.md`，按其步骤实施、运行测试、记录证据；验收失败不得开启下一阶段。

## Lightweight Agent Execution Contract（必读）

本计划是**具体执行包**，不是让轻量模型自行补全设计。每次只做当前阶段一个 `T<阶段号>.<任务号>`，不通读其余 6 个 Milestone。**单次执行输出**必须包含：当前改动范围、实际 diff、精确构建命令和退出码、测试输入/断言/输出、仍有风险、下一步。未实际运行的内容只能标为 `NOT RUN`，不可写 PASS。

1. `README.md` 只读一次建立全局规则；会话恢复时优先 `progress.md` + 当前 `mX*.md`，必要时只回读 README 的 Constraints / Resume Protocol。不要把 README 和全部 7 个 m*.md 一次性加载到上下文。
2. 当前 Milestone 文件按 `Baseline → Implementation Steps（总览）→ Implementation Blueprint（精确做法）→ Tests → Acceptance` 读取；优先执行 Blueprint 中 `T0.1/T1.1/...` 的**单个子任务**。
3. 如果源码在最新工作树与 2026-10-19 Drive 快照不一致，记录 `file/function/current behavior`，以**实际最新源码**为准；不要强行套用过时修改点。定位不到函数时先搜索符号/调用者，并在进度文件记录发现。
4. 每个任务只允许最小改动，构建及当前任务测试通过后再提交；禁止借清理架构顺手引入 Async Compute、Vulkan、D3D12MA、复杂新基类或改遍工程的无意义重排。
5. 碰到模棱两可的 Fence/Descriptor/State 同步语义，先建立复现或不变量断言。无法证明安全时**停止该子任务并记 BLOCKED**，不能用“更简洁”替代正确性证明。
6. 不强制生成大篇幅实施日志；每步更新 `progress.md` 对应任务行，长日志/CSV 保存到 TestArtifacts 并给出路径。当前阶段 PASSED 前不得修改后续阶段状态。

### 建议交给实现模型的启动指令

```text
严格按照此目录的 README.md 和 progress.md 执行 RHI ExecPlan。
只加载当前 Milestone 文件和它直接涉及的源码，按该文件 T* 子任务逐项推进。
每次只修改一个小主题：先验证基线、实施最小改动、编译测试、记录证据，再进入下一子任务。
不要跳过 Acceptance Criteria；无法确认资源生命周期 / GPU Fence 正确性时停止并记录 BLOCKED。
对源码与计划不一致的地方，以实际源码为准并更新 Decision Log，不要自行扩大任务范围。
```

## Purpose

以 **DX12 正确性优先、明确资源/命令生命周期、消除不必要的状态复制、逐步收敛 RHI 边界** 为原则，改进 AiluEngine 当前渲染基础设施。目标不是按 Unreal Engine 规模重新设计一套 API，而是让现有单 DX12 后端更可靠、更容易维护，未来仍可选择性扩展其他后端。

## Scope

- Shader-visible Descriptor Ring、持久 Bindless SRV/UAV index 的 GPU 在途生命周期。
- `D3DResourceStateTracker` / `D3DQueueResourceStateTracker` 的 Barrier 正确性、Subresource 跟踪、外部资源状态。
- Queue State 记录退役、`D3DContext::ExecuteRHICommandBuffers` 的提交开销。
- `GfxCommand` 命令包 Reset 的类型安全，Frontend Packet → Backend CommandList → Queue 的责任归属。
- `GraphicsContext` / `D3DContext` 的过宽职责、Shader 编译与 Capture 边界。
- RenderGraph 与 RHI 状态职责约定、全链路回归与最终清理。

## Non-goals

- **不**同时移植 Vulkan / D3D11 / Metal，不为未知后端制造巨量抽象。
- **不**全面重写 RenderGraph、材质系统、Shader/PSO 缓存、FrameDebugger 或 GPU 资源注册表。
- **不**引入通用 GPU 任务调度器、强制多队列、Async Compute、Enhanced Barriers 或 D3D12MA。
- **不**改变外部 Renderer API 的语义；无充分证据不以“代码风格现代化”为由修改热路径。
- 不要求单次提交一个巨大的重构 PR；每个 Milestone 独立提交、独立验收。

## Architecture Constraints

1. **先证明再修复：** 文中「风险」是源码静态检查发现的候选问题。先写可复现测试/调用链，确认条件；未复现也必须以明确的不变量及测试结果决定是否修复。
2. **正确性优先：** 不得使用帧号、Ring epoch、CPU 对象释放、CommandList Close 替代 GPU Fence 完成证明。
3. **保留两级录制：** `Render::CommandBuffer/GfxCommand` = 渲染前端数据包；`RHICommandBuffer/D3DCommandBuffer` = Native 命令录制；Queue = 提交和同步。除非证据表明某层没有价值，不合并成单层。
4. **Native Resource 是 Barrier 追踪单位：** 一个 `GpuResource` 可含多个 `D3DResource`，每个独立拥有 State ID；RenderGraph 只产生逻辑访问意图，低层落实硬件 Barrier。
5. **尊重现有寿命管理：** 保留 `GpuResourceRegistry` Handle + Generation、资源强引用、Fence 延迟释放、Backbuffer Resize 清理，不引入新的泄漏或悬挂引用。
6. **并行安全：** 并行录制的先后顺序可不同于提交顺序；任何 Descriptor / Command Pool 回收必须对这种顺序有效。
7. **性能可回退：** 录制、提交、Barrier、Descriptor copy 的指标须留存前后对比；纯架构改动在测试噪声之外产生显著退化时回滚本阶段。
8. **代码风格：** 普通变量 `lower_snake_case`；成员 `_lower_snake_case`；静态成员以 `s` 开头；常量 `kCamelCase`；类型和函数 `CamelCase`；尽量保持每行 ≤160 列；尽可能沿用当前局部风格，避免无意义重排。
9. **先完成当前阶段验收：** 每阶段有产出、检查和证据；未经通过，不开始下一阶段。
10. **不静默掩盖失败：** 资源耗尽/验证失败需可诊断，不能用越界、覆盖仍在途 GPU 数据、隐式全队列等待作为默认“修复”。

## Baseline（基于 2026-10-19 Google Drive 代码快照）

| 分层 | 当前代码 | 已观察事实 / 待核查 |
|---|---|---|
| 公共入口 | `Inc/Render/GraphicsContext.h` | 一个接口同时处理设备、窗口、命令、Shader 编译、Capture、Readback、Timer。 |
| 前端命令 | `Inc/Render/CommandBuffer.h`, `Inc/Render/GfxCommand.h`, `Src/Render/CommandBuffer.cpp` | Render 级命令包携带材质、Mesh、RDG Handle 等，不是纯硬件命令。 |
| DX12 执行 | `Inc/RHI/DX12/D3DContext.h`, `Src/RHI/DX12/D3DContext.cpp` | `ExecuteRHICommandBuffers` 负责提交；`ProcessGpuCommand` 中存在高层渲染命令解释。 |
| Native 命令 | `Inc/RHI/DX12/D3DCommandBuffer.h`, `Src/RHI/DX12/D3DCommandBuffer.cpp` | 自有 CommandList、局部 StateTracker、PostSubmit 回调、Fence 状态。 |
| Descriptor | `Inc/RHI/DX12/DescriptorManager.h`, `Src/RHI/DX12/DescriptorManager.cpp` | `GetOrCopy` 空间不足时直接回绕 `_ring_cursor`；持久 Bindless 索引 `Release...` 立即入 freelist。是否已受调用方更外层保护须测试确认。 |
| 状态 | `Inc/RHI/DX12/D3DResource.h`, `D3D*StateTracker.h`, `Src/RHI/DX12/D3D*StateTracker.cpp` | `ResolveInitialBarriers` 允许只读兼容免 Barrier，但 `CommitFinalStates` 直接提交局部逻辑终态；需检查真实 GPU 状态与 `StateBefore` 是否一致。 |
| 状态提交成本 | `Src/RHI/DX12/D3DContext.cpp` | `ExecuteRHICommandBuffers` 中先复制 `_queue_state_tracker` 再按 CommandList 处理；状态表暂无明确的退役 API。 |
| RenderGraph | `Inc/Render/RenderGraph/RenderGraph.h`, `Src/Render/RenderGraph/RenderGraph.cpp` | 图阶段形成资源访问要求并调用 `RequireState`；需检查外部资源、非图路径的状态责任。 |
| Native 资源 | `Inc/RHI/DX12/D3DResource.h` | `_state_id` 为 `{index, generation}`，当前索引单调增长，generation 初值 1；销毁后的队列状态清理需显式设计。 |
| 命令 Reset | `Inc/Render/GfxCommand.h` | 模板 `SafeResetCommand` 用 `memset` 覆盖派生命令的尾部；`DestroyCommand` 当前缺少 `kBuildAS`/`kDispatchRays` case；部分命令包含 `std::function` / `Vector` 等非平凡成员。须先核实真实调用路径。 |

> 该基线是静态代码审查，**未在用户的 Windows DX12 环境执行编译、GPU 验证或性能测试**。因此绝不把候选风险写成“已复现故障”。实现前需由 M0 建立准确的可执行基线。

## Target Outcome

- GPU in-flight Descriptor 使用与回收有可验证 Fence 规则；动态表和 Bindless Index 均无提前覆盖/复用。
- 录制侧、提交侧和硬件侧资源状态契约一致，跨 CommandList/局部 Subresource 不产生错误 `StateBefore`。
- 状态表大小随**活跃 Native Resource** 数量有界；提交不再无条件复制全部历史条目。
- Command Packet 无未定义的字节清零，所有资源与回调寿命可追溯。
- `GraphicsContext` 可继续兼容旧调用方，内部职责更清楚；RenderGraph/Shader/Backend 的边界写入文档并由测试证明。
- 生产渲染路径、窗口 Resize、Readback、DXR（有硬件时）、Blender Link / Editor 场景的回归不低于基线。

## Relevant Files

**源码目录基于 Drive 中 `Inc` 和 `Src` 结构，实施时先确认本地工作树真实路径及文件版本。**

- `Inc/Render/GraphicsContext.h` / `Src/Render/GraphicsContext.cpp`
- `Inc/Render/CommandBuffer.h` / `Src/Render/CommandBuffer.cpp`
- `Inc/Render/GfxCommand.h` / `Src/Render/GfxCommand.cpp`
- `Inc/Render/GpuResource.h` / `Src/Render/GpuResource.cpp`
- `Inc/Render/CoreType.h`, `Inc/Render/RenderGraph/RenderGraph.h`, `Src/Render/RenderGraph/RenderGraph.cpp`
- `Inc/RHI/DX12/D3DContext.h`, `Src/RHI/DX12/D3DContext.cpp`
- `Inc/RHI/DX12/D3DCommandBuffer.h`, `Src/RHI/DX12/D3DCommandBuffer.cpp`
- `Inc/RHI/DX12/D3DResource.h`, `Inc/RHI/DX12/D3DResourceStateTracker.h`, `Src/RHI/DX12/D3DResourceStateTracker.cpp`
- `Inc/RHI/DX12/D3DQueueResourceStateTracker.h`, `Src/RHI/DX12/D3DQueueResourceStateTracker.cpp`
- `Inc/RHI/DX12/DescriptorManager.h`, `Src/RHI/DX12/DescriptorManager.cpp`
- `Inc/RHI/DX12/D3DTexture.h`, `Src/RHI/DX12/D3DTexture.cpp`, `Inc/RHI/DX12/D3DBuffer.h`, `Src/RHI/DX12/D3DBuffer.cpp`
- `Inc/Render/Shader.h`, `Src/Render/Shader.cpp`, `Inc/RHI/DX12/D3DShader.h`, `Src/RHI/DX12/D3DShader.cpp`

## Milestones

| 顺序 | 文件 | 意图 | 独立验收关口 |
|---|---|---|---|
| M0 | `m0-baseline-tests.md` | 可重复的构建、RHI 测试和性能基线 | 构建及测试基线可重现、有日志与样本 |
| M1 | `m1-descriptor-lifetime.md` | Ring + Bindless GPU 生命周期 | 小容量回绕与延迟 Fence 压测无提前覆盖 |
| M2 | `m2-resource-state-correctness.md` | Barrier 进入态/终态不变量 | 多 CommandList / Subresource 状态用例通过 |
| M3 | `m3-state-lifetime-submit.md` | State Map 退役与提交成本 | 长时间 churn 状态表有界、无性能倒退 |
| M4 | `m4-command-packet-safety.md` | 命令包 Reset/Destroy 安全及录制边界 | 非平凡命令无 UB / 泄漏，多线程提交一致 |
| M5 | `m5-rhi-boundaries.md` | 缩小 Context 职责，保留兼容层 | 公共 API 无破坏、所有路径运行正常 |
| M6 | `m6-rendergraph-integration.md` | RG/RHI 契约、整体回归与收尾 | 完整回归矩阵及基准报告通过 |

依赖为 `M0 → M1 → M2 → M3 → M4 → M5 → M6`。任何阶段未通过，只允许继续本阶段修复/测试，不得更改后续阶段状态为 In Progress。

## Current Milestone

**M0 — Baseline & Tests。** 阅读 `m0-baseline-tests.md` 开始。首次实施前先在 `progress.md` 写入当前 commit、平台、分支与日期。

## Progress

维护在 `progress.md`，采用 `TODO / IN_PROGRESS / BLOCKED / PASSED`；当前全为 TODO。每完成一步立即记录变更文件、构建结果、测试数据和提交 SHA。

## Decision Log

- D001：保留现有前端命令包与 Native CommandList 两级结构，不合并为单一 `Immediate Context`。
- D002：资源状态追踪以 Native `D3DResource` 为单位，不以高层 `GpuResource` 为唯一状态原子。
- D003：优先修复已定位的潜在安全/正确性路径，不因“现代化”而重写 PSO 或整个 Shader 系统。
- D004：阶段 M2 优先选择**严格正确的边界 Transition**，读状态跨 CommandList 合并优化必须由专用测试证明才恢复。
- D005：阶段 M1 以 Fence 严格保护使用期，容量耗尽时可诊断失败优于静默覆盖；Heap 切换不能假定 Root Descriptor Table 自动保留。
- D006：所有候选问题先通过 M0 验证其条件，不得跳过确认直接大改。

## Surprises & Discoveries

- `GetOrCopy` 通过 `frame_id` + `_heap_epoch` + `_generation` 命中 CPU 缓存；**命中不证明 GPU 已结束使用**。
- `AllocBindless*Index` / `ReleaseBindless*Index` 的自由索引列表是立即可复用的，需完整追溯谁调用释放及其 Fence 协议。
- `D3DResourceStateTracker::ReleaseRecordedResources` 有为 Backbuffer `ResizeBuffers` 防止命令池持有引用的特别处理。不能把此处理删除来解决队列状态积累。
- `D3DContext.cpp` 中 `GpuCommandWorker` 和大体积 `ProcessGpuCommand` 与 Native 提交处于一个实现文件；应先测量/拆职责，不宜边修同步边迁移全部代码。

## Validation

全局最低验证规则：

1. 与 M0 同配置的 Debug 和 Release 全量编译；至少一套 DX12 Debug Layer + GPU-Based Validation 样本；`D3D12_MESSAGE` 相关错误为零（对已知基线噪声留有明确白名单及证据）。
2. 单线程与并行 CommandList 录制；普通场景、动态资源 churn、窗口 Resize、多帧 in-flight、异步 Readback、RenderGraph import/aliasing 流程。
3. 生命周期维度使用**提交 Fence 与完成 Fence**，不以固定 N 帧表示 GPU 完成。
4. 每阶段基准记录 CPU submit p50/p95、CommandList 数、Barrier 数、Descriptor copy/cache、StateMap 活跃/峰值、内存、GPU 时间；每组至少 3 次，记录场景/机器/驱动。
5. 无法在当前环境运行 GPU 时，阶段状态保持 `BLOCKED`，可提交静态变更但不可标记为 `PASSED`。

## Known Follow-ups（不属于本轮强制实现）

- 如果 profiling 证明有收益，再评估 D3D12MA/Placed Resources、Copy Queue、Async Compute、Enhanced Barriers。
- 如果业务明确要求非 DX12 后端，再抽象 ResourceState/Queue/Capability 的跨 API 语义。
- PSO 磁盘缓存、Pipeline Library、Shader 编译增量、Transient Heap 别名等单独立项，避免污染本计划。
- `_state_id` 索引长期递增及 generation 当前固定的极端溢出语义，后续可独立增强。

## Outcomes

初始状态：**无代码改动、无构建/运行测试；仅输出可执行计划**。验收通过后，将实测结果、修复前后指标、已知限制和相关 commit 填入 `progress.md` 的 Outcomes。

## Resume Protocol

1. `git status --short`、记录 `HEAD`；阅读 `README.md` + `progress.md` + 当前 `mX-*.md`。
2. 对照 `progress.md` 的 `Next Step` 确认唯一当前任务；发现未提交变更先识别归属，禁止覆盖他人改动。
3. 只加载当前 Milestone 的源码和测试；不主动一次读取全部 `m*.md`。
4. 实现一个可验证的最小改动，执行该阶段测试。编译不通过立即修复，不得把失败后移。
5. 更新 Progress、Decision Log、Surprises、Validation、Blockers、Next Step；附日志位置、测试命令和 commit。
6. 阶段所有 Acceptance Criteria 满足并记录证据后，标记 `PASSED`，再切换下一个 Milestone。

## Reference Designs（仅作原则参考）

- NVIDIA NVRHI：<https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md> —— Device/CommandList、资源状态与寿命追踪。
- Epic Unreal RDG：<https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine> —— 高层图声明意图、底层 RHI 执行与 Validation。
- Microsoft Shader-Visible Descriptor Heaps：<https://learn.microsoft.com/en-us/windows/win32/direct3d12/shader-visible-descriptor-heaps> —— Descriptor Heap 能力和语义。
