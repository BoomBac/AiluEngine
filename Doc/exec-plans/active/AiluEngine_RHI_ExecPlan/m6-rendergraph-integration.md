# M6 — RenderGraph/RHI Contract, Integration & Closure

**Status:** TODO　**Prerequisite:** M5 PASSED　**Exit gate:** 所有 RHI 使用路径在同一状态/寿命契约下工作，完整回归及性能报告通过。

## Purpose

审计 RenderGraph、直接 CommandBuffer、GPU Native callback、外部 SDK、Readback、Resize 与新的 RHI 分工，收尾所有跨层契约，进行长时间稳定性与真实性能验证。

## Scope

RenderGraph Import/Read/Write、资源/视图/子资源管理、临时资源寿命、GPU 状态/描述符语义、Editor/Renderer/DXR/SDK 的集成回归，性能与文档整理。

## Non-goals

- 不在最后阶段追加新的 RenderGraph 编译优化器或复杂 transient alias allocator。
- 不把 DLSS/FSR/Streamline/OIDN 的功能增强作为 RHI 验收前置；只验证现有集成不回归。
- 不启动 Vulkan/Metal、Async Compute、Enhanced Barriers 等独立工程。

## Architecture Constraints

- RenderGraph **声明资源访问意图/执行依赖**；RHI **维护真实 Native Resource 状态并落实 Barrier**。避免两个模块对同一物理状态分别维护冲突的真相源。
- 不要求所有操作都必须在 RenderGraph 中完成：上传、Readback、Present、Editor ImGui、Native SDK 等绕过图的路径须有清楚的入口/出口状态约定。
- Import 同名但不同 Native Resource 的场景必须区分资源身份和状态，不能依赖字符串名做唯一真实身份。
- RenderGraph 静态纹理 Read、帧跨越资源、Counter Buffer、AS 和独立 Subresource 均遵守前面修复的 State ID / Descriptor lifetime 契约。
- 防止扩展计划导致新类数和层级膨胀；在完成验收之前不删除便于比较的测试与指标。

## Baseline

- `Src/Render/RenderGraph/RenderGraph.cpp` 编译图阶段输出资源要求，执行阶段对资源调用 `CommandBuffer::RequireState`。
- `CommandBuffer` 支持 RenderGraph 标记、`RecordNativeCallback`、高层 Draw/Dispatch。
- `D3DContext` 具备 Readback、Swapchain Resize、DXR 命令处理、外部 GPU Fence/Signal 等非 RenderGraph 使用路径。

## Target Outcome

- 一份小而稳定的 RHI / RenderGraph 使用约定，覆盖外部状态声明、ownership、Subresource、提交队列和 Barrier 生成。
- 最终测试矩阵在 Debug/Release、单线程/并行场景下通过；静态泄漏/状态积累/Descriptor 复用风险没有遗留。
- 明确的性能前后对比、代码变更摘要、已知限制与后续独立课题。

## Relevant Files

`Inc/Render/RenderGraph/RenderGraph.h`, `Src/Render/RenderGraph/RenderGraph.cpp`, `Inc/Render/CommandBuffer.h`, `Src/Render/CommandBuffer.cpp`, `Inc/Render/GpuResource.h`, `Src/Render/GpuResource.cpp`, `Src/RHI/DX12/D3DContext.cpp`, `D3DCommandBuffer.cpp`, `DescriptorManager.cpp`, `D3DQueueResourceStateTracker.cpp`；需要时检查 Renderer/Editor/Shader/DXR/SDK 实际调用方。

## Implementation Steps

### Step 1 — 建立 RenderGraph/RHI 契约表

- RG compile 拿到的 `ResourceAccess` 仅表示逻辑读写需求，不应随意声称资源当前物理 State。
- RHI CommandRecorder 根据资源实际使用生成最小 State Requirements；Queue 决定进入态/跨列表 Barrier；实现与 M2 保持一致。
- 以 Native State ID（含 Subresource）作为真实跟踪键；RG Handle 与 Resource Import/Resolve 负责正确映射，不混用 Resource Name 与指针 identity。
- Native callback、外部 SDK（OIDN/Streamline/FSR）、Copy/Readback 必须提供：进入状态、所有权、排队方式、退出状态、最后 Fence。若现有集成无法声明全部条件，列出明确接入包装而非依赖侥幸状态。
- 文档写出**唯一状态所有者**、谁发 Barrier、谁退役 Descriptor、谁持有 Native Resource、谁完成 Fence 的表格，存放在工程合适的 `docs/` 路径。

### Step 2 — 穿透式自动化用例

| ID | 路径 | 验证目标 |
|---|---|---|
| RG01 | Import A → Read → Write → Export | 资源状态、提交顺序、最后物理状态正确 |
| RG02 | 同名 Import A 后替换为 Native B | State ID 不串资源、Descriptor 不指向旧资源 |
| RG03 | 静态 Texture Read、每帧重复 Read | 不产生不合法 Transition / 历史状态无限增长 |
| RG04 | 多 mip/array Subresource Read/Write | 不访问的 Subresource 状态保持原样 |
| RG05 | 生产/消费 UAV、不同 CommandList | 正确 Transition + 必要 UAV Barrier |
| RG06 | Async Readback / Screenshot / FrameCapture | Native 引用直到完成，回调没有悬挂对象 |
| RG07 | Editor RT / Swapchain Resize / Multi-window | 无 ResizeBuffers Invalid Call，旧状态退役 |
| RG08 | Material PSO 热重载 + Descriptor 更新 | 旧 Shader/PSO/Descriptor 在途资源安全 |
| RG09 | DXR AS build + DispatchRays（支持时） | UAV / AS 状态和生命周期正确 |
| RG10 | 外部 SDK 或 Native callback（启用时） | 声明状态与最后 Fence 相符 |

- 以上用例尽量复用现有 RenderGraph / Editor / AiluTestProj 场景，不为方便测试同时迁移渲染架构。
- 以异常路径覆盖：Frame 被取消、Worker Stop、设备失效、CommandList 为空、Readback callback 迟到、窗口最小化/尺寸为 0。
- 以长跑覆盖：至少连续 30 分钟高频动态资源 churn 或等量可重复提交压力，随后 WaitForGpu/Collect 统计没有稳态泄漏。若机器/时间限制无法满足，需记录替代用例和原因，并保持阻塞状态直至获明确确认。

### Step 3 — 性能验收与代码复杂度核对

- 和 M0 同硬件/驱动/分辨率/场景比较：CPU Submit p50/p95、Command Recording p50/p95、Barrier 数、Ring Copy/Cache、StateMap Size、显存、GPU Frame。
- 分别输出 M0 基线、M1/M2/M3/M4/M5 后数据，区分正确性强制 Barrier 开销和纯结构开销。
- 性能判断：相同工作负载下无稳定可复现、不可解释的显著退化；发现时精确定位到改动阶段并修正，不能因为“架构更现代”直接接受。
- 统计公开接口个数、热点额外间接调用、代码依赖方向、新增类数量；凡引入但没有明确责任的对象应当删除或合并。

### Step 4 — 交付与保留回归资产

- 保留核心测试（ST/CMD/RG、Descriptor 压测），让日后改 RenderGraph、PSO、Shader 或 DXR 可复用。
- 对架构、线程与寿命绘制**文字状态机/示意图**，不需要外部图片资源。
- 将 README 的 Baseline 与 Target Outcome 对比，填写实际 Outcomes；未实现项目写 Known Follow-ups。
- 最终记录 commit 列表、失败与修复列表、工程构建命令、测试产物路径、后续回归入口。

## Implementation Blueprint — RenderGraph/RHI 接缝全覆盖与项目收尾

> M6 是**集成验收**，不是再启动一次新 RHI 重写。只修复前面 Milestone 整合暴露出的真实缺陷；所有新增需求进入 Known Follow-ups。核心任务是让 RenderGraph、Immediate、Native callback、外部 SDK 共用同一套 ResourceState/Descriptor/Fence 约定。

### M6-A. 先形成一页状态与所有权契约

结合 `RenderGraph.cpp::CompileResourceUsages`、`RenderGraph::Execute` 的实际调用，写 `rhi-rendergraph-contract.md`，强制包含下表并填上源函数：

| 阶段 | 负责者 | 允许的操作 | 禁止的行为 |
|---|---|---|---|
| Pass Setup / RG Compile | RenderGraph | 声明每个 RGHandle 的 Read/Write、Subresource、依赖 | 假设一个资源的 Native D3D12 当前 State |
| Import / Resolve | RenderGraph / Resource registry | 从逻辑资源解析到当次 Native Resource + StateId | 仅用 Name 区分真实资源身份 |
| Frontend Record | CommandBuffer/GfxCommand | 记录 `RequireState`/Draw/Copy/Dispatch | 在 Packet 构造期自行回收 Native Descriptor |
| Backend Record | D3DCommandBuffer | 将访问要求变成 Local State 和 Native Barrier | 未声明前后状态就静默修改外部资源状态 |
| Queue Submit | D3DContext / QueueTracker | 根据上一提交终态补 Boundary Barrier，Signal Fence | 以 frame_id 代替 Fence 完成 |
| GPU completion/Retire | Resource/Descriptor lifecycle | 释放引用、可重用 Descriptor/StateId 记录 | 已提交未完成就重用 Descriptor 槽 |

明确 RG 缓存中的 `RGHandle` 是**图内标识**，`GpuResourceHandle` 是上层资源句柄，`ResourceStateId` 是 Native 真实状态键；三者不应混用。`RenderGraph::Import` 同名但不同 Native 资源时，必须把新资源关联到正确 Native ID，不能只复用旧状态/Descriptor 缓存。

### M6-B. 最小自动回归夹具（先复用已有工程）

不要求搭建新渲染框架。优先通过 M0 测试 target + 现有 RenderGraph Editor/测试场景新增**脚本式 Pass**。推荐一个固定小 Fixture：

```text
TestTextureA:  64x64, RGBA8, mips=4, arrays=2
TestTextureB:  64x64, RGBA8, mips=4, arrays=2
TestBufferA:   4096 bytes, pattern based on seed
TestCounter:   16 bytes
TestSwapchain: optionally current window backbuffer
```

输入固定 `seed`、格式、mip/array、渲染分辨率；为每个输出计算 CRC/精确整数期望。涉及光照/TAA 等不完全确定的浮点结果时使用合理的 RMSE/最大误差阈值，并保存设置和硬件信息。**不要**仅以肉眼判断“没变”作为自动验收。

### M6-C. RG01-RG10 的逐步动作与断言

**RG01：标准 Import → Read → Write → Export**

- 预置 TestTextureA，图外以已知状态进入；Pass1 Read(A)，Pass2 UAV Write(A 或 B)，Pass3 Copy/Readback。
- 对每个 RG pass 输出 `RGHandle, resolved Native StateId, subresource, required state`；对 Queue 输出有序 Barrier trace。
- 校验依赖拓扑、GPU 结果 CRC、提交终态和下一帧再次 Import 的进入态；不得因为 Pass 结尾逻辑状态与队列物理状态不同而报错。

**RG02：同名 Import 换对象**

- 第一帧 Import 名称 `Color` 对应 Native A；第二帧同名 Import 对应 Native B（B 预先填不同数据）。
- Assert：图内缓存可以保留同名 Name，但 `Native StateId`/Descriptor 不能复用 A 的内容；B 的 Readback 必须返回 B 的测试图案。
- A 仍在 GPU in-flight 时提交 B，验证旧 Descriptor 和状态不会被提前复用。包含一帧替换多次及延迟 Fence。

**RG03：静态纹理重复 Read**

- 长跑 1000 帧：静态 SRV 每帧采样，其他动态资源按负载变化。
- Assert：状态一致、Descriptor lifetime 正确；不会随访问次数产生无法解释的 StateMap 条目增长（允许稳定 cache），提交 CPU 不随帧数持续增长。

**RG04：Subresource 正确性**

- 创建 4 mip × 2 array，逐个子资源以不同 pattern 填充；Pass A 仅写 `(mip=1,array=0)`，Pass B 仅读 `(mip=0,array=1)`。
- 比较 8 个子资源的 Barrier、数据和终态；未访问的子资源不得莫名 Transition 或被污染。然后全资源读，检查局部状态展开和折叠。

**RG05：UAV WAW/RAW 与跨 CommandList**

- 两个 Compute Pass 对同一 UAV 写不同 pattern；中间显式同步；分别同列表和跨列表录制。
- Assert：结果序列精确、UAV Barrier 未被状态兼容/Barrier Cache 合并吞掉；Debug Layer/GBV 无 hazard。

**RG06：Readback / Capture**

- 提交 Copy/Readback 后立即释放调用方高级引用，故意让 Readback callback 延后执行。
- Assert：GPU 数据正确、callback 恰好一次、CommandList/UploadBuffer/资源引用直到 completion，捕获错误有可追踪状态。不要把 `RunPostSubmitCallbacks()` 误认为 GPU 数据已经可读。

**RG07：Editor RT + Resize + 多窗口**

- 反复创建/销毁 Editor RT、执行 0 尺寸 minimize→restore、交替变更两窗口尺寸。
- Assert：`ResizeBuffers` 不因 ComPtr 未释放而失败；旧 backbuffer 不再被新状态映射引用；后台录制在帧边界 resize 前结束或由正确 Fence 保护。

**RG08：PSO/Shader/Descriptor 热更新**

- 对同一材质连续两次 Shader 变体热重载，同时保留旧帧 GPU in-flight；验证新的 Pipeline/Descriptor 与旧帧各自使用正确版本。
- Assert：缓存 key、不丢异步编译、旧资源 pending retire 而不被过早覆盖；不需要增加新 PSO 缓存机制。

**RG09：DXR AS Build + DispatchRays**

- 仅在 `IsHardwareRayTracingSupported()` 时执行；构建 BLAS/TLAS，随后 DispatchRays 写 Buffer/Texture。
- Assert：AS 状态、Scratch Buffer、Descriptor/NativeResource lifetime 满足提交 Fence；不支持时 SKIP 必须有设备特性查询输出。

**RG10：外部 SDK / Native callback**

- 选一个当前能实际工作的 Native callback/FSR/Streamline/OIDN 集成作为 smoke；记录进入态、外部 Queue/CommandList、访问资源、退出态、最后完成条件。
- Assert：回到 RHI 时 `SetExternalState` 与真实 GPU 状态一致，跨 Queue 所有权/同步可证明。SDK 在当前环境不可用时以最小 Native callback 路径补足契约测试，SDK 专项才可 SKIP。

### M6-D. 产物结构与验证命令记录

每个用例输出：

```text
TestArtifacts/RHI/Final/<build>/<case_id>/
  args.json                  # seed, iterations, build, GPU, driver, SDK
  stdout.log                 # 测试日志
  validation_messages.log    # DX12 Debug Layer/GBV 信息
  barriers.csv               # command_id/resource_id/subresource/kind/before/after
  lifetimes.csv              # reservation/retire/owner/submitted_fence/completed_fence
  metrics.json               # CPU/GPU 中位数、p95、峰值、样本数量
  result.json                # PASS/FAIL/SKIP 与失败原因
```

目录为可调整的**建议模板**。真正测试命令从 M0 的 `baseline-build.md` 和 test runner 获得，不能在本 Milestone 虚构一个存在的 EXE 参数。新测试加入现有 `--case RGxx` 入口，如现有运行器只接受另一格式，就提供相应适配。

### M6-E. 错误注入与长时运行（不要只测正常帧）

- CommandList 录制到一半取消，验证 M1 的 RecordingOwned Descriptor 被释放，Queue State 不进入假提交终态。
- 在一个 Batch 中混合空 CommandList、带 External Wait/Signal、普通 Graphics、Readback，检查状态与 Fence 单调。
- Window resize 正好发生在旧命令已录但未提交时，确保帧边界同步或显式失效路径正确。
- Buffer/Texture 高频创建销毁（活跃上限固定），至少 30 分钟或事先批准的等效提交次数；验证 `StateMap`, Descriptor Pending 数量、Native ComPtr 占用最终回落。
- 强制触发 Ring 容量耗尽，必须得到受控错误/明确退化，而非继续返回已使用 Handle。
- 捕获 Worker 停止、应用退出、Device Removed/Reset（若可复现）的错误路径。不可把无法安全恢复的 DeviceRemoved 当作普通成功；应有明确失败/退出状态与错误日志。

### M6-F. 性能对比不变量与回退判定

每个场景至少保留基线 3 次、最终 3 次，条件一致：同一 GPU/驱动/显示分辨率/场景随机种子/线程策略/Build 配置。测量：

1. `command_record_cpu_ms` p50/p95，`submit_cpu_ms` p50/p95，Shader Compile time（仅涉及 Shader 的 case）。
2. `transition_barrier_count/uav_barrier_count`，预期严格正确性修复可能增加 Transition；注明原因而非硬性要求更少。
3. `descriptor_copy/cache_hit`, `ring_peak/pending`, `bindless_active/pending`，不能使用全帧平均掩盖在途峰值。
4. `queue_state_live/pending_retire/retired` 与 `touched_states_per_submit`；固定活跃资源负载下不能随总创建次数无限增长。
5. GPU Frame ms、显存驻留峰值、CPU 提交锁等待时间（可测时）。

推荐先设**预警阈值**而非通过拍脑袋的 GPU 硬性指标：p95 CPU submit/record > 基线 1.05 倍且超过测试样本波动时深入定位；GPU 时间 >1.05 倍先确认并发/温度/场景差异；只有解释清楚并经实际验证才能接受。该阈值用于触发分析，**不是**无条件 5% 内即可通过的规则。

### M6-G. 最终验收的严格退出顺序

```text
RG01-RG10 测试通过/有证据的硬件 SKIP
  -> 所有 M1-M4 专项测试再次通过
  -> Debug Layer/GBV 无新增错误
  -> 30min churn + Fence drain 后数值回归稳定
  -> Debug/Release 渲染 / Editor / Headless smoke
  -> CPU/GPU/内存对比报告签收
  -> 更新 README / progress.md / Known Follow-ups / Outcomes
  -> 标记 M6 PASSED、整体 COMPLETE
```

对照最终结果，写 `final-rhi-review.md`：

- `Before -> After` 源码责任图（实际实现类名和主要方法）以及 Native Resource/Descriptor/State/Fence 的单一所有者。
- 各 Milestone 关键 commit、可重复命令、日志路径、通过/失败/SKIP 数、遗留缺陷及风险分类。
- 哪些静态风险被确认、哪些因调用链已防护无需改、哪些已重写为明确不变量；不能把“计划发现的问题”全部宣称已修复。
- 是否仍有原工程基线噪声，是否属于本计划，是否需要新 issue。

### M6-G2. 单个子任务顺序（与 progress.md 对齐）

- **T6.1 — 契约：** 先输出 `rhi-rendergraph-contract.md`，填齐六行职责表和 10 条实际调用路径；每条结论引用真实函数。无需生产代码改动。
- **T6.2 — Fixture：** 在 M0 现有测试 runner 中逐个实现 RG01~RG10；每新增一个 case 立即单独编译和运行，失败不能先跳过。所有输出写 `TestArtifacts/RHI/Final`。
- **T6.3 — Stress：** 执行异常注入、30min churn（或经明确批准的等价压力）及 Debug Layer/GBV；每一失败记录 seed、Fence、资源 ID、日志路径并修复。
- **T6.4 — Close：** 做性能对比和 `final-rhi-review.md`，逐项复测 M1~M5 Acceptance，补 `README/ progress` Outcomes；全部证据齐全才标记 COMPLETE。

### M6-H. 强制停止条件与不属于本轮的工作

未通过核心 RG/CPU State/Descriptor 测试、GPU GBV 出现新错误、StateMap 稳态增长无解释、RHI 与 RG 保留两套冲突的物理状态源、或跨队列 SDK 没有明确同步约定，任何一个存在都不能写 COMPLETE。

D3D12MA/Placed Resource、Enhanced Barriers、真正 Async Compute、多 API、多 GPU、RenderGraph 全新编译器和 PSO 磁盘缓存均继续列在 Known Follow-ups；即使看起来“更现代”，也不是 M6 的附加实现任务。


## Acceptance Criteria

- [ ] RG01-RG10 均通过或存在经过证明的明确环境 SKIP（如无 DXR 硬件），所有核心非硬件依赖项不能 SKIP。
- [ ] Debug Layer / GPU-Based Validation 无新错误；原有错误如仍存在必须明确未关联范围和证据。
- [ ] 30 分钟或约定等效压力后 Descriptor Ring、Bindless Pending 和 Queue State Map 均没有非预期增长；Fence 等待无死锁。
- [ ] Editor 普通工作流、Resize、多线程开关、Shader 热重载、Readback、FrameDebugger/DXR 现有路径无功能回归。
- [ ] 完整性能对比有基线、样本、环境、波动与解释，无不可解释的显著退化。
- [ ] `README.md` 与 `progress.md` 的结果、决策、Known Follow-ups 完整；最终 `Outcomes` 与代码 commit 对应。
- [ ] 全部验收通过后可标记项目 `COMPLETE`，否则继续处于 M6，不将剩余问题默认为已完成。

## Decision Log / Surprises / Validation / Progress / Known Follow-ups / Outcomes / Resume Protocol

- Decision: 不因测试便利强制将所有 Native SDK 都移入 RenderGraph；契约一致优先。
- Discovery: 对直接获取 GPU Descriptor Handle 的外部组件，验证 M1 的寿命保护没有遗漏。
- Validation: RG01-RG10 + 各前置阶段所有测试 + 性能 + 长跑。
- Progress: TODO；Next: 创建 RG/非 RG 接入路径清单。
- Follow-ups: D3D12MA、Multi-queue、Bindless 进一步统一等单独任务。
- Outcomes: 未实施。Resume: README + progress + 本文件。
