# M4 — Command Packet Type Safety & Submission Ownership

**Status:** TODO　**Prerequisite:** M3 PASSED　**Exit gate:** GfxCommand 构造/Reset/销毁有严格类型安全，Frontend → Recorder → Queue 的所有权可证明。

## Purpose

保持现有低分配开销的 `CommandPool` 和双级命令录制模式，清理可能产生 UB 的命令包 Reset、完善资源和回调生命周期，避免渲染线程/提交线程的竞态与结构不清晰。

## Scope

`GfxCommand`, `TypedGfxCommand`, `SafeResetCommand`, `CommandPool`, `CommandPayload`；`CommandBuffer`, `RHICommandBufferPool`, `D3DCommandBuffer`, `GpuCommandWorker`、录制分组与提交边界。

## Non-goals

- 不用巨型 std::variant 或虚函数命令层全面替代当前 Packet 架构。
- 不改变 DrawMesh/Dispatch 等公共渲染调用方式，不删除 RenderGraph CommandBuffer。
- 不做大规模渲染线程调度器重写；不与 M5 Context 拆分同时进行。

## Architecture Constraints

- 活跃对象必须有恰好一次有效构造、至多一次合法销毁；对非 trivially copyable / trivially default constructible 的类型不可任意 `memset` 复位。
- 命令必须以明确的类型分派调用正确的析构；`CommandCustom` `std::function`、`CommandReadBack` 回调、`CommandReadBackTexture` 的 `Vector`、Material snapshots 均视为非平凡。
- Producer/Consumer 生命周期：命令包直到 Worker 读取完后才能回池；Native CommandList 的 GPU 持有资源直到 Fence 完成后才能释放；Submit 回调不能访问已销毁的栈变量。
- 以**类型级**保证安全，不能仅靠“现在没调用这条路径”证明 `SafeResetCommand` 合法。
- 尽可能避免新增热路径堆分配、全局锁或逐 Draw 动态分派。

## Baseline

- `Inc/Render/GfxCommand.h::SafeResetCommand<T,TBase>` 将 `sizeof(TBase)` 后的派生对象尾部用 `memset(...,0,...)` 清零。
- `CommandCustom::_func` 是 `std::function`，`CommandReadBackTexture::_sub_resources` 是 `Vector`，`CommandReadBack::_callback` 是回调；该类实例如果使用当前 SafeReset 实现会触发非平凡对象状态破坏风险。
- 读取的 `CommandBuffer.cpp` 中未直接定位到上述命令成员 `Reset()` 的显式调用，不能据此声称运行时已复现 UB。
- `GfxCommand.cpp::DestroyCommand` 是类型 switch 手动调用具体析构；已核对该版本缺少 `kBuildAS` 和 `kDispatchRays` 两个分支，而 `CommandBuffer.cpp` 已实际分配对应命令。**该遗漏应作为本阶段明确修复项**；此模式本身可以保持，但新增命令类型必须覆盖。

## Target Outcome

- 删除或限制不安全的按字节 Reset；每种命令的可用状态、Reset 和销毁语义可静态检查。
- Command Pool 的生命周期规则可通过一组自动化测试覆盖，资源强引用从 Recorder 到 Fence 的交接路径明确。
- 多线程录制与串行录制产生相同可观察结果，不发生重复释放或提前释放。

## Relevant Files

`Inc/Render/GfxCommand.h`, `Src/Render/GfxCommand.cpp`, `Inc/Render/CommandBuffer.h`, `Src/Render/CommandBuffer.cpp`, `Inc/RHI/DX12/D3DCommandBuffer.h`, `Src/RHI/DX12/D3DCommandBuffer.cpp`, `Src/RHI/DX12/D3DContext.cpp`, `Inc/Render/GpuResource.h`.

## Implementation Steps

### Step 1 — 完整命令类型与 Reset 调用扫描

- 列举全部 `EGpuCommandType`，对应 struct、`DestroyCommand` 分支、CommandPool 分配/释放和 `Reset` 调用。
- 使用 `static_assert` 或测试报告区分是否 `std::is_trivially_copyable_v<T>` / `std::is_trivially_destructible_v<T>`；不要使用偏移 `sizeof(TBase)` 的字节清零处理继承对象布局。
- 检查 `kBuildAS`、`kDispatchRays` 和各种 ReadBack 命令的 Destroy switch 是否完整；发现缺失在同一阶段补齐，并回归。
- 查询 `CommandPool` 实际是重用已构造命令对象，还是每次在 payload 内 placement new；Reset 策略必须匹配真实所有权模型。

### Step 2 — 替换危险 Reset，最小化改动

推荐优先级：

1. 若 Reset 根本不在生产路径被调用，**删除不安全 Reset 模板及其未使用调用者方法**，而不是为了兼容留潜在 UB。
2. 若必须重用：对各类型用显式成员 Reset；非平凡成员采用 `clear` / `std::function{}` / 正常赋值，`UploadParams*` 的所有权释放按析构逻辑对称处理。
3. 只有在对象完整析构、构造条件满足且 payload 内存对齐有保证时，才考虑 `std::destroy_at + std::construct_at`；必须保证 `_type` 和派生默认值重新初始化。
4. 单纯的 POD 命令可以使用安全的 aggregate 重新赋值，但不能基于 `sizeof(Base)` 部分 `memset` 假定布局稳定。

注意 `CommandGpuResourceUpload::~CommandGpuResourceUpload()` 会删除 `_params`。Reset 绝对不能先丢失该指针而不调用合法清理。

### Step 3 — 审查生命周期与线程契约

- 明确 frontend CommandBuffer 提交后是否允许继续修改；若禁止则 assert；如果允许则要求 snapshot/value ownership。
- 追踪 `GpuCommandWorker::RecordCommandGroup / SubmitRecordedCommandBuffers`；确保分组录制失败、无命令、重复提交、Stop、Shutdown 均有对称释放。
- 检查 `D3DCommandBuffer::MarkSubmitted`、`Finalize`、`RunPostSubmitCallbacks`、`ReleaseTrackedResources`、`RHICommandBufferPool::ReleaseCompletedTrackedResources` 的调用顺序；不得提前 Reset CommandAllocator 或 ComPtr。
- 对 ExternalGpuSignal/Wait、UploadBuffer/Temp allocations、PostSubmit callback 捕获、DXR AS build 等进行生存期检查。
- 只做必要的模块内辅助函数提取，不在本阶段改变公开 `GraphicsContext` 虚接口。

### Step 4 — 小范围结构整理与诊断

- 为 Packet Pool 添加测试期 `construct/destruct/acquire/release` 计数及 double-return / use-after-reset assert。
- `GpuCommandWorker` 在异常与停止时应确保未提交的命令不会错误标记为已提交。
- 对非 RAII 裸指针和 Payload 统一写清单所有权注释；所有权仍不明确的地方，保留阻塞项直至查清。

## Required Tests

| ID | 测试 | 目标 |
|---|---|---|
| CMD01 | 每个 `GfxCommand` 构造→入池→取出→销毁循环 | 析构次数匹配、无泄漏 |
| CMD02 | `std::function` 捕获强引用、取消录制 | 引用在正确线程/时机释放 |
| CMD03 | ReadBackTexture Vector 扩容、重置 | 无非法内存写与双释放 |
| CMD04 | 多线程批录→乱序完成→统一提交 | 数据包稳定、分组顺序可预测 |
| CMD05 | CommandPool 空/满/并发增长、异常回收 | 无竞态/死锁 |
| CMD06 | Resize + GPU in-flight + Worker Stop | 不能持有失效 Backbuffer 或提前 Release |

建议可用条件下采用 MSVC AddressSanitizer/运行时检查 + 自定义分配统计，GPU 对象仍需 Fence 验证。

## Implementation Blueprint — GfxCommand 安全与 Worker 所有权

> 这阶段目标是**堵住明确的 C++ 对象生命周期风险**，不顺手将全部 Command Packet 改成 `std::variant`、虚函数命令或全新分配器。可优先修已发现的缺失 Destroy case，再处理 Reset，再做 Worker/Pool 边界。

### M4-A. 已定位的两个具体事实，优先验证

1. `GfxCommand.h` 定义了 `EGpuCommandType::kBuildAS` / `kDispatchRays` 和对应 `CommandBuildAS` / `CommandDispatchRays`；`CommandBuffer.cpp` 中有 `CommandPool::Get().Alloc<CommandBuildAS>()`、`Alloc<CommandDispatchRays>()` 实际路径。
2. 2026-10-19 源码 `GfxCommand.cpp::DestroyCommand` switch **没有**这两个枚举分支；`GfxCommandTypeName` 也缺对应分支。`DeAlloc` 会先根据命令类型分派析构，然后归还原始 payload。如果缺 case，含有非平凡成员的 packet 将跳过正常析构，随后该内存又被 placement new，违反对象生命周期要求。

**T4.1 的最小修复：** 在 `DestroyCommand()` 与 `GfxCommandTypeName()` 补齐这两项，编译并运行 command 生命周期单测；不要一次把所有命令类型换成宏注册表。

### M4-B. 建立类型矩阵，模型必须逐个核对

列举全部 `EGpuCommandType`，形成 `gfx-command-type-audit.md`：

| Enum | Concrete type | Alloc site | Destructor case | Payload size/alignment | Reset site | Owns resources? |
|---|---|---|---|---|---|---|
| kDraw | CommandDraw | CommandBuffer.cpp | 有 | `kCommandPayloadSize/Align` | 待查 | MaterialDrawState 等 |
| kResourceUpload | CommandGpuResourceUpload | CommandBuffer.cpp | 有 | 同上 | 待查 | `UploadParams*` 独占 |
| kCustom | CommandCustom | CommandBuffer.cpp | 有 | 同上 | 待查 | `std::function` |
| kReadBackTexture | CommandReadBackTexture | CommandBuffer.cpp | 有 | 同上 | 待查 | Vector + 回调 |
| kBuildAS | CommandBuildAS | CommandBuffer.cpp | **缺** | 已包含 | 待查 | 结构体/句柄 |
| kDispatchRays | CommandDispatchRays | CommandBuffer.cpp | **缺** | 已包含 | 待查 | DispatchBindingSnapshot |

以上只列重点；**实施时必须枚举 enum 中全部值**，不能误以为示例表完整。可用短脚本提取 Enum 和两个 switch 的 case 集合自动报告差集；脚本不必成为新长期依赖，测试中要保留能防止未来新增 enum 漏加 Destroy 的断言/编译期映射检查。

### M4-C. 当前 `SafeResetCommand` 为什么不合法，以及最小替代

当前模板把 `sizeof(TBase)` 之后的对象字节全部清零：

```cpp
std::memset(reinterpret_cast<uint8_t*>(cmd) + sizeof(TBase), 0, sizeof(T) - sizeof(TBase));
```

它既假设继承对象后缀的布局满足这个偏移，也会破坏 `std::function`、`Vector`、`String`、包含资源所有权的成员。`CommandGpuResourceUpload::~CommandGpuResourceUpload()` 会 `AL_DELETE(_params)`，清零 `_params` 可能直接泄漏它。即使未发生线上 Reset 调用，也不应该继续公开这个泛型操作。

**推荐执行顺序：**

1. 全仓 `rg 'SafeResetCommand|\.Reset\(\)'` 并结合类型过滤找出**实际调用**；特别分清 `D3DCommandBuffer::Clear()`、`RenderingStates::Reset()` 与 `GfxCommand` 子类 Reset。
2. 如果命令对象仅使用 `CommandPool::Alloc<T>()` 的 placement new 构造、`CommandPool::DeAlloc` 的 `DestroyCommand` 正确析构后归还原始 payload，则**不需要 command-level Reset**；删除未使用的 `SafeResetCommand` 及相应无意义 `Reset()`，保持 pool 原有流程。
3. 如果确有命令级复用调用方，优先写**具体类型**的赋值与清理，确保所有持有引用被正确释放。不要把整片内存置零当默认值；禁止调用基类 `GfxCommand` 的非虚析构来代替派生析构。
4. 如果类型确实默认可构造、完整生命周期已结束，可使用 `std::destroy_at(cmd)` + `std::construct_at(cmd)` **在保证 storage 对齐的同一地址**重建；注意重建失败后的 pool 对象有效性和 `_type` 初始化，且不能在对象仍被 Worker 使用时调用。
5. `CommandProfiler::Reset` 原本显式清 `String`，可以单独保留；不需要为了形式统一都改成模板。

建议简单删除所有没有调用的 Reset 声明，而不是添加几十个 Reset 特化函数。这符合当前 CommandPool 每次 placement new 的构造约定。

### M4-D. Pool 的对象生命周期证明

根据源码当前结构：

```text
CommandPool::Init -> 分配 CommandPayload block
CommandPool::Alloc<T>
    -> AcquirePayload，_in_use=true
    -> ConstructCommand<T> 在 payload Data() 原地构造
CommandBuffer::Impl::_commands 保存 GfxCommand*
CommandBuffer::Impl::Clear
    -> CommandPool::DeAlloc
    -> _in_use.exchange(false)
    -> DestroyCommand(cmd) 通过 switch 析构真实类型
    -> push 原始 payload 回 pool
下一次 Alloc<T2> 可在同一 payload 上构造另一具体类型
```

实现时断言：`offsetof(CommandPayload, _storage)==0`（现有已存在）、`alignof(CommandPayload)>=alignof(T)`、`sizeof(CommandPayload::_storage)>=sizeof(T)`、重复 DeAlloc 不发生实际双析构、构造失败重新将 payload 归还且不把未构造对象当成活对象销毁。

添加 Debug/test only 的 `object_type`/`allocation_generation` 标记可以帮助定位重复释放；不要给每一个 Draw 的生产热路径增加字符串 Map。`Shutdown` 当前对 `_in_use` 的 payload 调 `DestroyCommand`，应证明 shutdown 在 Worker 停止且已提交 GPU 工作适当 drain 后发生；不得用 Shutdown 代替队列完成同步。

### M4-E. 线程与异步回调所有权时序

当前两级命令路径：

```text
Frontend Thread: CommandBuffer::Impl 录制并拥有 GfxCommand*
  -> GpuCommandWorker 接收 CommandGroup / commands
  -> (可选 JobSystem) RecordCommandGroup 调 ProcessGpuCommand
  -> D3DCommandBuffer 记录 Native CommandList
  -> D3DContext::ExecuteRHICommandBuffers
  -> MarkSubmitted(fence) / Finalize / RunPostSubmitCallbacks
  -> RHICommandBufferPool::Release（只是归还供未来使用）
  -> IsReady(fence) 后 Clear/Allocator Reset / 资源释放
```

**需要实际核对** `CommandBuffer::Impl::Clear` 与 Worker 完成使用 GfxCommand 的先后关系：谁持有 Commands 的容器、什么时候 move、何时允许复用前端命令。不要在生产代码凭注释猜测顺序。

审计要求：

- 录制阶段 `GfxCommand` 只能被当前 owner 修改；提交后的 Packet 内容冻结。对多线程并行录制同一 `CommandBuffer` 增加 Debug 断言，而不是全局互斥绕过错误。
- `CommandCustom::_func`、`CommandReadBack::_callback` 和 `CommandReadBackTexture::_callback` 可能捕获外部资源；检查是否 value-owned。`RecordNativeCallback()` 当前注释要求不捕获栈引用，应为 Debug 场景增加失败例子；生产代码不能通过任意回调的静态捕获分析证明安全，调用方仍需明确契约。
- `D3DCommandBuffer::RunPostSubmitCallbacks(fence)` **表示已提交而非 GPU 已完成**；不得在此释放仍在 GPU 使用的 NativeResource、Descriptor 或 UploadBuffer。真正释放需要另一个 completed-fence 阶段。
- `RHICommandBufferPool::Get()` 调 `IsReady()` 后才 `Clear()`，其中 `D3DCommandBuffer::Clear()` 会 Reset CommandAllocator 和 UploadBuffer。必须保留 `IsReady` 的 Fence 验证，且测试 GPU in-flight 情况。
- `ReleaseCompletedTrackedResources` 调 `ReleaseRecordedResources` 解除 Backbuffer COM 引用，不应和前端 Packet 的析构混淆。
- 异常/取消时 `record_jobs` 仍可能运行，先确保 jobs 完成/取消，再销毁它们正在读取的 command group；对于 Worker Stop 队列 drain，不能静默丢失可见 ownership。

### M4-F. 必须构造的测试

| ID | Test fixture | Procedure | Assert |
|---|---|---|---|
| CMD-01 | 所有 enum 对应命令 | Alloc<T>→设置非默认成员→DeAlloc→复用同一槽 T2 | 每个 type 正确析构且 `_type` 正确 |
| CMD-02 | `CommandCustom` callback 捕获计数对象 | 正常处理、未提交取消、重复 DeAlloc | 捕获对象引用计数归零、重复 DeAlloc 不二次析构 |
| CMD-03 | `CommandGpuResourceUpload` `_params` 有析构探针 | 录制、丢弃、完成 | `_params` 恰好删除一次、不因 Reset 泄漏 |
| CMD-04 | `CommandReadBackTexture` `_sub_resources` > small capacity | 反复重建 payload，随机长度 | Vector 构析安全、无堆损坏 |
| CMD-05 | `CommandDispatchRays` `DispatchBindingSnapshot` | Alloc/DeAlloc 1 万次并监测资源计数 | 补齐 Destroy case 后释放正确 |
| CMD-06 | `CommandBuildAS` | 构造/归还/复用多次 | Destroy case 覆盖且无非法生命周期 |
| CMD-07 | 3 个并行录制任务 + 阻塞其中一条 | B/C 提前完成但按明确 Submit 顺序提交 | Packets 不被提前回收、GPU 结果一致 |
| CMD-08 | GPU Fence 故意延迟，CommandList Release 后立即 Get | 测试 IsReady gate | 未完成的 Native CommandAllocator/UploadBuffer 不被 Reset |
| CMD-09 | Worker Stop/Shutdown，队列非空 | 记录完成/取消/停止时序 | 无在读 Packet 早析构、无 GPU 资源早释放 |

使用 MSVC `/fsanitize=address` 时注意环境支持差异，不能因为 ASan 不可用就把普通 C++ 生命周期测试全部 SKIP。测试独立输出构造/析构次数与 pool 峰值，避免只测画面截图。

### M4-G. 推荐最小 diff 与错误处置

- **Commit A：** 对照 enum、`GfxCommandTypeName`、`DestroyCommand`、`kCommandPayloadSize/Align` 和 Alloc sites，补齐 `kBuildAS`/`kDispatchRays`，加入 CMD-01/05/06。
- **Commit B：** 删除不安全 `SafeResetCommand` 或替换真实调用者；CMD-02/03/04 通过，禁止新增泛型 `memset` 重置。
- **Commit C：** 只对通过审计确认的 Worker/Pool 生命周期薄弱点做最小修改；CMD-07/08/09 + M1 Descriptor/GPU Resource 回归通过。
- **T4.4（Final）：** 对比前后 CPU recording p50/p95、pool blocks peak、每帧分配次数；Debug/Release 全量编译及 CMD-01~09、M1/M2/M3 回归全部通过；若额外 diagnostics 影响性能，使用 Test-only 条件编译而不是删除正确性断言。

阻塞条件：任何命令类型 ownership 不明、析构遗漏导致非平凡对象残留、或者回调从提交误当完成，均不可将 M4 标记 PASSED。


## Acceptance Criteria

- [ ] 非平凡命令类型不再使用 `memset` 风格 Reset；所有活跃 Packet 的 ctor/dtor 对称。
- [ ] `kBuildAS`、`kDispatchRays` 的 `DestroyCommand` 与 `GfxCommandTypeName` 分支已补全并通过针对性测试；Command 类型表、Destroy 分支、Pool 使用路径完整覆盖。
- [ ] 录制线程/提交线程/取消录制的所有权与生命周期得到证明；不增加无必要热路径分配。
- [ ] 单线程与并行录制结果相同；上传、Readback、Resize、FrameDebugger 路径无回归。
- [ ] CPU 录制 p50/p95 和内存分配数量记录并评估；所有过关证据写入 progress，方可进入 M5。

## Decision Log / Surprises / Validation / Progress / Known Follow-ups / Outcomes / Resume Protocol

- Decision: 清除未用 Reset 还是保留并安全重置，依据真实调用链决定。
- Discovery: 当前命令重置可能未被调用，不能把静态风险夸大成已发生内存破坏。
- Validation: CMD01-CMD06 / DX12 smoke / Leak checks。
- Progress: TODO；Next: 建命令类型与手动析构分支映射表。
- Follow-ups: 不强制迁移到 `std::variant`。Outcomes: 未实施。Resume: README + progress + 本文件。
