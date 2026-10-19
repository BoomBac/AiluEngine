# M2 — Resource State & Barrier Correctness

**Status:** TODO　**Prerequisite:** M1 PASSED　**Exit gate:** 跨 CommandList 与 Subresource 资源状态正确，Barriers 和真实进入态一致。

## Purpose

清除 Queue State 与 CommandList Local State 之间的语义不一致。将「可省略 Barrier」与「状态实际改变」区分，并对 UAV 顺序、Present/Common、外部资源和 Subresource 建立独立验证。

## Scope

`Render::IsResourceStateCompatible`、`D3DResourceStateTracker::RequireState`、`D3DQueueResourceStateTracker::ResolveInitialBarriers` / `CommitFinalStates`、`D3DCommandBuffer::RequireState / RecordQueueTransition / UavBarrier` 以及 RenderGraph 提交给 RHI 的状态使用点。

## Non-goals

- 不引入 Vulkan 风格 Pipeline Layout/AccessMask；不强制迁移 Enhanced Barriers。
- 不为减少 Barrier 数而牺牲 StateBefore 正确性，不把所有调度都移回 RenderGraph。
- 不改 ResourceStateId 的生命周期；该工作由 M3 承担。

## Architecture Constraints

- 每一 Subresource 的队列物理状态必须与后续 Barrier `StateBefore` 完全一致（允许 D3D12 明确规定的 promotion/decay 例外，需单独记录和验证）。
- 非零只读状态 bitmask superset 仅表示**访问兼容**，不表示硬件状态已隐式改为窄子集。
- `RequireState` 的同命令内部 Transition 以**该命令实际进入状态**为起点；只修 Queue Map 终态而不修录制内部链会留下隐患。
- 对未访问的 Subresource 禁止擅自声明新状态；ResourceStateId 必须对应真实 Native Resource，多个 Native Resource 不共享 State ID。
- UAV 写后复用相同 UAV 状态不能因 `StateBefore==StateAfter` 就自动省略必要的 UAV Barrier；须由显式 UAV Barrier 或更强语义覆盖。

## Baseline

- `CoreType.h::IsResourceStateCompatible` 在 `current` 只读 bitmask 包含 `required` 时返回 true。
- `D3DQueueResourceStateTracker::ResolveInitialBarriers` 在只读兼容时无 Transition，但 `CommitFinalStates` 直接使用 `local_state._current_state`。
- `D3DResourceStateTracker::RequireState` 在首次访问时使用请求态设 `_first_state/_current_state`，之后根据本地状态建立 Barrier。
- 由此可以构造候选反例：队列实际 `GenericRead`，CommandList 首次要求 `PixelShaderResource`（边界免 Barrier），后续同列表要求 `UnorderedAccess`；本地记录的 Barrier `PixelShaderResource→UnorderedAccess` 未必匹配实际进入态 `GenericRead`。

## Target Outcome

- StateTracker 的所有输出满足不变量：`before_state == physically_tracked_state`、`committed_final_state == state_after_command_execution`。
- 在不牺牲正确性的前提下保留单命令内部兼容读优化；跨命令兼容合并如果无法正确建模，先采用严格边界 Transition。
- RenderGraph 和 Immediate/Native callback 路径都遵循同一资源状态协议。

## Relevant Files

`Inc/Render/CoreType.h`; `Inc/RHI/DX12/D3DResource.h`; `Inc/RHI/DX12/D3DResourceStateTracker.h`, `Src/RHI/DX12/D3DResourceStateTracker.cpp`; `Inc/RHI/DX12/D3DQueueResourceStateTracker.h`, `Src/RHI/DX12/D3DQueueResourceStateTracker.cpp`; `Src/RHI/DX12/D3DContext.cpp`; `Src/RHI/DX12/D3DCommandBuffer.cpp`; `Src/Render/RenderGraph/RenderGraph.cpp`.

## Implementation Steps

### Step 1 — 定义状态模型和可验证不变量

- 给每个资源/Subresource 明确：`queue_actual_state`（上一个提交结束真实状态）、`command_first_requirement`、`command_local_barriers`、`command_final_actual_state`。
- 纯单元测试按照严格序列模拟提交前、Queue Boundary、CommandList 内部每条 Barrier 和 CommitFinalStates 后状态，逐步对比。
- 单独覆盖 `kCommon == kPresent` 的数值重叠、Upload/Readback Heap 固定状态、D3D12 可隐式 promotion/decay 的合法组合；不能依赖一个 `IsResourceStateCompatible` 处理全部特殊语义。

### Step 2 — 首先实现安全正确的兼容路径

**最小、安全的首选方案：** 跨 CommandList 的首次要求只要与队列真实态不同（即使只是宽读→窄读），优先显式 Transition，保证本地首次状态与硬件状态一致；仅在确认 D3D12 对该资源/状态允许时省略。

- 修订 Boundary `NeedsTransition` 策略并写明确注释：访问兼容 ≠ 状态相同。
- `ResolveInitialBarriers` 不能在无 Transition 时无条件将 tracked 状态视为 `required`；如果要继续省略 Barrier，必须修订整个 CommandList 的内部 Transition 链，使后续 `StateBefore` 仍以原始真实状态为依据（需要更复杂的 State Resolution）。
- 对单命令录制的本地连续只读访问，已有兼容逻辑可保持较宽状态，只要确保从入口到终点状态自洽。
- 分支 A：严格 Transition 先通过全部用例；分支 B：若 profiling 显示需要读兼容优化，再附带正确的入口状态注入/重定位设计及多列表测试，严禁未经证明直接恢复。

### Step 3 — Subresource / 外部资源 / 视图验证

- 按 `ResourceStateId` 而不是 `GpuResource*` 验证多个 Native Texture/Buffer 的状态互不污染。
- 覆盖仅第 0 mip 初始化，后续全资源访问；mip/array 混合；先全资源再部分访问；部分访问后非访问子资源继续保持之前状态。
- `SetExternalState` 的调用方需要在**真实 GPU 状态已变化**或有可信外部约定时调用；不得把它用成逻辑标记而不执行转换。
- 检查 Resize/Backbuffer import 的已知进入态，及 READ/WRITE 后 Present 的结果。
- 对带 Counter Buffer 的 GPUBuffer 和多 Native Resource 的组合做独立状态断言。

### Step 4 — Barrier 最终行为检查

- 对真实 D3D12 Barrier 注入 Debug Layer / GBV，捕捉 invalid before/after state。
- 确认只读访问、RTV/DSV/UAV、CopySrc/CopyDest、UAV Barrier、Aliasing Barrier 的路径；尤其检查相同 UAV 状态下两个 Dispatch 之间的内存可见性。
- 对一批 `ExecuteRHICommandBuffers([cmd1,cmd2,...])` 内的相邻多列表，与拆为多次提交应得到相同最终状态（除命令合法执行顺序差异）。

## Required Tests

| ID | 资源 / 操作 | 验收不变量 |
|---|---|---|
| ST01 | `GenericRead → PixelSRV → UAV`（不同列表） | Barrier 的 before 为真实态 |
| ST02 | `GenericRead → PixelSRV → UAV`（同列表） | 局部链条与入口态一致 |
| ST03 | `PixelSRV → NonPixelSRV → AllShaderSRV` | 读状态组合合法且不凭空缩窄 |
| ST04 | UAV 写→UAV 写 | 必要的 UAV Barrier 存在 |
| ST05 | mip0 write + mip1 read + full-resource read | 各 Subresource 状态正确 |
| ST06 | Backbuffer Common/Present, Resize, Import | 无无效转换 / Resize 失败 |
| ST07 | 带 Counter 的 Buffer 两个 Native Resource | State IDs 与状态独立 |
| ST08 | 多个 CommandList 同一次 Submit 与分批 Submit | 最终状态相同 |

## Implementation Blueprint — Barrier 正确性逐条落地

> **M2 只修语义，不叠加 M3 的 Map/Submit 优化。** 一个逻辑测试必须同时验证：①真正录制了什么 Barrier；②每条 Barrier 的 StateBefore 是什么；③列表内部发生什么状态变化；④后续列表读到的队列终态是什么。

### M2-A. 先建立真相表（按当前代码而不是凭印象）

真实调用路径：

```text
RenderGraph::Execute
  -> CommandBuffer::RequireState(GpuResource*, state, subresource)
  -> GfxCommand(kRequireResourceState)
  -> D3DContext::ProcessGpuCommand(..., RHICommandBuffer*)
  -> D3DCommandBuffer::RequireState(D3DResource, state, subresource)
  -> D3DResourceStateTracker::RequireState
       - 首次访问：_first_state = requested，_current_state = requested
       - 之后访问：_transitions += {before,after,subresource}
  -> D3DCommandBuffer::RecordQueueTransition/FlushResourceBarriers
提交时 D3DContext::ExecuteRHICommandBuffers
  -> planned_queue_states.ResolveInitialBarriers(queue_state_cmd, local_tracker)
  -> planned_queue_states.CommitFinalStates(local_tracker)
  -> 原生 GPU command lists
  -> _queue_state_tracker.CommitFinalStates(local_tracker)
```

精读 `CoreType.h::IsResourceStateCompatible`：当前宽读状态包含窄读访问 bit 即算兼容，属于**访问要求兼容**。不能由此推断硬件 State 真的变为窄状态。

### M2-B. 必须先写的两个反例

**反例 1（跨列表真实 StateBefore 错误）**：Queue 当前 `GenericRead`；列表 A 首次 `PixelShaderResource`，因为 compatible 跳过边界 Transition；A 的 `_current_state` 却成为 PixelSRV；Commit 使队列表错误地变窄；列表 B 要 `UnorderedAccess`，输出 `PixelSRV -> UAV`，但实际仍为 `GenericRead`。测试应在 list A 之后、B 之前分别断言跟踪/真实状态，不只断言最后能渲染。

**反例 2（同列表内部 StateBefore 错误）**：Queue 当前 `GenericRead`；列表 A 首次 `PixelSRV`，紧接 `UAV`。Boundary 因兼容省略；Local 内部已缓存 `PixelSRV -> UAV`。该 Barrier before 与实际 `GenericRead` 不一致。注意**只修改 `CommitFinalStates` 无法修复这个反例**。

编写 CPU Oracle 表格：

| 时间 | Queue 实际态 | Local 首次要求 | Barrier | 下一个实际态 |
|---|---|---|---|---|
| t0 | GenericRead | — | — | GenericRead |
| t1（A 入口） | GenericRead | PixelSRV | GenericRead → PixelSRV | PixelSRV |
| t2（A 内部） | PixelSRV | UAV | PixelSRV → UAV | UAV |
| t3（A 结束） | UAV | — | — | UAV |
| t4（B 入口） | UAV | PixelSRV | UAV → PixelSRV | PixelSRV |

此表是**安全严格模式**的预期；只有确认更宽只读状态可以保留且本地链与实际入口态一致，才允许少发首条 Barrier。

### M2-C. 首选修复：边界状态使用 Exact Transition

建议最小改法：**Queue Boundary 判定先从 `IsResourceStateCompatible` 变为 exact state equality**。不直接删除 `CoreType.h` 的兼容函数，因 Local `RequireState` 仍可利用宽读优化；也不引入 Enhanced Barrier。

参考伪代码（不是直接粘贴生产代码）：

```cpp
bool NeedsBoundaryTransition(EResourceState actual_state, EResourceState first_required_state)
{
    return actual_state != first_required_state; // 另行处理已确认合法的 promotion / decay
}
```

修改步骤：

1. 在 Queue Tracker 将原 `NeedsTransition` 明确命名为 `NeedsBoundaryTransition`（如项目习惯允许），`ResolveInitialBarriers()` 使用 exact 比较；不要影响 Local StateTracker 的 `NeedsTransition()`，也不要“全局改兼容判断”。
2. 覆盖 `_first_state._subresource_states.empty()`、有局部子资源数组、队列整体状态、队列局部子资源状态这四个组合，确保只给已初始化/已访问的 Subresource 生成必要 Barrier。
3. `CommitFinalStates()` 写入局部最终态前，用 CPU model 证明每条 local internal Barrier 的首态已经与真实 Boundary 之后的状态一致。Strict 模式下：Boundary 将实际态变为 Local First；列表内部逐条 Transition 必须从此前最终状态出发。
4. 对 `kCommon`/`kPresent`、默认堆/上传堆/回读堆的 D3D12 约束做独立规则，不能一概认为“任何两个不同状态之间都可以直接 Transition”。在 `D3DConvertUtils::FromALResState` 确认枚举映射正确。
5. 如有调用者通过 `SetExternalState()` 注入状态，必须确认它代表已由外部真实 Transition 得到的状态；不能把它用成“我希望这个资源以后是某状态”的声明。
6. 完成 Strict 路线并通过 ST01-ST08 后，记录 Barrier 增量。**本阶段不默认恢复宽读跨列表优化**：只有 profile 证明退化明显、且引入了完整 local Barrier patching / entry-state resolution 的独立测试，才能选择优化分支。

### M2-D. Local Tracker 的完整状态演算规则

第一版坚持以下不变量：

```text
QueueBefore           : 提交序下该 Native Subresource 的真实终态
LocalFirstRequirement : 当前列表最早一次 RequireState 的请求态
BoundaryAfter         : QueueBefore 或显式 BoundaryTransition 的 after
LocalAfter[k]         : 各条列表内部 Transition 执行后的真实态
CommittedFinal        : 执行列表后 LocalAfter[last]，不允许逻辑缩窄而无 Barrier
```

**典型合法序列：** `GenericRead -> GenericRead`（相同不发）→ 请求 `PixelSRV`（宽读兼容可继续保留实际 `GenericRead`）→ 请求 `UAV`（必须生成 `GenericRead -> UAV`）。**典型非法序列：** 上一步没有发宽读→窄读 Transition，却把内部当前状态直接改成 PixelSRV，随后以 PixelSRV 为 Before。

特别检查 `D3DResourceStateTracker::RequireState` 的分支：

- 首次资源访问只记录 `_first_state`，不生成 Local Barrier，边界补。
- 第二次访问全资源、`subresource_count==1` 和多子资源循环，是不同代码分支；同一逻辑资源其实际 NativeResource count 可大于 1。
- `_first_initialized` 为 false 的 Subresource 不应凭空发生 Transition；批量全资源访问时此前没访问过的 mip/array 要首次初始化。
- `CollapseSubresourceStates` 只在所有子资源确实等价时合并；合并不能改变未访问子资源的真实状态。
- `D3DCommandBuffer::FlushResourceBarriers` 会合并连续 Transition；必须用测试证明合并没有跨越 UAV/Aliasing Barrier 或实际 GPU 操作，且没有丢掉需要的同步语义。不要把 Barrier 数减少当成绝对正确目标。

### M2-E. 自动化测试给出逐条预期，而不是只看最终画面

| Test | 具体输入 | 预期输出 |
|---|---|---|
| ST01 | 进入态 GenericRead；A 首次 PixelSRV；B 首次 UAV | A 边界 `GenericRead→PixelSRV`；B 边界 `PixelSRV→UAV`（Strict 模式） |
| ST02 | 进入态 GenericRead；同列表 PixelSRV→UAV | 列表入口 `GenericRead→PixelSRV`，列表内部 `PixelSRV→UAV` |
| ST03 | 进入态 GenericRead；同列表 GenericRead→PixelSRV→UAV | PixelSRV 可兼容不发，UAV 前必须 `GenericRead→UAV` |
| ST04 | 进入态 UAV；Dispatch 写 A→UAV barrier→Dispatch 写 B | 至少存在真实 UAV 同步；仅状态相同不构成写后写内存顺序 |
| ST05 | 4 个 Subresource：mip0=RTV，mip1=CopyDst，其余=SRV；列表仅访问 mip1 | 只变 mip1；其他 3 项的记录不变 |
| ST06 | 已知 Present 资源、RTV 绘制、回到 Present、释放旧 backbuffer、Resize | 输出合法 Transition；旧 ID 不错误应用到新资源 |
| ST07 | 一个 GpuResource 管两个 NativeResource（主资源、Counter） | 两个不同 StateId，各自独立转换 |
| ST08 | A、B 两列表：一次批 Submit vs 两次 Submit | 可观测 GPU 输出相同；最终状态表相同 |
| ST09 | 混合 `Transition, UAV, Transition` 批处理 | UAV/Aliasing 语义不能被合并算法穿越 |
| ST10 | 非法 subresource index，空 resource，重复 require | 不越界；错误可诊断；不污染状态表 |

对 ST01-ST03 和 ST05，测试输出**OrderedBarrierTrace**：`command_list_id, sequence_index, native_resource_id, subresource, barrier_kind, before, after`。自动断言逐条序列及 StateBefore；同时比对 `ResourceState` 在提交完成后的结果。任何 `EXPECTED_FAIL` 在 M2 结束时必须为 PASS。

### M2-F. 生产场景交叉验证及诊断办法

1. 启用 DX12 Debug Layer + GPU-Based Validation；如启用 PIX，捕获两条相邻 CommandList 并人工核对临界 Barrier。真实 GBV 不支持的机器，要把证据落盘而不是无条件 SKIP 核心 CPU tests。
2. 分别运行 RenderGraph `Import -> Read -> Write -> Export`、非 RG 直接 `RequireState`、Buffer Update / Copy、Readback、RTV/DSV、Compute Dispatch、Present、DXR（可用时）。
3. 将出现错误的第一次 Barrier 输出为可读单行：`id/subresource/actual/first/before/after/cmd/fence`；生产 Release 禁止每个 Barrier 打印日志。
4. 出现“画面没变化但 GBV 报错”不能通过关闭 GBV 来通过；出现“GBV 没报错但 Oracle 比对失败”不能解释为可忽略优化。

### M2-G. 修改粒度与提交门槛

- **T2.1**：先补 ST01/02 RED 用例及 OrderedBarrierTrace，生产逻辑不变。
- **T2.2**：仅修改 Queue Boundary exact matching / 特例；ST01/02/03 全绿，包含所有子资源分支。
- **T2.3**：完成 ST04-ST10，修复局部 Tracker/Barrier 批处理确证问题；不得为通过一个测试而禁用其他 Barrier。
- **T2.4**：生产 RG+非 RG 录制、Debug/Release 性能对比。报告新增 Transition 数及原因。

**禁止的“修复”：** 强制每条 RequireState 都发 Barrier、让 Queue 每帧清空 StateMap、把 `SetExternalState` 作为万能补丁、只修 commit 而不验证同列表内部链。出现跨队列多 Queue 情况需 BLOCKED 并重估范围，不在此阶段仓促修改。


## Acceptance Criteria

- [ ] 每个 ST 用例有期望 Barrier 列表/状态追踪断言，并通过；旧反例不再生成错误 `StateBefore`。
- [ ] Debug Layer + GBV 无新增 GPU Resource State 错误；之前可复现的相关错误消失。
- [ ] 不改变 Native `D3DResource` 独立 State ID 原则，不破坏 Backbuffer Resize / RenderGraph Import。
- [ ] Barrier 数量与 M0 基线对比，增加的 Barrier 已解释为正确性需要；无明显 GPU 长尾回归而不调查。
- [ ] 独立 commit / 测试证据记录于 `progress.md`，方可进入 M3。

## Decision Log / Surprises / Validation / Progress / Known Follow-ups / Outcomes / Resume Protocol

- Decision: 先严格 Transition 还是采用入口态注入优化，需在通过 ST01-ST08 及 profiling 后确定并记录。
- Surprise: `Common/Present` 与 implicit promotion/decay 必须在真实 DX12 Device 测试，不得仅逻辑模拟。
- Validation: ST01-ST08 + RenderGraph smoke + Debug Layer/GBV。
- Progress: TODO；Next: 先实现 ST01 / ST02 反例并保存状态序列。
- Follow-up: Enhanced Barriers 单独立项。Outcomes: 未实施。Resume: README + progress + 本文件。
