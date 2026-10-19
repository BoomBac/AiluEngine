# M0 — Baseline, Test Harness & Invariants

**Status:** TODO　**Prerequisite:** none　**Exit gate:** 完整构建 + 基线记录 + 后续 Milestone 所需的最小测试入口。

## Purpose

建立可复现的验证手段，让后续每个改动都能够证明「正确」及「没有不合理性能回退」。这里只补小规模的测试、开关、计数器；不顺手重构生产实现。

## Scope

1. 识别真实 Windows 工程构建、测试、自动化运行入口（优先复用已有 Editor headless/CLI/测试工程；没有就增加最小 DX12 测试入口）。
2. 建立纯逻辑 ResourceState 单元测试，建立 GPU Descriptor / Resize / Readback 压测框架。
3. 记录 Debug Layer、GPU-Based Validation 可用性；如果硬件不支持 DXR，DXR 条目标记 SKIP 而不是 FAIL。
4. 新增 **低开销可开关统计**，用于 M1-M3 度量。

## Non-goals

- 不修改 Barrier 决策、不改 Ring 回绕、不清理 `D3DContext`。
- 不为了自动化测试重新实现整个 Editor 后台运行模式；如已有模式可复用，否则创建最小测试程序。

## Architecture Constraints

- 测试运行不得依赖睡眠 N 帧来推断 Fence 完成；必须使用 `GetCompletedValue` / 显式 Fence。
- 压测必须可固定随机种子、命令队列负载、容量和测试总轮数；记录测试参数。
- 统计必须可全量关闭且默认不开启逐 Descriptor/Barrier 日志。
- 尽量将纯逻辑测试写成无需创建 DX12 Device 的单元测试；GPU 验证独立。

## Baseline / Confirmed Observations

- `Src/RHI/DX12/D3DContext.cpp::ExecuteRHICommandBuffers` 存在 `_queue_state_tracker` 全量复制。
- `Src/RHI/DX12/DescriptorManager.cpp::GetOrCopy` 使用 Frame/Epoch/GPU Heap Ring。
- `Inc/Render/CoreType.h::IsResourceStateCompatible` 对只读 bitmask 支持子集兼容。
- `Inc/Render/GfxCommand.h::SafeResetCommand` 是按字节清零模板，但真实 Reset 调用路径尚待确定。

## Target Outcome

- 一条已验证、可复制的 Windows 构建命令；一条基本 GPU smoke test 命令（或已确认的现有工程步骤）。
- 状态逻辑用例和 Descriptor 压测能独立启动、可输出机器可读结果（例如 JSON / TSV），且退出码准确。
- 至少一组固定场景性能基线，包含三次重复测试。

## Relevant Files

`Inc/Render/CoreType.h`；`Inc/RHI/DX12/D3DResource.h`；`Src/RHI/DX12/D3DResourceStateTracker.cpp`；`Src/RHI/DX12/D3DQueueResourceStateTracker.cpp`；`Src/RHI/DX12/DescriptorManager.cpp`；`Src/RHI/DX12/D3DContext.cpp`；工程测试/构建配置（以本地发现为准）。

## Implementation Steps

### Step 0 — 仓库与命令定位

- 记录 `git rev-parse HEAD`、`git status --short`；不清理其他人的未提交变更。
- 找到现有 `.sln` / `CMakeLists.txt` / presets / 测试程序 / Editor 启动参数，确定 Debug 与 Release 的真实命令。
- 记下 GPU、驱动、Windows、Windows SDK、DXC 版本；确认 D3D12 Debug Layer 与 GPU-Based Validation 开关和运行能力。
- 选取至少三种基线：①简单 Raster + PostProcess，②RenderGraph 读写 + UAV，③动态资源 churn + Resize。若当前缺自动化，先用已存在场景截屏、散列和日志，避免另做大型测试框架。

### Step 1 — 纯逻辑 StateTracker 用例（先允许暴露失败）

- 创建轻量测试工程或在现有测试入口追加以下案例：
  - `kCommon` → `kCopyDest` → `kAllShaderResource`，单命令与双命令版本。
  - `kGenericRead` → `kPixelShaderResource`（兼容免 Barrier）→ `kUnorderedAccess`，跨 CommandList 与同 CommandList 版本。
  - 资源有 1/多个 Subresource，部分首次访问、整体访问、重新压缩/展开。
  - 跨 Queue 初始状态与外部资源导入、Present/Common、UAV 写→UAV 写（需要 UAV Barrier）。
- 断言每条 Barrier 的 `StateBefore` 为被跟踪的真实物理进入态，`StateAfter` 与执行路径一致；这些测试在修复前失败是合法的，记录为 Known Baseline Failure，后续 M2 消除。
- 无设备逻辑测试无法覆盖 DX12 implicit promotion/decay，单独增加 Debug Layer GPU 用例。

### Step 2 — Descriptor 生命周期压力用例

- 提供仅测试模式可覆盖 Ring 容量的配置，以便小容量稳定触发 wrap；生产配置不变。
- 同时录制多个 CommandList，故意乱序结束与提交；刻意保留 GPU 工作时间，使早期 Descriptor 仍在途。
- 持久 Bindless SRV/UAV 释放后迅速分配同一槽，检验引用该槽的旧 GPU 工作是否完成。
- 记录分配块 `[begin,end)`、录制所属对象、提交 Fence、完成 Fence、复用操作的序号；不能仅检查画面未闪烁。
- 若提交顺序与 Descriptor 分配顺序不一致，测试必须覆盖回收顺序变化。

### Step 3 — 测量与产物

- 为 queue tracker、Ring、freelist 添加聚合计数器：活跃/峰值、pending retire、copy/cache hit、overflow、Barrier 数、提交 CPU 时间（p50/p95）。
- 压测至少 3 次；保存启动参数、日志、Debug Layer 信息、截图或 capture、指标表。
- 对任何失败给出可复现命令和最小操作序列；不因预期「以后会修复」而隐藏错误。

## Implementation Blueprint — 轻量模型逐步执行说明

> **本节优先级高于泛化重构建议。** 执行者只需加载 README 的全局规则、本文件及相关源码。不要在 M0 修改 Descriptor/Barrier 的生产策略。每个子步骤仅允许一个主题的最小 diff；每步失败先修复，不跳步。

### M0-A. 固定源码与可执行入口（首先做）

1. 在真实仓库运行 `git status --short`、`git rev-parse HEAD` 和项目已有构建入口搜索；记录**工作树位置**、分支、SHA、dirty 文件（不能覆盖）、Windows/VS/SDK/GPU/驱动。不要把 Drive 的 2026-10-19 快照当成一定等于当前 HEAD。
2. 检查 `.sln`、`CMakeLists.txt`、`CMakePresets.json`、`build*.bat`、CTest/现有 Test/Smoke 可执行程序。优先借用现成命令；**禁止假造某个 Editor CLI 参数或 test target**。如果没有自动化入口，先创建独立小程序／单测 target，再根据真实工程编译系统接入。
3. 先完整构建未修改的 Debug 与 Release，记录精确命令和退出码；若现有工程已失败，保存未修改时的构建日志，状态 BLOCKED，不能将失败归因于本计划。
4. 确认 D3D12 Debug Layer / GPU-Based Validation 在目标机器上的启用方式：Debug Layer 必须先于 Device 创建；GBV 应在 Device 创建前通过 Debug 接口设置。无该 SDK/驱动时按环境不支持记录为 SKIP，不能假装验证已通过。
5. 为测试定义稳定输出目录，例如 `<repo>/TestArtifacts/RHI/<date>/<case_id>/`，保留 `environment.txt`、`command.txt`、`result.json`、`stdout.log`、必要的 GPU debug messages。路径应遵循项目忽略规则，避免把日志误提交到源码。

### M0-B. 最小测试框架结构（如果仓库已有则复用）

推荐使用**单个**轻量 `RhiValidation` 测试 target，测试分为纯 CPU 模型测试和真实 GPU 测试，不为了测试导出大量生产接口。仅当现有测试架构无法复用时增加：

```text
Tests/RHI/
  RhiValidationMain.cpp            # case 选择、退出码、日志路径、seed
  ResourceStateValidation.cpp      # CPU oracle / 提交 Barrier 序列
  DescriptorLifetimeValidation.cpp # CPU reservation 模型 + GPU 小堆压测
  SubmissionValidation.cpp         # Queue/Fence/Readback/Resize
```

这是**建议路径而非声称仓库已有路径**；目录按工程约定调整。命令行必须提供 `--case`、`--seed`、`--iterations`、`--output`（可增加选项，但不依赖复杂 CLI 框架）。异常应返回非零退出码；Skip 必须附原因并返回可区分状态，CI 聚合器再决定是否允许 Skip。

测试入口行为约定：

- **PASS**：断言通过、输出可解析的结果和 GPU 验证信息；退出 0。
- **FAIL**：断言失败、DeviceRemoved、Fence 超时、错误的 Barrier/Descriptor；退出非零。
- **SKIP**：目标功能不可用且有准确硬件/环境证据；仅硬件条件项可 SKIP。
- **EXPECTED_FAIL**：M0 故意复现 M1/M2 候选缺陷的测试，**仅基线阶段可记录为已确认预期失败**；M1/M2 验收时必须变为 PASS。避免 baseline 阶段为了全绿而隐藏风险。

### M0-C. 先添加测试辅助，而不是修改算法

**CPU State Oracle：** 使用单独 `expected_state[resource_id][subresource]` 表记录测试所认为的实际状态。测试流程固定为：

```text
for each command_list in submit_order:
    initial_state = expected_state                      // 仅测试模型，不是现有引擎 API
    evaluate_queue_boundary(initial_state, first_use)  // 记录实际发出的 Transition
    replay_local_barriers()                            // 逐条断言 before == current
    apply_operations_and_validate_uav_order()
    expected_state = states_after_execution
compare_to_tracker_committed_states()
```

不要通过 `CommitFinalStates()` 计算 oracle 的 expected 值，否则测试会重复同一个 bug；`D3DResourceStateTracker::RequireState` 在 resource 为空时直接返回，纯 CPU 测试不能用空 `D3DResource` 冒充真实记录。可选方案：把状态判定封装为纯函数供测试调用，或在 GPU fixture 创建极小真实资源并读取 Barrier 记录；不要编写伪造 COM 指针。

**Descriptor CPU 模型：** 模拟 `RecordingOwned/Submitted/Completed`；与真实分配器输出记录对比。GPU 测试使用固定 Buffer 内容并重复 SRV/UAV 读取，校验 Readback 数据；调小 Ring 容量必须采用**测试配置注入**（Debug/Test only），不能更改 Release 固定容量导致其他路径误用。

**Fence Probe：** 提交 A、B 后记录 `submitted_fence`、`completed_fence`、命令池状态；GPU 未完成时**不得**把 `current frame` 与 Fence 完成划等号。可以在 GPU 测试设置足够长的队列工作来制造延迟，但通过 fence 的实际完成值判断条件，不以 Sleep 固定时长作唯一依据。

### M0-D. 必须落地的测试用例（可先 RED）

| Test ID | Setup | Steps | Assertion / Expected | M0 状态 |
|---|---|---|---|---|
| BASE-01 | 2 个真实 CommandList | A、B 录制后一次提交，再改为分别提交 | GPU 输出一致；Fence 单调且非零 | PASS |
| BASE-02 | 2×2 mip/array Texture | mip0 写、mip1 读、全资源读 | 各子资源状态和 Debug Layer 输出可记录 | PASS/预期失败证据 |
| BASE-03 | GenericRead 资源 | A 请求 PixelSRV，B 请求 UAV | 每条 Barrier 前态与测试 oracle 比较 | PASS/EXPECTED_FAIL |
| BASE-04 | 小 Ring、至少两个未完成命令 | 先录制占槽，再申请直至回绕 | 绝不覆盖 in-flight；基线可 EXPECTED_FAIL | PASS/EXPECTED_FAIL |
| BASE-05 | Bindless SRV/UAV | 索引 A 提交后释放，再立即分配 B | A 未完成时 B 不复用 A 的槽 | PASS/EXPECTED_FAIL |
| BASE-06 | 固定最多 16 个活跃资源 | 重复创建/释放 1k、10k 次，待 GPU 完成 | 记录 Queue State 数量曲线，先不强制有界 | BASELINE |
| BASE-07 | 每批触及固定 1-4 个资源 | 向状态表加入 1k/10k 历史资源 | 记录 submit CPU 与 touched 数的关系 | BASELINE |
| BASE-08 | Window Resize/Readback | 连续变更尺寸、异步读取 | 不出现因未释放 GPU 引用导致的 ResizeBuffers 错误 | PASS/环境 SKIP |

M0 不应直接检查未添加的 `RetireState()` 等未来接口。对于预期失败记录：精确地保存异常、CPU/GPU 快照、首次出现的 iteration、seed 与当前 commit。

### M0-E. 最小性能埋点与输出规范

只在已有 `RenderingStates::RenderData()` 及 Context 提交路径等稳定位置加**聚合计数**，不得在每个 Draw 打印日志。建议统计：

```text
frame_id, scene_id, build_config, multithread_enabled, seed
command_lists_recorded, command_lists_submitted, submits
submit_cpu_p50_ms, submit_cpu_p95_ms, queue_state_resolve_cpu_ms
barrier_transition_count, barrier_uav_count, barrier_aliasing_count
ring_allocations, descriptor_copies, descriptor_cache_hits, ring_wraps
bindless_srv_active, bindless_uav_active, queue_state_entries
fence_cpu_submitted, fence_gpu_completed, gpu_frame_time_ms
```

计数原则：增量统计采用记录线程局部或已有聚合机制；跨线程写入不能无锁修改共享非原子结构。某指标无法可靠获取时写 `null` + `unavailable_reason`，不要填 0。各场景至少 3 轮，丢弃明确定义的预热帧；运行参数相同并输出样本数量。

结果 JSON 格式示例（**字段示意，不代表运行过**）：

```json
{
  "case_id": "BASE-03",
  "status": "EXPECTED_FAIL",
  "seed": 17,
  "iterations": 1000,
  "git_sha": "<runtime-sha>",
  "build_config": "Debug",
  "validation": {"debug_layer": true, "gpu_based_validation": true},
  "metrics": {"submit_cpu_p50_ms": null, "queue_state_entries": 123},
  "evidence": ["stdout.log", "debug_messages.log"]
}
```

### M0-F. 每个子任务的交付与停止条件

- T0.1 原工程构建：输出 `baseline-build.md`，含 Debug/Release 精确命令及日志路径。
- T0.2 纯逻辑与 GPU fixtures：`--help` 和 `--case BASE-01` 可运行，未知 case 非零退出。
- T0.3 风险复现：BASE-03/04/05 具备可再次运行的 deterministic seed；若无法复现，报告不可复现条件及被测调用链而不是改写预期。
- T0.4 性能基线：结果存档、解析脚本和至少三个有效样本；禁用埋点后行为不变。
- 任一步涉及工程原有损坏、无法识别 GPU 同步、测试可能静默挂死时，记录 BLOCKED 并停在 M0。**只有基线测试工具本身稳定、生产工程未回归，才可晋级 M1。**

### M0-G. 执行者必须回答的五个问题

1. 实际生产构建命令是什么？Debug Layer/GBV 具体在哪台机器成功启用？
2. 当前在 `ExecuteRHICommandBuffers` 前后哪些线程可录制 CommandList？
3. 哪些测试为 PASS、哪些是可重复 EXPECTED_FAIL？失败的最小调用序列是什么？
4. 当前哪些指标可测、哪些不能，原因是什么？
5. M1 必须先处理的 Descriptor 调用者是否全部出现在清单中？


## Acceptance Criteria

- [ ] Debug/Release 基线构建成功（或明确列出原有不可构建原因及证明，标记 BLOCKED，不能过关）。
- [ ] 测试工具可重复运行并返回正确退出码；针对后续里程碑的候选缺陷，允许在 M0 保留证据齐全、固定 seed 可复现的 `EXPECTED_FAIL`（后续 M1/M2 必须转 PASS）；GPU Debug Layer / GBV 可用性及 SKIP 原因有记录。
- [ ] Descriptor 录制在途、Bindless 复用、Resize、Subresource 至少各有一个自动化回归入口。
- [ ] 三组基线各运行至少三次，CPU/GPU/内存及 State/Descriptor 统计落盘。
- [ ] 将所有已知失败记入 `progress.md`；明确 M1/M2 的验证输入和预期结果。
- [ ] M0 全部达标并有 commit/日志证据前，不开始 M1。

## Validation

执行上面发现的真实构建、测试、Editor 场景命令；具体命令由执行者填入 `progress.md`。测试失败不能通过「忽略断言」来制造绿色结果。

## Decision Log / Surprises & Discoveries

记录在 `progress.md`。需要决策时优先最小测试入口，不创建并行的第二套 Job System 或完整测试框架。

## Progress / Known Follow-ups / Outcomes / Resume Protocol

- Progress: TODO。下一步：定位构建入口并添加首个纯逻辑状态用例。
- Follow-ups: 更复杂的全自动后台 Editor 验证属于另一个执行计划，非此 Milestone 阻塞条件。
- Outcomes: 尚未实施。
- Resume: 只读根 README、`progress.md`、本文件；更新证据和下一步。
