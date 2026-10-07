# M2 — Static Mesh Binary Sync

# Purpose

同步 Blender 最终 evaluated static mesh，并直接构造 Ailu transient `Render::Mesh`。

---

# Scope

支持：

- Mesh Object。
- Modifier 后的 evaluated mesh。
- triangle topology。
- position。
- normal。
- tangent。
- UV0。
- multiple material slots / submeshes。
- local bounds。
- geometry revision。
- mesh replacement。
- geometry debounce。
- malformed mesh payload guard。

---

# Non-goals

- Skeleton。
- skinning。
- animation。
- shape key animation。
- UV1+。
- vertex color。
- blend shapes。
- mesh compression。
- mesh dedup / shared mesh instances。
- Nanite/meshlet special path。

---

# Architecture Constraints

## Binary

Mesh 不允许 JSON vertex arrays。

## Main Thread

Ailu Mesh 构建与 `Apply()` 只在 plugin Tick。

## Correctness First

第一版 flatten triangle corners。

## Ownership

Mesh 使用 plugin cache `Ref<Mesh>` 保活。

不进入 AssetRegistry。

---

# Baseline

现有：

```text
Render::Mesh::SetVertices
SetNormals
SetTangents
SetUVs
AddSubmesh
SetBounds
Apply
```

`StaticMeshComponent` 已可以直接绑定 runtime Mesh Ref。

---

# Target Outcome

Blender：

```text
Cube + Bevel + Subdivision
```

修改 modifier：

```text
Ailu 中几何更新
```

UV seam / hard edge：

```text
显示正确
```

---

# Relevant Files

```text
Engine/Inc/Render/Mesh.h
Engine/Src/Render/Mesh.cpp
Engine/Inc/Scene/Component.h
Engine/Src/Scene/Scene.cpp

Plugins/BlenderLiveLink/Inc/LiveLinkMeshBridge.h
Plugins/BlenderLiveLink/Src/LiveLinkMeshBridge.cpp

Tools/Blender/ailu_live_link/mesh_sync.py
```

---

# Milestones

当前：

```text
M2
```

依赖：

```text
M1 PASS
```

---

# Current Milestone

M2。

---

# Implementation

## 2.1 Blender Evaluated Mesh

```python
depsgraph = bpy.context.evaluated_depsgraph_get()
obj_eval = obj.evaluated_get(depsgraph)
mesh = obj_eval.to_mesh()
```

随后：

```python
mesh.calc_loop_triangles()
```

需要 tangent 时：

```python
mesh.calc_tangents(uvmap=...)
```

finally：

```python
obj_eval.to_mesh_clear()
```

确保异常路径也 clear。

---

## 2.2 Flattened Vertex

每个 loop triangle corner 生成一个 vertex：

```text
position
normal
tangent.xyz + handedness
uv0
```

这样：

```text
vertex_count ≈ triangle_count * 3
```

但 seam/hard normal 正确。

---

## 2.3 Material Slot → Submesh

按：

```text
polygon.material_index
```

分组 triangle。

payload 中：

```text
submesh 0:
    material_slot
    indices

submesh 1:
    material_slot
    indices
```

Ailu material binding M3 再完善。

M2 可以暂时全部 checker/default material。

---

## 2.4 Binary Layout

建议不要直接 memcpy C++ struct containing pointer/vector。

固定 header：

```text
guid[16 bytes]
u64 revision
u32 flags
u32 vertex_count
u32 submesh_count
float bounds_min[3]
float bounds_max[3]
```

随后按固定顺序：

```text
float position[vertex_count][3]
float normal[vertex_count][3]
float tangent[vertex_count][4]
float uv0[vertex_count][2]
```

submesh：

```text
u32 material_slot
u32 index_count
u32 indices[index_count]
```

所有整数 little-endian。

需要对：

```text
vertex_count
submesh_count
index_count
total bytes
```

做 overflow 检查。

---

## 2.5 Ailu Mesh Construction

伪流程：

```text
validate payload
    ↓
convert arrays
    ↓
MakeRef<Mesh>
    ↓
SetVertices
SetNormals
SetTangents
SetUVs
AddSubmesh
SetBounds
Apply
    ↓
cache[object_guid] = mesh
    ↓
StaticMeshComponent::_p_mesh = mesh
```

若无 `StaticMeshComponent`：

```text
AddComponent
```

---

## 2.6 Mesh Update

不要修改正在使用的 Mesh 内存到一半。

推荐：

```text
Build new Mesh
Apply complete
    ↓
swap component pointer
    ↓
replace cache ref
```

这样旧 Mesh 在 Ref 释放前仍有效。

---

## 2.7 Bounds

必须明确设置 local bounds。

不能假设 `Apply()` 必然替你得到 `Mesh::_bounds`。

Blender addon 可以发送 bounds，也可以 Ailu 从 position 计算。

推荐 Ailu 再计算一次并用于 validation：

```text
received bounds vs computed bounds
```

debug 下可 assert epsilon。

---

## 2.8 Geometry Dirty Debounce

Blender：

```text
is_updated_geometry
    ↓
dirty object guid
    ↓
50~150 ms quiet period
    ↓
extract once
```

拖 modifier slider 时不要每个 depsgraph update 都同步大 Mesh。

---

## 2.9 Revision

每个 object mesh：

```text
u64 revision
```

Ailu 记录 latest。

如果收到：

```text
revision <= current
```

丢弃。

---

# Progress

- [x] evaluated mesh extraction。
- [x] cleanup on exception。
- [x] triangle flatten。
- [x] normals。
- [x] tangents。
- [x] uv0。
- [x] material submesh grouping。
- [x] binary encoder。
- [x] binary decoder bounds check。
- [x] runtime Mesh create。
- [x] SetBounds。
- [x] Apply。
- [x] StaticMeshComponent binding。
- [x] mesh pointer replacement。
- [x] revision reject。
- [x] debounce。
- [ ] large mesh test。

源码与 Editor Debug 构建通过；Blender evaluated mesh 与 large mesh 场景验收未运行。

---

# Decision Log

- binary mesh。
- flattened triangle corners。
- object GUID 作为 mesh cache key。
- build-new-then-swap。
- no persistent asset。

---

# Surprises & Discoveries

重点记录：

- `Mesh::Apply` 是否要求特定 index winding。
- Blender → Ailu handness conversion是否需要 triangle winding reverse。
- tangent handedness 是否因 basis reflection 需要翻转。
- replacement Mesh 是否被 Renderer/TLAS 当帧立即识别。
- `StaticMeshComponent` 新增时是否需要额外 render registration hook。

如果 replacement pointer 不自动生效，补最小 Scene/Render invalidation，不要引入完整 Asset Reload。

---

# Validation

至少准备：

1. Cube。
2. Suzanne smooth。
3. hard edge cube。
4. UV seam。
5. normal mapped plane。
6. two-material cube。
7. Mirror modifier。
8. Bevel modifier。
9. Subdivision modifier。
10. negative scale object。
11. 100k triangle mesh。

检查：

```text
position
normal
winding
tangent
UV
submesh
bounds
shadow
ray tracing/TLAS if enabled
```

性能记录：

```text
extract ms
encode ms
network ms
decode ms
Mesh::Apply ms
total ms
payload MB
```

---

# Known Follow-ups

- indexed dedup。
- LZ4/Zstd。
- shared mesh datablock。
- instancing。
- UV1。
- vertex color。
- incremental vertex-only update。

---

# Outcomes

M2 后 Blender static geometry 已经可以实时驱动 Ailu Scene。

---

# Resume Protocol

如果 winding/coordinate/tangent golden test 未通过，不进入 M3。
