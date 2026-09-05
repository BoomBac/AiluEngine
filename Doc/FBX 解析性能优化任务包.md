# FBX 解析性能优化任务包

## 目标

优化 AiluEngine 当前 FBX 导入性能，重点改善：

- 大型静态/蒙皮网格导入
- 长骨骼动画导入
- 同一个 FBX 中包含多个子资源时的重复解析
- Editor 长时间运行后的 FBX 导入内存占用

本次优化**不要尝试让 FBX SDK 本身多线程运行**。

FBX SDK 不保证线程安全，`FbxManager / FbxScene / FbxNode / FbxMesh / FbxEvaluator` 等对象统一保持单线程访问。

允许并行的范围仅限于：

```text
FBX SDK
   ↓
提取为 Ailu 自有 RawImportData
   ↓
从这里开始允许 Job System 并行处理
```

---

# P0：动画采样优化

当前动画采样如果是：

```text
for bone
    for frame
        EvaluateGlobalTransform
```

改为：

```text
for frame
    for bone
        EvaluateGlobalTransform
```

原因：

- FBX Evaluator 对同一时间点存在 evaluation cache
- frame-major 更容易复用缓存
- 当前 parent transform 可能被重复 Evaluate

每一帧流程调整为：

```text
sample_time
    ↓
一次计算所有 bone global transform
    ↓
local[bone] = inverse(global[parent]) * global[bone]
    ↓
写入 animation track
```

同时预计算每帧：

```text
sample_time[]
clip_time[]
```

避免在 `bone × frame` 循环内重复构造 `FbxTime`。

### 验收

Profile：

```text
100+ bones
1000~5000 frames
```

记录：

```text
EvaluateGlobalTransform 调用次数
ParserAnimation 总耗时
```

目标至少消除 parent 的重复 Evaluate。

---

# P0：限制 FBX 实际导入内容

根据 ImportFlag 配置 `FbxIOSettings`。

例如：

## Mesh-only

关闭不需要的数据：

```text
Animation
Gobo
Shape（如果暂未支持 BlendShape）
其它不需要的数据
```

## Animation-only

尽可能关闭：

```text
Material
Texture
其它与动画无关的数据
```

原则：

> 不要 Import 完整 Scene 后再丢弃数据，而是在 FbxImporter::Import 前告诉 SDK 不读取。

---

# P0：只导入目标 AnimationStack

当前如果只需要一个 AnimationStack：

```text
_animation_stack_index
```

不要先将全部 animation stack 导入，然后再 `SetCurrentAnimationStack()`。

在：

```text
FbxImporter::Initialize()
```

之后、

```text
FbxImporter::Import()
```

之前读取 TakeInfo，并只选择目标 AnimationStack。

目标：

```text
30 clips FBX
```

导入其中一个 clip 时，不读取其它 29 个 clip 的动画数据。

---

# P0：修正 FBX SDK 对象生命周期

检查并正确销毁：

```text
FbxImporter
FbxScene
FbxIOSettings
FbxManager
```

尤其检查当前析构函数中被注释的：

```cpp
Destroy();
```

避免每次资源导入后 FBX Scene 长期驻留。

验证：

```text
连续导入 FBX 100 次
```

Editor 内存不应持续线性上涨。

---

# P1：蒙皮权重解析优化

当前：

```cpp
std::map<u32, Vector<std::pair<u16, float>>>
```

不适合 ControlPoint 数据。

ControlPoint index 是连续整数：

```text
0 ~ control_point_count - 1
```

改为 dense array：

```text
Vector<control_point_skin_data>
```

推荐流程：

```text
遍历 Cluster
    ↓
收集 ControlPoint influence
    ↓
每个 ControlPoint 一次性：
    sort / top4 / normalize
    ↓
生成固定 bone_indices + bone_weights
    ↓
展开 polygon vertex 时直接读取
```

不要在 polygon vertex 阶段反复排序同一个 ControlPoint 的权重。

---

# P1：Indexed Mesh 临时内存优化

给大型数组提前：

```cpp
reserve(...)
```

至少包括：

```text
vertex_map
positions
normals
uvs
bone_indices
bone_weights
submesh_indices
```

检查 `GenerateIndexedMesh()` 中无实际用途的临时：

```cpp
std::vector<u32> indices;
```

如果最终没有消费，直接移除。

---

# P1：避免最终 Mesh 数据二次复制

当前如果流程类似：

```text
temporary positions
    ↓
Mesh::SetVertices(span)
    ↓
Mesh 内部 assign
```

则会额外完整复制一次网格数据。

优先使用已有 move 接口：

```cpp
SetVertices(std::move(positions));
SetNormals(std::move(normals));
SetUVs(std::move(uvs));
```

Bone data 同样检查是否可以 move。

目标：

> RawImportData → Mesh 最终数据尽量只有一次所有权转移。

---

# P1：只解析实际使用的 UV Set

如果 AiluEngine 当前只使用：

```text
UV0
```

则不要遍历并保存：

```text
UV1
UV2
UV3
...
```

避免无意义的：

```text
FBX accessor
内存分配
polygon vertex 数据写入
```

未来需要额外 UV channel 时再按 ImportSetting 开启。

---

# P1：优先使用 FBX Tangent

如果 FBX Mesh 已存在有效 tangent：

```text
直接读取
```

不要无条件执行：

```text
CalculateTangent()
```

建议策略：

```text
if source_has_tangent && !force_recalculate
    use source tangent
else
    CalculateTangent()
```

Normal 同样统一成：

```text
UseSource
Recalculate
```

模式。

---

# P2：Scene Validation 改为可选

当前如果每次 Import 后都会：

```text
SceneCheckUtility::Validate
```

但结果没有实际使用，则不要作为正常资产加载必经流程。

改成：

```text
Debug / ValidateImport
```

选项。

先 Profile 确认耗时，再决定默认是否关闭。

---

# 架构优化：同一个 FBX 只 Import 一次

重点检查这种情况：

```text
character.fbx
    Body
    Head
    Hair
    Weapon
    Skeleton
    Animation
```

如果每个 `.alasset` DDC miss 都执行：

```text
FbxImporter::Import(character.fbx)
```

则性能损失远高于局部算法优化。

目标结构：

```text
character.fbx
      ↓
FBX SDK Import
      ↓
ParsedSourceData / SourceImportCache
      ↓
 ┌────────┬─────────┬──────────┐
 Body    Head     Skeleton   Animation
```

同一个：

```text
source file + import settings + source timestamp/hash
```

对应一次 Source Import Cache。

长期可进一步保存：

```text
ParsedSourceArtifact
```

作为 source-level DDC。

---

# 多线程边界

禁止：

```text
Job A → FbxMesh
Job B → FbxNode
Job C → FbxEvaluator
```

推荐：

```text
Main/import thread
    |
    | FBX SDK
    v
RawMeshData / RawAnimationData
    |
    +------ Mesh Dedup Job
    |
    +------ Tangent Job
    |
    +------ Bounds Job
    |
    +------ Animation Key Reduce Job
    |
    +------ Artifact Serialization Job
```

FBX SDK 对象绝对不要泄漏到异步任务生命周期中。

---

# 建议实施顺序

```text
1. FBX 对象生命周期
2. FbxIOSettings 裁剪
3. AnimationStack Import 过滤
4. Animation frame-major sampling
5. Skin weight dense storage
6. Mesh reserve / remove redundant allocations
7. Mesh data move
8. UV / tangent 裁剪
9. SceneCheck 可选化
10. Source Import Cache
```

---

# 性能统计

为以下阶段增加独立 profile：

```text
FbxImporter::Import
Scene Validate
Axis / Unit Convert

Skeleton Parse
Mesh Raw Extract
Skin Weight Parse
Vertex Dedup
Tangent Generate

Animation Sampling
Animation Key Reduction

Mesh Build
Artifact Serialization
```

最终输出类似：

```text
FBX Import            420 ms
Mesh Extract           80 ms
Skin Parse             35 ms
Vertex Dedup          110 ms
Tangent                 52 ms
Animation Sampling    760 ms
Key Reduction           90 ms
```

先依据真实 profile 决定后续优化，而不是继续尝试 FBX SDK 多线程。

---

## 核心原则

> FBX SDK 阶段保持严格单线程，并尽可能让 SDK 少读取、少 Evaluate；一旦转换为 Ailu 自有数据，就立即脱离 FBX SDK，再进行并行和 CPU 算法优化。