# AiluEngine 动态骨骼 BLAS Update / Refit 重构任务包

## 1. 任务目标

为 AiluEngine 的 DXR / Hardware Ray Tracing 补齐 **动态骨骼网格 BLAS 更新能力**，并将现有两条骨骼动画路径：

- CPU Skinning
- Compute Skinning

统一到同一套：

```text
Skinning Output
    ↓
Dynamic BLAS Update / Refit
    ↓
Ray Tracing Pass
```

最终要求：

1. 静态 Mesh 的 BLAS 只构建一次。
2. Skinned Mesh 首次出现时 Full Build，后续骨骼姿态变化时执行 BLAS Update / Refit。
3. Compute Skinning 的 BLAS 更新必须发生在 Compute Skinning 之后。
4. CPU Skinning 与 Compute Skinning 最终都产出独立的 per-instance skinned vertex buffer。
5. Ray Tracing Hit Shader 读取到的 position / normal / tangent 必须与 BLAS 使用的蒙皮结果一致。
6. 同一个 Mesh 被多个角色实例使用时，每个角色必须拥有独立的动态 BLAS。
7. Transform 变化只更新 TLAS；骨骼形变更新 BLAS。
8. 整体设计保持简单，不增加复杂的动态 BLAS rebuild heuristic。

---

# 2. 当前工程现状

当前 AiluEngine 已经具备：

- DX12 Ray Tracing 基础设施
- `RayTracingGeometry`
- BLAS / TLAS 构建
- `SceneRayTracingProxy`
- Compute Skinning
- CPU Skinning
- `SkinCacheEntry`
- Render Graph
- Bindless vertex stream
- Hardware Ray Tracing GI

但动态 BLAS Update 尚未真正完成。

---

# 3. 当前存在的问题

## 3.1 `RayTracingGeometry::Update()` 实际仍然走 Full Build

当前逻辑类似：

```cpp
void RayTracingGeometry::Update()
{
    auto cmd = CommandBufferPool::Get();
    cmd->BuildAS(this, false);
}
```

这里调用参数仍然是：

```cpp
false
```

意味着没有请求 BLAS Update。

应改为：

```cpp
cmd->BuildAS(this, true);
```

---

## 3.2 `CommandBuffer::BuildAS()` 没有正确传递 `_is_update`

当前逻辑类似：

```cpp
void BuildAS(RayTracingGeometry *geometry, bool is_update = false)
{
    auto cmd = CommandPool::Get().Alloc<CommandBuildAS>();
    cmd->_dst = GetResourceHandle(geometry);
    cmd->_src = is_update ? cmd->_dst : GpuResourceHandle{};
    cmd->_scratch_size = geometry->GetScratchBufferSize();
    cmd->_is_blas = true;
    _commands.emplace_back(cmd);
}
```

缺少：

```cpp
cmd->_is_update = is_update;
```

需要补齐。

---

## 3.3 DX12 Backend 当前明确不支持 BLAS Update

当前 Backend 中存在类似：

```cpp
if (cmd_bas->_is_update)
{
    AL_ASSERT_MSG(false, "BLAS update is not supported yet!");
}
```

需要正式实现：

```text
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE
```

---

# 4. 总体设计

动态 BLAS 不应该理解：

- CPU Skinning
- GPU Skinning
- Skeleton
- Animator
- Bone Matrix

BLAS 只关心：

> 当前这一帧最终顶点位置所在的 GPU Buffer。

统一模型：

```text
Animation Pose
    ↓
Skinning
    ↓
Skinned Vertex Output
    ↓
BLAS Build / Update
    ↓
Ray Tracing
```

---

# 5. BLAS 类型划分

增加明确的构建模式：

```cpp
enum class ERayTracingGeometryBuildMode : u8
{
    kStatic,
    kDynamic
};
```

例如：

```cpp
struct RayTracingGeometryDesc
{
    ERayTracingGeometryBuildMode _build_mode = ERayTracingGeometryBuildMode::kStatic;
};
```

---

# 6. Static BLAS

Static Mesh 使用：

```cpp
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
```

特点：

```text
首次：
Build

后续：
不更新
```

适用于：

- 普通 StaticMesh
- 不发生顶点形变的 Mesh
- 地形静态块
- 静态环境资产

---

# 7. Dynamic BLAS

Skinned Mesh 使用：

```cpp
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
```

首次：

```text
Full Build
```

之后：

```text
PERFORM_UPDATE
```

---

# 8. D3D12 BLAS Flags 修改

当前创建 `_inputs` 时，不要所有 BLAS 都固定使用：

```cpp
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
```

改为：

```cpp
if (_desc._build_mode == ERayTracingGeometryBuildMode::kDynamic)
{
    _inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
}
else
{
    _inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
}
```

注意：

Dynamic BLAS **第一次 Full Build 时就必须带 `ALLOW_UPDATE`**。

否则后续不能进行 `PERFORM_UPDATE`。

---

# 9. Scratch Buffer 修改

当前如果只使用：

```cpp
prebuild_info.ScratchDataSizeInBytes
```

是不完整的。

Dynamic BLAS 还需要考虑：

```cpp
prebuild_info.UpdateScratchDataSizeInBytes
```

建议：

```cpp
const u64 scratch_size = std::max(
    prebuild_info.ScratchDataSizeInBytes,
    prebuild_info.UpdateScratchDataSizeInBytes);
```

最终：

```text
RayTracingGeometry::_scratch_buffer_size
```

保存 build / update 两者的最大需求。

这样可以复用同一个 scratch allocator。

---

# 10. DX12 BLAS Update 实现

构造：

```cpp
D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc = {};
build_desc.Inputs = blas->_inputs;
build_desc.ScratchAccelerationStructureData = scratch_res->GetGPUVirtualAddress();
build_desc.DestAccelerationStructureData = blas_res->GetGPUVirtualAddress();
```

如果：

```cpp
cmd_bas->_is_update
```

则：

```cpp
build_desc.Inputs.Flags |=
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

build_desc.SourceAccelerationStructureData =
    blas_res->GetGPUVirtualAddress();
```

执行：

```cpp
dxcmd->BuildRaytracingAccelerationStructure(
    &build_desc,
    0,
    nullptr);
```

随后：

```cpp
d3dcmd->UavBarrier(blas->_blas_resource);
```

注意不要修改永久保存的：

```cpp
blas->_inputs.Flags
```

应该只修改局部：

```cpp
build_desc.Inputs.Flags
```

避免下一次调用残留：

```text
PERFORM_UPDATE
```

---

# 11. CommandBuildAS 修改

确保命令正确记录：

```cpp
cmd->_is_update = is_update;
```

推荐最终结构类似：

```cpp
void CommandBuffer::BuildAS(
    RayTracingGeometry *geometry,
    bool is_update)
{
    auto cmd = CommandPool::Get().Alloc<CommandBuildAS>();

    cmd->_dst = GetResourceHandle(geometry);
    cmd->_src = is_update ? cmd->_dst : GpuResourceHandle{};
    cmd->_scratch_size = geometry->GetScratchBufferSize();
    cmd->_is_blas = true;
    cmd->_is_update = is_update;

    _commands.emplace_back(cmd);
}
```

---

# 12. RayTracingGeometry API

不要让外部系统自己决定 DX12 Flag。

继续保持高层 API：

```cpp
geometry->Build();
geometry->Update();
```

例如：

```cpp
void RayTracingGeometry::Build(CommandBuffer *cmd)
{
    cmd->BuildAS(this, false);
}

void RayTracingGeometry::Update(CommandBuffer *cmd)
{
    cmd->BuildAS(this, true);
}
```

推荐逐步淘汰：

```cpp
RayTracingGeometry::Update()
```

内部自己：

```text
Allocate CommandBuffer
Submit
```

这种方式。

原因是：

BLAS Update 必须参与 Render Graph 调度。

---

# 13. 当前最大的时序问题

当前 GPU Skinning 和 Ray Tracing Scene 准备顺序存在潜在冲突。

逻辑大致是：

```text
PrepareHardwareRayTracingScene
    ↓
SceneRayTracingProxy::Sync
    ↓
BLAS Build

ComputeSkinning Pass
    ↓
生成本帧蒙皮顶点
```

这样第一次构建动态 BLAS 时，很可能读取的是：

```text
上一帧数据
```

甚至：

```text
尚未经过 skinning 的 buffer
```

---

# 14. 正确的帧顺序

统一改为：

```text
Animation Evaluate
        ↓
PrepareVisibleSkinning
        ↓
ComputeSkinning / CPU Skin Upload
        ↓
Skinned BLAS Update
        ↓
Ray Tracing GI / Reflection / DDGI
        ↓
Raster / Other Passes
```

---

# 15. 新增 Render Graph Pass

新增：

```text
SkinnedBLASUpdate
```

推荐逻辑：

```cpp
void SceneRayTracingProxy::AddBLASUpdatePass(
    RenderGraph &graph,
    const RenderingData &rendering_data);
```

或者单独：

```cpp
class RayTracingBLASUpdatePass
```

第一版优先简单。

职责仅为：

```text
遍历当前帧 visible dynamic RT geometry
    ↓
检查 dirty
    ↓
执行 BLAS Update
```

不要在这个 Pass：

- 更新 Animation
- 计算 Bone Matrix
- 做 Skinning
- 更新 TLAS
- 重建 packed geometry buffer

保持职责单一。

---

# 16. Render Graph 资源 Usage 设计

不要新增：

```cpp
kRayTracingBuildInput
```

这个名字更像：

```text
某个业务 Pass 的用途
```

而不是：

```text
底层资源访问语义
```

当前 `EResourceUsage` 应继续围绕：

```text
资源如何被访问
```

而不是：

```text
资源被哪个算法使用
```

来设计。

---

## 16.1 BLAS Build Input 不需要专用 Usage

BLAS 构建时使用的：

```text
Vertex Buffer
Index Buffer
```

本质上都是 GPU 只读输入。

因此第一版直接使用已有：

```cpp
EResourceUsage::kReadSRV
```

例如：

```cpp
builder.Read(position_buffer, EResourceUsage::kReadSRV);
builder.Read(index_buffer, EResourceUsage::kReadSRV);
```

不需要：

```cpp
kRayTracingBuildInput
```

也不建议增加：

```cpp
kAccelerationStructureBuildInput
```

除非未来底层 API 的资源状态映射确实需要区分。

---

## 16.2 Acceleration Structure 本体应拆分 Read / Write

当前如果存在类似：

```cpp
kRaytracingAccel
```

并且默认被：

```text
ResourceAccess::isRead()
```

判定为 Read，则无法准确表达：

```text
Build / Update BLAS
```

因为：

```text
BuildRaytracingAccelerationStructure
```

会写入 BLAS / TLAS。

推荐改为：

```cpp
enum class EResourceUsage : u32
{
    kNone = 0,

    kReadSRV = 1 << 0,
    kWriteUAV = 1 << 1,
    kWriteRTV = 1 << 2,
    kDSV = 1 << 3,

    kCopySrc = 1 << 4,
    kCopyDst = 1 << 5,

    kIndirectArgument = 1 << 6,
    kVertexBuffer = 1 << 7,

    kAccelStructRead = 1 << 8,
    kAccelStructWrite = 1 << 9,
};
```

如果当前 bit 顺序已有兼容要求，可保留原 bit，只调整语义和名称。

---

## 16.3 两种 Usage 映射到同一个底层 Resource State

```cpp
kAccelStructRead
kAccelStructWrite
```

在 DX12 上都映射到：

```cpp
EResourceState::kRaytracingAccelerationStructure
```

也就是：

```text
D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
```

区别不在 DX12 Resource State。

区别在：

```text
Render Graph dependency
```

---

## 16.4 ResourceAccess 判定

应保证：

```cpp
kAccelStructRead
```

属于：

```text
Read
```

而：

```cpp
kAccelStructWrite
```

属于：

```text
Write
```

例如：

```cpp
bool ResourceAccess::IsRead() const
{
    return HasAnyFlags(
        _usage,
        EResourceUsage::kReadSRV |
        EResourceUsage::kCopySrc |
        EResourceUsage::kIndirectArgument |
        EResourceUsage::kVertexBuffer |
        EResourceUsage::kAccelStructRead);
}
```

```cpp
bool ResourceAccess::IsWrite() const
{
    return HasAnyFlags(
        _usage,
        EResourceUsage::kWriteUAV |
        EResourceUsage::kWriteRTV |
        EResourceUsage::kDSV |
        EResourceUsage::kCopyDst |
        EResourceUsage::kAccelStructWrite);
}
```

根据项目当前 bitmask helper 调整实际写法。

---

# 17. 推荐 RDG 数据流

Compute Skinning：

```text
position_buffer
    kWriteUAV
```

BLAS Update：

```text
position_buffer
    kReadSRV

index_buffer
    kReadSRV

blas
    kAccelStructWrite
```

Ray Tracing Pass：

```text
tlas
    kAccelStructRead
```

最终形成：

```text
ComputeSkinning
    position_buffer: kWriteUAV
        ↓
SkinnedBLASUpdate
    position_buffer: kReadSRV
    index_buffer:    kReadSRV
    BLAS:            kAccelStructWrite
        ↓
TLASUpdate
    BLAS:            kAccelStructRead
    TLAS:            kAccelStructWrite
        ↓
RayTraceGI
    TLAS:            kAccelStructRead
```

不要依赖手工：

```text
Flush
Wait
Transition
Submit Immediately
```

尽量让 Render Graph 根据 Read / Write 依赖自动完成调度与同步。

---

# 18. Compute Skinning Output Buffer 修改

当前 `SkinningSystem::CreateOutputBuffer()` 类似：

```cpp
desc._target = kVertex | kStructured;
desc._is_create_srv = false;
desc._is_create_uav = true;
```

动态 RT 场景需要：

```cpp
desc._is_create_srv = true;
desc._is_create_uav = true;
```

因为该 Buffer 同时承担：

```text
UAV
    Compute Skinning Output

Vertex Buffer
    Raster

SRV
    Ray Tracing Hit Shader

AS Build Input
    BLAS Build / Update
```

---

# 19. Hit Shader 数据一致性问题

当前 Hardware RT 不仅依赖 BLAS。

`SceneRayTracingProxy` 还会维护 packed：

```text
position
normal
uv
tangent
index
```

用于：

```text
Closest Hit
Material Evaluation
Normal Reconstruction
UV Sampling
```

当前如果仍然从：

```cpp
mesh->GetVertices()
mesh->GetNormals()
```

读取，则拿到的是：

```text
Bind Pose
```

而 BLAS 已经使用：

```text
Skinned Position
```

这样会产生：

```text
Triangle intersection 正确
Hit Shader position/normal 错误
```

必须保证：

```text
BLAS 使用的 Vertex Stream
```

和：

```text
Hit Shader 使用的 Vertex Stream
```

来自同一个 SkinCacheEntry。

---

# 20. Bindless Vertex Stream

GPU Skinning 时：

```text
SkinCacheEntry::_position_buffer
SkinCacheEntry::_normal_buffer
SkinCacheEntry::_tangent_buffer
```

必须能够创建 SRV。

否则：

```cpp
GetBindlessSRVIndex()
```

可能返回：

```text
invalid
```

因此动态蒙皮输出 Buffer：

```cpp
_is_create_srv = true;
_is_create_uav = true;
```

---

# 21. BLAS Cache Key 当前设计问题

当前类似：

```cpp
HashMap<Mesh *, CachedGeometry> _mesh_geometry_cache;
```

对于 Static Mesh 没问题。

对于 Skinned Mesh 不成立。

假设：

```text
CharacterMesh
```

被两个 Entity 使用：

```text
Character A
Character B
```

两者：

```text
Mesh*
```

相同。

但：

```text
Skinning VertexBuffer
```

不同。

应该是：

```text
Character A
Mesh + SkinOutputA
    ↓
BLAS A

Character B
Mesh + SkinOutputB
    ↓
BLAS B
```

不能共享 BLAS。

---

# 22. 修改 Geometry Cache Key

建议：

```cpp
struct GeometryCacheKey
{
    Mesh *_mesh = nullptr;
    VertexBuffer *_vertex_buffer = nullptr;

    bool operator==(const GeometryCacheKey &rhs) const
    {
        return _mesh == rhs._mesh &&
               _vertex_buffer == rhs._vertex_buffer;
    }
};
```

Hash：

```cpp
struct GeometryCacheKeyHash
{
    size_t operator()(const GeometryCacheKey &key) const
    {
        size_t hash = std::hash<Mesh *>()(key._mesh);
        hash ^= std::hash<VertexBuffer *>()(key._vertex_buffer) +
                0x9e3779b9 +
                (hash << 6) +
                (hash >> 2);
        return hash;
    }
};
```

最终：

```cpp
HashMap<GeometryCacheKey, CachedGeometry, GeometryCacheKeyHash> _geometry_cache;
```

---

# 23. Static Mesh 自动共享

Static Mesh：

```text
Mesh A
VertexBuffer A
```

多个 Entity：

```text
Mesh A + VertexBuffer A
```

cache key 一样。

因此：

```text
共享同一个 BLAS
```

这是期望行为。

---

# 24. Skinned Mesh 自动独立

Skinned Mesh：

```text
Entity A
Mesh A + SkinVB A

Entity B
Mesh A + SkinVB B
```

cache key 不同。

因此：

```text
BLAS A
BLAS B
```

无需额外特殊分支。

---

# 25. Dynamic Geometry Dirty 状态

为动态 BLAS 增加简单 dirty 状态。

例如：

```cpp
struct CachedGeometry
{
    Ref<RayTracingGeometry> _geometry;

    Mesh *_mesh = nullptr;
    VertexBuffer *_vertex_buffer = nullptr;

    bool _dynamic = false;
    bool _dirty = false;
};
```

ComputeSkinning 完成后：

```text
SkinCacheEntry
    ↓
MarkBLASDirty
```

---

# 26. 推荐 Dirty Version

比 bool 更稳妥的方案是：

```cpp
u64 _skin_version = 0;
u64 _last_blas_version = 0;
```

每次产生新的蒙皮结果：

```cpp
++_skin_version;
```

BLAS Update 完成：

```cpp
_last_blas_version = _skin_version;
```

判断：

```cpp
if (_skin_version != _last_blas_version)
{
    UpdateBLAS();
}
```

优点：

- 不容易漏 clear
- 更容易 Debug
- 后续多 consumer 也容易扩展

第一版如果为了改动小，也可以先使用 bool。

---

# 27. Full Build 的触发条件

以下情况必须重新 Build：

1. 第一次创建 BLAS。
2. Mesh 改变。
3. VertexBuffer GPU Address 改变。
4. IndexBuffer 改变。
5. Vertex Count 改变。
6. Index Count 改变。
7. Topology 改变。
8. Geometry Desc 改变。
9. 原 BLAS 没有使用 `ALLOW_UPDATE` 创建。

---

# 28. 普通骨骼动画

普通骨骼姿态变化：

```text
BLAS Refit / Update
```

不需要：

```text
Full Build
```

---

# 29. TransformComponent 变化

如果只有：

```text
Position
Rotation
Scale
```

发生变化：

```text
BLAS 不变
TLAS Update
```

因为 BLAS 存储：

```text
Object Local Geometry
```

TLAS Instance Transform 存储：

```text
World Transform
```

---

# 30. 骨骼 + Transform 同时变化

执行：

```text
BLAS Update
+
TLAS Update
```

---

# 31. TLAS 与动态 BLAS 的关系

只要 BLAS 的 GPU 地址没有改变：

```text
BLAS Update
```

后 TLAS 不需要因为：

```text
BLAS 内部节点内容变化
```

而重新创建引用关系。

TLAS instance 仍然指向同一个：

```text
AccelerationStructure GPU VA
```

如果角色 Transform 没变化：

```text
不需要因为 BLAS refit 单独更新 instance transform
```

---

# 32. CPU Skinning 当前存在的问题

当前 CPU Skinning 如果直接修改：

```text
SkeletonMesh 自身 VertexBuffer
```

会有两个问题。

---

## 32.1 多实例冲突

例如：

```text
Entity A
Entity B
```

使用同一个：

```text
SkeletonMesh
```

但 pose 不同。

如果都修改：

```text
Mesh VertexBuffer
```

则会出现：

```text
A 写
B 覆盖 A
```

无法正确支持多个角色实例。

---

## 32.2 GPU Lifetime / Hazard

CPU 直接写 persistent mapped resource 时：

```text
CPU 正在更新
GPU 可能仍然读取上一帧
```

加入 BLAS Update 后，这种 hazard 更明显。

因此不建议继续把：

```text
Mesh 自己的 VertexBuffer
```

作为 CPU Skinning 最终输出。

---

# 33. 推荐统一 SkinCacheEntry

让 CPU / GPU Skinning 都输出到：

```cpp
SkinCacheEntry
```

例如：

```text
SkinCacheEntry
 ├─ position_buffer
 ├─ normal_buffer
 ├─ tangent_buffer
 ├─ vertex_buffer
 ├─ skin_version
 └─ blas state
```

后续：

```text
Raster
RayTracing
MotionVector
Shadow
```

全部读取 SkinCacheEntry。

---

# 34. Compute Skinning 路径

保持：

```text
Pose
    ↓
Bone Palette
    ↓
ComputeSkinning
    ↓
SkinCacheEntry GPU Buffers
    ↓
BLAS Update
```

这是主路径。

---

# 35. CPU Skinning 路径

改为：

```text
Pose
    ↓
CPU Skinning
    ↓
CPU Temporary Memory
    ↓
Upload / Copy
    ↓
SkinCacheEntry GPU Buffers
    ↓
BLAS Update
```

不要再直接修改共享 Mesh VertexBuffer。

---

# 36. CPU Upload 第一版实现

第一版无需做复杂优化。

可以：

```text
CPU Skinning
    ↓
Frame Upload Buffer
    ↓
CopyBufferRegion
    ↓
SkinCacheEntry Default Heap Buffer
```

这样：

- 生命周期清晰
- GPU hazard 更容易控制
- 与 Compute Skinning 输出格式一致

后续 profiler 证明有必要时再优化。

---

# 37. SkinCacheEntry 推荐职责

建议：

```cpp
struct SkinCacheEntry
{
    Ref<VertexBuffer> _vertex_buffer;

    Ref<ComputeBuffer> _position_buffer;
    Ref<ComputeBuffer> _normal_buffer;
    Ref<ComputeBuffer> _tangent_buffer;

    u64 _skin_version = 0;
    u64 _last_blas_version = 0;

    bool _valid = false;
};
```

不要把：

```text
Animator
Skeleton evaluation
RayTracingScene
```

塞进 SkinCacheEntry。

它只是：

```text
Skinned Geometry Cache
```

---

# 38. RayTracingGeometry 不建议放进 SkinCacheEntry

如果当前结构更适合：

```text
SceneRayTracingProxy
```

持有 BLAS Cache，则继续由：

```text
SceneRayTracingProxy
```

管理。

推荐关系：

```text
SkinCacheEntry
    ↓
提供 vertex buffer

SceneRayTracingProxy
    ↓
(mesh, vertex_buffer)
    ↓
RayTracingGeometry
```

这样：

```text
Skinning
```

和：

```text
Ray Tracing
```

仍然解耦。

---

# 39. SceneRayTracingProxy::AcquireGeometry

推荐接口：

```cpp
RayTracingGeometry *SceneRayTracingProxy::AcquireGeometry(
    Mesh *mesh,
    VertexBuffer *vertex_buffer,
    bool dynamic);
```

Static：

```cpp
AcquireGeometry(
    mesh,
    mesh->GetVertexBuffer(),
    false);
```

Skinned：

```cpp
AcquireGeometry(
    mesh,
    skin_cache_entry->_vertex_buffer.get(),
    true);
```

---

# 40. SceneRayTracingProxy Sync 拆分

当前如果：

```cpp
SceneRayTracingProxy::Sync()
```

同时：

- 创建 BLAS
- 更新 BLAS
- 创建 instance
- 更新 packed geometry
- 构建 TLAS

建议至少逻辑拆分成：

```text
SyncInstances
SyncGeometry
UpdateDynamicGeometry
BuildOrUpdateTLAS
```

但不要求大规模抽象。

目标是让：

```text
Dynamic BLAS Update
```

可以被单独插入 Render Graph。

---

# 41. 推荐帧级流程

最终：

```text
AnimationSystem::Update
        ↓
Evaluate Skeleton Pose
        ↓
PrepareVisibleSkinning
        ↓
────────────────────────────
ComputeSkinning / CpuSkinUpload
────────────────────────────
        ↓
SkinCacheEntry Version++
        ↓
────────────────────────────
SkinnedBLASUpdate
────────────────────────────
        ↓
────────────────────────────
TLAS Update
────────────────────────────
        ↓
────────────────────────────
RayTracingGI / Reflection
────────────────────────────
        ↓
Raster
```

---

# 42. TLAS Update 顺序

如果动态 BLAS 更新和 TLAS Update 同一帧发生：

推荐：

```text
BLAS Update
    ↓
TLAS Update
    ↓
Ray Tracing Dispatch
```

保证所有 RT Pass 读取到：

```text
本帧最新 BLAS
+
本帧最新 Instance Transform
```

---

# 43. Dynamic BLAS Refit 策略

第一版统一：

```text
每次 skinning 输出发生变化
    ↓
BLAS Update
```

不要立即实现：

- 每 N 帧 Full Build
- AABB 形变量阈值
- Bone displacement threshold
- SAH degradation 估计
- GPU traversal cost heuristic

这些暂时都属于过度设计。

---

# 44. 后续可能增加的 Rebuild Heuristic

只有 profiler 确认：

```text
长期 Refit 导致 traversal 明显退化
```

再考虑。

例如：

```text
每 120 帧 Full Build
```

或者：

```text
AABB 相比初始扩大超过阈值
```

但这不是本任务范围。

---

# 45. BLAS Resource Address 稳定性

Dynamic BLAS Update 应尽量：

```text
in-place update
```

即：

```text
SourceAccelerationStructureData
==
DestAccelerationStructureData
```

这样：

```text
TLAS BLAS Address
```

保持稳定。

减少：

- TLAS instance rebuild
- descriptor refresh
- cache invalidation

---

# 46. BLAS 生命周期

动态角色：

```text
Entity Visible
    ↓
Acquire Dynamic BLAS
```

角色继续存在：

```text
Reuse BLAS
```

角色销毁：

```text
Release Cached Geometry
```

避免：

```text
每帧创建 RayTracingGeometry
```

---

# 47. Visible / Invisible 策略

第一版建议：

如果角色这一帧不进入 RT Scene：

```text
不更新 BLAS
```

下一次重新进入：

```text
检查 skin version
```

如果：

```text
skin_version != last_blas_version
```

则：

```text
先 Update BLAS
```

再进入 TLAS。

---

# 48. 不要只按 Animation Playing 判断

不要写：

```cpp
if (animator->IsPlaying())
{
    UpdateBLAS();
}
```

因为：

- IK 可能改变 pose
- Runtime bone override
- Physics bone
- Animation seek
- Pose reset
- Editor preview
- Script 修改骨骼

应该依赖：

```text
Skinning Output Version
```

---

# 49. Motion Vector

如果当前 GPU Skinning 已经有：

```text
previous position
```

机制，不需要因为 BLAS 再复制一套。

BLAS 只关心：

```text
current position
```

Motion Vector 保持原系统职责。

---

# 50. Index Buffer

骨骼动画不会改变：

```text
Index Buffer
```

因此动态 BLAS：

```text
IndexBuffer
```

继续复用 Mesh 自身 IB。

只有：

```text
Vertex Buffer
```

来自 SkinCacheEntry。

---

# 51. Geometry Desc

Dynamic Skinned BLAS：

```text
VertexBuffer:
    SkinCacheEntry Position

IndexBuffer:
    Mesh IndexBuffer

VertexCount:
    Mesh VertexCount

IndexCount:
    Mesh IndexCount
```

---

# 52. Position Format

确认 BLAS position stream 格式满足：

```text
float3
```

或者 DXR 当前支持的对应格式。

如果 Compute Skinning 的 position buffer：

```text
StructuredBuffer<float3>
```

并且 stride 与 DXR Geometry Desc 一致，则可以直接复用。

不要为了 BLAS 再复制一份 position。

---

# 53. 不要为 BLAS 创建专用 Skinned Position Buffer

避免：

```text
Compute Skinning
    ↓
Skin Position Buffer
    ↓
Copy
    ↓
BLAS Position Buffer
```

目标：

```text
Compute Skinning Position Buffer
    ↓
直接作为 BLAS Input
```

减少：

- 显存
- Copy
- Barrier
- Lifetime 管理

---

# 54. Normal / Tangent 不参与 BLAS

BLAS Build 只需要：

```text
position
index
```

normal / tangent 只给：

```text
Hit Shader
Raster
```

因此：

```text
normal/tangent
```

不应该作为 BLAS Update dependency。

但 Ray Tracing Pass 本身需要等待：

```text
Compute Skinning Normal/Tangent
```

完成。

---

# 55. RDG 依赖细分

推荐依赖关系：

```text
ComputeSkinning
    ├─ position : kWriteUAV
    ├─ normal   : kWriteUAV
    └─ tangent  : kWriteUAV

position
    ↓
SkinnedBLASUpdate
    ├─ position : kReadSRV
    ├─ index    : kReadSRV
    └─ BLAS     : kAccelStructWrite

BLAS
    ↓
TLASUpdate
    ├─ BLAS     : kAccelStructRead
    └─ TLAS     : kAccelStructWrite

position/normal/tangent + TLAS
    ↓
RayTracingGI
    ├─ position : kReadSRV
    ├─ normal   : kReadSRV
    ├─ tangent  : kReadSRV
    └─ TLAS     : kAccelStructRead
```

注意：

```text
BLAS Build Input
```

不需要新的：

```cpp
kRayTracingBuildInput
```

直接使用：

```cpp
kReadSRV
```

即可。

`kAccelStructRead / kAccelStructWrite` 只用于：

```text
BLAS / TLAS 本体
```

而不是用于普通 vertex/index buffer。

因此：

```text
BLASUpdate
```

不需要等待：

```text
normal
tangent
```

只需要等待：

```text
position
```

而 Ray Tracing shading pass 如果读取动态：

```text
normal
tangent
```

则它本身会对 ComputeSkinning 建立对应依赖。

---

# 56. Packed Geometry Buffer 建议

如果当前：

```text
SceneRayTracingProxy
```

会把所有 vertex attribute 复制进一套：

```text
Packed Vertex Buffer
```

建议针对 Skinned Geometry 优先考虑：

```text
直接记录 bindless stream index
```

而不是每帧把动态 position/normal/tangent 再复制进 packed buffer。

Static Mesh 可以继续使用 packed buffer。

Dynamic Mesh 第一版也可以保持现有结构，但必须保证：

```text
packed dynamic data
```

每帧同步。

若同步成本较高，后续再切到：

```text
bindless stream direct access
```

---

# 57. 推荐 PrimitiveData

Dynamic RT primitive 可以保存：

```text
position_srv
normal_srv
tangent_srv
uv_srv
index_srv
```

Hit Shader：

```text
PrimitiveData
    ↓
bindless access
```

这样 CPU 不需要每帧重建 packed vertex payload。

---

# 58. 第一阶段改动范围

必须完成：

1. BLAS Update flag 打通。
2. Dynamic BLAS `ALLOW_UPDATE`。
3. Update Scratch Size。
4. DX12 `PERFORM_UPDATE`。
5. Geometry Cache Key 改为 `(Mesh*, VertexBuffer*)`。
6. Skinning Output 开启 SRV。
7. ComputeSkinning 后执行 BLAS Update。
8. Dynamic BLAS Update 进入 RDG。
9. 删除 `kRayTracingBuildInput` 方案，BLAS vertex/index input 使用 `kReadSRV`。
10. 将 AS 本体 Usage 拆分为 `kAccelStructRead / kAccelStructWrite`。
11. 修正 `ResourceAccess::IsRead / IsWrite` 对 AS 的判定。
12. Hit Shader 使用正确 skinned vertex stream。
13. 多 Skinned Entity 不共享同一个 BLAS。

---

# 59. 第二阶段改动范围

完成 CPU / GPU Skinning 输出统一：

```text
CPU Skinning
Compute Skinning
        ↓
SkinCacheEntry
```

CPU Skinning 不再直接修改共享：

```text
SkeletonMesh VertexBuffer
```

---

# 60. 第三阶段可选优化

不属于本次必须项：

- BLAS compaction
- Dynamic BLAS periodic rebuild
- BLAS rebuild heuristic
- GPU skinning culling
- Async Compute Skinning
- Async BLAS Build
- Batched BLAS Update
- BLAS memory pool
- scratch arena 优化
- TLAS partial update
- RT geometry streaming

---

# 61. 推荐实现顺序

## Step 1

修复：

```text
RayTracingGeometry::Update
```

以及：

```text
CommandBuildAS::_is_update
```

---

## Step 2

实现 Dynamic BLAS creation flags：

```text
ALLOW_UPDATE
```

---

## Step 3

实现：

```text
UpdateScratchDataSizeInBytes
```

---

## Step 4

实现 DX12：

```text
PERFORM_UPDATE
```

---

## Step 5

创建一个简单动态测试：

```text
CPU 修改 vertex position
    ↓
BLAS Update
```

验证 BLAS refit 基础能力。

先不接骨骼。

---

## Step 6

接入 ComputeSkinning Position Buffer。

---

## Step 7

新增：

```text
SkinnedBLASUpdate RDG Pass
```

保证顺序：

```text
ComputeSkinning
    ↓
BLAS Update
```

---

## Step 8

修复：

```text
Skinning Output SRV
```

---

## Step 9

修复：

```text
SceneRayTracingProxy Geometry Cache Key
```

---

## Step 10

修复 Hit Shader 动态 vertex attribute。

---

## Step 11

多角色实例测试。

---

## Step 12

最后统一 CPU Skinning 输出。

---

# 62. Debug 日志建议

Debug Build 下增加：

```text
[RT] Build static BLAS mesh=...
[RT] Build dynamic BLAS mesh=... vb=...
[RT] Update dynamic BLAS mesh=... vb=...
```

并记录：

```text
vertex_count
index_count
blas gpu address
vertex buffer gpu address
skin version
```

注意避免 Release 输出大量日志。

---

# 63. GPU Marker

增加：

```text
ComputeSkinning
SkinnedBLASUpdate
TLASUpdate
RayTraceGI
```

方便 PIX / RenderDoc / Nsight 分析执行顺序。

---

# 64. Debug Assertion

Dynamic BLAS Update 时检查：

```cpp
AL_ASSERT(_desc._build_mode == ERayTracingGeometryBuildMode::kDynamic);
```

以及：

```cpp
AL_ASSERT(_inputs.Flags &
          D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE);
```

---

# 65. Debug 验证场景

## Case 1：Static Mesh

场景：

```text
100 个静态物体
```

预期：

```text
BLAS 首次 Build
后续不 Build / Update
```

---

## Case 2：单 Skinned Mesh

场景：

```text
1 个角色
Idle Animation
```

预期：

```text
Frame 1:
Full Build

Frame N:
BLAS Update
```

---

## Case 3：角色停止动画

预期：

如果 pose 不变：

```text
不执行 BLAS Update
```

---

## Case 4：Transform 移动

不播放动画，只移动 Entity。

预期：

```text
BLAS 不 Update
TLAS Update
```

---

## Case 5：骨骼动画 + Entity 移动

预期：

```text
BLAS Update
TLAS Update
```

---

## Case 6：两个角色共享 Mesh

```text
Character A
Character B
```

使用同一个 Mesh。

预期：

```text
BLAS A != BLAS B
```

但：

```text
IndexBuffer 可以共享
```

---

## Case 7：同一个角色的 animation pause

暂停后：

```text
skin version
```

不再变化。

预期：

```text
BLAS Update 停止
```

---

## Case 8：Compute Skinning

确认 GPU Capture 中：

```text
Dispatch Compute Skinning
    ↓
UAV Barrier / State Transition
    ↓
BuildRaytracingAccelerationStructure PERFORM_UPDATE
    ↓
DispatchRays
```

---

## Case 9：Hit Shader Normal

使用强 normal map / 光照检查：

```text
RT shading
```

跟随骨骼动作。

如果 intersection 正确但 shading 像 bind pose：

说明：

```text
Hit Shader 仍然读取 static vertex stream
```

---

# 66. 性能检查

Profiler 记录：

```text
SkinnedBLASUpdate CPU Time
SkinnedBLASUpdate GPU Time
```

至少测试：

```text
1
10
50
100
```

个 Skinned Mesh。

第一版目标不是极限优化。

重点确保：

```text
没有 Full Build every frame
```

---

# 67. 禁止实现

本任务禁止：

```text
每帧销毁并重新创建 BLAS
```

禁止：

```text
每帧重新创建 SkinCacheEntry Buffer
```

禁止：

```text
为 BLAS 单独复制一份 skinned position
```

禁止：

```text
根据 Animator::IsPlaying 判断 BLAS dirty
```

禁止：

```text
按 Mesh* 单独缓存动态 BLAS
```

禁止：

```text
RayTracingGeometry::Update 内部立即创建并 Submit CommandBuffer
```

作为最终实现。

禁止为了支持动态 BLAS 引入复杂：

```text
Geometry Manager
BLAS Scheduler
BLAS Graph
BLAS Registry
```

第一版保持直接。

---

# 68. 推荐模块职责

```text
AnimationSystem
    负责 Pose

SkinningSystem
    负责 Skinned Vertex Output

SceneRayTracingProxy
    负责 Mesh + VertexBuffer → BLAS

RenderGraph
    负责 Skinning → BLAS → TLAS → RT Pass 时序
    通过 kReadSRV / kAccelStructRead / kAccelStructWrite 表达依赖

DX12 Backend
    负责 Build / Update AS
    将 kAccelStructRead / kAccelStructWrite 映射到同一个 AS Resource State
```

不要跨层混合。

---

# 69. 最终核心数据关系

```text
Mesh
 ├─ IndexBuffer
 ├─ Static VertexBuffer
 └─ Skeleton Data

Entity A
 └─ SkinCacheEntry A
      ├─ PositionBuffer A
      ├─ NormalBuffer A
      └─ TangentBuffer A

Entity B
 └─ SkinCacheEntry B
      ├─ PositionBuffer B
      ├─ NormalBuffer B
      └─ TangentBuffer B
```

Ray Tracing：

```text
Mesh + StaticVB
    ↓
Static BLAS

Mesh + SkinVB A
    ↓
Dynamic BLAS A

Mesh + SkinVB B
    ↓
Dynamic BLAS B
```

---

# 70. 最终帧流程

```text
Animation Evaluation
        ↓
Skinning Prepare
        ↓
┌─────────────────────────────┐
│ Compute Skinning / CPU Skin │
└─────────────────────────────┘
        ↓
Skin Output Version++
        ↓
┌─────────────────────────────┐
│ Dynamic BLAS Update         │
└─────────────────────────────┘
        ↓
┌─────────────────────────────┐
│ TLAS Update                 │
└─────────────────────────────┘
        ↓
┌─────────────────────────────┐
│ Hardware Ray Tracing        │
│ GI / Shadow / Reflection    │
└─────────────────────────────┘
        ↓
Raster / PostProcess
```

---

# 71. 最终核心原则

整个系统保持一个核心约束：

> BLAS 不关心顶点是 CPU Skinning 还是 Compute Skinning 产生的；它只关心当前用于 Ray Tracing 的最终 GPU Position Buffer。

并保持：

```text
Static Geometry
    Build Once

Dynamic Skinned Geometry
    Build Once
    Update When Skin Output Changes

Object Transform
    TLAS Only
```

这样后续：

- DDGI
- RT Shadow
- RT Reflection
- RT AO
- Path Tracing Preview

都可以直接复用统一的动态几何系统，而不再为每个 Ray Tracing Feature 单独处理骨骼动画。


---

# 72. EResourceUsage 最终建议

本任务完成后，Ray Tracing 相关 Usage 推荐保持：

```cpp
enum class EResourceUsage : u32
{
    kNone = 0,

    kReadSRV = 1 << 0,
    kWriteUAV = 1 << 1,
    kWriteRTV = 1 << 2,
    kDSV = 1 << 3,

    kCopySrc = 1 << 4,
    kCopyDst = 1 << 5,

    kIndirectArgument = 1 << 6,
    kVertexBuffer = 1 << 7,

    kAccelStructRead = 1 << 8,
    kAccelStructWrite = 1 << 9,
};
```

如果当前工程已有更多 Usage，则只需要：

```text
保留已有枚举
+
将原 kRaytracingAccel 拆为 Read / Write
```

不要求为了匹配上面的 bit 顺序重排全部枚举。

核心约束：

```text
Vertex / Index / Skinning Output
    → 普通资源访问语义
    → kReadSRV

BLAS / TLAS 本体
    → Acceleration Structure 访问语义
    → kAccelStructRead / kAccelStructWrite
```

不要加入：

```cpp
kRayTracingBuildInput
```

因为它把：

```text
算法用途
```

混入了：

```text
资源访问模型
```

会让后续 RDG Usage 越来越碎。

---

# 73. AS Usage 验收项

必须确认：

## BLAS Build

```text
position : Read
index    : Read
BLAS     : Write
```

对应：

```text
kReadSRV
kReadSRV
kAccelStructWrite
```

---

## BLAS Update

```text
position : Read
index    : Read
BLAS     : Read + Write 语义
```

如果当前 RDG 一个资源一次只能声明一个 Usage：

```text
声明 kAccelStructWrite
```

并由 BLAS Update Pass 自身保证：

```text
SourceAccelerationStructureData
==
DestAccelerationStructureData
```

同时在 dependency 层把该访问视为：

```text
写依赖
```

即可。

如果未来 RDG 支持：

```text
ReadWrite
```

再考虑显式表达。

---

## TLAS Build / Update

```text
BLAS : kAccelStructRead
TLAS : kAccelStructWrite
```

---

## DispatchRays / RayQuery

```text
TLAS : kAccelStructRead
```

如果 shader 直接访问其他 BLAS 数据结构，则按实际 binding 声明。

---

# 74. 最终 RDG 核心原则

资源 Usage 只表达：

> 这个 Pass 如何访问资源。

不要表达：

> 这个 Pass 为什么访问资源。

因此：

```text
Compute Skinning Output
BLAS Build Input
Ray Tracing Vertex Attribute
```

如果底层都是只读 SRV / read-only resource：

```text
统一使用 kReadSRV
```

而：

```text
BLAS / TLAS
```

由于其底层状态特殊，并且 RDG 需要区分构建写入与 shader 读取，所以单独使用：

```text
kAccelStructRead
kAccelStructWrite
```

这是本次动态 BLAS 重构的最终 Usage 设计。
