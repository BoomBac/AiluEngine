# RHI ExecPlan — Progress / Resume State

> 这是长期维护的 **唯一执行状态文件**。文档初始化时无任何实施或运行结果。

## Current Milestone

- `M0 — Baseline & Tests`
- Status: `TODO`
- Branch: `<待填写>`
- Base commit SHA: `<待填写>`
- Last verified commit SHA: `<待填写>`
- Environment: `<Windows version / GPU / driver / VS / SDK>`
- Updated: `2026-10-19（计划创建）`

## Current Subtask — Lightweight Model Checkpoint

- Current Task ID: `T0.1`（尚未执行）
- Last completed task: `NONE`
- Current task owner: `<填入执行模型/会话>`
- Sources read in current session: `<填入源码文件>`
- Working tree dirty files not owned by this task: `<填入>`
- Test runner / build command discovered: `<待 M0 确认>`
- Failing test IDs / seed: `<无，尚未运行>`
- Explicit stop condition: `当前 T* 子任务编译或验证失败时，不开始下一个任务`

### Milestone Subtask Board

| Milestone | Task IDs（全部初始 TODO） | Next required verification |
|---|---|---|
| M0 | T0.1 repo/build；T0.2 fixture；T0.3 RED reproduction；T0.4 metrics | BASE-01..08 + build |
| M1 | T1.1 audit；T1.2 dynamic ring；T1.3 bindless retire；T1.4 integration | DESC-01..08 |
| M2 | T2.1 Oracle；T2.2 exact boundary；T2.3 subresource/UAV；T2.4 integration | ST01..ST10 |
| M3 | T3.1 ID lifetime audit；T3.2 retire；T3.3 delta overlay；T3.4 profile | LIFE-01..05 + state-map differential |
| M4 | T4.1 command enum/cases；T4.2 reset；T4.3 worker lifetime；T4.4 regression | CMD-01..09 |
| M5 | T5.1 API audit；T5.2 submit extraction；T5.3 packet owner；T5.4 services；T5.5 cleanup | BND-01..08 |
| M6 | T6.1 RG contract；T6.2 RG fixtures；T6.3 full stress；T6.4 benchmark/closure | RG01..RG10 + final |

每个子任务完成后在下方添加一行：`date | task id | files changed | exact command | result | evidence path | commit | next`。不得把下一阶段 TODO 提前改为 PASSED。

| Date | Task ID | Files | Build/Test Command | Result | Evidence | Commit | Next |
|---|---|---|---|---|---|---|---|
| — | — | — | — | NOT RUN | — | — | T0.1 |

## Progress

| Milestone | Status | Commit / Evidence | Remarks |
|---|---|---|---|
| M0 Baseline & Tests | TODO | — | 当前阶段 |
| M1 Descriptor Lifetime | TODO | — | 必须等 M0 PASSED |
| M2 Resource State Correctness | TODO | — | 必须等 M1 PASSED |
| M3 State Lifetime & Submit | TODO | — | 必须等 M2 PASSED |
| M4 Command Packet Safety | TODO | — | 必须等 M3 PASSED |
| M5 RHI Boundaries | TODO | — | 必须等 M4 PASSED |
| M6 RenderGraph Integration | TODO | — | 必须等 M5 PASSED |

## Decision Log

| ID | Date | Decision | Reason / Alternative |
|---|---|---|---|
| D001 | 2026-10-19 | 不重写整个 RHI，只做分阶段修整 | 降低跨模块回归风险 |
| D002 | 2026-10-19 | M0 先建立可复现测试/指标 | 不把静态风险当已发生 bug |
| D003 | 2026-10-19 | 严格以 GPU Fence 完成定义资源回收 | 多帧、多线程录制不能依赖帧号 |

后续新增决策一律附具体源码、实验/数据和备选方案，不改写历史。

## Surprises & Discoveries

- 初始：`DescriptorManager::GetOrCopy` Ring wrap 和 Bindless freelist 均需要 in-flight 验证。
- 初始：`IsResourceStateCompatible` 使宽只读状态满足窄读需求，但可能影响后续 Barrier 的 `StateBefore`。
- 初始：队列级 `_states` 缺少可见回收接口；每次提交复制完整 tracker。
- 初始：`SafeResetCommand` 的 `memset` 触及非平凡命令成员；尚未确认是否在运行路径被调用。`DestroyCommand` 缺少 `kBuildAS`、`kDispatchRays` 的分派分支（对应命令在 `CommandBuffer.cpp` 中实际分配）。

## Validation

| Date | Milestone | Build / Tests | Result | Artifact | Notes |
|---|---|---|---|---|---|
| — | — | — | NOT RUN | — | 尚未执行 |

建议指标记录（每个场景 × Debug/Release × 单线程/并行）：

| Metric | Baseline | After | Delta | Evidence |
|---|---:|---:|---:|---|
| Submit CPU p50 / p95 ms | — | — | — | — |
| Descriptor Copies / Cache Hit | — | — | — | — |
| Descriptor Ring Used / Peak / Pending | — | — | — | — |
| Bindless In-use / Pending Retire | — | — | — | — |
| Queue State Map Active / Peak / Retired | — | — | — | — |
| Barrier Count / Validation Errors | — | — | — | — |
| GPU Frame ms / Memory MB | — | — | — | — |

## Blockers

- 当前无可确认的实施环境；需执行代理在本机确定工程构建入口和 D3D12 Debug Layer / GPU-Based Validation 可用性。
- 已有的其他工作树改动应先确认；严格禁止覆盖不属于本阶段的修改。

## Next Step

1. 启动 M0；读取 `m0-baseline-tests.md`。
2. 在当前 checkout 定位 `.sln` / CMake / test runner / Editor 启动参数，写入精确命令。
3. 建立最小状态追踪单元测试和 Descriptor lifetime 压测入口；记录基线。

## Known Follow-ups

- Advanced DX12 allocator、Async Compute、多 API、Enhanced Barriers 等不在本轮。

## Outcomes

- **尚无实施结果**。
- 每阶段填写：变更摘要、关键 commit、通过的命令、证据路径、残余问题、性能前后对比。

## Resume Protocol

每次恢复执行：`README.md → progress.md → 当前 mX*.md → 相关源码`；一次只处理一个 Milestone，未通过不得晋级。完成阶段后更新 `Current Milestone` 和 `Next Step`。
