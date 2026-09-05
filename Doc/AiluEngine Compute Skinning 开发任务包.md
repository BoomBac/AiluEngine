# AiluEngine Compute Skinning 开发任务包

## 1. 目标

将当前 CPU Skinning 改造为 GPU Compute Skinning。

核心目标：

- CPU 继续负责动画状态更新与 Skeleton Pose 求值。
- GPU Compute Shader 负责最终 Vertex Skinning。
- 原始 SkinMesh 顶点数据通过 Bindless SRV 访问。
- Bone Palette 使用统一 GPU Atlas。
- Compute 输出 Skinned Vertex Buffer。
- 多个对象如果使用相同 `Mesh + Pose`，共享同一份 Skinning 结果。
- 只为实际可见的 SkinMesh 执行 Compute Skinning。
- 多个 Skinning Job 尽量批量执行，避免大量独立 Dispatch。
- 第一版尽量保持现有 `DrawMesh / DrawIndexed` 渲染路径不变。

不要求第一版实现：

- GPU Animation Evaluation
- Mesh Shader
- GPU Driven Rendering
- DrawIndirect
- Cloth
- Morph Target
- Ragdoll GPU Skinning

---

# 2. 当前目标数据流

整体流程调整为：

```text
AnimationSystem
    ↓
Skeleton Pose Evaluation
    ↓
PoseHandle
    ↓
BonePaletteAtlas

Culling
    ↓
Visible SkinMeshRenderer
    ↓
SkinningSystem

SkinningSystem
    ↓
SkinCache 查询
    ↓
去重 Mesh + Pose
    ↓
Unique Skin Jobs
    ↓
Compute Skinning
    ↓
Skinned Vertex Buffer

Render Pipeline
    ↓
DrawIndexed
```

关键原则：

> Skinning 不再以 Entity 为单位执行，而以唯一的 `(Mesh, PoseHandle)` 为单位执行。

---

# 3. 原始 Mesh 数据

原始 SkinMesh 保持不可变。

Compute Shader 每帧始终从 Bind Pose 数据读取，禁止读取上一帧已经 Skinning 后的 Vertex Buffer 作为输入。

推荐保留现有 Mesh 多 Stream 设计，例如：

```text
Mesh
├── Position Stream
├── Normal Stream
├── Tangent Stream
├── UV Stream
├── Bone Index Stream
├── Bone Weight Stream
└── Index Buffer
```

Compute Skinning 只需要：

```text
Position
Normal
Tangent
Bone Indices
Bone Weights
```

这些 Buffer 统一通过 Bindless SRV ID 访问。

Index Buffer 不参与 Skinning Compute。

---

# 4. Compute Skinning 输入

增加 Skinning Job 描述数据。

建议：

```cpp
struct SkinningJobData
{
    u32 _position_srv;
    u32 _normal_srv;
    u32 _tangent_srv;
    u32 _bone_index_srv;
    u32 _bone_weight_srv;

    u32 _output_uav;
    u32 _palette_offset;

    u32 _vertex_offset;
    u32 _vertex_count;
};
```

如果 Mesh Stream 已经可以通过 Mesh GPU Descriptor 统一查询，也可以只保存：

```cpp
struct SkinningJobData
{
    u32 _mesh_data_index;
    u32 _output_uav;
    u32 _palette_offset;
    u32 _vertex_offset;
    u32 _vertex_count;
};
```

优先复用现有 Bindless Mesh 数据结构，不重复维护一套 GPU Mesh 描述。

---

# 5. Bone Palette Atlas

不要给每个 SkinMeshRenderer 单独创建 Bone Buffer。

增加全局或每帧 Bone Palette Atlas：

```text
BonePaletteAtlas
├── Pose A matrices
├── Pose B matrices
├── Pose C matrices
└── ...
```

每个 Pose 只保存：

```cpp
struct PoseGpuData
{
    u32 _palette_offset;
    u32 _bone_count;
};
```

多个实例共享相同 Pose 时：

```text
entity_a ─┐
entity_b ─┤
entity_c ─┘
          ↓
PoseHandle
          ↓
same palette_offset
```

避免重复上传 Bone Matrix。

---

# 6. PoseHandle

AnimationSystem 应提供稳定的 Pose 标识：

```cpp
struct PoseHandle
{
    u32 _id;
};
```

不要通过每帧 Hash 整块 Bone Matrix 判断 Pose 是否相同。

PoseHandle 应由动画系统的 Pose Cache / Animation State Cache 产生。

第一版可以先满足：

- 完全共享同一个 Animator Pose 的对象得到同一个 PoseHandle。
- 同一个动画 Clip、时间、Blend 状态完全相同时允许共享。
- 后续再考虑动画时间量化。

---

# 7. Skin Cache

新增 `SkinCache`。

核心 Key：

```cpp
struct SkinCacheKey
{
    MeshHandle _mesh;
    PoseHandle _pose;
};
```

或者使用现有稳定的 Asset / Resource ID。

对应：

```cpp
struct SkinCacheEntry
{
    GPUBuffer* _buffer;

    u32 _vertex_offset;
    u32 _vertex_count;

    u64 _last_used_frame;
};
```

逻辑：

```text
SkinMeshRenderer
      ↓
(mesh, pose)
      ↓
SkinCache::Acquire()

Cache Hit
      ↓
直接使用现有 Skinned Buffer

Cache Miss
      ↓
创建 Skinning Job
```

多个 Entity：

```text
soldier_0
soldier_1
soldier_2
soldier_3
```

如果：

```text
same mesh
+
same pose
```

则：

```text
1 次 Skinning
4 次 Render Instance
```

---

# 8. Skin Cache 生命周期

SkinCache 不要求永久保存。

建议按照 Frame 生命周期维护：

```cpp
_last_used_frame
```

长期没有使用的 SkinCacheEntry 延迟释放。

第一版可以：

```text
当前帧使用
    ↓
保留

连续 N 帧未使用
    ↓
进入延迟释放
```

具体 N 值复用现有 GPU Resource 延迟销毁机制。

不要在 SkinMesh 刚离开视锥时立即销毁 Buffer，避免频繁创建/销毁 GPU Resource。

---

# 9. 可见性驱动 Skinning

非常重要：

不要为所有 Animator 每帧执行 Compute Skinning。

推荐执行顺序：

```text
Animation Update
      ↓
Culling
      ↓
Visible SkinMesh
      ↓
SkinCache Acquire
      ↓
Compute Skinning
      ↓
Render
```

Animation Pose 是否继续更新由动画系统策略决定。

但 Vertex Skinning 只针对：

```text
Visible SkinMesh
```

执行。

---

# 10. Compute 输出

Compute 不需要复制完整原始 Vertex。

第一版输出：

```cpp
struct SkinnedVertex
{
    Vector3f _position;
    Vector3f _normal;
    Vector4f _tangent;
};
```

UV、Vertex Color 等不会被 Skinning 修改的数据继续使用原始 Mesh。

如果当前 DrawIndexed / VertexLayout 不方便同时绑定：

```text
原始静态 Attribute
+
Skinned Attribute
```

那么第一版可以输出现有 Render Pipeline 能直接消费的完整 Vertex Stream。

优先保证低侵入。

后续再优化成最小 Skinned Stream。

---

# 11. Compute Shader

增加：

```text
ComputeSkinning.hlsl
```

基础结构：

```hlsl
[numthreads(64, 1, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint vertex_id = dispatch_id.x;

    if (vertex_id >= vertex_count)
        return;

    // Load bind pose vertex.

    // Load bone indices / weights.

    // Load bone matrices from BonePaletteAtlas.

    // Skin position.

    // Skin normal.

    // Skin tangent.

    // Write output vertex.
}
```

必须至少正确处理：

```text
position
normal
tangent
```

当前 CPU Skinning 如果只处理 Position，此次改造应顺手修复 Normal / Tangent Skinning。

---

# 12. Bone Matrix 计算

CPU AnimationSystem 负责产生最终 Skin Matrix：

```text
final_skin_matrix
=
current_global_pose
*
inverse_bind_pose
```

Compute Shader 不负责重新构建 Skeleton Hierarchy。

Compute 只消费最终 Bone Palette。

职责保持：

```text
CPU:
Animation Evaluation
Skeleton Hierarchy
Bone Palette

GPU:
Vertex Skinning
```

避免第一版复杂化。

---

# 13. Batch Skinning

禁止采用：

```text
1 SkinMesh
=
1 Dispatch
```

作为最终结构。

SkinningSystem 每帧收集：

```cpp
Vector<SkinningJobData> _jobs;
```

去重以后形成：

```text
unique_skin_jobs[]
```

再统一上传 Job Buffer。

推荐 Shader 使用二维逻辑：

```text
job_index
vertex_index
```

或者通过全局 Thread Index 映射到 Job。

第一版如果为了简单，可以允许：

```text
每个 Unique Mesh/Pose 一个 Dispatch
```

但接口设计必须允许后续无痛改成 Batch Dispatch。

不要让 Render Entity 直接调用 Dispatch。

---

# 14. SkinningSystem 职责

建议最终职责：

```cpp
class SkinningSystem
{
public:
    SkinHandle Acquire(MeshHandle mesh, PoseHandle pose);

    void PrepareVisibleSkinning(...);

    void Execute(...);

    void EndFrame();

private:
    SkinCache _cache;
    Vector<SkinningJobData> _jobs;
};
```

概念上：

```text
Acquire()
    ↓
查询 Cache
    ↓
返回 SkinHandle

PrepareVisibleSkinning()
    ↓
收集 Cache Miss

Execute()
    ↓
上传 Job Data
    ↓
Compute Dispatch

EndFrame()
    ↓
Cache GC
```

避免 AnimationSystem 直接知道 GPU Dispatch。

---

# 15. SkinHandle

Renderer 不应该直接依赖 SkinCacheEntry。

增加轻量 Handle：

```cpp
struct SkinHandle
{
    u32 _id;
};
```

Renderer 根据 SkinHandle 获取：

```text
Skinned Buffer
Vertex Offset
Vertex Count
```

这样以后 SkinCache：

```text
重新分配
合并 Buffer
GPU Arena
Compaction
```

不会影响 Renderer。

---

# 16. RenderGraph 集成

增加 Compute Skinning Pass：

```text
ComputeSkinningPass
```

依赖：

```text
BonePaletteAtlas        ReadSRV
OriginalVertexBuffer    ReadSRV
SkinWeightBuffer        ReadSRV

SkinnedVertexBuffer     WriteUAV
```

之后 Graphics Pass：

```text
SkinnedVertexBuffer
        ↓
Vertex Buffer / SRV
```

如果 Skinned Buffer 最终走 IA Vertex Buffer，需要让 RenderGraph 正确表达：

```text
WriteUAV
    ↓
VertexBuffer
```

如果当前 `EResourceUsage` 没有 Vertex Buffer 使用类型，增加类似：

```cpp
kVertexBuffer
kIndexBuffer
```

状态映射：

```text
kVertexBuffer
→ VertexAndConstantBuffer

kIndexBuffer
→ IndexBuffer
```

确保自动生成：

```text
UAV
↓ barrier
VERTEX_AND_CONSTANT_BUFFER
```

---

# 17. Draw 路径

第一版优先保持：

```cpp
DrawMesh(...)
```

或：

```cpp
DrawIndexed(...)
```

不要因为实现 Compute Skinning 强制整个 SkinMesh Render Pipeline 改成 `DrawProcedural`。

推荐：

```text
Bind Pose Vertex SRV
      ↓
Compute Skinning
      ↓
Skinned Vertex Buffer
      ↓
现有 DrawIndexed
```

`DrawProcedural + Vertex Pulling` 可以作为未来 GPU Driven Render 改造的一部分独立实现。

---

# 18. Index Buffer

Compute Skinning 不读取 Index Buffer。

Skinning 工作量按照：

```text
vertex_count
```

计算，而不是：

```text
index_count
```

例如：

```text
10,000 vertices
30,000 indices
```

Skinning 只处理：

```text
10,000 vertices
```

避免同一 Vertex 因多个 Triangle 重复执行 Skinning。

Index Buffer 继续由 Graphics Draw 使用。

---

# 19. Motion Vector

架构预留：

```text
current skinned vertex
previous skinned vertex
```

不要第一版强制完成，但 SkinCache / Buffer Pool 设计不要阻碍实现：

```text
CurrentSkinBuffer
PreviousSkinBuffer
```

SkinMesh Motion Vector 最终需要使用：

```text
previous skinned position
→ current skinned position
```

不能只依赖 Object World Matrix，因为骨骼动画本身也会产生运动。

---

# 20. Pose 共享优化

第一阶段只处理完全一致 Pose：

```text
Mesh A + Pose 12
Mesh A + Pose 12
Mesh A + Pose 12
```

只产生：

```text
1 Skin Cache Entry
```

以后可以增加 Animation Time Quantization。

例如远距离对象只按照：

```text
30 FPS
15 FPS
```

更新 Pose。

原本：

```text
idle 0.232
idle 0.237
idle 0.241
```

量化后可能全部映射：

```text
idle frame 7
```

因此自动共享同一 PoseHandle 和 SkinCache。

此功能不属于第一版必须项。

---

# 21. LOD 预留

SkinCache Key 实际建议保留 Mesh LOD 信息。

例如：

```cpp
SkinCacheKey
{
    mesh_lod,
    pose,
};
```

因为：

```text
LOD0
LOD1
LOD2
```

Vertex 数量不同，不能共享同一 Skinning Buffer。

如果当前不同 LOD 本身就是不同 Mesh Handle，则无需额外字段。

---

# 22. 后续扩展 Key

第一版：

```text
SkinCacheKey
=
Mesh
+
Pose
```

未来支持以下 Vertex Deformation 后：

```text
Morph Target
Cloth
Per-instance Bone Override
IK Override
Ragdoll
Vertex Animation
```

Key 应扩展为：

```text
Mesh
+
Pose
+
Deformation State
```

不要在第一版提前实现这些系统，只保证结构可扩展。

---

# 23. GPU Buffer 分配

不要长期采用：

```text
一个 SkinCacheEntry
=
一个独立 GPUBuffer
```

作为最终方案。

第一版为了快速跑通可以这么实现。

之后推荐改为：

```text
SkinnedVertexArena
```

统一大 Buffer：

```text
SkinnedVertexArena
├── Skin A
├── Skin B
├── Skin C
├── Skin D
└── ...
```

SkinCacheEntry 保存：

```cpp
u32 _vertex_offset;
u32 _vertex_count;
```

这样更适合：

```text
Batch Compute
Bindless
GPU Driven
统一生命周期管理
减少 Resource 数量
```

但 Arena 不属于第一阶段阻塞项。

---

# 24. 调试支持

增加基础 Debug 信息：

```text
Visible SkinMesh Count
Unique Pose Count
Unique Skin Job Count
Skin Cache Hit Count
Skin Cache Miss Count
Skinned Vertex Count
Compute Dispatch Count
Skinning GPU Time
```

典型目标：

```text
Visible SkinMesh = 1000
Unique Pose = 73
Unique Mesh + Pose = 92

Skin Jobs = 92
```

而不是：

```text
Skin Jobs = 1000
```

这样很容易判断 Cache 是否真正生效。

---

# 25. 实现阶段

## Phase 1：单对象 Compute Skinning

完成：

- Bone Palette GPU Buffer
- Bindless 原始 Vertex 数据读取
- Compute Skinning Shader
- Position / Normal / Tangent Skinning
- Skinned Vertex Buffer
- DrawIndexed

验证结果与 CPU Skinning 一致。

---

## Phase 2：多对象

支持：

```text
多个 SkinMesh
多个 Pose
多个 Skeleton
```

确保：

```text
每个 Skinning Job
→ 正确 palette_offset
→ 正确 vertex range
```

---

## Phase 3：PoseHandle

AnimationSystem 输出：

```cpp
PoseHandle
```

多个完全相同 Pose 可以共享 Bone Palette。

---

## Phase 4：SkinCache

实现：

```text
Mesh + Pose
↓
SkinCache
```

Cache Hit 不产生新 Skinning Job。

---

## Phase 5：Visible Skinning

将 Skinning Job 收集移动到 Culling 后。

只有可见 SkinMesh 产生 SkinCache 请求。

---

## Phase 6：Batch Compute

将多个 Unique Skin Job：

```text
N Dispatch
```

逐步合并为：

```text
1~少量 Dispatch
```

使用 GPU Job Buffer 描述 Skinning 输入。

---

## Phase 7：Profiler / Debug

增加统计信息和 GPU Profile Marker。

确认优化收益。

---

# 26. 验收标准

必须满足：

- SkinMesh 动画结果与当前 CPU Skinning 基本一致。
- Position 正确。
- Normal 正确。
- Tangent 正确。
- 多 SubMesh 正常。
- Shadow Pass 正常。
- Depth Pass 正常。
- 主渲染 Pass 正常。
- 多 SkinMesh 同时工作正常。
- 相同 Mesh + Pose 只执行一次 Skinning。
- 不可见 SkinMesh 不执行 Vertex Skinning。
- Compute 与 Graphics 之间不存在 Resource State 错误。
- 原始 Bind Pose Vertex Buffer 不被修改。
- 不产生逐 Entity 的永久 GPU Skin Buffer。
- Render Pipeline 不需要普遍增加 SkinMesh 专用 `DrawProcedural` 分支。

---

# 27. 最终目标架构

```text
                       AnimationSystem
                              │
                              ▼
                         PoseHandle
                              │
                     ┌────────┴────────┐
                     ▼                 ▼
                Pose Cache      BonePaletteAtlas
                                      │
                                      │
Culling                               │
   │                                  │
   ▼                                  │
Visible SkinMesh                      │
   │                                  │
   ▼                                  │
SkinningSystem                        │
   │                                  │
   ▼                                  │
SkinCache                             │
Mesh + Pose                           │
   │                                  │
   ├── Cache Hit ─────────────────┐   │
   │                              │   │
   └── Cache Miss                 │   │
          │                       │   │
          ▼                       │   │
   Unique Skin Jobs               │   │
          │                       │   │
          ▼                       │   │
    Compute Skinning ◄────────────┘───┘
          │
          ▼
   Skinned Vertex Buffer
          │
          ▼
      DrawIndexed
          │
          ▼
      Render Output
```

---

# 核心实现原则

整个改造保持三个明确边界：

```text
AnimationSystem
负责 Pose

SkinningSystem
负责 Pose → Skinned Vertex

Renderer
负责 Draw
```

同时遵循：

> 原始 Mesh 数据保持不可变，通过 Bindless SRV 输入 Compute。

> Bone Palette 按 Pose 共享。

> Skinning Result 按 `Mesh + Pose` 共享。

> Skinning 只针对可见对象产生。

> Renderer 只消费 SkinHandle，不关心结果是否被多个 Entity 共享。

第一版优先跑通：

```text
CPU Animation
+
GPU Compute Skinning
+
SkinCache
+
DrawIndexed
```

暂时不要把 Mesh Shader、Vertex Pulling、GPU Animation、DrawIndirect 等内容一起塞进本次改造。