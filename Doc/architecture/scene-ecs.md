# Scene and ECS Architecture

## Purpose

本文档描述 AiluEngine 当前 Scene、Entity、ECS、hierarchy、persistent identity 和 render-facing data 的边界。

主要入口：

- `Engine/Inc/Scene/Scene.h`
- `Engine/Inc/Scene/Entity.h`
- `Engine/Inc/Scene/Component.h`
- `Engine/Inc/Scene/TransformSystem.h`
- `Engine/Inc/Scene/EntityReference.h`
- `Engine/Inc/Scene/PrefabSystem.h`

## Scene ownership

`Scene` 当前拥有一个 ECS `Register`，并承担 entity 生命周期。

核心职责包括：

```text
Entity create/remove
ECS component storage access
Hierarchy
Persistent entity GUID
Scene root
Dirty/edit/structure revision
Prefab instance data
Scene commands
Update integration
```

## Entity identity

必须区分：

```text
ECS::Entity
```

和：

```text
persistent Guid
```

`ECS::Entity` 是运行时 handle/id。

Persistent GUID 用于：

- save/load；
- stable reference；
- rebuild 后保持 identity；
- cross-system persistent lookup。

不要把 runtime entity integer 序列化为长期稳定 identity。

`Scene` 维护 GUID → Entity index，并提供 identity validation/repair。

## Scene root and hierarchy

Scene 具有显式 scene root。

Hierarchy API 包括：

```text
Reparent
Detach
IsDescendantOf
SetEntityEnabled
```

结构修改应通过 Scene 提供的 API，以便同步：

- hierarchy links；
- structure revision；
- edit revision；
- dirty state；
- dependent systems。

不要绕过 hierarchy API 直接改 component link，除非当前系统内部实现明确要求这样做。

## Revisions

当前至少存在：

```text
_structure_revision
_edit_revision
_dirty
```

用途不同：

- structure revision：实体/组件/hierarchy 等结构级变化；
- edit revision：用户可观察编辑变化；
- dirty：需要保存或重新处理的状态。

新增缓存或 read model 时，应选择正确的 revision，而不是默认每帧 rebuild。

## Update responsibilities

`Scene` 当前还包含：

```text
FixedUpdate
script updates
LateUpdate
render transform update
bounds update
camera update
acceleration structure update
GPU scene update
pending delete
scene command processing
```

这是当前实现事实，但不是“任何新 scene-related 功能都应继续加入 Scene”的理由。

## Render-facing data

`Scene` 当前还拥有部分 GPU/ray-tracing-oriented 数据，例如：

```text
scene mesh data buffer
BLAS buffer
TLAS buffer
BVH nodes
triangle offsets
```

这属于现阶段较强耦合区域。

新增 renderer-only cache 前先判断它是否可以存在于：

- Renderer；
- SceneRenderData；
- SceneRayTracingProxy；
- render subsystem；

而不是默认成为 `Scene` 新成员。

## SceneMgr

`SceneMgr` 管理：

- active scene；
- runtime scene；
- temporary scene；
- play/simulate mode；
- retired runtime scene。

Scene 切换与 render thread 生命周期相关。

删除/替换 scene 时不能只看 main-thread ownership，还需要确认上一帧 render 已不再引用旧 scene。

## Prefab

Prefab instance information 属于 Scene 持久状态。

涉及 prefab mutation 时要同时检查：

- hierarchy；
- entity GUID；
- scene edit revision；
- serialization；
- temporary prefab scene；
- save/apply workflow。

## Serialization

Scene 的持久化表示通过 asset document 体系完成。

当前 `SceneAssetDocument` 具有显式 scene format version。

修改 Scene/Component 持久结构时：

1. 不要只修改 runtime struct；
2. 检查 document；
3. 检查 reflection/generated serialization；
4. 考虑旧版本迁移；
5. 保留 stable entity GUID。

## Current pressure point

`Scene` 当前职责很重。

新增逻辑时使用以下判断：

```text
Does this define entity/scene state?
    yes → Scene/ECS may own it

Is it only a render cache/view?
    yes → prefer rendering subsystem

Is it an editor-only representation?
    yes → prefer Editor

Is it asset/import state?
    yes → prefer asset subsystem
```

不要因为“可以从 Scene 访问到”就把数据放进 Scene。

## Change checklist

修改 Scene/ECS 时检查：

- persistent GUID；
- hierarchy；
- dirty/edit/structure revision；
- serialization；
- prefab；
- play mode clone/runtime scene；
- temporary scene；
- script references；
- render thread lifetime；
- renderer caches。
