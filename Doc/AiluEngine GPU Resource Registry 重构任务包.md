# AiluEngine GPU Resource Registry 重构任务包

## 1. 目标

重构当前 GPU 资源生命周期管理，彻底移除 `CommandBuffer` 基于 `shared_ptr / keep_alive / unordered_set` 的资源保活机制。

新的核心模型：

```text
Frontend Object
    ↓
GpuResourceHandle
    ↓
Command Stream
    ↓
GpuResourceRegistry
    ↓
Backend GpuResource
    ↓
Fence Deferred Retirement
```

要求：

- Render Command 不再持有 `Object* / Ref<Object>`。
- GPU 资源由 `GpuResourceRegistry` 唯一拥有。
- Command 只保存 Handle、POD 数据和必要的状态快照。
- Create / Use / Destroy 统一进入有序 Render Submission Timeline。
- GPU 资源销毁不允许 `WaitForFence()`。
- 资源在对应 submission fence 完成后统一回收。
- 删除现有 `KeepAlive` 和逐资源 `TrackFence` 热路径。
- 优先代码简洁、连续内存和 O(1) 操作，禁止为了扩展性过度抽象。

---

# 2. 当前需要解决的问题

当前存在类似：

```text
CommandBuffer
    ↓
CollectKeepAliveObjects
    ↓
遍历所有 Command
    ↓
unordered_set<Object*>
    ↓
SharedFromThis
    ↓
vector<shared_ptr<Object>>
    ↓
RHICommandBuffer
    ↓
GPU fence 完成
```

Profile 中：

```text
CommandBuffer::TakeKeepAliveObjects        ~3.68%
CommandBuffer::CollectKeepAliveObjects     ~3.24%
CommandBuffer::KeepAlive                   ~2.72%
unordered_set::insert                      ~1.44%
shared_ptr emplace                         ~0.91%
```

同时目前部分 DX12 资源析构存在：

```cpp
WaitForFence(_fence_value);
```

导致 CPU Object 生命周期、Render Command 生命周期和 GPU 生命周期耦合。

本次重构直接删除该模型，不做局部优化。

---

# 3. 新增 GpuResourceHandle

实现轻量 generational handle。

建议：

```cpp
template<typename tag_t>
struct GpuHandle
{
    u32 _index = kInvalidIndex;
    u32 _generation = 0u;

    bool IsValid() const
    {
        return _index != kInvalidIndex;
    }

    bool operator==(const GpuHandle &) const = default;
};
```

提供基础类型：

```cpp
using GpuResourceHandle = GpuHandle<GpuResourceTag>;
using BufferHandle = GpuHandle<BufferTag>;
using TextureHandle = GpuHandle<TextureTag>;
using ShaderHandle = GpuHandle<ShaderTag>;
```

要求：

- Handle 保持 trivially copyable。
- 不包含引用计数。
- 不包含指针。
- 不做复杂智能 Handle 类。
- Debug 下检查 generation。
- Release 下保持 Resolve 路径尽量轻量。

---

# 4. 新增 GpuResourceRegistry

Registry 成为 Backend GPU Resource 的唯一 owner。

基础结构：

```cpp
enum class EGpuResourceSlotState : u8
{
    kFree,
    kAlive,
};

struct GpuResourceSlot
{
    u32 _generation = 1u;
    EGpuResourceSlotState _state = EGpuResourceSlotState::kFree;
    std::unique_ptr<GpuResource> _resource;
};
```

Registry：

```cpp
class GpuResourceRegistry
{
public:
    GpuResourceHandle Add(std::unique_ptr<GpuResource> resource);

    GpuResource *Resolve(GpuResourceHandle handle);
    const GpuResource *Resolve(GpuResourceHandle handle) const;

    void Retire(GpuResourceHandle handle, u64 fence);
    void Collect(u64 completed_fence);

private:
    Vector<GpuResourceSlot> _slots;
    Vector<u32> _free_indices;
    Deque<RetiredGpuResource> _retired_resources;
};
```

不要增加：

- 多级 Registry。
- virtual Registry interface。
- shared_ptr。
- weak_ptr。
- intrusive_ptr。
- resource manager service graph。

一个 Registry 即可。

---

# 5. Generational Slot

Slot 复用时增加 generation。

旧：

```text
{ index = 42, generation = 5 }
```

销毁后再次分配：

```text
{ index = 42, generation = 6 }
```

因此旧 Command 即使错误访问，也不能 Resolve 到新的 GPU 资源。

Debug Resolve：

```cpp
GpuResource *GpuResourceRegistry::Resolve(GpuResourceHandle handle)
{
    if (handle._index >= _slots.size())
        return nullptr;

    auto &slot = _slots[handle._index];

    if (slot._generation != handle._generation || slot._state != EGpuResourceSlotState::kAlive)
        return nullptr;

    return slot._resource.get();
}
```

非法 Handle 在 Debug 下增加 assert/log，方便发现生命周期错误。

---

# 6. GpuResource 不再负责 CPU 引用生命周期

Registry 建立后，清理 `GpuResource`。

如果当前：

```cpp
class GpuResource : public Object
```

仅为了：

```text
SharedFromThis
Ref<>
keep alive
```

则移除该继承关系。

目标：

```cpp
class GpuResource
{
public:
    virtual ~GpuResource() = default;
};
```

`GpuResource` 只表示 Backend GPU 对象。

不再承担：

- shared ownership。
- Object 生命周期。
- Command 保活。
- GPU fence 等待。

---

# 7. Frontend Object 与 Backend Resource 分离

Frontend 示例：

```cpp
class Texture : public Object
{
public:
    TextureHandle Handle() const
    {
        return _handle;
    }

private:
    TextureHandle _handle;
};
```

Backend：

```cpp
class D3DTexture final : public GpuResource
{
private:
    ComPtr<ID3D12Resource> _resource;

    DescriptorAllocation _srv;
    DescriptorAllocation _uav;

    i32 _bindless_srv_index = -1;
    i32 _bindless_uav_index = -1;
};
```

不要要求：

```text
Texture Object 生命周期 == D3DTexture 生命周期
```

二者通过 Handle 解耦。

---

# 8. Command 全面 Handle 化

所有可能跨线程、延迟执行的 Render Command 中禁止保存：

```text
Object*
GpuResource*
Material*
Texture*
Buffer*
Ref<>
shared_ptr
```

例如：

```cpp
struct CommandDraw
{
    BufferHandle _vertex_buffer;
    BufferHandle _index_buffer;
    BufferHandle _per_object_buffer;

    MaterialDrawState _draw_state;

    u32 _index_count = 0u;
    u32 _instance_count = 1u;
};
```

执行阶段：

```cpp
auto *vertex_buffer = registry.Resolve(cmd->_vertex_buffer);
```

Resolve 应尽量集中在 Command 执行入口，不要多层重复 Resolve。

---

# 9. PipelineBindingSnapshot Handle 化

当前类似：

```cpp
struct PipelineBindingSnapshotEntry
{
    GpuResource *_resource;
};
```

改为：

```cpp
struct PipelineBindingSnapshotEntry
{
    GpuResourceHandle _resource;
    u32 _slot = 0u;
};
```

Material / Shader 状态捕获完成后，只保留：

```text
Handle
POD data
immutable snapshot
```

异步 Render Thread 不再访问原始 Material Object。

---

# 10. CommandDraw 删除 Material*

如果当前：

```cpp
CommandDraw::_mat
```

主要被 Frame Debugger 使用，则删除。

需要的 Debug 信息在 frontend command capture 时存下来：

```cpp
struct DrawDebugInfo
{
    String _material_name;
    ObjectId _material_id;
};
```

或者仅 Debug/Profile 构建开启。

原则：

> Material 不允许进入 Backend Command Stream。

---

# 11. Shader 逐步改为 Backend Handle

Shader Backend Resource 同样注册到 Registry。

Render Command 最终使用：

```cpp
ShaderHandle
```

而不是：

```cpp
Shader*
ComputeShader*
RayTracingShader*
```

如当前一次完成成本过高，可优先完成 Texture / Buffer / RenderTarget，再迁移 Shader。

但最终目标必须是 Command Stream 中不存在 frontend Shader Object 指针。

---

# 12. Resource Destroy Command 化

资源销毁必须成为有序 Render Command。

新增类似：

```cpp
struct CommandDestroyGpuResource
{
    GpuResourceHandle _handle;
};
```

生命周期：

```text
Submission 100
    Use Texture X

Submission 101
    Use Texture X

Submission 102
    Destroy Texture X
```

Backend 执行 `Destroy` 时：

```cpp
registry.Retire(handle, submission_fence);
```

不立即 delete。

---

# 13. Submission Timeline

Create / Use / Destroy 必须服从同一个逻辑 submission 顺序。

例如：

```cpp
using SubmissionId = u64;
```

允许：

```text
Submission 100 → Job 3 record
Submission 101 → Job 1 record
```

并行录制。

但是最终 submission 顺序必须保持正确。

要求：

> Recording 可以并行，Resource Lifetime 必须以 Submission Order 为准。

不要依赖：

- Job 完成顺序。
- Worker Thread 顺序。
- shared_ptr refcount 顺序。

---

# 14. Fence Deferred Retirement

新增简单结构：

```cpp
struct RetiredGpuResource
{
    u64 _fence = 0u;
    std::unique_ptr<GpuResource> _resource;
};
```

Registry：

```cpp
void GpuResourceRegistry::Collect(u64 completed_fence)
{
    while (!_retired_resources.empty())
    {
        if (_retired_resources.front()._fence > completed_fence)
            break;

        _retired_resources.pop_front();
    }
}
```

因为 fence 单调递增，不需要：

```text
unordered_map
priority_queue
tree
hash
```

保持简单 FIFO 即可。

---

# 15. 整个 GpuResource 一次性 Retire

不要分别 retire：

```text
ID3D12Resource
counter buffer
SRV
UAV
bindless slot
allocation
```

统一移动：

```cpp
_retired_resources.emplace_back(RetiredGpuResource{
    ._fence = fence,
    ._resource = std::move(slot._resource),
});
```

Fence 完成以后：

```text
~D3DGPUBuffer()
```

一次释放内部所有 native resource。

因此：

> Lifecycle 原子仍然是逻辑 `GpuResource`，不是单个 `ID3D12Resource`。

一个 `GpuResource` 包含多个 D3D Resource 不需要特殊处理。

---

# 16. 删除析构 WaitForFence

清理所有：

```cpp
WaitForFence(_fence_value);
```

正常 GPU Resource 析构必须可以直接释放：

```cpp
D3DTexture::~D3DTexture()
{
    // COM、descriptor、bindless slot 正常释放
}
```

因为进入 destructor 时：

```text
completed_fence >= retire_fence
```

已经由 Registry 保证。

`WaitForFence()` 仅允许用于明确同步操作，例如：

```text
Device Shutdown
Explicit Flush
Swapchain destructive rebuild
Synchronous Readback
特殊调试操作
```

正常资源销毁绝对不能阻塞 GPU。

---

# 17. Descriptor / Bindless 生命周期

Descriptor、Bindless Index、Heap Allocation 等必须作为 `GpuResource` 内部状态一起 Retire。

禁止：

```text
先 Free Descriptor
再 Wait GPU
```

资源 retire 后，整个 Backend Object 留在 deferred queue。

直到 fence 完成再执行 destructor。

这样 naturally 保证：

```text
ID3D12Resource
Descriptor
Bindless Index
Counter Resource
```

生命周期一致。

---

# 18. 删除 `_used_resources + TrackFence`

如果当前主要使用一个有序 Graphics Queue Timeline，则 Resource Destroy 已经位于 submission stream 中：

```text
Use X
Use X
Destroy X
Signal Fence
```

因此 Destroy Submission 对应 Fence 完成时，之前对 X 的 GPU 使用必然也已完成。

可以删除：

```cpp
GpuResource::_fence_value
GpuResource::Track()
GpuResource::GetFenceValue()

D3DCommandBuffer::_used_resources
D3DCommandBuffer::_used_resource_set
D3DCommandBuffer::MarkUsedResource()
D3DCommandBuffer::PostExecute()
```

这样进一步删除每个 command list 的：

```text
resource hash
resource dedup
resource fence tracking
```

热路径 bookkeeping。

---

# 19. 多 Queue 注意事项

如果当前：

```text
Graphics
Compute
Copy
```

存在独立 GPU queue 且资源可能跨 queue 使用，则不要假装单 fence 可以解决全部问题。

第一版可采用最简单策略：

```text
资源 Destroy 前要求相关 Queue 已通过显式 queue synchronization
```

即 Resource Lifetime 仍跟随统一 submission dependency。

如果当前实际上只有 Direct Queue 为主要执行队列，则不要提前设计复杂：

```text
per-resource multi-queue fence array
```

等真正启用独立 Async Compute / Copy 生命周期后再扩展。

---

# 20. Resource State 与 Registry 分离

Registry 只负责：

```text
Identity
Lifetime
Ownership
```

Resource Barrier 系统负责：

```text
Physical Resource State
Subresource State
Transition
```

不要把二者重新揉在一起。

如果一个逻辑资源内部有多个 physical resource，可使用：

```cpp
struct ResourceStateKey
{
    GpuResourceHandle _resource;
    u16 _physical_resource = 0u;
    u16 _subresource = kAllSubresources;
};
```

例如：

```text
BufferHandle #123

physical 0 = main buffer
physical 1 = counter buffer
```

Registry 生命周期仍然只管理：

```text
#123
```

---

# 21. 可考虑统一 Buffer Backend

如果改动过程中成本合适，可把：

```text
VertexBuffer
IndexBuffer
ConstantBuffer
GPUBuffer
StructuredBuffer
```

Backend 实现逐渐收敛为：

```text
D3DBuffer
```

Command 使用 binding descriptor 表达差异：

```cpp
struct VertexBufferBinding
{
    BufferHandle _buffer;
    u32 _stride = 0u;
    u32 _offset = 0u;
};

struct IndexBufferBinding
{
    BufferHandle _buffer;
    EIndexFormat _format;
    u32 _offset = 0u;
};
```

这项不是 Registry 重构的强制阻塞项。

避免为了本次任务扩大到完整 Buffer API 重写。

---

# 22. 必须删除的旧代码

完成后应不存在：

```text
CommandBuffer::TakeKeepAliveObjects
CommandBuffer::CollectKeepAliveObjects
CommandBuffer::KeepAlive

_keep_alive_objects
_keep_alive_set

RHICommandBuffer::AddKeepAliveObjects
CommandGroup::_keep_alive_objects
```

同时尽量删除：

```text
GpuResource::SharedFromThis
GpuResource::Track
GpuResource::_fence_value

D3DCommandBuffer::_used_resources
D3DCommandBuffer::_used_resource_set
```

禁止为了兼容旧体系长期保留双生命周期路径。

---

# 23. 推荐实施顺序

按以下顺序完成，期间允许工程短暂不可运行，不要求兼容旧接口：

```text
1. 建立 GpuHandle
2. 建立 GpuResourceRegistry
3. Texture / Buffer Backend 注册到 Registry
4. Command Buffer/Texture 指针全面 Handle 化
5. PipelineBindingSnapshot Handle 化
6. Material 从 Command Stream 移除
7. 建立 DestroyGpuResource Command
8. 接入 Submission Fence
9. 实现 RetiredGpuResource Queue
10. 删除 Resource Destructor WaitForFence
11. 删除 KeepAlive 系统
12. 删除 _used_resources / TrackFence
13. 清理旧 API
14. 性能与生命周期测试
```

优先一次迁移完成，不做大量 adapter。

---

# 24. 必测场景

重点测试：

### 高频创建销毁

```text
创建 Texture
提交 Draw
立即释放 frontend Texture
创建新 Texture 导致旧 slot reuse
```

确保 generation 可以检测旧 Handle。

### GPU 延迟

制造 3~5 帧 GPU backlog：

```text
Use Resource
Destroy Resource
```

确认 native resource 不会提前释放。

### Command 延迟执行

Main Thread 创建 Command 后立即销毁 frontend Object。

确认 Render Thread 不访问任何悬空 frontend pointer。

### Descriptor 复用

确保旧资源 fence 完成前：

```text
SRV
UAV
Bindless Index
```

不会进入 allocator free pool。

### 多线程 Recording

乱序完成 command list recording，最终 submission order 正确。

### Frame Debugger

确认删除 `Material*` 后仍能显示必要的材质/Shader调试信息。

---

# 25. 性能验收

Profile 中应完全消失：

```text
CommandBuffer::TakeKeepAliveObjects
CommandBuffer::CollectKeepAliveObjects
CommandBuffer::KeepAlive
unordered_set<Object*>::insert
SharedFromThis
shared_ptr<Object>::emplace_back
```

同时应显著减少或删除：

```text
D3DCommandBuffer::MarkUsedResource
_used_resource_set::insert
GpuResource::Track
```

Render Command 热路径主要应该只剩：

```text
Handle copy
Vector append
Registry Resolve
D3D12 API calls
```

---

# 26. 架构硬约束

最终代码必须遵守以下规则：

> **规则 1：异步 Render Command 不保存 frontend Object 指针。**

> **规则 2：GpuResourceRegistry 是 Backend GPU Resource 的唯一 owner。**

> **规则 3：Handle 只表示资源身份，不参与引用计数。**

> **规则 4：Create / Use / Destroy 必须服从统一 Submission Timeline。**

> **规则 5：Destroy 只使资源进入 Retirement Queue，不立即释放 GPU Resource。**

> **规则 6：GPU Fence 决定 Backend Resource 真正销毁时间。**

> **规则 7：正常 GPU Resource destructor 中禁止 WaitForFence。**

> **规则 8：Registry 管生命周期，Barrier Tracker 管状态，两者禁止重新耦合。**

---

# 27. 期望最终结果

重构完成后的资源生命周期：

```text
Frontend Texture
       │
       ▼
TextureHandle
       │
       ▼
CommandDraw
       │
       ▼
GpuResourceRegistry::Resolve()
       │
       ▼
D3DTexture
       │
       ▼
ID3D12Resource
```

销毁：

```text
DestroyGpuResource(handle)
        │
        ▼
Submission N
        │
        ▼
Signal Fence N
        │
        ▼
Registry::Retire(handle, N)
        │
        ▼
Retired Queue
        │
completed_fence >= N
        ▼
~D3DTexture()
```

最终完全不再依赖：

```text
shared_ptr keep alive
Command resource scanning
unordered_set pointer dedup
per-command-list resource fence tracking
resource destructor GPU wait
```

这次重构的重点不是给现有 KeepAlive 提速，而是让 **KeepAlive 这个问题从架构上消失**。