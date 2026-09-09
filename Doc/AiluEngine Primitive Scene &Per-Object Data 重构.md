# AiluEngine Primitive Scene / Per-Object Data 重构

## 1. 目标

彻底移除场景 Mesh 渲染对“每 Primitive 一个 `ConstantBuffer`”的依赖，将当前：

```text
CBufferPerObjectData
+
ObjectInstanceData
+
obj_index / scene_id
```

统一为：

```text
PrimitiveData Buffer
+
primitive_index
+
PrimitiveDrawData Root Constants
+
InstancePrimitiveIndices
```

目标架构：

```text
Scene
 │
 ├── PrimitiveData[]
 │      ├─ transform
 │      ├─ previous transform
 │      ├─ entity id
 │      ├─ material id
 │      ├─ submesh
 │      ├─ motion flags
 │      ├─ bindless geometry
 │      └─ RT/BVH data
 │
 ├── MaterialData[]
 │
 └── FramePrimitiveIndexBuffer[]
        └─ arbitrary primitive indices for instanced batches

Draw
 │
 ├─ primitive_base
 ├─ instance_index_offset
 ├─ flags
 └─ instance_count
```

场景 Mesh 的一次 Draw 不再绑定 `CBufferPerObjectData`。

普通单对象 Draw：

```text
primitive_index -> PrimitiveData
```

连续 Instance Draw：

```text
primitive_base + SV_InstanceID
```

任意对象合批：

```text
SV_InstanceID
      ↓
InstancePrimitiveIndices[offset + instance_id]
      ↓
primitive_index
      ↓
PrimitiveData
```

本次重构完成后，应具备继续实现：

- CPU automatic instancing
- GPU culling
- ExecuteIndirect
- GPU-driven rendering

的基础结构，不需要再次修改 Primitive 数据模型。

---

# 2. 当前需要解决的问题

当前代码存在两套重复的 per-object 数据：

```cpp
CBufferPerObjectData
```

以及：

```cpp
ObjectInstanceData
```

`Renderer::PrepareScene()` 中，同一个 submesh 同时填写：

```text
world
world_to_local
object id
material
submesh
...
```

并且当前 `obj_index` 实际语义已经不是“Object”，而是：

```text
一个 Entity 的一个 Submesh Render Instance
```

因此其真实语义应统一为：

```text
primitive_index
```

另外当前存在：

```cpp
FrameResource::_obj_cbs
GetObjCB()
RenderingData::_p_per_object_cbuf
RenderableObjectData::_scene_id
CommandDraw::_per_obj_cb
```

整条链路都应移除。

当前 `SceneInstanceBuffer` 仅在 static mesh 数据写完后上传，而 skeleton mesh 又在之后继续创建 object CB，场景 Primitive 数据本身也没有真正统一。

---

# 3. 数据模型

## 3.1 Primitive 定义

统一定义：

> 一个可独立参与 Cull / Material / Draw / Picking / MotionVector / RayTracing 的 Submesh Render Instance
> 就是一个 Primitive。

例如：

```text
Entity A
 └─ Mesh
     ├─ Submesh 0 -> Primitive 100
     ├─ Submesh 1 -> Primitive 101
     └─ Submesh 2 -> Primitive 102
```

不要把 Entity ID、Primitive ID、Instance ID 混为一个概念。

---

## 3.2 GPU PrimitiveData

删除：

```cpp
ObjectInstanceData
```

删除场景用途的：

```cpp
CBufferPerObjectData
```

统一为 ShaderInterop 中的：

```cpp
struct PrimitiveData
{
    float4x4 _local_to_world;
    float4x4 _world_to_local;
    float4x4 _prev_local_to_world;

    float _max_inv_scale;
    uint _entity_id;
    uint _material_id;
    uint _submesh_id;

    uint _flags;
    uint _global_triangle_offset;
    uint _blas_node_start;
    uint _blas_node_count;

    uint _position_bindless_idx;
    uint _normal_bindless_idx;
    uint _uv_bindless_idx;
    uint _tangent_bindless_idx;

    uint _index_bindless_idx;
    uint _submesh_triangle_offset;
    uint _submesh_triangle_count;
    uint _reserved0;
};
```

根据实际 HLSL packing 调整 padding，但 CPU / HLSL layout 必须完全一致。

不再要求 256 byte 对齐。

这是 `StructuredBuffer` element，不是 Constant Buffer。

---

## 3.3 Primitive Flags

将当前：

```cpp
_MotionVectorParam.x
_MotionVectorParam.y
```

改为明确 bit flags。

例如：

```cpp
enum EPrimitiveFlags : u32
{
    kPrimitiveNone = 0u,
    kPrimitivePerObjectMotion = 1u << 0u,
    kPrimitiveForceZeroMotion = 1u << 1u,
    kPrimitiveSkinned = 1u << 2u
};
```

Shader 和 CPU 均使用 flags。

不要继续使用 `float4` 表达 bool 状态。

---

# 4. CPU Primitive Record

GPU 数据和 CPU 渲染元数据不要混在一个结构。

增加：

```cpp
struct ScenePrimitive
{
    u32 _primitive_index = 0u;
    u32 _entity_id = 0u;

    Mesh *_mesh = nullptr;
    Material *_material = nullptr;

    VertexBuffer *_vertex_buffer = nullptr;
    IndexBuffer *_index_buffer = nullptr;

    AABB _world_bounds{};

    u16 _submesh_index = 0u;
    u16 _flags = 0u;
};
```

Renderer 每帧维护：

```cpp
Vector<ScenePrimitive> _scene_primitives;
Vector<PrimitiveData> _primitive_data;
```

二者：

```text
_scene_primitives[i]
_primitive_data[i]
```

必须对应同一个：

```text
primitive_index = i
```

这样 CPU Pass 不需要从 GPU `PrimitiveData` 反查数据。

例如 MotionVector Pass 判断：

```cpp
primitive._flags & kPrimitivePerObjectMotion
```

而不是：

```cpp
ConstantBuffer::As<CBufferPerObjectData>(...)->_MotionVectorParam.x
```

---

# 5. Primitive 构建

增加统一入口，例如：

```cpp
void Renderer::BuildScenePrimitives(const Scene &scene);
```

不要继续让：

```text
PrepareScene()
Cull()
EndScene()
```

分别使用独立循环推算 `obj_index`。

`BuildScenePrimitives()` 一次性遍历：

```text
StaticMeshComponent
CSkeletonMesh
```

并按 submesh 创建 Primitive。

负责填充：

```text
primitive_index
entity_id
mesh
material
submesh
world bounds
resolved vertex buffer
PrimitiveData
```

之后 Cull 直接遍历：

```cpp
_scene_primitives
```

不要再维护：

```cpp
scene_render_obj_index
```

这种依赖遍历顺序保持完全一致的隐式映射。

---

# 6. Skinning

当前：

```cpp
ResolveSkinningVertexBuffer(mesh, per_obj_cb)
```

依赖：

```cpp
CBufferPerObjectData::_ObjectID
```

需要删除。

构建 `ScenePrimitive` 时直接根据：

```text
entity_id
mesh
```

resolve skinning output：

```cpp
primitive._vertex_buffer =
    ECS::SkinningSystem::ResolveVertexBuffer(mesh, entity_id);
```

找不到时 fallback：

```cpp
mesh->GetVertexBuffer()
```

因此 `CommandBuffer` 不应该为了确定 VB 再去解析 per-object CB。

特别注意：

> 不同 Skeleton Instance 即使使用同一个 Mesh，只要 compute skinning 输出 VB 不相同，就不能自动合并为同一个
> IA instanced draw。

所以 Instance Batch Key 必须包含：

```text
resolved VertexBuffer*
```

---

# 7. Primitive GPU Buffer

将：

```cpp
GetSceneInstanceBuffer()
```

改为类似：

```cpp
GPUBuffer *GetScenePrimitiveBuffer(u64 scene_hash);
```

element：

```cpp
sizeof(PrimitiveData)
```

Shader：

```hlsl
StructuredBuffer<PrimitiveData> g_primitive_data;
```

`BuildScenePrimitives()` 完成之后统一：

```cpp
primitive_buffer->SetData(...);
```

一帧上传一次。

不要：

```text
Depth Pass upload
Shadow Pass upload
GBuffer upload
MotionVector upload
```

同一 Primitive 的 transform 数据。

---

# 8. Per Draw Root Constants

增加：

```cpp
struct PrimitiveDrawData
{
    u32 _primitive_base = 0u;
    u32 _instance_index_offset = 0u;
    u32 _flags = 0u;
    u32 _reserved0 = 0u;
};
```

对应 HLSL：

```hlsl
cbuffer CBufferPrimitiveDrawData : register(b0)
{
    uint g_primitive_base;
    uint g_instance_index_offset;
    uint g_primitive_draw_flags;
    uint g_primitive_draw_reserved;
};
```

大小：

```text
4 DWORD / 16 bytes
```

不要为这个结构分配 CB。

DX12 Root Signature 中将：

```text
CBufferPrimitiveDrawData / b0
```

特殊处理为：

```cpp
InitAsConstants(4u, 0u);
```

并保证存在该 cbuffer 的 scene shader：

```text
PrimitiveDrawData 永远位于固定 root parameter
```

建议固定为：

```text
root parameter 0
```

方便后续 ExecuteIndirect command signature 使用。

---

# 9. Shader Primitive Helper

建立统一 shader include，例如：

```text
Primitive.hlsli
```

提供：

```hlsl
static const uint kPrimitiveDrawUseInstanceIndex = 1u << 0u;

uint GetPrimitiveIndex(uint instance_id)
{
    if ((g_primitive_draw_flags & kPrimitiveDrawUseInstanceIndex) != 0u)
        return g_instance_primitive_indices[g_instance_index_offset + instance_id];

    return g_primitive_base + instance_id;
}
```

以及：

```hlsl
PrimitiveData LoadPrimitive(uint instance_id)
{
    return g_primitive_data[GetPrimitiveIndex(instance_id)];
}
```

所有 Scene Mesh VS 使用：

```hlsl
uint instance_id : SV_InstanceID
```

然后：

```hlsl
const PrimitiveData primitive = LoadPrimitive(instance_id);
```

使用：

```text
primitive._local_to_world
primitive._world_to_local
primitive._prev_local_to_world
primitive._entity_id
primitive._flags
```

替换旧：

```text
_MatrixWorld
_MatrixInvWorld
_MatrixWorld_Pre
_ObjectID
_SubmeshID
_MotionVectorParam
```

PS 如果只需要 entity ID / primitive ID，优先由 VS 使用 `nointerpolation` 输出传递，不要无意义地重复读取整个
`PrimitiveData`。

---

# 10. InstancePrimitiveIndices

增加：

```hlsl
StructuredBuffer<uint> g_instance_primitive_indices;
```

用于任意 Primitive 合批。

例如：

```text
Primitive 10
Primitive 38
Primitive 91
Primitive 105
```

instance buffer：

```text
[offset + 0] = 10
[offset + 1] = 38
[offset + 2] = 91
[offset + 3] = 105
```

Draw：

```text
instance_count = 4
instance_index_offset = offset
flags |= kPrimitiveDrawUseInstanceIndex
```

Shader：

```text
SV_InstanceID 0 -> primitive 10
SV_InstanceID 1 -> primitive 38
SV_InstanceID 2 -> primitive 91
SV_InstanceID 3 -> primitive 105
```

---

# 11. FramePrimitiveIndexBuffer

不要每一个 Instance Draw 创建一个 GPUBuffer。

实现一个 frame-slot scoped 的：

```cpp
class FramePrimitiveIndexBuffer
```

推荐：

```text
Frame Slot 0 -> persistently mapped upload buffer
Frame Slot 1 -> persistently mapped upload buffer
Frame Slot 2 -> ...
```

资源整体作为：

```hlsl
StructuredBuffer<uint>
```

使用。

接口类似：

```cpp
struct PrimitiveIndexAllocation
{
    u32 *_cpu_ptr = nullptr;
    u32 _offset = 0u;
    u32 _count = 0u;
};

PrimitiveIndexAllocation Allocate(u32 count);
```

每帧 slot fence 完成后：

```cpp
Reset();
```

写入过程中只 append，不覆盖已有 range。

因为每个 index 仅 4 bytes，这种数据非常适合 CPU-visible frame buffer。

不要直接复用当前分页 `AllocFrameUpload()`，除非能够正确为跨 page allocation 提供稳定的 SRV。

这里更适合一个单独的连续 StructuredBuffer。

---

# 12. Draw Fast Path

### 单 Primitive

```text
primitive_base = primitive_index
instance_count = 1
flags = 0
```

shader：

```hlsl
primitive_index = primitive_base;
```

---

### 连续 Primitive Instance Draw

例如：

```text
primitive 100
primitive 101
primitive 102
primitive 103
```

提交：

```text
primitive_base = 100
instance_count = 4
flags = 0
```

Shader：

```hlsl
primitive_index = primitive_base + SV_InstanceID;
```

不访问 index indirection buffer。

---

### 非连续 Instance Draw

例如：

```text
10
38
91
105
```

写入：

```text
g_instance_primitive_indices
```

然后：

```text
instance_index_offset = xxx
instance_count = 4
flags = kPrimitiveDrawUseInstanceIndex
```

---

# 13. CommandBuffer API

Scene draw 不再接收：

```cpp
ConstantBuffer *per_obj_cb
```

增加明确 API，例如：

```cpp
void DrawSceneMesh(Mesh *mesh, Material *material, u16 submesh_index, u16 pass_index,
                   const PrimitiveDrawData &draw_data, u32 instance_count = 1u);
```

或者使用：

```cpp
struct PrimitiveDrawRange
{
    PrimitiveDrawData _draw_data{};
    u32 _instance_count = 1u;
};
```

Scene rendering path 全部迁移到此接口。

原有 generic：

```cpp
DrawIndexed()
DrawIndexedInstanced()
DrawProcedural()
```

仍允许 UI / Sprite / Debug Geometry 使用。

不要为了统一接口而让 UI 也创建假的 Primitive。

---

# 14. CommandDraw

删除场景语义：

```cpp
ConstantBuffer *_per_obj_cb;
```

增加：

```cpp
PrimitiveDrawData _primitive_draw_data{};
bool _is_scene_primitive_draw = false;
```

Scene Draw Command 在 DX12 执行时：

1. bind PSO/root signature；
2. bind material/global resources；
3. 设置 Primitive root constants；
4. DrawIndexedInstanced。

例如：

```cpp
dxcmd->SetGraphicsRoot32BitConstants(root_slot, 4u, &draw_cmd->_primitive_draw_data, 0u);
```

非 Scene Draw 不执行此操作。

---

# 15. Root Signature

修改：

```text
D3DShader::GenerateInternalPSO()
```

当前对：

```text
CBufferPerObjectData
```

有专门 Root CBV 逻辑。

新的逻辑：

```text
CBufferPrimitiveDrawData
    -> root constants

CBufferPerCameraData
    -> root CBV

CBufferPerSceneData
    -> root CBV

CBufferPerMaterialData
    -> 保持现有方案
```

不要把 `PrimitiveData` 本身作为 root parameter。

它仍然是：

```text
SRV / StructuredBuffer
```

并通过全局资源系统绑定。

---

# 16. RenderableObjectData

当前：

```cpp
u16 _scene_id;
```

改为：

```cpp
u32 _primitive_index;
```

不要再用 `u16` 限制 Primitive 数量。

移除：

```cpp
const Matrix4x4f *_world_matrix;
```

如果没有其他用途。

`_instance_count` 对普通 Cull Result 应始终是一个 Primitive，建议移除。

Instance Count 应由 **Batch Builder** 产生，而不是存在单个 RenderableObject 上。

---

# 17. CPU Automatic Instancing

当前 `EmitQueuedDraws()` 只是：

```text
sort
+
逐条 Draw
```

本次重构直接实现真正的 Batch Merge。

兼容 batch key 至少包含：

```text
render queue
shader
shader variant
pass
material
mesh
submesh
resolved vertex buffer
index buffer
raster state
stencil ref
alpha-test state
```

排序以后，对连续 compatible items 分组。

假设：

```text
Tree primitive 11
Tree primitive 23
Tree primitive 31
Tree primitive 72
```

变成：

```text
1 x DrawIndexedInstanced(instance_count = 4)
```

并把：

```text
11, 23, 31, 72
```

写入 `FramePrimitiveIndexBuffer`。

---

# 18. Stencil / Motion Vector

当前 DeferredGeometryPass 使用：

```cpp
_MotionVectorParam.x
```

修改 Shader stencil ref。

这会影响 batching。

因此 Primitive CPU record 必须保留 motion flags，并在 batch key 中加入对应 stencil category。

例如：

```text
static primitive   -> stencil_ref 0
dynamic primitive  -> stencil_ref 1
```

两种 primitive 即使其他状态完全相同也不能进入同一个 Draw。

MotionVectorPass 直接通过：

```cpp
ScenePrimitive::_flags
```

过滤。

彻底删除 CPU 对 `CBufferPerObjectData` 的读取。

---

# 19. Transparent

不要为了减少 Draw Call 破坏透明排序。

Opaque / Shadow：

```text
允许完整 state sorting + instancing
```

Transparent：

```text
保持 back-to-front 顺序
```

只允许：

```text
相邻并且完全兼容
```

的 primitive 合批。

不要跨透明排序位置重新排列对象。

---

# 20. Ray Tracing

最终目标是：

```text
Raster
Compute
RayTracing
```

共享：

```hlsl
StructuredBuffer<PrimitiveData> g_primitive_data;
```

`SceneRayTracingProxy` 当前自己的：

```cpp
Vector<ObjectInstanceData>
GPUBuffer *_instance_data
```

应逐步移除。

TLAS instance：

```cpp
RayTracingInstance::_instance_id
```

改成对应：

```text
primitive_index
```

Ray shader：

```hlsl
InstanceID()
```

直接作为：

```text
g_primitive_data[InstanceID()]
```

的索引。

RT Proxy 若仍需要：

```text
packed vertex
packed normal
packed index
```

可以继续保留这些 **RT geometry buffers**。

但不要再维护第二份 transform/material/object instance metadata。

如果某些 RT-only geometry offset 当前只能在 Proxy rebuild 阶段获得，则将结果写回对应 PrimitiveData，或者拆成明确的
RT geometry table；不要重新创建 `ObjectInstanceData`。

---

# 21. MaterialData

当前已有：

```cpp
MaterialData
g_material_buffer
_material_data_lut
```

保持。

Primitive：

```cpp
_material_id
```

继续指向该 buffer。

本任务**不要顺带重构通用 Material 参数系统**。

Raster Material 当前 shader-specific constant buffer / texture binding 继续工作。

`MaterialData` 主要保持作为 Scene/RT/Compute 的标准化 material representation。

避免把任务扩大成整个 Material System 重写。

---

# 22. 非 Scene Primitive Draw

以下系统不要强制迁入 Scene Primitive：

```text
UIRenderer
TextRenderer
SpriteBatcher
Fullscreen Pass
Gizmo
临时 Procedural Geometry
```

它们可以继续使用：

```text
FrameUploadBuffer
+
小型 transient constant data
```

但建议将名字从：

```cpp
CBufferPerObjectData
```

改为语义更准确的：

```cpp
CBufferDrawTransformData
```

只包含这些临时 Draw 真正需要的数据，例如：

```cpp
struct CBufferDrawTransformData
{
    float4x4 _local_to_world;
};
```

避免 UI 为了一个 identity matrix 携带 256B 的 Scene Primitive 数据。

SpriteBatcher 已经有自己的：

```text
SpriteInstanceData
g_sprite_instances
```

继续保持独立，不接入 Scene Primitive。

---

# 23. ExecuteIndirect 预留

本任务不要求完成 GPU Culling，但接口必须支持后续直接接入。

增加 Scene Primitive 专用 indirect argument 设计：

```cpp
struct PrimitiveIndirectDrawArguments
{
    PrimitiveDrawData _primitive_draw_data;
    DrawIndexedArguments _draw_arguments;
};
```

DX12 后续对应 command signature：

```text
CONSTANT
    primitive draw data

DRAW_INDEXED
```

不要破坏现有：

```text
GpuTerrain
generic DrawMeshIndirect
```

使用的 command signature。

需要 Scene Primitive 专用 signature/API。

这样未来 GPU culling 可以直接输出：

```text
PrimitiveDrawData
+
D3D12_DRAW_INDEXED_ARGUMENTS
```

CPU 不需要重新参与。

---

# 24. 不要依赖 StartInstanceLocation 传 instance list offset

Scene Primitive shader 应显式使用：

```hlsl
g_instance_index_offset + SV_InstanceID
```

不要把架构建立在：

```text
StartInstanceLocation 是否改变 SV_InstanceID
```

这类 backend/API 细节之上。

`StartInstanceLocation` 可以继续作为普通 IA instance stream 功能使用，但不是 Primitive Scene 的核心寻址方式。

---

# 25. 需要修改的主要代码

重点检查：

```text
Inc/Render/ShaderInterop.h
Inc/Render/RenderConstants.h

Inc/Render/FrameResource.h
Src/Render/FrameResource.cpp

Inc/Render/RenderingData.h

Inc/Render/GfxCommand.h
Inc/Render/CommandBuffer.h
Src/Render/CommandBuffer.cpp

Inc/Render/Renderer.h
Src/Render/Renderer.cpp

Src/Render/Features/CommonPasses.cpp
Src/Render/Features/VoxelGI.cpp

Src/RHI/DX12/D3DShader.cpp
Src/RHI/DX12/D3DContext.cpp

Inc/Render/RayTracing/SceneRayTracingProxy.h
Src/Render/RayTracing/SceneRayTracingProxy.cpp
```

并全局搜索：

```text
CBufferPerObjectData
GetObjCB
_obj_cbs
_p_per_object_cbuf
_scene_id
ObjectInstanceData
g_instance_data
_MatrixWorld
_MatrixInvWorld
_MatrixWorld_Pre
_MotionVectorParam
_ObjectID
_SubmeshID
```

逐一处理。

---

# 26. 命名冲突

当前 `GltfParser.cpp` anonymous namespace 已存在：

```cpp
struct PrimitiveData
```

用于 glTF import 临时数据。

将它改名为：

```cpp
GltfPrimitiveBuildData
```

避免与新的：

```cpp
Render::PrimitiveData
```

产生歧义。

---

# 27. 推荐实施顺序

### Step 1

建立：

```text
PrimitiveData
ScenePrimitive
BuildScenePrimitives()
```

让 static + skinned primitive 使用统一索引。

### Step 2

Cull 改为遍历：

```text
_scene_primitives
```

`_scene_id` 全面改成 `_primitive_index`。

### Step 3

创建并全局绑定：

```text
g_primitive_data
```

Raster shader 开始从 StructuredBuffer 获取 transform。

### Step 4

实现：

```text
PrimitiveDrawData root constants
```

删除 Scene Draw 的 `_per_obj_cb`。

### Step 5

删除：

```text
FrameResource::_obj_cbs
GetObjCB()
_p_per_object_cbuf
```

### Step 6

迁移：

```text
GBuffer
Shadow
Wireframe
MotionVector
VoxelGI
Picking
```

等 Scene Mesh Pass。

### Step 7

实现：

```text
FramePrimitiveIndexBuffer
```

### Step 8

升级 `EmitQueuedDraws()`：

```text
sort
-> group
-> contiguous fast path
-> arbitrary primitive indirection path
-> DrawIndexedInstanced
```

### Step 9

接入 RayTracing：

```text
TLAS InstanceID = primitive_index
```

去除第二份 instance metadata。

### Step 10

清理旧 API、旧字段、旧 ConstantBuffer。

不要长期同时维护两套 Scene rendering path。

---

# 28. 验收标准

## 功能

必须保证：

- StaticMesh 正常渲染。
- SkeletonMesh 正常渲染。
- 多 Submesh Entity 正确。
- 每个 Submesh primitive_index 唯一。
- Material 正确。
- Entity ID / Picking 正确。
- GBuffer 正确。
- Shadow 正确。
- Motion Vector 正确。
- Wireframe 正确。
- VoxelGI 等依赖场景对象数据的 Pass 正确。
- RayTracing InstanceID 与 primitive_index 一致。
- UI/Text/Sprite 不受影响。

---

## Instance Draw

构造：

```text
100 个相同 mesh/material 的 StaticMesh Entity
```

应观察到：

```text
100 draw
```

下降为接近：

```text
1 DrawIndexedInstanced
```

前提是状态兼容且可见。

Shader 中每个 Instance：

```text
transform
entity id
material id
```

必须分别正确。

---

## 非连续 Primitive

测试：

```text
primitive = [2, 8, 17, 35]
```

同一个 instanced draw 中必须正确读取四个不同 transform。

---

## Motion Vector

动态和静态 Primitive 即使：

```text
mesh
material
shader
```

相同，也不能因为 batching 导致 stencil/motion 状态混乱。

---

## 性能

RenderDoc / Frame Debugger 中应明显看到：

```text
per draw CBufferPerObjectData bind
```

消失。

同一批 StaticMesh：

```text
IA / material / descriptor state
```

不再重复设置。

CPU 侧：

```text
PrepareScene
CommandBuffer construction
Draw submission
```

不应出现大量 `ConstantBuffer` wrapper 操作。

---

# 29. 最终应删除的架构

场景 Mesh 路径中不应再存在：

```cpp
FrameResource::_obj_cbs
FrameResource::_obj_cb_refs
FrameResource::GetObjCB()

RenderingData::_p_per_object_cbuf

CommandDraw::_per_obj_cb

RenderableObjectData::_scene_id

ObjectInstanceData
```

也不应存在这种调用：

```cpp
cmd->DrawMesh(mesh, material, per_obj_cb, ...);
```

替换为：

```text
primitive_index / PrimitiveDrawData
```

驱动的 Scene Draw。

---

# 30. 最终架构

```text
                         ┌──────────────────────┐
                         │   BuildScenePrimitives
                         └──────────┬───────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    ▼                               ▼
            ScenePrimitive[]                PrimitiveData[]
              CPU metadata                   GPU scene data
                    │                               │
                    │                               ▼
                    │                       g_primitive_data
                    │
                    ▼
                  Cull
                    │
                    ▼
             primitive_index[]
                    │
                    ▼
              Batch Builder
                    │
            ┌───────┴────────┐
            │                │
            ▼                ▼
       contiguous        arbitrary
          batch             batch
            │                │
            │                ▼
            │       FramePrimitiveIndexBuffer
            │                │
            └───────┬────────┘
                    ▼
             PrimitiveDrawData
                Root Constants
                    │
                    ▼
          DrawIndexedInstanced
                    │
                    ▼
              SV_InstanceID
                    │
                    ▼
             primitive_index
                    │
                    ▼
              PrimitiveData
```

这个结构作为新的 Scene Rendering 基线。

不要为了兼容旧 PerObject CB 架构增加长期双轨逻辑；迁移完成后直接删除旧 Scene PerObject 系统。