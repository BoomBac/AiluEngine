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

DX12 teardown stops recording and waits for GPU completion before releasing window swapchains, the GPU timer and
the root-signature cache. The native swapchain reference is released before its Streamline proxy; the queue is
released before the device proxy, and Streamline shuts down while the native device is still alive.
`IGPUTimer` has a virtual destructor because `D3DContext` owns it through the base interface.
DXGI live-object diagnostics run after both the graphics context and GPU resource manager have been destroyed;
the report is copied to the engine log and is distinct from process-exit success.

`D3DContext` also owns an optional `FsrRuntime`, independent of Streamline. When enabled, it loads the signed FSR SDK
DLLs from the `FSR` directory beside the Engine module and queries providers using the native DX12 device; it does
not proxy the device, factory or swapchain. Shutdown follows the GPU idle wait and precedes native device teardown.
FSR context creation declares inverted depth when `SystemInfo::kReverseZ` is enabled; the camera uses a finite far plane.

DX12 depth-stencil conversion keeps `DepthEnable` enabled whenever depth writes are requested, including
`ZTest: Always` / `ZWrite: On`. Unconditional depth-copy passes must still enable the depth stage to write `SV_Depth`;
turning off the comparison alone must not suppress their writes or preserve stale pooled depth contents.

`GPUVisibleDescriptorAllocator` owns CPU staging pages in address-stable storage. Each allocation borrows a
`DescriptorPage*`; appending pages must preserve every existing page address until allocator teardown. Staging
page growth is separate from the fixed shader-visible MainHeap and its persistent bindless regions/descriptor ring.

## Command submission

区分：

```text
record
submit
CPU completion
GPU completion
```

提交函数返回或 command list record 完成不意味着 GPU 已完成。

`CommandBuffer` 的暂存 viewport 是一次性状态：绑定 render target 时消费；`Clear()` 必须重置未消费值，避免池化复用把旧尺寸带到下一个目标。
DX12 绑定 render target 时还需按当前 RTV/DSV 的最小有效尺寸裁剪 viewport 和 scissor，保证最终提交的绘制区域不越过任一附件；command buffer 缓存重置不能替代此校验。

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

## Compute texture array SRVs

`ComputeShader::CaptureDispatchState` selects the main SRV for a default `Texture2DArray` binding
(`_sub_res == UINT32_MAX`), exposing all array layers to shader indexing such as directional CSM.
An explicit array subresource binding retains its single-layer/mip view; UAV selection is unchanged.

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

## Vertex input slot contract

DX12 input slots use the shader reflection register / VertexBufferLayoutDesc::Stream. Reflection enumeration order is
not the binding slot: system-value inputs such as SV_INSTANCEID can make them differ. Compiled and hydrated layouts
must keep the same slots. GPU-backed vertex streams may rotate without recreating their VertexBuffer; stream changes
advance its view version, and native resources/addresses refresh before resource-state transitions and vertex binding.
