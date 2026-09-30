# RHI and DirectX 12 Architecture

## Purpose

本文档描述 AiluEngine rendering abstraction 与 DX12 backend 的基本边界。

主要入口：

- `Engine/Inc/Render/GraphicsContext.h`
- `Engine/Inc/Render/GpuResource.h`
- `Engine/Inc/Render/CommandBuffer.h`
- `Engine/Inc/RHI/DX12`
- `Engine/Src/RHI/DX12`

## Layers

当前大体分层：

```text
Renderer / RenderGraph
        ↓
GraphicsContext / GpuResource / RHICommandBuffer
        ↓
RHI::DX12 implementation
        ↓
D3D12 / DXGI
```

上层 feature/pass 不应无必要直接依赖 DX12 concrete type。

需要 backend-specific capability 时，优先通过明确 API/capability 暴露。

## GraphicsContext

`GraphicsContext` 是全局 graphics execution 抽象。

当前职责包括：

```text
context initialization/finalization
present
window registration
command buffer execution
sync execution
GPU resource creation
readback
shader compilation dispatch
capture
swapchain resize
GPU wait/fence
RHI command buffer submission
ray tracing capability query
GPU memory/resource maintenance
```

这仍是较宽的接口，但它是当前上层与 backend 的主要边界。

## GpuResource ownership

GPU resource 必须同时考虑：

```text
CPU object lifetime
backend native resource lifetime
descriptor lifetime
command buffer references
GPU fence completion
```

CPU `delete`/Ref 归零不一定意味着 native resource 可以立即释放。

延迟释放/keep-alive 应基于实际 GPU completion。

## Command submission

区分：

```text
record
submit
CPU completion
GPU completion
```

提交函数返回或 command list record 完成不意味着 GPU 已完成。

需要复用 upload/default resource、descriptor、frame arena 或删除资源时，应基于 fence。

## Frame resources

资源按多个 frame slot 轮转时：

```text
slot reuse
```

必须晚于 GPU 对该 slot 上一次使用完成。

任何优化 upload ring、FrameBufferArena、descriptor arena、per-frame buffer 时都应保留这一约束。

## Resource state tracking

RenderGraph 描述 logical usage，DX12 backend 执行具体 state transition。

Resource state tracker 应明确：

- state 的 storage owner；
- whole-resource/subresource；
- command-list local state；
- queue/global final state；
- transition merge；
- cross-queue synchronization。

不要让多个 tracker 各自把自己当作唯一真实状态而没有同步协议。

## Uploads

上传路径优化时区分：

```text
CPU staging copy
upload heap allocation
copy command
resource state
GPU use
frame lifetime
```

减少 `memcpy` 次数是局部优化，不能破坏：

- alignment；
- lifetime；
- overwrite safety；
- command ordering。

## Readback

Readback 路径通常会引入明显 GPU→CPU synchronization。

不要把同步 ReadBack 放入普通 render hot path。

调试/capture 功能应和 runtime rendering path 隔离。

## Swapchain resize

Resize 属于危险的资源生命周期边界。

修改时检查：

- backbuffer references；
- RTV/SRV references；
- outstanding GPU work；
- viewport/scissor；
- RenderGraph imported handles；
- window/editor target cache。

当前 `GraphicsContext::ApplyPendingSwapChainResizes` 表达了“在安全 frame boundary 应用 pending resize”的方向。

## Ray tracing

硬件/inline ray tracing capability 通过 GraphicsContext 查询。

上层 feature 应以 capability 为条件，不要通过 concrete adapter/device 强转判断。

## Change checklist

修改 RHI/DX12 时检查：

- Debug layer（任务需要时）；
- native resource lifetime；
- descriptor lifetime；
- frame slot；
- fence；
- resource state；
- command list reset/reuse；
- swapchain；
- multithread record/submission；
- RenderGraph integration；
- capture/readback path。
