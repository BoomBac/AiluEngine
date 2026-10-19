# M5 — RHI Interface Boundaries & Context Simplification

**Status:** TODO　**Prerequisite:** M4 PASSED　**Exit gate:** RHI 边界清晰、公共兼容接口可用、不因架构拆分增加无意义运行开销。

## Purpose

将 `GraphicsContext` / `D3DContext` 过宽的职责逐步收敛，令渲染前端、Shader 编译、GPU 设备、命令录制、Queue 提交和 Capture 成为易于定位的模块。目标是**少量稳定接口 + 最少实现分散**，不是为每项操作创建虚基类。

## Scope

`GraphicsContext` Facade、`D3DContext` 实现分拆、Shader Compiler/Cache 的调用入口、Capture/Profiler、Native GPU Device/Queue 与 Render Command 翻译职责。

## Non-goals

- 不打破现有广泛使用的 `GraphicsContext::Get()` 接口；不当场改遍全部 Render Pass 的调用。
- 不引入动态后端选择或 Plugin Factory 系统；在唯一 DX12 后端下优先普通具体类/内部对象。
- 不重写 Shader 编译缓存或 PSO 命中算法，仅理清其归属与依赖。

## Architecture Constraints

- 高频 Draw/Dispatch 不引入新的重复虚调、跨线程消息或全局 map 查找；能在编译期决定的调用保持静态。
- `Render/` 的高层 `Material/Mesh/RenderGraph` 不应作为低层 Device/Queue 接口的核心类型。
- 兼容迁移采用「**保留旧 Facade，内部委托**」模式，逐步移动实现；本阶段不为强迫调用方一起改名而产生巨量 diff。
- Shader **编译/变体/文件依赖**属于高层 Shader System；RHI 只负责 Native Shader Bytecode、RootSignature/PSO 等设备对象构建与绑定。
- Capture/Profiler 是跨层服务；允许 Backend 参与 Native Marker/Capture，但不要求 GraphicsContext 成为所有业务工具入口。
- 不把当前 CommandBuffer frontend 封装命令直接降级为完全 Native API；维护 M4 验证过的两级录制契约。

## Baseline

- `Inc/Render/GraphicsContext.h` 包含 Present、Window、Fence、CreateResource、Readback、ProcessGpuCommand、CompileShaderAsync、TakeCapture、GPU Timer 等方法。
- `Src/RHI/DX12/D3DContext.cpp` 实现近 3.4k 行，包括 `GpuCommandWorker`、CommandBuffer 录制与提交、Shader/Material/DXR 绑定、Resize、Readback、Capture。
- `D3DCommandBuffer` 已具备 Native CommandList 语义；Device 与 Queue 的职责还未以一个明确的小边界集中展示。

## Target Outcome

一张准确的依赖图和一个更小、更好维护的模块布局：

```text
RenderPipeline / RenderGraph / Material / ShaderSystem
             | logical usage / command packets
RenderCommand Frontend (CommandBuffer / GfxCommand)
             | recorded packets
RHI Core Facade (GraphicsContext, legacy API kept)
             | small device / cmd / queue responsibilities
DX12 Device & Resource | DX12 Command Recorder | DX12 Queue & Fence
             | Native D3D12 API
```

最终允许 `GraphicsContext` 继续保留兼容转发方法，但核心操作的真实实现不再混成超大型函数/类。

## Relevant Files

`Inc/Render/GraphicsContext.h`, `Src/Render/GraphicsContext.cpp`, `Inc/Render/CommandBuffer.h`, `Src/Render/CommandBuffer.cpp`, `Src/RHI/DX12/D3DContext.cpp`, `Inc/RHI/DX12/D3DContext.h`, `Src/RHI/DX12/D3DCommandBuffer.cpp`, `Inc/RHI/DX12/D3DShader.h`, `Src/RHI/DX12/D3DShader.cpp`, `Src/RHI/DX12/D3DShaderCompiler.cpp`, `Src/Render/Shader.cpp`；Capture、Readback 相关实际调用点。

## Implementation Steps

### Step 1 — 静态依赖图和接口审计

- 为 `GraphicsContext` 每个虚函数统计生产调用数量、线程归属、对 `Render::*` 类型的依赖、底层 D3D12 归属。
- 标记 Core（Device/Queue/Fence/CommandList/Native Resource）与服务（Shader 编译、Capture、Window、Readback、Profiler、Render Packet Dispatch）。
- 从 `ProcessGpuCommand` 的 switch 中识别 Material/Mesh/Shader 解析与纯 DX12 Native Draw 的分界。注意一些命令需要两个层次配合，不能硬拆成错误方向的依赖。
- 记录需要保留兼容入口的列表；没有需求不要先设计抽象类树。

### Step 2 — 内聚化提交 / 资源 / 命令实现

- 先将经过 M1-M3 验证的 **Queue Submit + State Resolve + Fence** 收敛到 `D3DContext` 内部一个清晰的 `Submit` 责任域（可只提取私有对象/函数，不强迫新增公开虚类）。
- 将 `Device Resource Create/Readback` 与 Queue Submit 分离，不能让资源创建偷偷跳过 StateTracker/Fence 契约。
- 将 `GpuCommandWorker` 的前端解释/组装过程与低层 Queue 操作边界写清；若实际拆文件引起大量交叉字段暴露，则选择保留 Context 外观、内部以 helpers/translation unit 分离。
- 确认 `D3DCommandBuffer` 是唯一接触 `ID3D12GraphicsCommandList` 绘制录制的主要对象，直接 Native callbacks 必须保持在明确的 RHI escape hatch 范围。

### Step 3 — 迁移 Shader Compiler / Capture Service 调用归属

- `CompileShaderAsync` 的未来入口应在 Shader System/Compiler Service；先提供适配转发，逐步迁移新调用点，不能本阶段影响热重载/Cache 行为。
- `TakePixCapture/TakeRenderDocCapture` 最终归 Capture Service，GPU Marker 在 Backend；保留 UI/Editor 使用旧入口的兼容代理。
- `GetTimer/TotalGPUMemeryUsage` 属于 Diagnostic/Capability 读接口；建议归到轻量查询对象或只读 Context Facade，避免引入 Runtime 对象生命周期问题。
- `RegisterWindow/ResizeSwapChain/Present` 归 Swapchain/Window 管理责任域；维持现有 ApplyPendingSwapChainResizes 的 Frame Boundary 保证。

### Step 4 — 形成稳定 RHI 契约文档

- 文档明确允许：Resource Create/Native Handle/Descriptor View/CommandList/Queue/Capabilities；RHI 禁止依赖高层 `Material`、Scene ECS 和 RenderGraph Pass 对象。
- 明确 RenderGraph/Immediate 两条入口的状态/提交行为；明确直通 Native callback 的外部状态声明责任。
- 只要未来确需 API 多后端，才提取真正必要的纯接口；当前的具体 DX12 类保持简单。
- 只做有价值的 include 去耦，观察 clean build 和增量 build 时间；不能用头文件一次迁移掩盖业务修改。

## Tests

| 场景 | 预期 |
|---|---|
| Legacy Facade | 所有现有 `GraphicsContext::Get()` 调用无行为变化 |
| Async Shader Compile/Hot Reload | 无丢任务、缓存 key 不变、Editor 反应正常 |
| Record & Queue | M1-M4 压测不回退，调用顺序和 Fence 一致 |
| DXR / AS Build | 条件允许时 BuildAS、DispatchRays 运行正确 |
| Swapchain + MultiWindow + Resize | 不提前释放 Backbuffer，不死锁 |
| Debug / Capture | PIX/RenderDoc 启动及 GPU Marker/FrameDebugger 仍可用 |
| Build boundary | Render/RHI 头依赖方向清楚，无额外链接/循环 include |

## Implementation Blueprint — 保兼容的职责收敛（禁止大爆炸重构）

> M5 不是“重新设计第二套 RHI”。本阶段用**细粒度内聚化**降低 `D3DContext.cpp` 的阅读/维护负担，最多引入确有稳定职责的内部辅助对象。必须提供修改前后调用链和依赖方向；迁移顺序比文件命名重要。

### M5-A. 先把现有 GraphicsContext 公共接口逐个分组

从 `Inc/Render/GraphicsContext.h` 出发，给每个函数生成 `rhi-api-audit.md` 表：

| Responsibility | Existing API examples | Recommended owner | Migration rule |
|---|---|---|---|
| Device/Capabilities | `Init`, `IsHardwareRayTracingSupported`, `IsInlineRayTracingSupported` | Device facade / D3DContext | 保留旧虚接口转发 |
| Queue/Fence | `ExecuteRHICommandBuffers`, `WaitForGpu`, `WaitForFence`, `GetFenceValueCPU/GPU` | Queue submit domain | **不**修改 M1-M3 Fence 语义 |
| Native resource | `CreateResource`, `CreateResourceSync` | Device/Resource creation domain | 不破坏 pending upload/retire |
| Render packets | `ExecuteCommandBuffer`, `ProcessGpuCommand`, `SubmitGpuCommandSync` | Frontend decoder / worker domain | 高层 Material/Mesh 解析不下沉至 Device |
| Swapchain/window | `RegisterWindow`, `UnRegisterWindow`, `ResizeSwapChain`, `Present` | Swapchain domain | 保留帧边界 resize |
| Shader compilation | `CompileShaderAsync` 三种 overload | Shader/Compiler Service | 先 adapter，后迁移内部实现 |
| Tools/Diagnostics | `TakeCapture`, `TakePixCapture`, `TakeRenderDocCapture`, `GetTimer`, `TotalGPUMemeryUsage` | Capture/Diagnostics service | Editor 旧入口仍可用 |
| Readback | `ReadBack`, `ReadBackAsync` | Transfer/Readback domain | 回调完成/Owner 不变 |

还应审计 `EndFrame/GetFrameCount/SetMultiThreadRendering/TryReleaseUnusedResources` 等未列函数。每条记录：定义、调用者数量、线程/锁、依赖高层 Render 类型、是否必须公开虚调用、迁移后静态/动态依赖变化。**不要只按名字移动代码。**

### M5-B. 推荐最小目标布局（仅为职责参考）

```text
Render/
  CommandBuffer + GfxCommand       frontend packet builder / snapshots
  Shader + ShaderCompilerService   编译/变体/文件依赖/缓存
  RenderGraph + Material           渲染策略/逻辑使用意图
RHI/DX12/
  D3DContext                       保留对外 GraphicsContext Facade
  D3DCommandBuffer                 实际 Native CommandList recording
  DescriptorManager               Shader-visible heap + Views
  D3DQueueSubmit                   可选私有 submit helper：Barrier/Execute/Fence
  D3DSwapchain                     窗口交换链领域
  D3DResources                     原有 D3DTexture/D3DBuffer 等
```

`D3DQueueSubmit` 是**可选**辅助类/翻译单元。如果抽出它必须复制 `D3DContext` 十几个成员、引入环形依赖或额外锁，应改为小的 `D3DContext::SubmitCommandListsImpl()` 等私有函数，先保证实际复杂度下降。

优先聚合行为，不优先增加类数量；一个职责非常薄的工具函数不值得抽象为带虚析构的对象。真正稳定的公共接口少于稳定的私有实现边界也完全可以接受。

### M5-C. 精确迁移顺序与每次编译门槛

**T5.1 — 审计，不修改行为**

1. `rg 'GraphicsContext::Get\(|g_pGfxContext|ProcessGpuCommand\('` 并记录文件/调用线程类别；检查 `Renderer`, `RenderGraph`, Editor、Shader 热重载和 Native SDK 回调。
2. 统计 `Src/RHI/DX12/D3DContext.cpp` 的函数数量/大函数体/包含头文件、当前编译时间；找出大块与 `GpuCommandWorker`, `ProcessGpuCommand`, Queue, Swapchain, Capture 耦合的共享数据。
3. 给每个候选模块列出**成员拥有者**：Device、Queue、Fence、Worker、Swapchain、Shader tasks、Readback buffers；如果两个模块都写同一份可变成员，需要重新划分。

**T5.2 — 提取 Queue Submit，严格保留 M1-M3 算法**

1. 只整理 `ExecuteRHICommandBuffers` 里的 `Close -> Resolve -> Execute/Wait/Signal -> Commit -> MarkSubmitted -> Callback/Collect` 阶段，使执行顺序清晰。
2. 保留 `_command_submit_mtx` 的原始线性化和 `_cmd_fence_mtx` 次序；不在提取过程中把 `RunPostSubmitCallbacks` 提前到 Signal 前，也不让 GPU 资源的 last-fence 丢失。
3. 把 Queue State / Descriptor Collect 责任写成一份表，避免分拆后 `D3DContext` 和另一个 Manager 各自收集一遍。
4. 完成这一小改动后立即 Debug+Release 编译、M1/M2/M3 回归、CPU submit 基准；不允许先大迁移再统一补测试。

**T5.3 — 梳理 Render Packet 解码职责**

1. 分析 `D3DContext::ProcessGpuCommand` 的 switch，逐个命令标注：纯 DX12（Clear/Copy/Barrier/Dispatch）、高层资产解析（Mesh/Material/ShaderHandle）、生命周期（Destroy/Readback/EndFrame）、工具（Profiler/Custom）。
2. 对确实高层的解析逻辑，考虑移入独立 frontend translation helper；最终调用 `D3DCommandBuffer` 的 Native operation。不要求一次迁移所有 case；先挑 1-2 个可独立测试的简单命令证明接口方向。
3. 不要把 `GfxCommand*` 和 `Material*` 成批传入新建的 `D3DDevice`；Device 接口应只接触 native/resource/pipeline 类型。
4. 为 `kCustom` Native Callback 留明确 escape hatch：调用端记录 resource state/Fence 所有权责任，不允许它绕过 M2 状态表后仍声称 RHI 能自动知道外部 Barrier。

**T5.4 — 抽离 Shader 编译与工具入口**

1. 为 `CompileShaderAsync(Shader/ComputeShader/RayTracingShader*)` 找到现有队列与真正编译函数；记录 `D3DContext` 自身是否拥有 Worker、任务缓存和生命周期。
2. 最小方案是 Shader System 新增 Compiler Service，旧 `GraphicsContext::CompileShaderAsync` **委托**给它。先不改变同步/异步选择、任务优先级、Shader Artifact key 与热点重载时序。
3. Capture 服务 `TakePixCapture/TakeRenderDocCapture/TakeCapture` 也采用同样的 adapter 策略；不要迁移 Native `PIXBeginEvent` 的必要后端实现。对于旧 Editor 菜单入口运行回归测试。
4. Readback/Swapchain 职责保持可追踪，最少以源码段或内部 owner 分开；Window Resize 当前依赖帧边界 Worker 保证，不能移到 UI 线程的立即调用路径。

**T5.5 — 删除实际冗余，减少头依赖**

只删除已经确认没有调用者/功能重复的成员或方法，避免顺带重命名所有符号。先测 Clean Build 与典型修改 `GraphicsContext.h` 的 Incremental Build 耗时；如果新增 include 循环、重复编译依赖，撤回该文件拆分方案。

### M5-D. 推荐的 API 兼容技巧（示例而非强制类树）

保留旧入口、内部委托，类似：

```cpp
// D3DContext仍实现GraphicsContext对外入口。
u64 D3DContext::ExecuteRHICommandBuffers(const Vector<RHICommandBuffer*>& command_buffers)
{
    return SubmitCommandBuffersImpl(command_buffers); // 只把稳定的提交责任下移，保留返回 Fence 语义
}
```

如果依赖注入后产生多个队列所有者，宁可简单地让 `D3DContext` 持有唯一实例并提供清晰私有方法，不引入全局单例来解决另一个单例过大的问题。

不要在热路径引入 `std::function<void()>` 打包每个 Draw 来“消除 virtual”，也不要频繁构造临时 `RenderCommand` 对象引入额外动态分配；线程模型和当前 CPU 性能基线不应变化。

### M5-E. 关键性能/正确性与负面测试

| ID | Operation | Required check |
|---|---|---|
| BND-01 | 旧 `GraphicsContext::Get().ExecuteCommandBuffer()` / Sync | 结果、Fence、报错行为与旧版本一致 |
| BND-02 | shader 文件修改→热重载→PSO 再创建 | Artifact key 未变、缓存结果正确、任务不丢 |
| BND-03 | 6 个并行记录 batch | 命令顺序、资源状态及 Queue Fence 稳定 |
| BND-04 | ReadBackAsync + 资源提前释放 | 完成回调恰好一次且读取有效 |
| BND-05 | PixCapture/RenderDocCapture/GPU Timer | 旧 Editor 菜单仍触发对应行为，失败有原因 |
| BND-06 | 反复多窗口 resize/minimize/present | Backbuffer 引用和 Worker 帧边界不破坏 |
| BND-07 | RHI include 依赖扫描 | Native Device 头无新的 RenderGraph/Scene 强依赖 |
| BND-08 | DXR 支持/不支持两套环境 | 能力检查与失败回退仍正确，不无条件创建 DXR 资源 |

性能比较必须有 `record_cpu_p50/p95`、`submit_cpu_p50/p95`、每帧 heap allocations、Native CommandList count、Shader compile key/cache hit；若只迁移文件而所有指标无变化，这是可接受结果，因为本阶段追求维护性，而不是虚构的加速收益。

### M5-F. 不得采用的“现代化”做法

- 用 5 个新抽象类代替单一 `GraphicsContext`，却让它们共同读写原有 Context 的几十个可变成员。
- 把 `Material`, `Mesh`, `RGHandle` 作为底层 `D3DDevice` 的必需类型，让未来其他后端更难独立编译。
- 删除旧虚接口导致全仓大量调用方一起迁移，掩盖本阶段真正的职责收敛。
- 迁移 Worker 时顺便改变线程优先级、提交批次、Shader 编译任务队列或 Fence 序列。
- 对帧性能退化说“接口更现代所以没关系”，不测量就宣称收益。

### M5-G. 交付的客观判据

提交 `rhi-api-audit.md`（所有接口及 owner）、`rhi-dependency-before-after.md`（修改前后依赖图）、`rhi-contract.md`（允许/禁止的职责，Native callback、状态所有权、Fence 规则）。`D3DContext` 仍可以存在，但 ProcessGpuCommand 中高层业务解析必须有清楚的责任边界或已提取一批可验证子模块。

每完成 T5.2、T5.3、T5.4 任一小改动，都执行 BND-01/03/04 及 M1-M4 核心测试；Shader/Capture 改动执行其专项测试。任意大范围头文件循环依赖/ABI 破坏或难以定位的运行时回归，恢复上一个通过的子任务，不进入 M6。


## Acceptance Criteria

- [ ] 完整前后依赖图和 API 使用清单落盘；新职责通过最少必要的类/模块划分实现。
- [ ] 旧公共入口保留，RenderGraph、Shader Hot Reload、Editor、D3D12 Raster/DXR 场景通过。
- [ ] `ProcessGpuCommand` 的高层业务职责得到明确归属；不能仅把代码移动到另一个巨大文件就宣称收敛。
- [ ] 无明显 CPU 录制/提交或 GPU 性能退化、无额外全局锁或重复状态缓存。
- [ ] 完整构建与独立 commit 已验证，方可进入 M6。

## Decision Log / Surprises / Validation / Progress / Known Follow-ups / Outcomes / Resume Protocol

- Decision: 优先保留兼容 Facade、做内部委托，抽象类新增必须经过明确用例审核。
- Surprise: Shader Compile/Readback 可能含复杂线程时序；迁移先梳理，避免改变实际任务调度。
- Validation: 以上集成矩阵 + M0 性能场景。
- Progress: TODO；Next: 按 GraphicsContext 逐函数列出职责和线程归属。
- Follow-ups: Multi-backend interface 单独立项。Outcomes: 未实施。Resume: README + progress + 本文件。
