# Threading Architecture

## Purpose

本文档描述 AiluEngine 当前 CPU/GPU 并发模型中需要区分的几个层次。

主要入口：

- `Application`
- `MainThreadDispatcher`
- `JobSystem`
- `Core::ThreadPool`
- `RenderPipeline`
- `GraphicsContext`
- DX12 queue/fence implementation

## Thread categories

不要把所有“异步”工作视为同一种线程模型。

当前至少存在：

```text
Main thread
Render thread / render synchronization
JobSystem workers
Core::ThreadPool workers
GPU queues
```

每种线程的 ownership、可访问对象和同步方式不同。

## Main thread

典型 main-thread-owned 工作包括：

- application/window event orchestration；
- 大部分 Editor UI；
- 需要稳定 Scene mutation point 的操作；
- 一部分 resource publish/reload；
- subsystem lifecycle。

`Application::IsMainThread()` 可用于明确主线程语义。

`MainThreadDispatcher` 提供将 callable 排队并在主线程 pump 的方式。

如果 worker 需要修改 main-thread-owned state，优先考虑 queue/dispatcher，而不是直接加锁后修改任意对象。

## JobSystem

`JobSystem` 提供：

- worker threads；
- `Job`；
- dependency / continuation；
- `JobGroup`；
- `Dispatch`；
- `Wait`；
- `ParallelFor`；
- work stealing。

`JobGroup` 适合“一组并行工作 + 一个明确 join point”。

当前内部名为 `LockFreeJobQueue` 的队列实现实际上仍使用 mutex；不要根据类型名假设它是 lock-free。

### JobSystem usage principles

适合：

- frame-local parallel work；
- 有明确等待点的批量 CPU 工作；
- culling/preparation 等可并行阶段。

不适合默认用于：

- 无生命周期管理的无限后台任务；
- 持续持有 Scene/Editor raw pointer 的长期任务；
- DLL/plugin 卸载后仍可能执行的 callback。

捕获对象到 job 前确认：

- 对象在 job 完成前一定存活；
- 是否允许 worker thread 读取/写入；
- 是否需要 snapshot；
- 是否必须在 frame boundary apply result。

## Core::ThreadPool

`Core::ThreadPool` 是另一套通用 background task pool。

它提供：

- `Enqueue`；
- future；
- task queue；
- worker thread lifecycle。

它与 `JobSystem` 不是同一个抽象。

新增代码不要随意在二者中挑一个；先判断任务属于：

```text
frame job / parallel compute
```

还是：

```text
background/longer-lived task
```

如果现有 subsystem 已经有明确模式，优先跟随该模式。

## Render thread synchronization

`Application` 和 `RenderPipeline` 具有显式 render synchronization。

修改 Scene lifetime、FrameResource、render command、camera、runtime scene retirement 等时，需要检查：

```text
main frame N
render frame N or N-1
resource lifetime
frame slot reuse
```

“CPU 逻辑已经不再使用”不等于“render thread/GPU 已经不再使用”。

## GPU synchronization

`GraphicsContext` 提供：

```text
WaitForGpu
WaitForFence
GetFenceValueGPU
GetFenceValueCPU
ExecuteRHICommandBuffer(s)
```

GPU resource lifetime 应绑定到实际 queue/fence 使用状态，而不是仅绑定 CPU scope。

不要用 CPU mutex 代替 GPU synchronization。

## Preferred mutation pattern

跨线程写共享容器时，优先考虑：

```text
worker:
    produce immutable result / command

synchronization point:
    apply container mutation

read hot path:
    read stable snapshot
```

对于高频读取的数据，这通常比在热路径上到处加 mutex 更符合引擎结构。

## Frame-local data

当数据仅服务于一帧或固定数量的 in-flight frame 时，优先考虑：

- frame resource；
- frame allocator；
- frame packet/snapshot；
- per-frame command list。

不要让 worker 随意长期持有临时帧内存地址。

## Shutdown requirements

所有 worker system 在 shutdown 时都必须满足：

```text
stop accepting work
→ wait/cancel outstanding work
→ prevent new callbacks
→ release captured state
→ destroy workers
```

尤其是 Plugin/DLL、Scene unload、Editor close、resource reload 路径。

## Validation

涉及线程改动时至少检查：

- Debug build；
- normal single-thread path；
- multi-thread rendering path（若相关）；
- shutdown；
- scene/project switch；
- worker exception/error path；
- repeated enter/exit play mode（若相关）。

只验证“正常运行 1 帧”不足以证明线程生命周期正确。
