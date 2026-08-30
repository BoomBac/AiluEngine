# AiluEngine SkeletonAsset 与多蒙皮网格共享动画重构方案

## 1. 目标

当前 `SkeletonMesh` 内嵌 `Skeleton`，`AnimationSystem` 以 Animator 所在 Entity 为单位维护
`SkeletonAnimationBinding / SkeletonPose / MatrixPalette`，因此身体、衣服、头发等拆成多个 Entity 后，
无法自然共享同一份动画解析结果与 Pose。

本次重构目标是一步到位完成：

- 将 `Skeleton` 从 `SkeletonMesh` 中拆分为独立 `SkeletonAsset`。
- `AnimatorComponent` 作为层级动画驱动器，不再使用 `animator_source` 之类的显式引用。
- Animator 自动扫描自身子树中的 `CSkeletonMesh`。
- 子树中所有使用同一个 `SkeletonAsset` 的 `CSkeletonMesh`：
  - 共享一次 `AnimationEvaluation`。
  - 共享一次 `SkeletonAnimationBinding`。
  - 共享一次动画采样与 Blend。
  - 共享一份 `SkeletonPose`。
  - 共享一份 Global Bone Pose Palette。
- 每个 `SkeletonMesh` 根据自身 Mesh Bind Transform 生成最终 Skin Matrix Palette。
- 遇到另一个 `AnimatorComponent` 时停止向下扫描，形成自然的动画层级边界。
- FBX 导入阶段支持将独立衣服/装备绑定到已有 `SkeletonAsset`，并完成 Bone Index Remap。

核心原则：

> Animator 决定“什么时候、播放什么”，SkeletonAsset 决定“有哪些骨骼”，
> Animator × SkeletonAsset 产生唯一 Pose，SkeletonMesh 只负责使用该 Pose 蒙皮自己的顶点。

---

## 2. 最终运行时结构

```text
Character [AnimatorComponent]
│
├── Body
│   └── CSkeletonMesh
│       ├── BodyMesh
│       └── HumanSkeleton.asset ─┐
│                                │
├── Shirt                        │
│   └── CSkeletonMesh            ├── 同一个 SkeletonAsset
│       ├── ShirtMesh            │
│       └── HumanSkeleton.asset ─┤
│                                │
├── Hair                         │
│   └── CSkeletonMesh            │
│       └── HumanSkeleton.asset ─┘
│
└── Pet [AnimatorComponent]
    └── PetBody
        └── CSkeletonMesh
            └── PetSkeleton.asset
```

Animator 扫描规则：

```text
Character Animator
├── Body
├── Shirt
├── Hair
└── Pet Animator   ← 遇到新的 Animator 后停止向下扫描
```

因此：

```text
Character Animator
    ├── Body
    ├── Shirt
    └── Hair

Pet Animator
    └── PetBody
```

---

## 3. 动画运行时数据流

```text
AnimatorComponent
        ↓
AnimationController
        ↓
AnimationEvaluation
        ↓
按 SkeletonAsset 分组
        ↓
SkeletonAnimationBinding
        ↓
SkeletonPose
        ↓
Global Bone Pose Palette
        │
        ├── Body  → Body Skin Palette  → Skinning
        ├── Shirt → Shirt Skin Palette → Skinning
        └── Hair  → Hair Skin Palette  → Skinning
```

对于同一个 Animator 下的多个 HumanSkeleton Mesh：

```text
AnimationEvaluation         1 次
Skeleton Clip Resolve       1 次
SkeletonAnimationBinding    1 份
Clip Sample                 1 次
Pose Blend                  1 次
Global Pose Build           1 次
```

但最终 Skin Matrix 仍然按 Mesh 单独计算。

---

# 4. SkeletonAsset

## 4.1 新增 SkeletonAsset

新增：

```text
Inc/Animation/SkeletonAsset.h
Src/Animation/SkeletonAsset.cpp
```

建议结构：

```cpp
ACLASS()
class AILU_API SkeletonAsset : public Object
{
    GENERATED_BODY()

public:
    [[nodiscard]] const Skeleton& GetSkeleton() const
    {
        return _skeleton;
    }

    Skeleton& GetSkeletonMutable()
    {
        return _skeleton;
    }

    [[nodiscard]] u64 LayoutHash() const
    {
        return _layout_hash;
    }

    void Rebuild();

private:
    Skeleton _skeleton;
    u64 _layout_hash = 0u;
};
```

`SkeletonAsset` 是骨架资源身份。

Runtime 判断两个 Mesh 是否可以共享 Pose：

```cpp
mesh_a->GetSkeletonAsset().get() == mesh_b->GetSkeletonAsset().get()
```

不要在 Runtime 每帧比较 joint name / hierarchy。

---

## 4.2 Skeleton 变成纯静态描述

共享的 `SkeletonAsset` 中只允许保存静态骨架信息：

```text
Joint hierarchy
Joint name
Parent index
Inverse bind pose
Bind local transform
Joint lookup
```

`Skeleton` 中不应保存任何角色实例状态。

需要移除或迁移：

```cpp
Map<String, Solver *> _solvers;
```

IK / Solver 应属于 Animator Runtime 或独立 IK Runtime。

如果当前存在：

```cpp
Matrix4x4f Joint::_cur_pose;
```

且没有必要用途，也应移除。

建议 `Joint` 保留：

```cpp
struct Joint
{
    static constexpr u16 kInvalidJointIndex = 0xffff;

    String _name;
    u16 _parent = kInvalidJointIndex;
    u16 _self = kInvalidJointIndex;
    Vector<u16> _children;
    Matrix4x4f _inv_bind_pos = Matrix4x4f::Identity();
};
```

其中：

- `_self` 可以由数组下标重建。
- `_children` 可以由 `_parent` 重建。
- Asset 文件中不必冗余保存这些可推导数据。

---

# 5. SkeletonAsset 序列化

新增 Skeleton Asset Document。

建议：

```cpp
ASTRUCT()
struct SkeletonJointDocument
{
    GENERATED_BODY()

    APROPERTY()
    String _name;

    APROPERTY()
    u16 _parent = Joint::kInvalidJointIndex;

    APROPERTY()
    Matrix4x4f _inverse_bind_pose = Matrix4x4f::Identity();

    APROPERTY()
    Transform _bind_local_transform;
};
```

```cpp
ACLASS()
class SkeletonAssetDocument : public Object
{
    GENERATED_BODY()

public:
    APROPERTY()
    AssetDocumentHeader _header;

    APROPERTY()
    Vector<SkeletonJointDocument> _joints;
};
```

新增：

```text
SkeletonAssetHandler
```

负责：

```text
.alasset
    ↓
SkeletonAssetDocument
    ↓
SkeletonAsset
```

加载后调用：

```cpp
SkeletonAsset::Rebuild();
```

重建：

```text
_self
_children
name → joint index lookup
layout hash
```

---

# 6. SkeletonMesh 重构

当前 `SkeletonMesh` 内嵌：

```cpp
Skeleton _skeleton;
```

删除。

改为：

```cpp
class SkeletonMesh : public Mesh
{
public:
    void SetSkeletonAsset(Ref<SkeletonAsset> skeleton_asset)
    {
        _skeleton_asset = std::move(skeleton_asset);
    }

    [[nodiscard]] const Ref<SkeletonAsset>& GetSkeletonAsset() const
    {
        return _skeleton_asset;
    }

private:
    Ref<SkeletonAsset> _skeleton_asset;

    Vector<Vector4f> _bone_weights;
    Vector<Vector4D<u32>> _bone_indices;
    Vector<Vector3f> _previous_vertices;

    Matrix4x4f _mesh_bind_global = Matrix4x4f::Identity();
    Matrix4x4f _mesh_current_global_inv = Matrix4x4f::Identity();
};
```

删除：

```cpp
SetSkeleton(...)
GetSkeleton()
Skeleton _skeleton;
```

以后使用：

```cpp
const Skeleton &skeleton = mesh->GetSkeletonAsset()->GetSkeleton();
```

---

# 7. Mesh Asset 与 Mesh Artifact

## 7.1 MeshAssetDocument

增加 Skeleton Asset GUID：

```cpp
APROPERTY()
Guid _skeleton = Guid::EmptyGuid();
```

加载 Mesh 时：

```cpp
if (!doc._skeleton.IsEmpty())
{
    resource_mgr.Load<SkeletonAsset>(doc._skeleton);

    Ref<SkeletonAsset> skeleton_asset =
        resource_mgr.GetRef<SkeletonAsset>(doc._skeleton);

    skeleton_mesh->SetSkeletonAsset(std::move(skeleton_asset));
}
```

---

## 7.2 MeshArtifact

从 Mesh Artifact 中删除：

```text
Joint[]
BindPose[]
Skeleton hierarchy
```

Mesh Artifact 只保留 Mesh 自身相关数据：

```text
Vertices
Indices
BoneIndices
BoneWeights
MeshBindGlobal
其它 GPU-ready Mesh 数据
```

升级：

```cpp
kMeshArtifactVersion = 2;
```

旧缓存直接失效，不需要兼容。

---

# 8. FBX 导入重构

## 8.1 一个 FBX 输出 SkeletonAsset

当前 FbxParser 已经只支持一套骨架，可以直接利用该限制。

FBX Parser 输出：

```text
SkeletonAsset
Mesh[]
AnimationClip[]
```

推荐创建顺序：

```text
1. SkeletonAsset
2. Mesh Assets
3. AnimationClip Assets
```

原因是 Mesh Asset 保存时已经需要 SkeletonAsset GUID。

同一个 FBX：

```text
Character.fbx
├── Body
├── Shirt
└── Hair
```

导入后：

```text
Character_Skeleton.alasset

Body.alasset  ─────┐
Shirt.alasset ─────┼── Character_Skeleton.alasset
Hair.alasset  ─────┘
```

---

# 9. 独立衣服 / 装备导入

这是换装系统必须支持的能力。

例如：

```text
Character.fbx
    ↓
HumanSkeleton.asset

Shirt.fbx
```

如果 Shirt.fbx 自己再次生成：

```text
ShirtSkeleton.asset
```

即使骨架内容完全相同，Runtime 也无法将：

```text
HumanSkeleton.asset
ShirtSkeleton.asset
```

视为同一个 Skeleton。

因此 `MeshImportSetting` 增加：

```cpp
APROPERTY(Category = "Mesh"; Order = 3)
Guid _skeleton = Guid::EmptyGuid();
```

语义：

### `_skeleton` 为空

```text
读取 FBX Skeleton
↓
创建新的 SkeletonAsset
```

### `_skeleton` 已指定

```text
读取 FBX Skeleton
↓
加载目标 SkeletonAsset
↓
验证兼容性
↓
Bone Name Remap
↓
Mesh BoneIndices 转为目标 SkeletonAsset Index
↓
不创建新的 SkeletonAsset
```

---

# 10. Bone Remap

不要假设两个 FBX 的 Bone Index 一致。

Importer 中构建：

```cpp
Vector<u16> bone_remap;

bone_remap.resize(source_skeleton.JointNum(), Joint::kInvalidJointIndex);

for (u32 source_index = 0u; source_index < source_skeleton.JointNum(); ++source_index)
{
    const Joint &source_joint = source_skeleton[source_index];

    const i32 target_index =
        Skeleton::GetJointIndexByName(target_skeleton, source_joint._name);

    if (target_index >= 0)
        bone_remap[source_index] = static_cast<u16>(target_index);
}
```

再转换 Mesh BoneIndices：

```cpp
for (Vector4D<u32> &indices : bone_indices)
{
    for (u32 influence = 0u; influence < 4u; ++influence)
    {
        const u32 source_index = indices[influence];

        AL_ASSERT(source_index < bone_remap.size());
        AL_ASSERT(bone_remap[source_index] != Joint::kInvalidJointIndex);

        indices[influence] = bone_remap[source_index];
    }
}
```

原则：

> 所有 Skeleton 兼容性和 Bone Index 重定向问题都在 Import 阶段解决，
> Runtime 永远只认 SkeletonAsset 的 Joint Index。

如果衣服中包含目标骨架不存在的 Bone：

```text
Import Error
```

不要 Runtime fallback。

---

# 11. Animator 层级扫描

`AnimatorComponent` 不保存任何 `animator_source`。

Animator 自动驱动：

```text
自身 Entity
+
所有 descendant Entity
```

直到遇到另一个 `AnimatorComponent`。

示例：

```text
Player [Animator A]
│
├── Body
├── Shirt
│
└── Mount
    └── Horse [Animator B]
        └── HorseMesh
```

结果：

```text
Animator A
├── Body
└── Shirt

Animator B
└── HorseMesh
```

扫描伪代码：

```cpp
void AnimationSystem::CollectSkeletonMeshes(Register &r, Entity animator_entity,
                                            Vector<Entity> &out_entities)
{
    const auto collect = [&](auto &&self, Entity entity) -> void
    {
        if (entity != animator_entity)
        {
            if (r.GetComponent<AnimatorComponent>(entity) != nullptr)
                return;

            if (r.GetComponent<CSkeletonMesh>(entity) != nullptr)
                out_entities.emplace_back(entity);
        }

        const CHierarchy *hierarchy = r.GetComponent<CHierarchy>(entity);
        if (hierarchy == nullptr)
            return;

        for (Entity child = hierarchy->_first_child; child != kInvalidEntity;)
        {
            const CHierarchy *child_hierarchy = r.GetComponent<CHierarchy>(child);

            const Entity next =
                child_hierarchy != nullptr ? child_hierarchy->_next_sibling : kInvalidEntity;

            self(self, child);

            child = next;
        }
    };

    collect(collect, animator_entity);
}
```

第一版可以每帧扫描。

角色子树通常很小，相比动画采样与 CPU Skinning 成本可以忽略。

暂时不要引入复杂 Dirty System。

后续若 profiler 确认有必要，再利用：

```cpp
Register::HierarchyRevision()
```

缓存消费者列表。

---

# 12. Animator Runtime 分组

共享 Pose 的 key 不能只是：

```text
SkeletonAsset
```

因为多个角色可能共享同一个 SkeletonAsset，但播放不同动画。

正确 key 为：

```text
Animator Entity + SkeletonAsset
```

建议：

```cpp
struct SkeletonRuntimeGroup
{
    Ref<SkeletonAsset> _skeleton_asset;

    SkeletonAnimationBinding _binding;
    SkeletonPose _pose;

    Vector<Matrix4x4f> _global_pose_palette;
    Vector<Entity> _consumers;
};

struct AnimatorRuntime
{
    AnimationEvaluation _evaluation;

    Map<const SkeletonAsset *, SkeletonRuntimeGroup> _skeleton_groups;
};

Map<Entity, AnimatorRuntime> _animator_runtimes;
```

含义：

```text
AnimatorRuntime
│
├── HumanSkeleton
│   ├── Body
│   ├── Shirt
│   └── Hair
│
└── TailSkeleton
    └── Tail
```

---

# 13. AnimationSystem 重构

当前 `AnimationSystem::Update()` 同时负责：

```text
State Machine
Controller
Clip Resolve
Event
Skeleton Sampling
Pose
Skinning
Sprite Animation
```

应拆分职责。

最终主流程建议：

```cpp
void AnimationSystem::Update(Register &r, f32 delta_time)
{
    PROFILE_BLOCK_CPU("AnimationSystem::Update")

    _skinning_system->Clear();
    _event_queue.Clear();

    const f32 dt = delta_time * TimeMgr::s_time_scale;

    for (const Entity animator_entity : _entities)
    {
        if (!r.IsEntityEnabled(animator_entity) ||
            !r.IsComponentEnabled<AnimatorComponent>(animator_entity))
        {
            continue;
        }

        AnimatorComponent *animator =
            r.GetComponent<AnimatorComponent>(animator_entity);

        if (animator == nullptr)
            continue;

        AnimatorRuntime &runtime =
            _animator_runtimes[animator_entity];

        if (!EvaluateAnimator(
                r,
                animator_entity,
                *animator,
                dt,
                runtime._evaluation))
        {
            continue;
        }

        CollectSkeletonGroups(r, animator_entity, runtime);

        for (auto &[skeleton_asset, group] : runtime._skeleton_groups)
        {
            if (group._consumers.empty())
                continue;

            EvaluateSkeletonGroup(runtime._evaluation, group);

            for (const Entity mesh_entity : group._consumers)
                SkinMesh(r, mesh_entity, group);
        }

        EvaluateSpriteAnimation(
            r,
            animator_entity,
            runtime._evaluation);
    }
}
```

---

# 14. Animator Evaluation

Animator 只负责：

```text
AnimationInstance
AnimationController
State Machine
Transition
Blend Space
Animation Event
AnimationEvaluation
```

不要直接处理 Skeleton。

原有：

```cpp
controller.Update(*instance, dt);
const AnimationEvaluation evaluation = controller.Evaluate(*instance);
```

调整为：

```cpp
controller.Update(*instance, dt);

AnimatorRuntime &runtime = _animator_runtimes[entity];

runtime._evaluation =
    controller.Evaluate(*instance);
```

Animator 不应该知道：

```text
SkeletonMesh
SkeletonPose
Skinning
Body
Clothes
```

---

# 15. ResolveClip 解耦

当前 `ResolveClip()` 如果包含：

```text
Entity
Skeleton
SpriteAnimationBinding
SkeletonAnimationBinding
```

应拆掉这些依赖。

最终：

```cpp
const AnimationClip *AnimationSystem::ResolveClip(const Guid &clip_id);
```

职责只剩：

```text
Guid
↓
ResourceMgr
↓
AnimationClip*
```

绑定工作由具体 Runtime Group 自己负责。

---

# 16. 收集 Skeleton Group

每个 Animator 每帧扫描子树：

```cpp
void AnimationSystem::CollectSkeletonGroups(Register &r,
                                            Entity animator_entity,
                                            AnimatorRuntime &runtime)
{
    for (auto &[skeleton_asset, group] : runtime._skeleton_groups)
        group._consumers.clear();

    Vector<Entity> mesh_entities;

    CollectSkeletonMeshes(
        r,
        animator_entity,
        mesh_entities);

    for (const Entity mesh_entity : mesh_entities)
    {
        CSkeletonMesh *component =
            r.GetComponent<CSkeletonMesh>(mesh_entity);

        if (component == nullptr ||
            component->_p_mesh == nullptr)
        {
            continue;
        }

        const Ref<SkeletonAsset> &skeleton_asset =
            component->_p_mesh->GetSkeletonAsset();

        if (skeleton_asset == nullptr)
            continue;

        SkeletonRuntimeGroup &group =
            runtime._skeleton_groups[skeleton_asset.get()];

        group._skeleton_asset = skeleton_asset;
        group._consumers.emplace_back(mesh_entity);
    }
}
```

---

# 17. Skeleton Group Evaluate

每个：

```text
Animator + SkeletonAsset
```

只 Evaluate 一次。

```cpp
void AnimationSystem::EvaluateSkeletonGroup(
    const AnimationEvaluation &evaluation,
    SkeletonRuntimeGroup &group)
{
    const Skeleton &skeleton =
        group._skeleton_asset->GetSkeleton();

    for (u8 sample_index = 0u;
         sample_index < evaluation._sample_count;
         ++sample_index)
    {
        const Guid &clip_id =
            evaluation._samples[sample_index]._clip;

        if (group._binding.FindClip(clip_id) != nullptr)
            continue;

        const AnimationClip *clip =
            ResolveClip(clip_id);

        if (clip != nullptr)
        {
            group._binding.Resolve(
                clip_id,
                *clip,
                skeleton);
        }
    }

    if (group._pose.Size() != skeleton.JointNum())
        group._pose = skeleton.GetBindPose();

    group._binding.Evaluate(
        evaluation,
        skeleton,
        group._pose);

    group._pose.GetMatrixPalette(
        group._global_pose_palette);
}
```

---

# 18. 最终 Skin Palette

共享的是：

```text
Global Bone Pose Palette
```

不要直接共享最终 Skin Matrix Palette。

当前 Skin Matrix 仍依赖 Mesh：

```text
mesh_bind_global
mesh_current_global_inv
```

因此：

```cpp
void AnimationSystem::BuildSkinPalette(
    const SkeletonRuntimeGroup &group,
    SkeletonMesh &mesh,
    Vector<Matrix4x4f> &out_palette)
{
    const Skeleton &skeleton =
        group._skeleton_asset->GetSkeleton();

    out_palette.resize(skeleton.JointNum());

    const Matrix4x4f &mesh_bind_global =
        mesh.GetMeshBindGlobalTransform();

    const Matrix4x4f &mesh_current_global_inv =
        mesh.GetMeshCurrentGlobalInverseTransform();

    for (const Joint &joint : skeleton)
    {
        out_palette[joint._self] =
            mesh_bind_global *
            joint._inv_bind_pos *
            group._global_pose_palette[joint._self] *
            mesh_current_global_inv;
    }
}
```

最终：

```text
                    Shared
                      ↓
               Skeleton Pose
                      ↓
             Global Pose Palette
                 ↙    ↓    ↘
              Body  Shirt  Hair
                ↓     ↓     ↓
              Skin   Skin   Skin
             Palette Palette Palette
```

---

# 19. AnimationSystem Cache 重构

删除当前按 SkeletonMesh Entity 保存的共享动画状态：

```cpp
Map<Entity, SkeletonPose> _skeleton_poses;
Map<Entity, SkeletonAnimationBinding> _skeleton_bindings;
```

改成：

```cpp
Map<Entity, AnimatorRuntime> _animator_runtimes;
```

最终 Mesh Skin Palette 如有 CPU Skinning / Upload 需要，可以保留：

```cpp
Map<Entity, Vector<Matrix4x4f>> _skin_palettes;
```

其 key 为：

```text
SkeletonMesh Entity
```

而：

```text
SkeletonAnimationBinding
SkeletonPose
Global Pose Palette
```

全部位于：

```text
AnimatorRuntime
    ↓
SkeletonRuntimeGroup
```

---

# 20. Animation Event

Animation Event 必须只由 Animator 产生。

不要因为：

```text
Body
Shirt
Hair
```

有三个消费者，就触发三遍 Event。

事件执行时机：

```text
AnimationController Update
↓
AnimationEvaluation
↓
Animation Event
↓
Skeleton Group Evaluate
```

即：

```text
1 Animator
=
1 份动画状态
=
1 次 Event
```

---

# 21. Sprite Animation

Sprite Animation 不需要跟 Skeleton Group 混在一起。

保留：

```text
Animator
↓
AnimationEvaluation
↓
SpriteAnimationBinding
```

Skeleton 分支：

```text
Animator
↓
AnimationEvaluation
↓
SkeletonRuntimeGroup[]
```

两者共享 Animator Evaluation 即可。

---

# 22. 生命周期与 Cache 清理

需要处理：

```text
Animator Entity Destroy
AnimatorComponent Remove
Animator Controller Change
SkeletonMesh Destroy
SkeletonAsset Change
```

最低要求：

### Animator 移除

删除：

```cpp
_animator_runtimes.erase(animator_entity);
```

### Skeleton Group 无 Consumer

不要立即频繁创建/删除。

每帧：

```cpp
group._consumers.clear();
```

重新扫描。

长期不存在的 group 可在之后增加延迟清理。

### Mesh 删除

由于 `_consumers` 每帧重建，不需要显式维护反向关系。

---

# 23. 第一阶段暂时不要做的事情

本次不要加入：

```text
Animator Source Entity
复杂 Hierarchy Dirty Graph
Runtime Bone Name Remap
多个 SkeletonAsset 自动模糊匹配
GPU Shared Bone Buffer
Skeleton LOD
Animation Retargeting
Humanoid Avatar
复杂 IK Asset
```

先保证基础模型正确。

尤其不要引入：

```text
CSkeletonMesh::_animator_entity
```

Animator 与 Mesh 的关系由 Scene Hierarchy 决定。

---

# 24. 后续可扩展方向

完成本次重构以后，可以自然扩展：

## GPU Bone Palette 共享

当前：

```text
CPU shared pose
↓
每 Mesh 生成 Skin Palette
```

如果导入约束进一步统一，可以研究：

```text
Animator + SkeletonAsset
↓
Shared GPU Bone Buffer
```

多个 Mesh 只保存 Buffer Offset。

---

## Skeleton Retargeting

以后可以加入：

```text
AnimationSkeleton
↓
Retarget
↓
Target SkeletonAsset
```

但不要放进本次任务。

---

## Skeleton LOD

后续可以：

```text
SkeletonAsset
├ LOD0 bones
├ LOD1 bones
└ LOD2 bones
```

依旧保持 Animator × SkeletonAsset Runtime Group 模型。

---

# 25. 推荐实施顺序

## Phase 1：SkeletonAsset

完成：

- 新增 `SkeletonAsset`。
- 新增 `SkeletonAssetDocument`。
- 新增 `SkeletonAssetHandler`。
- ResourceMgr 注册 SkeletonAsset。
- Skeleton 清除 runtime state。

验收：

```text
SkeletonAsset 能独立保存、加载。
```

---

## Phase 2：SkeletonMesh

完成：

- `SkeletonMesh` 删除内嵌 Skeleton。
- 改为 `Ref<SkeletonAsset>`。
- MeshAssetDocument 增加 Skeleton GUID。
- MeshArtifact 删除 Skeleton 内容。
- MeshArtifact Version +1。

验收：

```text
SkeletonMesh 加载后能正确引用 SkeletonAsset。
```

---

## Phase 3：FBX Import

完成：

- FBX Parser 输出 SkeletonAsset。
- 同 FBX 多 Mesh 共享 SkeletonAsset。
- SkeletonAsset 先于 Mesh 创建。
- AnimationClip 保持正常导入。

验收：

```text
Body / Clothes / Hair 同 FBX 导入后引用完全相同的 SkeletonAsset。
```

---

## Phase 4：外部 Skeleton Import

完成：

- MeshImportSetting 增加 `_skeleton`。
- 支持选择已有 SkeletonAsset。
- Bone name 验证。
- Bone index remap。

验收：

```text
独立 Shirt.fbx 可以绑定 HumanSkeleton.asset。
```

并保证：

```cpp
shirt_mesh->GetSkeletonAsset() ==
body_mesh->GetSkeletonAsset()
```

---

## Phase 5：Animation Runtime

完成：

- 新增 `AnimatorRuntime`。
- 新增 `SkeletonRuntimeGroup`。
- Animator 扫子树。
- Nested Animator 截断。
- 按 SkeletonAsset 分组。
- 每 Group 只 Evaluate 一次 Pose。
- 每 Mesh 生成自己的 Skin Palette。

验收：

```text
Body + Shirt 使用同 SkeletonAsset：
SkeletonAnimationBinding::Evaluate 每帧只执行一次。
```

---

## Phase 6：清理旧实现

删除：

```text
SkeletonMesh 内嵌 Skeleton
AnimationSystem::_skeleton_poses
AnimationSystem::_skeleton_bindings
旧 ResolveClip Skeleton 参数
旧 Animator 与 SkeletonMesh 同 Entity 假设
旧 MeshArtifact Skeleton 数据
```

---

# 26. 调试信息

建议增加 Debug / Profiler 信息：

```text
Animator Entity
Skeleton Group Count
Consumer Count
Skeleton Joint Count
Animation Sample Count
Pose Evaluate Time
Skin Palette Build Time
```

例如：

```text
Animator Player
  HumanSkeleton
    joints: 78
    consumers: 4
    evaluate: 0.041 ms

  TailSkeleton
    joints: 12
    consumers: 1
    evaluate: 0.006 ms
```

可以快速确认是否真的实现 Pose Sharing。

---

# 27. 测试用例

## Case 1：单 Mesh

```text
Player [Animator]
└ Body [HumanSkeleton]
```

要求：

- 动画正常。
- 与改造前视觉一致。

---

## Case 2：Body + Clothes

```text
Player [Animator]
├ Body  [HumanSkeleton]
└ Shirt [HumanSkeleton]
```

要求：

- 严格同步。
- Skeleton Pose 每帧只 Evaluate 一次。
- 两个 Mesh 均正确蒙皮。

---

## Case 3：多个服装

```text
Player [Animator]
├ Body
├ Shirt
├ Pants
├ Hair
└ Shoes
```

全部使用：

```text
HumanSkeleton
```

要求：

```text
1 × AnimationEvaluation
1 × Skeleton Pose Evaluate
5 × Mesh Skin Palette
```

---

## Case 4：Nested Animator

```text
Player [Animator A]
├ Body
└ Pet [Animator B]
    └ PetMesh
```

要求：

- Animator A 不驱动 PetMesh。
- Animator B 正确驱动 PetMesh。

---

## Case 5：两个角色共享 SkeletonAsset

```text
PlayerA [Animator] → HumanSkeleton → Walk
PlayerB [Animator] → HumanSkeleton → Run
```

要求：

- SkeletonAsset 共享。
- Pose 不共享。
- Runtime Group 分别属于两个 Animator。

即：

```text
AnimatorA + HumanSkeleton → Pose A
AnimatorB + HumanSkeleton → Pose B
```

---

## Case 6：独立 Shirt FBX

```text
HumanSkeleton.asset

Shirt.fbx
    MeshImportSetting._skeleton = HumanSkeleton.asset
```

要求：

- 不创建 ShirtSkeleton。
- Bone index 成功 remap。
- Shirt 与 Body 可以共享 Pose。

---

## Case 7：不兼容骨架

Shirt FBX 包含：

```text
ExtraBone
```

而目标 HumanSkeleton 不存在该 Bone。

要求：

```text
Import Failed
```

并给出明确错误：

```text
Skeleton remap failed:
source bone "ExtraBone" does not exist in target skeleton "HumanSkeleton".
```

---

# 28. 验收标准

本任务完成必须满足：

- [ ] `Skeleton` 不再内嵌于 `SkeletonMesh`。
- [ ] `SkeletonAsset` 可独立序列化和加载。
- [ ] 多个 `SkeletonMesh` 可以引用同一个 `SkeletonAsset`。
- [ ] 同 FBX 的多个蒙皮 Mesh 默认共享 SkeletonAsset。
- [ ] 独立衣服 FBX 可以指定已有 SkeletonAsset。
- [ ] Bone Index Remap 在 Import 阶段完成。
- [ ] Animator 不再要求和 `CSkeletonMesh` 位于同一个 Entity。
- [ ] Animator 自动扫描 descendant。
- [ ] 遇到另一个 Animator 时停止扫描。
- [ ] 同一 Animator 下相同 SkeletonAsset 只产生一份 Skeleton Runtime Group。
- [ ] AnimationEvaluation 每 Animator 每帧只计算一次。
- [ ] SkeletonAnimationBinding 每 Animator + SkeletonAsset 共享。
- [ ] SkeletonPose 每 Animator + SkeletonAsset 共享。
- [ ] Global Bone Pose Palette 每 Animator + SkeletonAsset 共享。
- [ ] 最终 Skin Palette 每 SkeletonMesh 独立生成。
- [ ] Body / Clothes / Hair 动画严格同步。
- [ ] Animation Event 不会因为多个 SkinMesh 重复触发。
- [ ] 两个角色共享 SkeletonAsset 时仍拥有独立 Pose。
- [ ] 原单 Mesh 角色行为不回归。

---

# 29. 最终架构总结

```text
                       SkeletonAsset
                            │
             ┌──────────────┴──────────────┐
             │                             │
       Character A                   Character B
        Animator A                    Animator B
             │                             │
     AnimationEvaluation             AnimationEvaluation
             │                             │
   SkeletonRuntimeGroup            SkeletonRuntimeGroup
             │                             │
          Pose A                         Pose B
             │
       Global Pose Palette
        ┌────┼─────┐
        │    │     │
      Body Shirt  Hair
        │    │     │
      Skin Skin   Skin
```

资源层：

```text
SkeletonAsset
    ↓
Skeleton definition

SkeletonMesh
    ↓
Ref<SkeletonAsset>
Bone indices
Bone weights
Mesh bind transform
```

运行时：

```text
Animator
    ↓
AnimationEvaluation
    ↓
Animator + SkeletonAsset
    ↓
SkeletonRuntimeGroup
    ↓
Shared Pose
    ↓
Multiple SkinMesh Consumers
```

最终职责：

> `SkeletonAsset` 是骨架身份与静态数据；
> `AnimatorComponent` 是层级动画驱动器；
> `AnimatorRuntime + SkeletonAsset` 产生共享 Pose；
> `SkeletonMesh` 是纯蒙皮消费者。
