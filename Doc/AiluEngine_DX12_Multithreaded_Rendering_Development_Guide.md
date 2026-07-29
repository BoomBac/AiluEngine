# AiluEngine DX12 多线程渲染开发文档

## 1. 文档目的

本文档用于指导 AiluEngine 在现有 DirectX 12 渲染管线基础上实现 CPU 侧多线程命令录制。

本次改造的核心目标不是简单增加一个渲染线程，而是让多个 JobSystem 工作线程并行执行 DX12 command list
录制，并由唯一的 RenderThread 按 RenderGraph 顺序统一提交。

本文档面向直接实现，要求优先保证以下属性：

- 渲染结果与当前单线程实现一致。
- 资源状态转换确定且可复现。
- command allocator 和 command list 生命周期满足 DX12 约束。
- PSO、Shader、Material 和资源绑定状态不存在跨线程可变共享。
- RenderThread 保持唯一 GPU queue 提交者和 Present 调用者。
- 第一阶段不引入 Async Compute，不并行执行 RenderGraph pass callback。

---

## 2. 当前架构概述

当前大致执行流程：

```text
Main Thread
  RenderGraph::Compile
  RenderGraph::Execute
    顺序执行 pass callback
    生成 GfxCommand
        |
        v
RenderThread
  GpuCommandWorker::RunAsync
    顺序处理 CommandGroup
    ProcessGpuCommand
    录制到单个 D3DCommandBuffer
    ExecuteCommandLists(1, ...)
    Signal fence
    Present
```

当前系统已经具备 Main Thread 和 RenderThread 分离，但 RenderThread 内部仍然串行录制所有 DX12 command list。

这属于“渲染线程化”，不属于“帧内多线程命令录制”。

---

## 3. 目标架构

目标执行流程：

```text
Main Thread
  RenderGraph::Compile
    确定 pass 顺序
    预计算资源状态转换
  RenderGraph::Execute
    顺序执行 pass callback
    生成不可变 GfxCommand 快照
    生成 FrameCommandBatch
        |
        v
RenderThread
  划分串行区间和并行录制区间
  为并行区间派发录制 Job
        |
        +------------------+------------------+
        v                  v                  v
JobWorker 0            JobWorker 1        JobWorker 2
D3DCommandBuffer 0     D3DCommandBuffer 1 D3DCommandBuffer 2
独立 allocator/list    独立 allocator/list 独立 allocator/list
录制 Pass A            录制 Pass B        录制 Pass C
        |
        v
RenderThread
  等待录制 Job 完成
  按 submission_index 排序
  ExecuteCommandLists(n, lists)
  Signal 一次 fence
  Present
```

核心约束：

1. 一个 command list 同一时间只能由一个线程录制。
2. 一个 command allocator 同一时间只能服务于一个正在录制的 command list。
3. 多个 command list 可以由多个线程并行录制。
4. GPU 提交顺序必须严格遵循 RenderGraph 编译顺序。
5. RenderThread 是唯一调用 `ExecuteCommandLists`、`Signal` 和 `Present` 的线程。
6. 工作线程不得修改共享资源状态、共享 Shader 状态或共享 PSO 绑定状态。

---

## 4. 实施范围

### 4.1 第一版必须实现

第一版范围：

- Main Thread 仍然顺序执行 RenderGraph pass callback。
- Main Thread 仍然顺序生成 GfxCommand。
- RenderGraph 在录制前确定每个资源转换的明确 before/after state。
- RenderThread 将普通 Draw/Dispatch CommandGroup 划分为可并行录制任务。
- JobSystem 工作线程并行执行 `ProcessGpuCommand`。
- 每个任务独占一个 `D3DCommandBuffer`。
- 多个 command list 按原顺序批量提交。
- 同一个提交批次只 Signal 一次 fence。
- ImGui、Upload、Readback、资源创建、Custom Command、Present 先保持串行。
- PSO encode state 移入 `D3DCommandBuffer`。
- 录制过程中禁止修改共享 Shader、Material、PSO 和资源全局状态。
- Descriptor 分配至少满足同一帧内不会覆盖和跨帧 fence 安全。

### 4.2 第一版明确不实现

以下内容不属于第一版：

- Async Compute。
- Graphics Queue 和 Compute Queue 跨队列同步。
- Copy Queue 异步上传。
- 并行执行 RenderGraph pass callback。
- GPU-driven rendering。
- Bundle。
- Secondary command buffer 抽象。
- 动态负载均衡。
- 自动按照历史录制耗时切分大型 Pass。
- 多线程 PSO 实时编译优化。
- 多线程 Shader 编译。

---

## 5. 总体设计原则

### 5.1 录制顺序和提交顺序分离

各 command list 可以以任意完成顺序录制，但最终提交顺序必须由 `submission_index` 决定。

禁止按照 Job 完成顺序提交。

### 5.2 录制任务只能读取不可变输入

进入工作线程前，以下数据必须已经稳定：

- GfxCommand 内容。
- Material draw state。
- Shader variant。
- Render target formats。
- Depth stencil format。
- Primitive topology。
- Blend、Rasterizer、DepthStencil 状态。
- Resource barrier before/after。
- Descriptor source handles。
- Vertex buffer 和 index buffer。
- Draw/Dispatch 参数。

工作线程不允许通过共享对象重新推导这些状态。

### 5.3 共享对象只允许只读访问

以下对象在并行录制阶段必须视为不可变：

- Shader。
- Material。
- GraphicsPipelineStateObject。
- RootSignature。
- InputLayout。
- Texture/Buffer 的 native resource。
- RenderGraph 编译结果。
- GfxCommand 内存。

### 5.4 录制上下文状态必须 command-list-local

以下状态不得保存在全局单例中：

- 当前 Shader。
- 当前 Shader pass。
- 当前 Shader variant。
- 当前 PSO。
- 当前 RenderTarget state。
- 当前 topology。
- Pending resource bindings。
- 当前 descriptor table。
- Profiler marker stack。
- 当前 render statistics。
- 临时 CPU descriptor 数组。
- 临时 D3D12_RECT 数组。

这些状态必须放入 `D3DCommandBuffer` 或其持有的 encode state。

---

## 6. 数据结构设计

## 6.1 CommandGroup 执行类型

增加命令组执行分类：

```cpp
enum class ECommandGroupExecution
{
    kParallelRecord,
    kSerialRecord,
    kImmediateSubmit
};
```

建议规则：

| 命令类型 | 执行方式 |
|---|---|
| 普通 Graphics Pass | `kParallelRecord` |
| 普通 Compute Dispatch，仍在 Graphics Queue | `kParallelRecord` |
| 资源创建 | `kSerialRecord` |
| Upload | `kSerialRecord` |
| Readback | `kSerialRecord` |
| BLAS/TLAS 构建 | `kSerialRecord` |
| `CommandCustom` | `kSerialRecord` |
| ImGui | `kSerialRecord` |
| Present | `kImmediateSubmit` |

`CommandGroup` 增加字段：

```cpp
struct CommandGroup
{
    CommandGroupParams _params;
    Vector<GfxCommand *> _cmds;
    ECommandGroupExecution _execution = ECommandGroupExecution::kSerialRecord;
    u32 _submission_index = 0u;
};
```

---

## 6.2 编译后的资源 Barrier

当前只保存“转换到目标状态”的命令不适合并行录制。

增加明确的 Barrier 描述：

```cpp
struct CompiledResourceBarrier
{
    GpuResource *_resource = nullptr;
    EResourceState _before = EResourceState::kCommon;
    EResourceState _after = EResourceState::kCommon;
    u32 _sub_resource = kTotalSubRes;
};
```

为 RenderGraph Pass 保存编译结果：

```cpp
struct CompiledRenderPass
{
    RenderPass *_pass = nullptr;
    Vector<CompiledResourceBarrier> _pre_barriers;
    Vector<CompiledResourceBarrier> _post_barriers;
    u32 _submission_index = 0u;
    bool _allow_parallel_recording = true;
};
```

新增明确的 GfxCommand：

```cpp
struct CommandResourceBarrier : public TypedGfxCommand<EGpuCommandType::kResourceBarrier>
{
    GpuResource *_resource = nullptr;
    EResourceState _before = EResourceState::kCommon;
    EResourceState _after = EResourceState::kCommon;
    u32 _sub_resource = kTotalSubRes;
};
```

`ProcessGpuCommand` 只负责将该描述转换为 `D3D12_RESOURCE_BARRIER`，不再查询或修改资源的共享当前状态。

---

## 6.3 帧命令批次

```cpp
struct FrameCommandBatch
{
    u64 _frame_index = 0u;
    Vector<CommandGroup *> _groups;
};
```

录制结果：

```cpp
struct RecordedCommandList
{
    Ref<RHICommandBuffer> _command_buffer;
    u32 _submission_index = 0u;
    bool _is_valid = false;
};
```

并行录制区间：

```cpp
struct ParallelRecordingSegment
{
    Vector<CommandGroup *> _groups;
};
```

---

## 6.4 CommandBuffer 局部统计

```cpp
struct CommandBufferStatistics
{
    u32 _draw_command_count = 0u;
    u32 _draw_call = 0u;
    u32 _dispatch_call = 0u;
    u64 _triangle_num = 0u;
    u64 _vertex_num = 0u;
    u64 _material_upload_bytes = 0u;

    void Reset()
    {
        _draw_command_count = 0u;
        _draw_call = 0u;
        _dispatch_call = 0u;
        _triangle_num = 0u;
        _vertex_num = 0u;
        _material_upload_bytes = 0u;
    }

    void Merge(const CommandBufferStatistics &other)
    {
        _draw_command_count += other._draw_command_count;
        _draw_call += other._draw_call;
        _dispatch_call += other._dispatch_call;
        _triangle_num += other._triangle_num;
        _vertex_num += other._vertex_num;
        _material_upload_bytes += other._material_upload_bytes;
    }
};
```

录制线程只更新本 command buffer 的统计。

RenderThread 在提交完成后统一汇总到 `RenderingStates`。

---

## 7. RenderGraph Barrier 编译

## 7.1 问题说明

当前资源状态逻辑如果在工作线程录制期间执行：

```cpp
command_buffer->StateTransition(resource, target_state);
```

并在内部：

1. 查询资源保存的当前状态。
2. 生成 Barrier。
3. 修改资源保存的当前状态。

即使加 mutex，也只能避免数据竞争，无法保证录制逻辑正确。

例如提交顺序：

```text
Pass A: Common -> RenderTarget
Pass B: RenderTarget -> ShaderResource
```

如果 Pass B 的 Job 先录制，它可能先读取并修改共享状态，导致 Pass A 得到错误的 before state。

### 结论

资源状态规划必须在单线程阶段完成。

---

## 7.2 状态跟踪键

建议支持 subresource 状态：

```cpp
struct ResourceSubresourceKey
{
    GpuResource *_resource = nullptr;
    u32 _sub_resource = kTotalSubRes;

    bool operator==(const ResourceSubresourceKey &other) const
    {
        return _resource == other._resource && _sub_resource == other._sub_resource;
    }
};
```

Hash：

```cpp
struct ResourceSubresourceKeyHash
{
    size_t operator()(const ResourceSubresourceKey &key) const
    {
        size_t hash = std::hash<GpuResource *>{}(key._resource);
        hash ^= std::hash<u32>{}(key._sub_resource) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        return hash;
    }
};
```

编译阶段临时状态表：

```cpp
HashMap<ResourceSubresourceKey, EResourceState, ResourceSubresourceKeyHash> resource_states;
```

---

## 7.3 编译流程

对 `_sorted_passes` 顺序执行：

1. 获取 Pass 声明的所有资源读取和写入。
2. 根据访问类型计算目标状态。
3. 从临时状态表获取 before state。
4. before 和 after 不同则生成 `CompiledResourceBarrier`。
5. 更新临时状态表。
6. 将 Barrier 存入 Pass 的 `_pre_barriers` 或 `_post_barriers`。
7. 为 Pass 分配递增 `_submission_index`。

伪代码：

```cpp
void RenderGraph::CompileResourceBarriers()
{
    HashMap<ResourceSubresourceKey, EResourceState, ResourceSubresourceKeyHash> resource_states;

    for (CompiledRenderPass &compiled_pass : _compiled_passes)
    {
        for (const RenderGraphResourceAccess &access : compiled_pass._pass->ResourceAccesses())
        {
            ResourceSubresourceKey key{
                ._resource = ResolvePhysicalResource(access._handle),
                ._sub_resource = access._sub_resource
            };

            const EResourceState before = ResolveTrackedState(resource_states, key);
            const EResourceState after = ResolveRequiredState(access);

            if (before != after)
            {
                compiled_pass._pre_barriers.emplace_back(CompiledResourceBarrier{
                    ._resource = key._resource,
                    ._before = before,
                    ._after = after,
                    ._sub_resource = key._sub_resource
                });
            }

            resource_states[key] = after;
        }
    }
}
```

`ResolveTrackedState` 首次遇到资源时应使用：

- 外部资源导入时声明的初始状态。
- Transient resource 的创建初始状态。
- Swapchain back buffer 的 Present 状态。
- 不允许直接依赖工作线程修改过的 native resource state。

---

## 7.4 Barrier 命令生成

Main Thread 在执行 Pass callback 前后，将编译结果写入高层 CommandBuffer：

```cpp
void RenderGraph::EmitPassCommands(const CompiledRenderPass &compiled_pass, CommandBuffer *command_buffer)
{
    for (const CompiledResourceBarrier &barrier : compiled_pass._pre_barriers)
    {
        command_buffer->ResourceBarrier(
            barrier._resource,
            barrier._before,
            barrier._after,
            barrier._sub_resource);
    }

    compiled_pass._pass->Execute(command_buffer);

    for (const CompiledResourceBarrier &barrier : compiled_pass._post_barriers)
    {
        command_buffer->ResourceBarrier(
            barrier._resource,
            barrier._before,
            barrier._after,
            barrier._sub_resource);
    }
}
```

工作线程只翻译，不执行状态推导。

---

## 8. PSO 和资源绑定状态重构

## 8.1 当前问题

`GraphicsPipelineStateMgr` 目前包含大量共享可变字段，例如：

- Pending bindings。
- Render target state。
- 当前 PSO。
- 当前 Shader。
- 当前 Shader pass。
- 当前 variant。
- Input layout hash。
- Topology hash。
- Blend hash。
- Rasterizer hash。
- DepthStencil hash。

多个工作线程并行录制时，这些字段会互相覆盖。

不能通过给整个管理器加大锁解决，因为锁住 Shader Bind 到 Draw 的整个过程会使多线程录制退化为串行。

---

## 8.2 拆分方案

拆分为：

1. 全局共享的 PSO Library。
2. 每个 command list 独立的 Encode State。

### PSO Library

```cpp
class GraphicsPipelineLibrary
{
public:
    GraphicsPipelineStateObject *GetOrCreate(const GraphicsPipelineKey &key);

private:
    HashMap<GraphicsPipelineKey, Scope<GraphicsPipelineStateObject>, GraphicsPipelineKeyHash> _pso_library;
    std::shared_mutex _library_mutex;
};
```

查找流程：

1. 使用 shared lock 查询。
2. 未命中时释放 shared lock。
3. 使用 unique lock 再次查询。
4. 仍未命中才创建 PSO。
5. PSO 创建后不可变。

伪代码：

```cpp
GraphicsPipelineStateObject *GraphicsPipelineLibrary::GetOrCreate(const GraphicsPipelineKey &key)
{
    {
        std::shared_lock lock(_library_mutex);
        auto iter = _pso_library.find(key);
        if (iter != _pso_library.end())
            return iter->second.get();
    }

    std::unique_lock lock(_library_mutex);

    auto iter = _pso_library.find(key);
    if (iter != _pso_library.end())
        return iter->second.get();

    Scope<GraphicsPipelineStateObject> pso = CreatePipelineState(key);
    GraphicsPipelineStateObject *result = pso.get();
    _pso_library.emplace(key, std::move(pso));
    return result;
}
```

注意：

- 建议在 Main Thread 或 RenderThread 预热常用 PSO。
- 第一版允许工作线程在未命中时进入创建锁，但应记录统计。
- 后续可禁止工作线程创建 PSO，并在缺失时回退到串行或延迟创建。

---

## 8.3 Command-list-local Encode State

```cpp
struct PipelineResourceBinding
{
    EPipelineResourceType _type = EPipelineResourceType::kUnknown;
    GpuResource *_resource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE _cpu_handle{};
    u32 _root_parameter_index = 0u;
};

struct GraphicsPipelineEncodeState
{
    GraphicsPipelineKey _pipeline_key;
    Array<PipelineResourceBinding, 32> _resources;
    u32 _resource_mask = 0u;
    u16 _max_resource_slot = 0u;
    GraphicsPipelineStateObject *_current_pso = nullptr;

    void Reset()
    {
        _pipeline_key = {};
        _resource_mask = 0u;
        _max_resource_slot = 0u;
        _current_pso = nullptr;

        for (PipelineResourceBinding &resource : _resources)
            resource = {};
    }
};
```

放入：

```cpp
class D3DCommandBuffer final : public RHICommandBuffer
{
public:
    GraphicsPipelineEncodeState &GraphicsEncodeState()
    {
        return _graphics_encode_state;
    }

    CommandBufferStatistics &Statistics()
    {
        return _statistics;
    }

private:
    GraphicsPipelineEncodeState _graphics_encode_state;
    CommandBufferStatistics _statistics;
    Vector<CommandProfiler *> _profiler_stack;
};
```

`D3DCommandBuffer::Reset` 时必须重置这些状态。

---

## 8.4 禁止录制时修改 Shader

类似以下行为必须移除：

```cpp
shader->SetCullMode(cull_mode);
shader->SetBlendState(blend_state);
shader->SetDepthState(depth_state);
```

这些调用会修改共享 Shader。

正确方式是 Main Thread 在捕获 draw state 时将状态写入不可变快照：

```cpp
struct MaterialDrawState
{
    Shader *_shader = nullptr;
    u32 _pass_index = 0u;
    u64 _variant_hash = 0u;
    ERasterizerCullMode _cull_mode = ERasterizerCullMode::kBack;
    BlendState _blend_state;
    DepthStencilState _depth_stencil_state;
    EPrimitiveTopology _topology = EPrimitiveTopology::kTriangleList;
    RenderTargetState _render_target_state;
    Array<PipelineResourceBinding, 32> _resources;
    u32 _resource_mask = 0u;
};
```

工作线程根据 `MaterialDrawState` 生成 `GraphicsPipelineKey`。

---

## 8.5 PSO 对象不可保存 per-draw 绑定

以下模式不允许：

```cpp
pso->SetPipelineResource(...);
```

如果该调用把资源绑定保存到 PSO 对象内部，那么多个 command list 会互相污染。

PSO 只应保存：

- `ID3D12PipelineState`。
- Root signature。
- 固定 root parameter layout。
- 固定 input layout 信息。
- 固定 shader bytecode 标识。

每次 Draw 的 SRV/UAV/CBV/Sampler 绑定必须保存在 command buffer encode state，最终直接写入 command list。

---

## 9. D3DCommandBuffer 生命周期

## 9.1 修复 fence ready 判断

当前如果使用：

```cpp
completed_fence > _fence_value
```

应修改为：

```cpp
completed_fence >= _fence_value
```

当 GPU completed fence 等于该 command buffer 的 submitted fence 时，allocator 已经可以安全复用。

初始 command buffer 的 `_fence_value` 为 0 时也应视为可用。

建议：

```cpp
bool D3DCommandBuffer::IsReady() const
{
    if (!_is_submitted)
        return true;

    return GraphicsContext::Get().GetFenceValueGPU() >= _submitted_fence;
}
```

---

## 9.2 使用正确的 command list 类型

创建 allocator 和 list 时不要硬编码 DIRECT：

```cpp
ThrowIfFailed(_device->CreateCommandAllocator(_dx_cmd_type, IID_PPV_ARGS(_command_allocator.GetAddressOf())));

ThrowIfFailed(_device->CreateCommandList(
    0u,
    _dx_cmd_type,
    _command_allocator.Get(),
    nullptr,
    IID_PPV_ARGS(_command_list.GetAddressOf())));
```

---

## 9.3 Reset 流程

```cpp
void D3DCommandBuffer::Reset()
{
    AL_ASSERT(IsReady());

    ThrowIfFailed(_command_allocator->Reset());
    ThrowIfFailed(_command_list->Reset(_command_allocator.Get(), nullptr));

    _graphics_encode_state.Reset();
    _statistics.Reset();
    _profiler_stack.clear();
    _is_closed = false;
    _is_submitted = false;
}
```

注意：

- `ID3D12CommandAllocator::Reset` 之前必须确认 GPU 已完成使用。
- 一个 allocator 不允许被两个 command list 并行使用。
- 一个 `D3DCommandBuffer` 对象同一时刻只允许属于一个录制任务。

---

## 9.4 Pool 线程安全

整个获取流程必须持锁，包括：

- 遍历可用 command buffer。
- 检查 ready。
- 标记占用。
- 必要时创建新对象。

```cpp
Ref<RHICommandBuffer> RHICommandBufferPool::Get(StringView name, ECommandBufferType type)
{
    std::lock_guard lock(s_pool_mutex);

    for (Ref<RHICommandBuffer> &command_buffer : s_command_buffers)
    {
        if (command_buffer->Type() != type)
            continue;

        if (command_buffer->IsAcquired())
            continue;

        if (!command_buffer->IsReady())
            continue;

        command_buffer->SetAcquired(true);
        command_buffer->SetName(name);
        command_buffer->Reset();
        return command_buffer;
    }

    Ref<RHICommandBuffer> command_buffer = CreateCommandBuffer(type, name);
    command_buffer->SetAcquired(true);
    s_command_buffers.emplace_back(command_buffer);
    return command_buffer;
}
```

释放时只修改 pool 所有权状态：

```cpp
void RHICommandBufferPool::Release(const Ref<RHICommandBuffer> &command_buffer)
{
    std::lock_guard lock(s_pool_mutex);
    command_buffer->SetAcquired(false);
}
```

释放到 pool 不代表可以立即 Reset，仍需由 `IsReady` 检查 fence。

---

## 10. GpuCommandWorker 并行调度

## 10.1 队列分段

RenderThread 读取 `FrameCommandBatch` 后，按原始顺序划分：

```text
Serial
Parallel segment
Serial
Parallel segment
Immediate submit
```

示例：

```text
ResourceCreate
Upload
Pass A
Pass B
Pass C
Readback
Pass D
Pass E
ImGui
Present
```

分段后：

```text
Serial: ResourceCreate + Upload
Parallel: Pass A + Pass B + Pass C
Serial: Readback
Parallel: Pass D + Pass E
Serial: ImGui
Immediate: Present
```

不能跨越串行命令将两侧 Parallel group 合并。

---

## 10.2 并行录制接口

```cpp
void GpuCommandWorker::RecordGroup(
    CommandGroup *group,
    RecordedCommandList *result,
    u32 submission_index)
{
    Ref<RHICommandBuffer> command_buffer = RHICommandBufferPool::Get(
        group->_params._name,
        ECommandBufferType::kDirect);

    for (GfxCommand *command : group->_cmds)
        _ctx->ProcessGpuCommand(command, command_buffer.get());

    command_buffer->Close();

    result->_command_buffer = std::move(command_buffer);
    result->_submission_index = submission_index;
    result->_is_valid = true;
}
```

工作线程不得：

- Execute command list。
- Signal fence。
- Present。
- 修改全局资源状态。
- 修改全局渲染统计。
- 修改共享 Shader。
- 修改共享 Material。
- 修改共享 PSO。

---

## 10.3 处理并行区间

以下 JobSystem API 名称按现有实现适配：

```cpp
void GpuCommandWorker::ProcessParallelSegment(const ParallelRecordingSegment &segment)
{
    Vector<RecordedCommandList> recorded_lists(segment._groups.size());
    Vector<JobHandle> job_handles;
    job_handles.reserve(segment._groups.size());

    for (u32 i = 0u; i < segment._groups.size(); ++i)
    {
        CommandGroup *group = segment._groups[i];
        RecordedCommandList *result = &recorded_lists[i];

        job_handles.emplace_back(JobSystem::Get().Dispatch(
            group->_params._name,
            [this, group, result, i]()
            {
                RecordGroup(group, result, group->_submission_index);
            }));
    }

    for (const JobHandle &job_handle : job_handles)
        JobSystem::Get().Wait(job_handle);

    std::sort(
        recorded_lists.begin(),
        recorded_lists.end(),
        [](const RecordedCommandList &lhs, const RecordedCommandList &rhs)
        {
            return lhs._submission_index < rhs._submission_index;
        });

    Vector<RHICommandBuffer *> command_buffers;
    command_buffers.reserve(recorded_lists.size());

    for (RecordedCommandList &recorded : recorded_lists)
    {
        AL_ASSERT(recorded._is_valid);
        command_buffers.emplace_back(recorded._command_buffer.get());
    }

    const u64 submitted_fence = _ctx->ExecuteRHICommandBuffers(command_buffers);

    for (RecordedCommandList &recorded : recorded_lists)
    {
        recorded._command_buffer->SetSubmittedFence(submitted_fence);
        MergeStatistics(recorded._command_buffer->Statistics());
        RHICommandBufferPool::Release(recorded._command_buffer);
    }
}
```

如果 `JobSystem::Wait` 支持等待线程执行其他任务，应保留该行为，使 RenderThread 在等待期间参与录制。

---

## 10.4 串行组处理

串行组仍然使用独立 command buffer，但可以连续录制多个相邻串行 group，再一次提交：

```cpp
void GpuCommandWorker::ProcessSerialGroups(std::span<CommandGroup *const> groups)
{
    Ref<RHICommandBuffer> command_buffer = RHICommandBufferPool::Get(
        "SerialRenderCommands",
        ECommandBufferType::kDirect);

    for (CommandGroup *group : groups)
    {
        for (GfxCommand *command : group->_cmds)
            _ctx->ProcessGpuCommand(command, command_buffer.get());
    }

    command_buffer->Close();

    RHICommandBuffer *raw_command_buffer = command_buffer.get();
    const u64 submitted_fence = _ctx->ExecuteRHICommandBuffers(
        std::span<RHICommandBuffer *const>(&raw_command_buffer, 1u));

    command_buffer->SetSubmittedFence(submitted_fence);
    MergeStatistics(command_buffer->Statistics());
    RHICommandBufferPool::Release(command_buffer);
}
```

---

## 11. 批量提交

## 11.1 新增 Context 接口

```cpp
u64 D3DContext::ExecuteRHICommandBuffers(std::span<RHICommandBuffer *const> command_buffers)
{
    Vector<ID3D12CommandList *> native_lists;
    native_lists.reserve(command_buffers.size());

    for (RHICommandBuffer *command_buffer : command_buffers)
    {
        if (command_buffer == nullptr)
            continue;

        auto *d3d_command_buffer = static_cast<D3DCommandBuffer *>(command_buffer);

        if (!d3d_command_buffer->IsClosed())
            d3d_command_buffer->Close();

        native_lists.emplace_back(d3d_command_buffer->NativeCommandList());
    }

    if (native_lists.empty())
        return _fence_value;

    _command_queue->ExecuteCommandLists(
        static_cast<u32>(native_lists.size()),
        native_lists.data());

    u64 submitted_fence = 0u;

    {
        std::lock_guard lock(_cmd_fence_mutex);
        submitted_fence = ++_fence_value;
        ThrowIfFailed(_command_queue->Signal(_command_buffer_fence.Get(), submitted_fence));
    }

    for (RHICommandBuffer *command_buffer : command_buffers)
    {
        if (command_buffer == nullptr)
            continue;

        command_buffer->SetSubmittedFence(submitted_fence);
        command_buffer->PostExecute();
    }

    return submitted_fence;
}
```

要求：

- `ExecuteRHICommandBuffers` 只能由 RenderThread 调用。
- 输入 command buffer 顺序就是 GPU 执行顺序。
- 同一批次共享同一个 fence value。
- 不要每个 command list 调用一次 `Signal`。
- 不要在工作线程调用该接口。

---

## 11.2 提交批次边界

遇到以下情况可结束当前批次：

- 需要 CPU 立即读取结果。
- 需要执行 Present。
- 需要等待某个 GPU fence。
- 需要切换到其他 command queue。
- 当前命令语义要求前一批次已提交。
- 调试模式要求隔离特定 command group。

普通串行和并行录制区间之间不一定必须分批提交。

第一版为了降低复杂度，可以每个区间单独提交；后续再合并相邻区间。

---

## 12. Present 拆分

## 12.1 当前问题

如果 `PresentImpl` 同时执行：

- ImGui Render。
- Present barrier。
- Execute command list。
- Swapchain Present。
- MoveToNextFrame。

那么 Present 命令无法和普通 command list 调度统一。

---

## 12.2 拆分接口

```cpp
void D3DContext::RecordPresentCommands(D3DCommandBuffer *command_buffer);
void D3DContext::PresentSwapchains();
```

建议流程：

```text
1. 完成普通 RenderGraph command list 录制。
2. RenderThread 串行录制 ImGui。
3. RenderThread 录制 back buffer -> Present Barrier。
4. 提交所有待提交 command list。
5. Signal fence。
6. 调用 PresentSwapchains。
7. MoveToNextFrame。
```

示例：

```cpp
void GpuCommandWorker::FinishFrame()
{
    Ref<RHICommandBuffer> present_command_buffer = RHICommandBufferPool::Get(
        "Present",
        ECommandBufferType::kDirect);

    auto *d3d_command_buffer = static_cast<D3DCommandBuffer *>(present_command_buffer.get());

    ImGuiRenderer::Get().Render(d3d_command_buffer);
    _ctx->RecordPresentCommands(d3d_command_buffer);
    d3d_command_buffer->Close();

    RHICommandBuffer *raw_command_buffer = present_command_buffer.get();
    const u64 submitted_fence = _ctx->ExecuteRHICommandBuffers(
        std::span<RHICommandBuffer *const>(&raw_command_buffer, 1u));

    present_command_buffer->SetSubmittedFence(submitted_fence);
    RHICommandBufferPool::Release(present_command_buffer);

    _ctx->PresentSwapchains();
    _ctx->MoveToNextFrame();
}
```

---

## 13. Descriptor Heap 并发与生命周期

## 13.1 当前风险

即使 descriptor allocator 有 mutex，也不代表设计正确。

如果动态 GPU-visible descriptor ring 发生 wrap：

```cpp
if (_ring_cursor + descriptor_num > kMainHeapDescriptorNum)
    _ring_cursor = _ring_base_index;
```

可能覆盖 GPU 尚未使用完的 descriptor table。

多线程录制会提高 descriptor 分配速度，使这个问题更容易触发。

---

## 13.2 第一版最低要求

动态 descriptor 区域必须按 frame slot 隔离：

```text
GPU-visible heap
  Persistent bindless region
  Frame 0 dynamic region
  Frame 1 dynamic region
  Frame 2 dynamic region
```

每个 frame dynamic region 只有在对应 fence 完成后才能重用。

```cpp
struct DescriptorFrameRegion
{
    u32 _begin = 0u;
    u32 _end = 0u;
    std::atomic<u32> _cursor = 0u;
    u64 _submitted_fence = 0u;
};
```

分配：

```cpp
DescriptorAllocation GPUVisibleDescriptorAllocator::AllocateDynamic(u32 descriptor_num)
{
    DescriptorFrameRegion &region = _frame_regions[_current_frame_slot];

    const u32 begin = region._cursor.fetch_add(descriptor_num, std::memory_order_relaxed);
    AL_ASSERT(begin + descriptor_num <= region._end);

    return BuildAllocation(begin, descriptor_num);
}
```

Frame 开始时：

```cpp
void GPUVisibleDescriptorAllocator::BeginFrame(u32 frame_slot, u64 completed_fence)
{
    DescriptorFrameRegion &region = _frame_regions[frame_slot];
    AL_ASSERT(completed_fence >= region._submitted_fence);

    region._cursor.store(region._begin, std::memory_order_relaxed);
    _current_frame_slot = frame_slot;
}
```

Frame 提交后：

```cpp
void GPUVisibleDescriptorAllocator::EndFrame(u32 frame_slot, u64 submitted_fence)
{
    _frame_regions[frame_slot]._submitted_fence = submitted_fence;
}
```

---

## 13.3 后续优化

后续可为每个工作线程预留 descriptor block：

```cpp
struct ThreadDescriptorBlock
{
    u32 _cursor = 0u;
    u32 _end = 0u;
};
```

线程本地小分配不再访问共享 atomic，block 用完后才从 frame region 申请新 block。

第一版可以先使用 atomic cursor。

---

## 14. ProcessGpuCommand 线程安全清理

必须检查并移除所有共享 static 可变数据。

错误示例：

```cpp
static std::stack<CommandProfiler *> s_begin_profiler_stack;
static D3D12_CPU_DESCRIPTOR_HANDLE handles[8];
static D3D12_RECT s_d3d_rects[RenderConstants::kMaxMRTNum];
```

修改为：

- Profiler stack 存入 `D3DCommandBuffer`。
- 临时数组使用函数栈变量。

```cpp
D3D12_CPU_DESCRIPTOR_HANDLE handles[8]{};
D3D12_RECT d3d_rects[RenderConstants::kMaxMRTNum]{};
```

同时检查以下全局访问：

- `RenderingStates::RenderData()`。
- `GraphicsPipelineStateMgr`。
- 临时 descriptor cache。
- 当前 render target cache。
- 当前 root signature cache。
- 当前 bound heap cache。
- Shader debug 状态。
- PIX marker stack。
- Query heap allocator。
- Upload staging allocator。
- Dynamic constant buffer allocator。
- FrameAllocator。

如果它们会在 `ProcessGpuCommand` 内写入，必须改为：

1. command-buffer-local。
2. frame-slot-local。
3. thread-safe allocator。
4. 保持串行。

---

## 15. Profiler 和 GPU Query

## 15.1 CPU Profiler

每个录制 Job 独立使用 CPU profiler scope。

禁止所有线程共享同一个 begin/end stack。

## 15.2 GPU Timestamp Query

如果每个 command list 会写入 timestamp query，则 QueryHeap 索引分配必须线程安全。

建议在 Main Thread 生成 GfxCommand 时预分配 query index：

```cpp
struct CommandBeginProfiler
{
    StringView _name;
    u32 _begin_query_index = 0u;
    u32 _end_query_index = 0u;
};
```

工作线程只按指定索引写 timestamp。

禁止工作线程动态从非线程安全 QueryHeap allocator 获取索引。

Query resolve 可以：

- 每个 command list 自己 resolve 自己的范围。
- 在帧末 command list 统一 resolve 所有已分配范围。

第一版优先选择帧末统一 resolve。

---

## 16. FrameAllocator 和 GfxCommand 生命周期

Main Thread 生成的 GfxCommand 内存必须至少存活到：

1. 所有录制 Job 完成。
2. 所有工作线程不再访问 GfxCommand。

不需要等待 GPU 完成，因为 command list 录制完成后 GPU 不再读取 GfxCommand CPU 内存。

因此 FrameAllocator 的 reset 时机必须晚于录制 Job join。

建议：

```text
Main Thread frame allocator
  生成 GfxCommand
  提交 FrameCommandBatch
  等待 RenderThread 返回“CPU command consumed”标记
  才允许复用对应 frame slot
```

如果当前通过多帧 ring 保证生命周期，必须确认最大 render latency 不会覆盖仍在录制的 frame slot。

---

## 17. Job 粒度

## 17.1 第一版

第一版可以一个 `CommandGroup` 对应一个录制 Job。

但对于极小 Pass，Job 调度开销可能高于录制收益。

可设置最小命令数量阈值：

```cpp
constexpr u32 kMinParallelCommandCount = 64u;
```

规则：

- 命令数大于阈值：独立 Job。
- 命令数小于阈值：与相邻兼容 Pass 合并。
- 串行命令不会参与合并。
- 不允许改变 submission 顺序。

---

## 17.2 后续优化

后续根据上一帧 CPU 录制耗时动态切分：

- Shadow caster draws 按对象范围切分。
- GBuffer opaque draws 按 RenderQueue chunk 切分。
- Forward transparent 保持稳定排序后按连续区间切分。
- Sprite batch 按 batch range 切分。
- 大型 compute dispatch sequence 按独立 dispatch group 切分。

建议目标录制任务时长：

```text
0.2 ms 到 1.0 ms
```

不要在第一版写死复杂动态调度。

---

## 18. 故障处理

录制 Job 抛出异常或返回失败时：

1. 标记该 `RecordedCommandList::_is_valid = false`。
2. RenderThread 不提交不完整 command list。
3. 输出 group name、submission index 和首个失败 command 类型。
4. Debug 模式触发断言。
5. Release 模式跳过本帧 Present 或提交错误画面由项目策略决定。
6. 不允许提交部分录制但未 Close 的 command list。
7. command buffer 必须安全释放回 pool 或销毁。

建议录制结果增加：

```cpp
struct RecordedCommandList
{
    Ref<RHICommandBuffer> _command_buffer;
    String _error_message;
    u32 _submission_index = 0u;
    bool _is_valid = false;
};
```

---

## 19. 调试开关

增加运行时配置：

```cpp
struct MultithreadRenderingConfig
{
    bool _enabled = false;
    bool _parallel_recording = false;
    bool _batch_submission = false;
    bool _validate_submission_order = true;
    bool _force_serial_pso_creation = true;
    u32 _max_recording_jobs = 0u;
    u32 _min_parallel_command_count = 64u;
};
```

建议支持以下模式：

| 模式 | 并行录制 | 批量提交 |
|---|---:|---:|
| 旧路径 | 否 | 否 |
| 对照路径 | 否 | 是 |
| 验证路径 | 是 | 否 |
| 完整路径 | 是 | 是 |

用于快速定位问题来自：

- 并行录制。
- 批量提交。
- Barrier 编译。
- Descriptor 生命周期。
- PSO 并发。

---

## 20. 实施阶段

## 阶段 0：基线和开关

任务：

- 增加多线程渲染配置。
- 保留旧执行路径。
- 增加单线程批量提交路径。
- 增加 command group submission index。
- 记录当前每帧 command list 数量、提交次数和录制耗时。

验收：

- 关闭新功能时行为和当前版本一致。
- 单线程批量提交画面一致。
- PIX 中 command list 顺序正确。

---

## 阶段 1：CommandBuffer 基础修复

任务：

- 修复 `IsReady` 的 `>=`。
- 增加 `_is_submitted`。
- 使用正确 `_dx_cmd_type`。
- Pool Get/Release 全流程加锁。
- Reset command-buffer-local 状态。
- 清除 `ProcessGpuCommand` static 可变对象。
- 统计改为 command-buffer-local。

验收：

- 单线程路径稳定运行。
- DX12 Debug Layer 无 allocator reset 错误。
- Pool 中 command buffer 可正常复用。
- RenderDoc/PIX 捕获结果一致。

---

## 阶段 2：Barrier 预计算

任务：

- 增加 `CompiledResourceBarrier`。
- RenderGraph 编译明确 before/after。
- GfxCommand 使用显式 Barrier。
- 工作线程路径不再修改资源全局 state。
- 外部资源导入时必须声明初始状态。
- Swapchain back buffer 初始状态固定为 Present。

验收：

- 单线程新 Barrier 路径画面一致。
- DX12 Debug Layer 无 state mismatch。
- Pass 禁用/启用时 Barrier 仍正确。
- HZB mip、Shadow mip/array、GBuffer 等 subresource 状态正确。
- RenderGraph DOT 或 Debug UI 能显示 before/after。

---

## 阶段 3：PSO Encode State 去全局化

任务：

- 拆分 `GraphicsPipelineLibrary`。
- Encode state 移到 `D3DCommandBuffer`。
- PSO 对象改为创建后不可变。
- 移除 PSO 内 per-draw resource bindings。
- Material draw state 捕获完整 pipeline state。
- 禁止工作线程修改 Shader 和 Material。

验收：

- 单线程路径画面一致。
- 同时录制两个不同 Shader/Material 的 command list 不互相污染。
- 同时录制不同 MRT format 的 Pass 不互相污染。
- PSO cache 命中结果稳定。
- TSAN 类工具或自定义并发检测无明显共享写入。

---

## 阶段 4：并行录制

任务：

- 增加 `FrameCommandBatch`。
- RenderThread 划分并行区间。
- JobSystem 并行调用 `RecordGroup`。
- 每个 Job 独占 command buffer。
- 等待完成后按 submission index 排序。
- 仍可先逐 list 提交用于对照。

验收：

- 强制不同 Job 随机 sleep 后提交顺序仍正确。
- 1、2、4、8 个工作线程画面一致。
- Debug Layer 无多线程 command list/allocator 错误。
- CPU RenderThread 录制耗时下降。
- 关闭并行开关可回到单线程。

---

## 阶段 5：批量提交

任务：

- 实现 `ExecuteRHICommandBuffers`。
- 一个批次只 Signal 一次 fence。
- command buffer 记录共享 submitted fence。
- 调整 pool ready 判断。
- 提交后汇总统计。

验收：

- 每帧 `ExecuteCommandLists` 调用次数明显下降。
- 每帧 fence signal 次数明显下降。
- command buffer 不会提前 Reset。
- GPU 时间和画面不回退。
- PIX 中 command list 顺序与 submission index 一致。

---

## 阶段 6：Present 拆分

任务：

- 拆分 RecordPresentCommands 和 PresentSwapchains。
- ImGui 保持 RenderThread 串行录制。
- Present barrier 进入最后 command list。
- 所有提交完成后调用 Present。

验收：

- 多窗口 swapchain 正常。
- Resize、Minimize、Restore 正常。
- VSync 开关正常。
- Present 前 back buffer 状态为 Present。
- Frame fence 和 back buffer index 正确推进。

---

## 阶段 7：Descriptor 生命周期

任务：

- 动态 descriptor 按 frame slot 分区。
- frame region 与 submitted fence 绑定。
- 多线程分配使用 atomic cursor 或线程 block。
- 禁止 ring wrap 覆盖未完成 frame。

验收：

- 高 descriptor 压力场景无随机贴图错乱。
- 连续 Resize 和高 draw call 场景稳定。
- Debug Layer 无 descriptor heap 使用异常。
- 强制 GPU 慢帧时不会覆盖仍在使用的 descriptor。
- 三帧以上 in-flight 稳定。

---

## 21. 验收测试场景

至少覆盖以下场景。

### 21.1 基础场景

- 空场景。
- 单相机。
- 多相机。
- Scene View + Game View。
- 编辑器 ImGui。
- Swapchain resize。
- 窗口最小化和恢复。

### 21.2 渲染功能

- GBuffer。
- SSAO。
- HZB 全 mip。
- Directional shadow。
- Spot shadow。
- Point shadow cubemap。
- Area light shadow。
- Volumetric fog。
- TAA。
- Motion vector。
- Transparent。
- Sprite。
- UI。
- Post process。
- Readback。
- Texture upload。
- Buffer upload。
- BLAS/TLAS 构建。

### 21.3 压力测试

- 1000 以上 draw call。
- 多材质和多 Shader variant。
- 高频 PSO 切换。
- 高频 descriptor table 分配。
- GPU 故意限速，制造多帧 in-flight。
- 随机化 Job 执行顺序。
- 随机给录制 Job 增加 0 到 5 ms sleep。
- 每帧切换线程数。
- 每帧切换串行/并行路径。
- Resize 连续拖动窗口。
- Shader 热重载。
- RenderGraph Pass 动态启用和禁用。

---

## 22. 正确性断言

Debug 模式增加以下断言。

### CommandBuffer

```cpp
AL_ASSERT(!command_buffer->IsAcquired() || command_buffer->OwnerThreadId() == CurrentThreadId());
AL_ASSERT(command_buffer->IsReady());
AL_ASSERT(!command_buffer->IsSubmitted());
```

录制结束：

```cpp
AL_ASSERT(command_buffer->IsClosed());
AL_ASSERT(command_buffer->ProfilerStackEmpty());
```

提交：

```cpp
AL_ASSERT(IsRenderThread());
AL_ASSERT(std::is_sorted(command_buffers.begin(), command_buffers.end(), CompareSubmissionIndex));
```

### Resource Barrier

```cpp
AL_ASSERT(barrier._resource != nullptr);
AL_ASSERT(barrier._before != barrier._after);
AL_ASSERT(barrier._sub_resource < resource_subresource_count || barrier._sub_resource == kTotalSubRes);
```

### Descriptor

```cpp
AL_ASSERT(allocation_begin >= frame_region_begin);
AL_ASSERT(allocation_end <= frame_region_end);
AL_ASSERT(completed_fence >= frame_region_submitted_fence_before_reset);
```

### Pool

```cpp
AL_ASSERT(!command_buffer->IsAcquired());
AL_ASSERT(command_buffer->IsReady());
```

---

## 23. 性能统计

增加以下统计项：

```cpp
struct MultithreadRenderingStatistics
{
    u32 _recording_job_count = 0u;
    u32 _recorded_command_list_count = 0u;
    u32 _execute_command_lists_count = 0u;
    u32 _fence_signal_count = 0u;
    u32 _pso_cache_hit = 0u;
    u32 _pso_cache_miss = 0u;
    u32 _dynamic_descriptor_count = 0u;
    u32 _command_buffer_pool_count = 0u;

    f32 _render_thread_recording_time_ms = 0.0f;
    f32 _worker_recording_time_ms = 0.0f;
    f32 _recording_wait_time_ms = 0.0f;
    f32 _submission_time_ms = 0.0f;
};
```

建议在 Editor Debug Panel 显示：

- 当前并行开关。
- 工作线程数量。
- 每帧录制 Job 数量。
- 每帧 command list 数量。
- 每帧提交次数。
- 每帧 fence 次数。
- 最慢录制 Job。
- Job 平均录制耗时。
- RenderThread 等待耗时。
- PSO cache miss 数。
- Descriptor 使用峰值。
- CommandBuffer pool 使用量。

---

## 24. 预期收益

主要收益来源：

1. 将 `ProcessGpuCommand` 和 DX12 API command recording 分散到多个 CPU 核心。
2. 降低单一 RenderThread 的 CPU 峰值。
3. 批量提交减少 `ExecuteCommandLists` 固定开销。
4. 减少 fence signal 次数。
5. 为后续大型 Shadow/GBuffer Pass 分块录制提供基础。

不应期待以下工作自动加速：

- Main Thread RenderGraph callback。
- Culling。
- RenderQueue 构建。
- Material draw state 捕获。
- Shader 编译。
- PSO 首次创建。
- GPU 执行时间。
- Present 等待。
- 资源上传本身。

若当前瓶颈主要在 Main Thread 生成 GfxCommand，而不在 RenderThread `ProcessGpuCommand`，第一版收益会有限。

---

## 25. 实现注意事项

### 25.1 不要通过大锁包装整个录制过程

以下实现不可接受：

```cpp
std::lock_guard lock(s_graphics_pipeline_mutex);
BindShader();
BindResources();
DrawIndexed();
```

这会使多个工作线程实际串行执行。

### 25.2 不要按照 Job 完成顺序提交

即使 Barrier 已预计算，也必须保持 RenderGraph submission order。

### 25.3 不要在 command list 提交前释放 allocator

Pool Release 只代表 CPU 所有权释放。

真正可 Reset 仍需等待 submitted fence 完成。

### 25.4 不要让工作线程调用 Present

Present、MoveToNextFrame 和 swapchain 操作必须在 RenderThread。

### 25.5 不要让工作线程修改资源当前状态

全局资源状态只允许由编译阶段或提交完成后的受控逻辑更新。

### 25.6 不要在第一版同时引入 Async Compute

CPU 多线程录制和多 Queue 调度必须分阶段完成。

### 25.7 不要假设加 mutex 就等于逻辑正确

资源状态、PSO encode state 和 descriptor 生命周期都需要结构性拆分。

---

## 26. 推荐文件改造范围

根据当前工程命名适配，预计涉及：

```text
RenderGraph/
  RenderGraph.h
  RenderGraph.cpp
  RenderPass.h
  RenderGraphResource.h

Render/
  CommandBuffer.h
  CommandBuffer.cpp
  GfxCommand.h
  GpuCommandWorker.h
  GpuCommandWorker.cpp
  RenderingStates.h

RHI/
  RHICommandBuffer.h
  RHICommandBufferPool.h
  GraphicsContext.h

RHI/DX12/
  D3DCommandBuffer.h
  D3DCommandBuffer.cpp
  D3DContext.h
  D3DContext.cpp
  D3DResourceStateGuard.h
  D3DDescriptorAllocator.h
  D3DDescriptorAllocator.cpp
  D3DGraphicsPipelineLibrary.h
  D3DGraphicsPipelineLibrary.cpp

Material/
  Material.h
  Material.cpp
  MaterialDrawState.h

Shader/
  Shader.h
  Shader.cpp

Editor/
  RenderingDebugPanel.cpp
```

实际路径以仓库为准。

---

## 27. Claude 实现要求

实现时遵守以下规则：

1. 先阅读现有相关类，不要重新设计一套与当前引擎不兼容的渲染框架。
2. 每个阶段独立提交，保证每一步都可编译和运行。
3. 始终保留旧单线程路径作为回退。
4. 不要在未完成 Barrier 预计算和 PSO state 去全局化前启用并行录制。
5. 不要用全局大锁掩盖共享状态问题。
6. 新增成员变量使用 `_lower_snake_case`。
7. 普通变量使用 `lower_snake_case`。
8. 静态变量以 `s` 开头。
9. 常量使用 `kCamelCase`。
10. 类名和函数名使用 CamelCase。
11. 代码按 120 列格式化，避免不必要换行。
12. 修改后运行全量编译，并修复所有新增 warning。
13. Debug 构建启用 D3D12 Debug Layer 和 GPU-based validation 进行验证。
14. 对所有新增线程共享结构明确说明所有权和同步方式。
15. 所有 command list、allocator、descriptor region 都必须能关联到 submitted fence。
16. 不允许在工作线程中调用 `ExecuteCommandLists`、`Signal`、`Present`。
17. 不允许通过工作线程修改 Shader、Material、PSO 和资源全局状态。
18. 每完成一个阶段，输出修改文件列表、核心设计、风险和测试结果。

---

## 28. 建议的首个实现任务

首个实现任务不要直接启用并行录制。

先完成以下内容：

```text
1. 增加 MultithreadRenderingConfig 和旧路径开关。
2. 修复 D3DCommandBuffer fence ready 判断。
3. 修复 command allocator/list 类型硬编码。
4. 让 RHICommandBufferPool Get/Release 完整线程安全。
5. 将 ProcessGpuCommand 中所有 static 可变临时对象移除。
6. 将 render statistics 改成 command-buffer-local。
7. 实现单线程 ExecuteRHICommandBuffers 批量提交。
8. 保持画面和现有行为一致。
```

该任务完成并验证后，再执行 Barrier 预计算阶段。

---

## 29. 最终完成标准

只有同时满足以下条件，才能认为第一版 DX12 多线程渲染完成：

- 多个工作线程确实同时录制不同 command list。
- 同一个 allocator/list 没有跨线程共享。
- RenderThread 是唯一 GPU queue 提交者。
- command list 按 RenderGraph submission order 提交。
- Barrier before/after 在录制前已经确定。
- 工作线程不修改资源全局状态。
- PSO encode state 不再是全局共享可变状态。
- Shader、Material、PSO 在并行录制阶段只读。
- 动态 descriptor 不会覆盖 GPU 尚未使用的区域。
- command buffer 只有在 submitted fence 完成后才 Reset。
- ImGui、Upload、Readback、Present 串行路径稳定。
- D3D12 Debug Layer 无新增错误。
- 随机化 Job 完成顺序后渲染结果仍一致。
- 关闭功能开关能够恢复旧路径。
- Profiler 能显示并行录制带来的 CPU 收益。
