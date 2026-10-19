# M3 — Queue State Lifetime & Submission Cost

**Status:** TODO　**Prerequisite:** M2 PASSED　**Exit gate:** Queue State 生命周期有界，提交路径不再按历史资源总数无条件复制完整 State Map。

## Purpose

消除长时间运行 / 频繁创建销毁 GPU 资源时 Queue State Map 潜在无限增长，以及每次 `ExecuteRHICommandBuffers` 全量复制 Tracker 的 CPU 开销。

## Scope

`D3DResource` State ID、`D3DQueueResourceStateTracker`、`D3DContext::ExecuteRHICommandBuffers`、实际 Native Resource 退役路径、CommandBuffer Pool 保留资源与 Backbuffer Resize 的交互。

## Non-goals

- 不改已有逻辑 `GpuResourceHandle`/Generation 方案；不以全局锁保护所有渲染状态来替代队列提交锁。
- 不重复实现资源回收系统；优先复用现有 `GpuResourceRegistry::Sync` / deferred releases / submitted Fence。
- 不重新调整 M2 Barrier 语义或引入多 Queue。

## Architecture Constraints

- StateMap 键是 `ResourceStateId`，生命周期绑定**Native ID3D12Resource 的最后合法 GPU 使用者**，而非上层 `GpuResource` 对象析构时刻。
- Resource 从 CPU 上解绑/提交给销毁，并不意味着尚未提交的 CommandList 不再引用它；需要保护 pending recording、pending submission 和已提交尚未完成的三种状态。
- `D3DResourceStateTracker::ReleaseRecordedResources` 解除命令池强引用是为 `ResizeBuffers` 服务的，不允许无评估删除。
- 状态退役必须满足：不会再次被新的合法提交请求、完成最后一次 GPU 使用、也不会被早录后提交的命令重新引入。
- 提交锁应维持单队列统一提交序，严格避免因临时复制减少而使 Barrier 解析竞态。

## Baseline

- `D3DQueueResourceStateTracker::StateMap` 为 `unordered_map<ResourceStateId, ResourceState>`；当前可见 API 含 `ResolveInitialBarriers`, `CommitFinalStates`, `SetExternalState`，没有显式 `Retire`。
- `D3DContext::ExecuteRHICommandBuffers` 以 `_queue_state_tracker` 的**全量副本** `planned_queue_states` 处理每条命令；真实 `_queue_state_tracker` 在 Submit 后再次对每条命令提交终态。
- `D3DResource::ResetStateId` 使用单调递增的静态 ID 索引；generation 目前恒为 1，StateMap 曾记录的 ID 不会自动复用。

## Target Outcome

- 显式、事件驱动的 State ID 退役；运行长期 churn 后表大小回落至活跃资源规模。
- 提交中的计划状态追踪只复制/覆盖**本批触及的状态**或采用可回滚的暂存增量，不再复制完整历史表。
- 仍具备严格队列顺序和异常安全，不退化成全局 `WaitForGpu()`。

## Relevant Files

`Inc/RHI/DX12/D3DResource.h`, `D3DQueueResourceStateTracker.h`, `Src/RHI/DX12/D3DQueueResourceStateTracker.cpp`, `Src/RHI/DX12/D3DContext.cpp`, `Inc/RHI/DX12/D3DCommandBuffer.h`, `Src/RHI/DX12/D3DCommandBuffer.cpp`, `Src/Render/GpuResource.cpp`, `Src/Render/CommandBuffer.cpp`, `D3DTexture.cpp`, `D3DBuffer.cpp`.

## Implementation Steps

### Step 1 — 清点状态表和使用者时序

- 采样启动→运行→热重载→创建/销毁 Texture/Buffer→窗口 Resize→结束时的 `StateMap.size()` / 活跃 NativeResource / Pending Recording / 已提交 InFlight 数。
- 对 State ID 的产生、`D3DResource::Set / Reset / GetAddressOf`、CommittedResource 创建、被拷贝/替换、释放各路径画具体时间线。
- 建立 stress：固定最多 N 个活跃资源、反复创建销毁 1000/10000/100000 次；原实现有增长则记录曲线，否则确认外部管理机制。

### Step 2 — 引入 Native Resource 的退役通知

- 首选与现有 GPU 资源退役/最后使用 Fence 对接：`ScheduleStateRetire(state_id, safe_completed_fence)`（名称可调整）。
- 若 Native Resource 被命令缓冲录制保持引用，状态不能在该命令可能提交之前退役；否则未来再次 `try_emplace` 会把先前状态误认为 Common。
- 设计明确的状态：`Live / RecordingReferenced / SubmittedLastUseFence / RetirePending / Retired`，不必为每资源创建重量级对象。最小实现可采用 submit 记录 + 待退役队列。
- 若当前销毁动作只能看见 `ComPtr` 引用计数而无法得到最后 GPU Fence，先桥接到已有 `PostSubmitCallback` / registry retire 流程，而不是在 `~D3DResource` 无条件擦表。
- `StateId` 复用/轮转暂不实现；清理时仅删除精确 `(index,generation)` 防止 ABA。
- 把 `CollectRetiredStates(completed_fence)` 放到现有的安全收集点；内部遍历增量退休队列而非每帧扫描所有活跃 StateMap。

### Step 3 — 提交时用 Delta 代替整表复制

**建议最小模型：** 在同一个 `_command_submit_mtx` 内为本次批次维护 `unordered_map<StateId, ResourceState> pending_final_states`，读状态先查 pending，再查真实队列状态，记录每个 CommandList 产生的增量终态。

- Queue 初态读取：先命中当前批次 delta，失败再查询 `_queue_state_tracker`；M2 的 Barrier 语义必须原样使用。
- 每个 CommandList 的终态提交到 delta，下一条 CommandList 以该状态解析；只有 `ExecuteCommandLists` 与 Fence Signal 成功，才一次性应用 delta 到真实 tracker。
- 若异常发生于真正 GPU 提交**之前**，丢弃 delta；如果 ExecuteCommandLists 已成功但 Fence Signal/后续操作异常，状态不能简单回滚，必须单独定义失败恢复（通常设备进入失效/停止提交状态），不得假定事务具备 GPU 级原子回滚。
- 初期保留可切换旧路径（仅测试环境）做 differential compare：对同样的提交列表，两种算法的 Barrier 序列和终态一致。
- 提交数/受访状态数变化时，CPU 应趋于 `O(本批触及状态)`，而不是 `O(所有历史状态)`。必要时保持全局 queue lock 但不强行拆线程。

### Step 4 — 补充诊断与寿命压测

- 输出：`live_state_count`, `retired_state_count`, `pending_retire_count`, `touched_states_per_submit`, `queue_state_copy_time_ms(旧版对照)`。
- 验证 `ResizeBuffers` 前 ReleaseRecordedResources 不会因 StateMap 增加 NativeResource 强引用而失败（StateMap 只保存轻量状态）。
- 对异步 Texture Upload、Readback、临时 RT、Buffer 版本切换进行随机序列并验证 Barriers。

## Implementation Blueprint — StateMap 退役与增量 Submit

> M3 必须分成两个**独立可验收的变更**：先保证状态表生命周期，后移除整表复制。不要在同一个 diff 中同时改 retire 时序和 Barrier 计算，否则一旦出错无法判断是删早了还是算错了。

### M3-A. 当前精确瓶颈路径（执行前再次核对）

```cpp
// Src/RHI/DX12/D3DContext.cpp::ExecuteRHICommandBuffers（当前基线片段）
D3DQueueResourceStateTracker planned_queue_states = _queue_state_tracker;
for (D3DCommandBuffer *d3dcmd : d3d_cmds)
{
    planned_queue_states.ResolveInitialBarriers(*queue_state_d3dcmd, d3dcmd->StateTracker());
    planned_queue_states.CommitFinalStates(d3dcmd->StateTracker());
}
// Queue Execute / Signal 后，又对 _queue_state_tracker CommitFinalStates
```

问题包括：每批提交复制**所有历史 ResourceStateId 条目**，即使这批只访问少量资源；`D3DQueueResourceStateTracker::_states` 当前只有 `ResolveInitialBarriers/CommitFinalStates/SetExternalState`，没有 `Remove` 或 `Collect`。实际增长规模需要用 M0 的 BASE-06/07 实测，不要未经测量填写优化收益。

**排查 Native Resource ID：** `D3DResource` 中的 `ResetStateId()` 使用静态单调递增 ID；`Set()` 分配新 ID；`Reset()` 清空；拷贝 D3DResource 共享相同 ID；`GetAddressOf()` 可能清空 ID 并返回 ComPtr 存储地址。因此**绝对不要仅在 D3DResource 的某个临时对象析构时删除状态**，否则局部 Tracker 或资源视图持有的副本可仍使用同一个 Native ID。

### M3-B. 画出资源状态的完整生命周期，先决定 retire owner

建议在 `state-lifetime-audit.md` 写每条主要路径的阶段：

```text
Native resource create / wrap (StateId)
 -> recorded in CommandList (D3DResource copy / pending reference)
 -> ExecuteCommandLists and Signal(fence)
 -> resources retained while in flight
 -> last CPU-side owner released (or resource re-created)
 -> no future pending recording references, GPU completed_fence >= last use
 -> queue state table entry removed
```

要求检查：Texture/Create/Destroy；Buffer/Resize/版本替换；Backbuffer Swapchain Resize；Temp RT 归还资源池；Readback staging；DXR AS；外部 Native Resource；RHI Command Pool 回收。每条路径至少找出「谁知道不再产生未来提交」「谁知道最后提交 Fence」「哪处可以 Collect」。

候选最小实现：**状态登记表不持 ComPtr 强引用**，以 `ResourceStateId` 对应的资源所有权纪录 + 已提交 Fence 作退役判定。对于尚未提交的 CommandList，给 StateId 增加 pending recording user/token 或将 retire 事件放到所有 pending list 完成/取消之后。资源被高层释放不代表还能用于后续提交的录制已全部结束。

```text
State entry:
  Current / RetireRequested / RetirePending / Retired
  last_submitted_fence (单 Queue)
  pending_recorders_or_explicit_lifetime_token
```

这是状态定义，**不强制**额外为每个 StateId heap-allocate 一个对象；可以用 HashMap 记录轻量元数据、退休队列按完成值收集。若现有 registry 的 RetireFence 与 pending 关系已能证明满足要求，优先直接复用，不添加第二套重复生命周期。

### M3-C. Retire API 及线程规则

建议接口语义（仅示意，函数命名按项目风格）：

```cpp
void ScheduleStateRetire(ResourceStateId state_id, u64 last_submitted_fence);
void CollectRetiredStates(u64 completed_fence);
```

1. `ScheduleStateRetire` 仅登记候选；不可立刻 `_states.erase(state_id)`。调用方必须确认当前 Native Resource 已不再用于**未来**命令录制，所有既有未提交录制也已结束/取消，或者显式追踪了 pending token。
2. `CollectRetiredStates` 的先决条件是 `completed_fence >= last_submitted_fence` 且 pending recorder 数为 0。无法证明 pending recorder 归零就**不能**删除记录；记录异常并停在 M3。
3. `SetExternalState` 应允许新引入的合法外部资源登记精确 ID，但不能使已 Retired 的旧 `(index,generation)` 复活。对同一 native pointer 重新包装是否获得新 ID，须有测试；不能依据 ptr 做状态身份。
4. 在当前 single direct queue 上，Fence 序号仅用于已通过该 Queue 提交的 Native use；如果存在外部 Queue/SDK 使用同一资源，必须加入额外 completion 条件，而不是拿 Direct Queue Fence 覆盖其他 Queue。
5. 明确锁序：`_command_submit_mtx` 保护 QueueState 的线性化；不要在持有它时进入可能等待该 Queue 提交的阻塞操作。`GpuResourceRegistry::Sync`、CommandBufferPool 收集与 Context shutdown/resize 的锁顺序应画出图。
6. 在 `RHICommandBufferPool::ReleaseCompletedTrackedResources()` 中保留 `ReleaseRecordedResources()`；其特殊作用是解除 backbuffer 强引用以让 `ResizeBuffers` 成功。新 retire 通知与这一行为并行，不得粗暴用 `Clear()` 代替。
7. 追踪 `D3DResource::GetAddressOf()`：若赋值后 `_state_id` 仍为 0，必须先修复资源包装流程，使新 Native Resource 在**首次**进入 tracker 前具备合法唯一 ID；本阶段不扩展 32-bit ID rollover 机制。

### M3-D. State Map 长跑测试，先独立通过

| ID | 条件 | 验证 |
|---|---|---|
| LIFE-01 | 活跃资源始终 16 个，创建销毁 1k→10k→100k 次 | `peak` 可暂升，`WaitForGpu+Collect` 后回到常驻资源数附近 |
| LIFE-02 | 同 ID 被 A/B 不同录制持有，A 先提交 B 延迟提交 | 在 B 结束前不得 retire，否则后续 Barrier 从 Common 算错 |
| LIFE-03 | Native Resource 重建，新的 State ID，旧 ID pending | 新/旧记录不互相覆盖；不存在 ABA |
| LIFE-04 | Backbuffer resize，命令池保留若干已完成列表 | ResizeBuffers 成功，ComPtr 不被状态记录保留 |
| LIFE-05 | 外部 Native Resource / SDK | 在真实完成点之后 retire，不用虚假的本 Queue Fence |

一个关键断言：**曾触碰过的历史资源数量与 steady-state StateMap entry count 不应长期正相关**。如果测试无法让引擎真实销毁资源（资源池持续缓存），要区分 Pool Cached vs Alive，不得把合理的池驻留直接判泄漏。

### M3-E. 提交快照替换：只用 touched entries 的 Overlay

目标保持精确行为：同一 SubmitBatch 内后一个 CommandList 的入口态由前一个 CommandList 的终态决定；成功提交才让全局 QueueState 可见。推荐轻量 `QueueStateOverlay`，只为 touched StateId 存临时修改。

伪代码（强调次序与异常边界，不是完整可编译实现）：

```cpp
QueueStateOverlay planned_states(_queue_state_tracker); // 对全局 tracker 只读、Delta 本地写
for (D3DCommandBuffer* command_buffer : d3d_cmds)
{
    planned_states.ResolveInitialBarriers(*queue_barrier_command, command_buffer->StateTracker());
    planned_states.ApplyFinalStates(command_buffer->StateTracker());
}

// queue_state_commands 与 d3d_cmds 按原始顺序组成 native_cmds
m_commandQueue->ExecuteCommandLists(static_cast<UINT>(native_cmds.size()), native_cmds.data());
const u64 submitted_fence = SignalSubmittedBatch();
planned_states.CommitTo(_queue_state_tracker); // 仅触及到的 ID
```

这里 `CommitTo()` 在 GPU 提交与 Signal 后，仍必须位于 `_command_submit_mtx` 保护区内（保持与当前一致的同步顺序）。不改真实设备执行次序，唯一变化是从 full copy 改为 read-through delta。

**数据结构约束：**

- `ReadState(id)` 优先查 `_overrides`，未命中读取 `_queue_state_tracker` 的状态；未知 ID 只有在真实 create / import 合法时才默认为 Common。
- `WriteState(id)` 只更改 `_overrides`；提交后合并 touched entries，不全表遍历，也不得触发 `ComPtr` 引用保持。
- Subresource 状态用现有 `ResourceState`（整体/局部压缩）模型；不能用一个 `EResourceState` 覆盖多 mip/array 的不同状态。
- `ResolveInitialBarriers` 和 `CommitFinalStates` 的 Barrier 语义由 M2 规定；Overlay 只换存储策略，不重新定义兼容判定。
- 可先给 Tracker 增加私有 `FindState/TrySetState` 和轻量 View，暂时保留旧接口作测试对照；避免复制粘贴两份 Barrier 核心算法造成后续漂移。

**重要异常规则：** Queue Execute 成功后，即使后续 Fence Signal 返回错误，也可能已执行部分 GPU 工作，不能当事务失败简单丢掉已提交 CommandList 或恢复旧状态。应立即标记设备/Queue 不可继续提交，并让外层错误处理走明确停止或恢复路径；不得在没有 Fence 保证时释放依赖本次提交的资源。

### M3-F. 差分测试证明 Overlay 与原算法等价

1. 仅在测试构建里运行旧版 full-copy planning 与新版 overlay planning 的**纯数据副本**，输入同一初态和同一批 LocalTracker 状态，不重复向 GPU 提交。
2. 对每个 CommandList 比较 `barrier_kind/resource_id/subresource/before/after` 的有序序列、批次终态和新插入资源数；依次运行 M2 所有 ST01-ST10。
3. 准备 4 组负载：小 StateMap（16 资源）、中 StateMap（1k）、大 StateMap（10k）、批次 touched 固定 1-4；证明新版提交开销不再随历史 ID 总数等比例增长。
4. 另外测试 `N=0` 空 batch、重复资源 100 次、1 个资源 100 个 subresource、同一 batch 交替读写、非常大的单 batch、两个录制线程交错提交请求。
5. 比较 p50/p95 及 3 轮测量波动；如果 Overlay 退化，先用 `touched_state_count`、HashMap rehash 数、分配次数定位，而非再加一层 cache。

### M3-G. 需要避免的实现错误

- **过早 Erase：** 以 `GpuResource::~GpuResource` 或任何 `D3DResource` **副本**析构作为唯一 state retire 依据。
- **错误优化：** 提交阶段先直接更新 `_queue_state_tracker` 再构造 Barrier，导致 CPU 上的失败提交污染全局状态。
- **两份真相：** 保留两套 QueueStateMap 且所有 Normal/External path 不同步，久而久之不一致。
- **全局隐式等待：** 每次 Collect/Retire 都调用 WaitForGpu，让功能正确但严重阻塞编辑器。
- **不合法的 ID：** `GetAddressOf` 后未更新 `_state_id`、把 0 作为所有资源的共用 ID。
- **不完整的队列边界：** 外部 Queue 的完成条件未纳入 retire，错误地仅检查 Direct Queue `completed_fence`。

### M3-H. 子提交与验收证据

- **T3.1** StateId 生命周期图和命令使用者完整清单；LIFE-01 能可靠复现初始曲线。
- **T3.2** Retire 注册、Collect、Pending user 防早删；LIFE-01~05 通过；commit A。
- **T3.3** QueueStateOverlay 增量路径 + 差分测试；M2 ST 测试与 4 档 state-map 微基准通过；commit B。
- **T3.4** Debug/Release + RenderGraph / Readback / Resize / DXR 条件回归；性能报告，提交前后核心指标图表/CSV。

无法验证已有录制但未提交命令的资源释放时机，**必须 BLOCKED**；宁可维持有限的可解释状态记录，也不要引入早删后误判 Common 的正确性回归。


## Acceptance Criteria

- [ ] 固定活跃资源数的长时间 churn 后 `StateMap` 有界：停止提交并 WaitForGpu + Collect 之后，表项回落到常驻资源集合（记录对应数量，不用拍脑袋设置固定值）。
- [ ] 提交过程中没有全表深拷贝，单批 touched-count 统计可靠。
- [ ] 无早删导致 Common 误判、无漏删增长、无 ABA、无 Fence 之前回收。
- [ ] 旧/新 Queue 规划算法在测试序列下生成语义一致的 Barrier 和终态。
- [ ] Submit CPU p50/p95 与 M0 相比在场景噪声外不得恶化；若改变优化复杂度产生短期开销，须附理由与数据批准，否则修正。
- [ ] Resize、MultiThread、Delayed Readback、DXR 可用时的测试通过；进度和日志完整后才进入 M4。

## Decision Log / Surprises / Validation / Progress / Known Follow-ups / Outcomes / Resume Protocol

- Decision: State 退役到底由 NativeResource wrapper 通知还是由 registry/命令生命周期事件通知，以能正确捕获 Last GPU Use 为准，记录设计选择。
- Discovery: `GetAddressOf()` 可以令 `_state_id` 为空，须检查所有 native 赋值/重建时 ID 重置流程。
- Validation: 100k churn + 双命令乱序 + Resize + 提交差分比对。
- Progress: TODO；Next: 首先采集 StateMap 随 churn 的增长曲线。
- Follow-ups: StateId 32-bit wrap、真正多队列状态交接单独立项。
- Outcomes: 未实施。Resume: README + progress + 本文件。
