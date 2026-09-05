# AiluEngine 现代资产加载与热重载系统重构任务包

## 1. 任务目标

重构 AiluEngine 资产系统，建立统一的：

```text
Persistent Asset Identity
+
Runtime Asset Handle
+
Asset Slot Registry
+
Immutable Asset Snapshot
+
Revision-based Reload
+
Atomic Publish
```

模型。

本任务**不要求兼容当前所有 `Ref<Asset>` 使用方式**。如果现有架构阻碍正确设计，可以直接重构调用链。

核心原则：

> GUID 是持久化身份。

> AssetHandle 是运行时稳定身份。

> AssetSlot 是可变状态容器。

> Asset Object 是不可变 Snapshot。

> Reload 不修改旧 Asset，而是创建新版本后 Publish。

---

# 2. 总体架构

最终关系：

```text
Persistent Asset Reference
        │
       Guid
        │
        ▼
   AssetRegistry
        │
        ▼
 AssetHandle<T>
        │
        ▼
    AssetSlot
 ┌──────┼────────────┐
 │      │            │
state revision    current
                   │
                   ▼
             Ref<const T>
                   │
             Asset Snapshot
```

Reload：

```text
Asset v12
   │
   │ 保持可用
   ▼
Build Asset v13
   │
Validate
   │
Publish
   ▼
Slot.current
v12 → v13
```

旧版本由现有引用自然保持生命周期。

---

# 3. 禁止使用稳定 Asset Object 作为 Reload 基础

不要采用：

```text
同一个 Texture2D object
↓
Reload 时修改内部字段
↓
替换 GPU resource
```

禁止将以下模式作为统一资产框架：

```cpp
asset.Reload(...)
asset.ReplaceInternalData(...)
asset.SetNewResource(...)
```

因为 Texture / Mesh / Skeleton / Shader 等资产 Reload 时，可能发生：

```text
format change
dimension change
layout change
bone count change
shader reflection change
GPU resource change
metadata change
```

资产本身应该表示：

> 一个完整且有效的版本。

而不是可反复变异的容器。

---

# 4. Asset Snapshot

资产对象尽量设计成 immutable。

例如：

```cpp
class Texture2D final : public Asset
{
public:
    const TextureDesc& GetDesc() const;
    const Ref<RhiTexture>& GetResource() const;

private:
    TextureDesc _desc;
    Ref<RhiTexture> _resource;
};
```

构造完成并 Publish 后：

```text
Texture2D 不再被修改。
```

Reload：

```text
Texture2D v7
↓
new Texture2D
↓
Texture2D v8
↓
Publish
```

同样适用于：

```text
Mesh
Shader
Material
AnimationClip
Skeleton
Audio
```

---

# 5. AssetHandle

新增轻量运行时 Handle。

建议：

```cpp
struct AssetHandleBase
{
    u32 _index = kInvalidIndex;
    u32 _slot_generation = 0u;

    bool IsValid() const;
};

template<typename T>
class AssetHandle : public AssetHandleBase
{
public:
    Ref<const T> Resolve() const;
};
```

运行时 Handle 不存 GUID hash 查找结果。

热路径应通过：

```text
index
↓
AssetSlot array
```

访问。

避免每次：

```cpp
unordered_map<Guid, Ref<Asset>>
```

查询。

---

# 6. AssetSlot

建议结构：

```cpp
struct AssetSlot
{
    Guid _guid;

    Ref<const Asset> _current;

    AssetArtifactKey _artifact_key;

    u64 _revision = 0u;

    u32 _slot_generation = 0u;

    EAssetLoadState _state = EAssetLoadState::kUnloaded;

    bool _last_update_failed = false;
    String _last_error;

    Vector<AssetDependency> _dependencies;
    Vector<AssetDependency> _dependents;

    mutable Mutex _mutex;
};
```

AssetRegistry：

```cpp
class AssetRegistry
{
private:
    Vector<AssetSlot> _slots;
    HashMap<Guid, u32> _guid_to_slot;
};
```

GUID lookup 主要发生于：

```text
首次创建 Handle
反序列化资产引用
编辑器资产查询
```

运行时使用 Handle 后不重复 hash。

---

# 7. Slot Generation 与 Asset Revision 分离

必须明确区分两个 generation 概念。

## Slot Generation

用于 Handle 生命周期验证。

例如：

```text
slot 17
generation = 3
Texture A
```

删除后复用：

```text
slot 17
generation = 4
Texture B
```

旧 Handle：

```text
index = 17
generation = 3
```

自动失效。

---

## Asset Revision

用于表示：

> 同一个 GUID 的资产内容更新次数。

例如：

```text
Texture fish

revision 12
↓
reload
revision 13
↓
reload
revision 14
```

Handle 完全不变。

因此：

```text
slot_generation
= slot identity lifetime

revision
= asset content version
```

禁止混用。

---

# 8. Persistent Reference 与 Runtime Handle 分离

序列化中禁止保存：

```text
slot index
slot generation
```

Persistent 资产引用始终保存：

```text
Guid
```

建议新增：

```cpp
template<typename T>
struct SoftAssetRef
{
    Guid _guid;
};
```

含义：

```text
可序列化
不保证 loaded
不直接持有 runtime snapshot
```

加载后：

```cpp
AssetHandle<T> handle = asset_registry.Resolve(_guid);
```

即：

```text
Persistent:
SoftAssetRef<T> / Guid

Runtime:
AssetHandle<T>
```

---

# 9. Handle 不强持有 Asset 生命周期

AssetHandle 本身只表示 slot identity。

不等于：

```cpp
Ref<T>
```

因此：

```text
Material
 └─ AssetHandle<Texture2D>
```

不会永久阻止 Texture unload。

这为未来提供：

```text
Asset Residency
Asset GC
Memory Budget
Streaming
LRU unload
```

基础。

---

# 10. Resolve 返回 Snapshot

推荐：

```cpp
template<typename T>
Ref<const T> AssetHandle<T>::Resolve() const;
```

使用：

```cpp
Ref<const Texture2D> texture = texture_handle.Resolve();

if (texture)
{
    ...
}
```

Resolve 返回当前 Snapshot。

例如：

```text
Resolve()
↓
Texture v12
```

同时发生 Reload：

```text
Slot.current
v12 → v13
```

当前代码持有：

```text
Texture v12
```

仍然合法。

下一次 Resolve：

```text
Texture v13
```

这样自然实现多版本并存。

---

# 11. Publish 模型

新增统一 Publish 原语。

建议：

```cpp
bool AssetRegistry::Publish(
    AssetHandleBase handle,
    Ref<const Asset> candidate,
    const AssetArtifactKey& artifact_key);
```

语义：

```text
Build Candidate
↓
Validate
↓
Lock Slot
↓
Swap current
↓
Update artifact key
↓
revision++
↓
state = Ready
↓
Unlock
↓
Dispatch event
```

概念代码：

```cpp
bool AssetRegistry::Publish(
    AssetHandleBase handle,
    Ref<const Asset> candidate,
    const AssetArtifactKey& artifact_key)
{
    AssetSlot* slot = TryGetSlot(handle);
    if (slot == nullptr || candidate == nullptr)
        return false;

    u64 new_revision = 0u;

    {
        LockGuard lock(slot->_mutex);

        slot->_current = std::move(candidate);
        slot->_artifact_key = artifact_key;
        slot->_state = EAssetLoadState::kReady;
        slot->_last_update_failed = false;

        new_revision = ++slot->_revision;
    }

    DispatchAssetPublished(handle, new_revision);

    return true;
}
```

Publish 是整个系统的核心原语。

---

# 12. Load 与 Reload 共用创建逻辑

禁止为每种 Asset 创建专门的：

```cpp
Texture::Reload()
Mesh::Reload()
Shader::Reload()
Skeleton::Reload()
```

AssetHandler 只需要负责：

```text
Artifact
↓
Create complete Asset Snapshot
```

例如：

```cpp
class IAssetHandler
{
public:
    virtual Ref<const Asset> Load(const AssetLoadContext& context) = 0;
};
```

首次 Load：

```text
empty
↓
Load candidate
↓
Publish
```

Reload：

```text
old snapshot exists
↓
Load candidate
↓
Publish
```

区别只在 Slot 当前有没有旧版本。

---

# 13. AssetImporter 与 AssetLoader 分离

严格区分：

## Importer

```text
Source
+
ImportSetting
↓
Artifact
```

例如：

```text
PNG
↓
DirectXTex
↓
TextureArtifact
```

---

## Loader / Handler

```text
Artifact
↓
Runtime Asset Snapshot
```

例如：

```text
TextureArtifact
↓
Texture2D
↓
RHI Texture
```

不得混成：

```text
Source
↓
Texture2D
↓
再生成 Artifact
```

---

# 14. Reimport 与 Reload 分离

统一定义：

## Reimport

```text
Source / ImportSetting
↓
Artifact
```

触发条件：

```text
Source Changed
ImportSetting Changed
Importer Version Changed
Dependency Build Change
```

---

## Reload

```text
Artifact
↓
Asset Snapshot
↓
Publish
```

因此：

```text
Source changed
↓
Reimport
↓
Artifact
↓
Reload
```

而：

```text
DDC Hit
↓
Artifact
↓
Reload
```

完全不需要跑 Importer。

---

# 15. Asset Update Pipeline

推荐最终流程：

```text
FileSystemWatcher
        │
        ▼
    Mark Dirty
        │
        ▼
Recompute ArtifactKey
        │
        ▼
   DDC Lookup
     /     \
   hit     miss
   │        │
   │     Importer
   │        │
   └───┬────┘
       ▼
    Artifact
       │
       ▼
 AssetHandler
       │
       ▼
Candidate Snapshot
       │
    Validate
       │
       ▼
     Publish
       │
       ▼
  revision++
```

---

# 16. FileSystemWatcher 只负责 Dirty

Watcher callback 禁止执行：

```text
Import
Load
GPU resource creation
Shader compilation
Asset Publish
```

Watcher 只做：

```cpp
asset_reload_system.MarkDirty(path);
```

原因：

Windows 文件保存通常可能触发：

```text
Modified
Modified
Rename
Size Changed
Modified
```

必须做：

```text
Watcher
↓
Dirty Queue
↓
Debounce
↓
Hash Validation
```

文件事件表示：

```text
可能发生变化
```

Hash / ArtifactKey 才决定：

```text
是否真正需要重新导入
```

---

# 17. Revision 防止异步结果乱序

必须处理连续更新。

例如：

```text
save A
save B
save C
```

A 的 BC7 compression 可能比 C 更晚完成。

因此 AssetSlot 或 update request 中维护：

```cpp
u64 _requested_revision = 0u;
```

每次 Dirty：

```cpp
const u64 revision = ++slot->_requested_revision;
```

异步任务完成：

```cpp
if (revision != slot->_requested_revision)
{
    DiscardCandidate();
    return;
}
```

确保：

```text
旧任务永远不能覆盖新版本。
```

---

# 18. Update 期间继续提供旧 Asset

状态建议：

```cpp
enum class EAssetLoadState : u8
{
    kUnloaded,
    kLoading,
    kReady,
    kUpdating,
    kFailed
};
```

Reload 时：

```text
state = Updating
```

但：

```text
slot.current
```

仍然保留旧版本。

所以：

```cpp
handle.Resolve()
```

仍然返回 Last Known Good Snapshot。

---

# 19. Reload 失败不能销毁旧版本

流程：

```text
old v12
   │
Build v13
   │
   ├─ success → Publish v13
   │
   └─ failed  → discard v13
```

失败后：

```text
slot.current = v12
slot.revision = old revision
slot.state = Ready
slot.last_update_failed = true
slot.last_error = ...
```

不要变成：

```text
current = nullptr
```

这对于：

```text
Shader
Texture
Mesh
Animation
Skeleton
```

都应统一成立。

---

# 20. GPU Asset 生命周期

Asset Snapshot 生命周期和 GPU Resource 生命周期必须分成两层。

例如：

```text
Texture v12
 └─ RhiTexture12
```

Reload：

```text
Slot
v12 → v13
```

如果 Render Thread 还持有：

```cpp
Ref<const Texture2D> v12;
```

则 v12 保持存活。

最后一个 CPU Ref 释放：

```text
Texture v12 destructor
↓
RhiTexture12 release request
↓
DeferredReleaseQueue
↓
GPU Fence
↓
真正释放
```

即：

```text
Asset lifetime
↓
Ref counting / intrusive ref

GPU lifetime
↓
Fence-based deferred destruction
```

AssetReloadSystem 不自己管理 GPU fence。

直接复用统一 Render Resource lifetime 系统。

---

# 21. Render Thread 不直接访问 AssetRegistry

不要在 draw loop 中：

```cpp
material_handle.Resolve();
texture_handle.Resolve();
shader_handle.Resolve();
```

每 draw 访问 AssetRegistry。

推荐：

```text
Game / Scene
↓
Build Render Proxy
↓
Resolve Asset Handles
↓
Capture Asset Snapshots
↓
Render Thread
```

例如：

```cpp
struct MaterialRenderData
{
    Ref<const Material> _material;
    Ref<const Shader> _shader;

    Vector<Ref<const Texture>> _textures;
};
```

一帧或一个 Render Proxy 生命周期中使用稳定 snapshot。

Reload 后：

```text
future render proxy
↓
capture new revision
```

Render Thread 不需要感知 Reload。

---

# 22. Revision-based Cache Validation

依赖缓存优先使用 revision，而不是依赖大量 Reload Event。

例如：

```cpp
struct SkeletonBinding
{
    u64 _skeleton_revision = 0u;
};
```

使用：

```cpp
const u64 revision = skeleton_handle.GetRevision();

if (_skeleton_revision != revision)
{
    RebuildBinding();
    _skeleton_revision = revision;
}
```

适用：

```text
Material binding
Shader reflection cache
PSO cache
Skeleton binding
Animation runtime cache
Descriptor cache
Render Proxy
```

这样避免 Asset Reload 后：

```text
全局广播
↓
大量 dependent 同时重建
```

---

# 23. Dependency 类型

建议增加：

```cpp
enum class EAssetDependencyType : u8
{
    kRuntime,
    kBuild,
    kBuildAndRuntime
};
```

含义：

## Runtime

依赖 Asset 内容更新后：

```text
当前 Artifact 不失效
Runtime Cache 可能需要 Refresh
```

例如：

```text
Material → Texture
```

Texture Reload 通常不需要重新 Import Material。

---

## Build

依赖变化会影响 Artifact：

```text
Dependency Changed
↓
ArtifactKey Changed
↓
Reimport
```

---

## BuildAndRuntime

两者都需要。

---

# 24. 不要无脑传播 Reload

例如：

```text
Texture
↑
Material
↑
Prefab
↑
Scene
```

Texture Reload 后禁止：

```text
Material Reload
↓
Prefab Reload
↓
Scene Reload
```

如果 Material 只保存：

```cpp
AssetHandle<Texture>
```

则 Texture Publish 后 Material 不需要任何操作。

下次 Resolve 自动得到新 Texture。

只有存在 derived cache 时才通过 revision 重建。

---

# 25. Asset Event 定位

Event 是辅助通知，不是 Asset Reload 的核心同步机制。

建议：

```cpp
struct AssetPublishedEvent
{
    AssetHandleBase _asset;

    u64 _old_revision = 0u;
    u64 _new_revision = 0u;

    EAssetUpdateReason _reason;
};
```

用途：

```text
Editor Inspector refresh
Asset Browser refresh
Preview refresh
主动 cache invalidation
debug/profiler
```

不要依赖 Event 保证 Runtime Asset 的一致性。

Runtime consistency 应依赖：

```text
Handle
+
Snapshot
+
Revision
```

---

# 26. Asset Update Reason

建议：

```cpp
enum class EAssetUpdateReason : u8
{
    kInitialLoad,
    kSourceChanged,
    kImportSettingChanged,
    kDependencyChanged,
    kManualReimport,
    kArtifactChanged
};
```

用于：

```text
Editor 显示
日志
Profiler
统计
Debug
```

不需要改变基本 Publish 机制。

---

# 27. AssetRegistry API

建议至少提供：

```cpp
class AssetRegistry
{
public:
    template<typename T>
    AssetHandle<T> GetOrCreateHandle(Guid guid);

    template<typename T>
    Ref<const T> Resolve(AssetHandle<T> handle) const;

    bool Publish(
        AssetHandleBase handle,
        Ref<const Asset> candidate,
        const AssetArtifactKey& artifact_key);

    u64 GetRevision(AssetHandleBase handle) const;

    EAssetLoadState GetState(AssetHandleBase handle) const;

    bool IsValid(AssetHandleBase handle) const;

    void Unregister(Guid guid);
};
```

Handle 本身可以内部转发到 Registry，但不要塞过多复杂逻辑。

---

# 28. Asset System 职责拆分

建议拆成以下模块。

## AssetRegistry

负责：

```text
Guid ↔ Slot
Handle
State
Revision
Current Snapshot
Dependency metadata
```

---

## AssetImporter

负责：

```text
Source
+
ImportSetting
↓
Artifact
```

---

## AssetHandler / Loader

负责：

```text
Artifact
↓
Asset Snapshot
```

---

## DerivedDataCache

负责：

```text
ArtifactKey
↓
Artifact
```

---

## AssetReloadSystem

负责：

```text
Dirty detection
Async update scheduling
Revision cancellation
Reimport
Reload
Publish
```

---

## AssetResidencySystem

未来负责：

```text
Unload
Memory budget
Streaming
LRU
```

本任务只保留接口，不要求完整实现。

---

# 29. Texture Reload 示例

```text
fish.png changed
        │
        ▼
MarkDirty(Texture Guid)
        │
        ▼
TextureImporter
        │
        ▼
BC7 TextureArtifact
        │
        ▼
TextureAssetHandler
        │
        ▼
new Texture2D v9
        │
        ▼
new RhiTexture
        │
      upload
        │
        ▼
     Publish
        │
        ▼
Slot.current
v8 → v9
```

旧：

```text
Texture2D v8
```

如果还被当前 Frame 使用，则继续存在。

最后 CPU Ref 消失后才进入 GPU DeferredRelease。

---

# 30. Shader Reload 示例

```text
shader.hlsl changed
↓
Compile candidate
↓
success?
```

成功：

```text
Shader v41
↓
Publish Shader v42
↓
revision++
↓
Material 检测 revision
↓
重建 binding / PSO
```

失败：

```text
Discard candidate
↓
继续使用 Shader v41
```

Shader 编译失败绝不能破坏当前 Scene。

---

# 31. AnimationClip Reload

AnimationClip 天然适合作为 immutable snapshot。

```text
Clip v3
├─ curves
├─ tracks
├─ events
└─ duration
```

Reload：

```text
new Clip v4
↓
Publish
```

Animator：

```text
cached revision != current revision
↓
重新建立 clip runtime state
```

不要在旧 Clip 上原地修改 tracks。

---

# 32. Skeleton Reload

Skeleton：

```text
Skeleton v7
├─ hierarchy
├─ names
├─ bind poses
└─ bone map
```

修改后：

```text
new Skeleton v8
↓
Publish
```

SkinMesh 保留：

```cpp
AssetHandle<Skeleton> _skeleton;
```

而不是强绑定旧 Skeleton C++ 地址。

需要 binding cache 时：

```text
Skeleton revision changed
↓
Rebuild skin binding
```

---

# 33. Material Reference

Material Asset 中建议保存：

```cpp
AssetHandle<Shader> _shader;
HashMap<Name, AssetHandle<Texture2D>> _textures;
```

序列化时保存对应：

```text
Guid
```

运行时 Load 后解析成 Handle。

Material 不需要强持有所有 Texture Snapshot。

Render Proxy build 时统一 Capture Snapshot。

---

# 34. Prefab / Scene 特殊处理

Prefab / Scene 不应该完全按照普通数据 Asset 的方式处理 Runtime Instance。

Asset 层：

```text
PrefabAsset v5
↓
PrefabAsset v6
```

只是 Definition 更新。

已经存在的：

```text
Prefab Instance A
Prefab Instance B
```

由单独：

```text
PrefabInstanceSystem
```

处理 reconciliation：

```text
新增组件
删除组件
属性 override
层级变化
instance runtime state
```

AssetReloadSystem 不负责直接修改 ECS 实例。

Scene 同理。

---

# 35. Asset State

推荐：

```cpp
enum class EAssetLoadState : u8
{
    kUnloaded,
    kLoading,
    kReady,
    kUpdating,
    kFailed
};
```

语义：

## Unloaded

没有 runtime snapshot。

## Loading

首次加载中。

## Ready

存在有效 snapshot。

## Updating

存在旧 snapshot，同时新版本处理中。

## Failed

首次加载失败，且没有可用 snapshot。

如果 Reload 失败但旧版本存在：

```text
state 仍为 Ready
last_update_failed = true
```

不要设成 Failed。

---

# 36. 并发原则

第一版优先正确性，不追求完全 lock-free。

允许：

```text
AssetSlot mutex
+
worker jobs
+
short Publish critical section
```

要求：

```text
Importer
Asset deserialization
CPU processing
GPU resource preparation
```

尽可能不持有 Slot lock。

Slot lock 仅用于：

```text
swap current
revision update
state update
artifact key update
```

---

# 37. 不急着实现 RCU / Hazard Pointer

目前不要为了 Asset Publish 引入：

```text
完整 RCU
Hazard Pointer
Epoch GC
Custom lock-free shared ownership
```

当前可以继续复用现有：

```text
Ref<>
```

让 Snapshot 生命周期安全。

后续 profiling 明确证明：

```text
Resolve / Publish locking
```

成为瓶颈后再升级。

---

# 38. Asset Handle Resolve 性能

运行时 Resolve 应主要为：

```text
index bounds check
slot generation check
读取 current Ref
```

禁止：

```text
每次 Guid hash lookup
文件系统访问
AssetDocument lookup
DDC lookup
```

Resolve 必须是廉价 runtime operation。

---

# 39. Source、Artifact、Snapshot 生命周期独立

明确三层：

```text
Source
↓
Authoring Data

Artifact
↓
Cooked / Derived Data

Asset Snapshot
↓
Runtime Object
```

不能混合。

例如 Texture：

```text
PNG
↓
TextureArtifact BC7
↓
Texture2D
```

Mesh：

```text
FBX
↓
MeshArtifact
↓
Mesh
```

Animation：

```text
FBX Animation
↓
AnimationClipArtifact
↓
AnimationClip
```

---

# 40. DDC 与 Reload

完整流程：

```text
Asset Dirty
↓
Build ArtifactKey
↓
DDC Lookup
```

Hit：

```text
Artifact
↓
AssetHandler
↓
Candidate
↓
Publish
```

Miss：

```text
Importer
↓
Artifact
↓
Store DDC
↓
AssetHandler
↓
Candidate
↓
Publish
```

修改回旧版本 source 时，如果旧 ArtifactKey 存在：

```text
直接 DDC Hit
```

无需重新 Import。

---

# 41. 调试与 Profiler

建议记录：

```text
Asset GUID
Asset Type
Slot Index
Revision
State
Current Artifact Key
Memory Size
Last Update Time
Last Update Reason
Last Error
Dependencies
Dependents
```

Asset Profiler 可以直接显示：

```text
Texture/Fish
revision: 18
state: Ready
snapshot refs: 4
artifact: ...
gpu memory: ...
last reload: 12 ms
```

这也方便后续 Memory Profiler 集成。

---

# 42. 迁移策略

由于允许破坏旧架构，不需要维护长期兼容层。

建议按以下顺序迁移。

## Phase 1

建立：

```text
AssetRegistry
AssetSlot
AssetHandle
SoftAssetRef
Revision
Publish
```

先使用简单资产验证。

---

## Phase 2

迁移 Texture。

将：

```cpp
Ref<Texture2D>
```

资产引用逐步改为：

```cpp
AssetHandle<Texture2D>
```

Render Proxy 中 Capture：

```cpp
Ref<const Texture2D>
```

---

## Phase 3

迁移：

```text
Shader
Material
Mesh
AnimationClip
Skeleton
```

这些最适合 immutable snapshot。

---

## Phase 4

接入：

```text
FileSystemWatcher
DDC
Reimport
Async Reload
```

---

## Phase 5

处理：

```text
Prefab
Scene
Script
```

这些有 runtime instance/state，需要独立 reconciliation。

---

# 43. 不要机械替换所有 Ref

需要区分：

## Asset Ownership Reference

应该改成：

```cpp
AssetHandle<T>
```

例如：

```text
Material → Texture
SkinMesh → Skeleton
Animator → AnimationClip
```

---

## Temporary Snapshot Reference

继续使用：

```cpp
Ref<const T>
```

例如：

```text
Render Proxy
Frame Render Data
Async Job working snapshot
```

---

## 非 Asset Runtime Object

仍然可以正常使用：

```cpp
Ref<T>
```

不要为了资产系统把整个引擎引用体系全部改成 Handle。

---

# 44. 验收测试

至少覆盖以下场景。

## Texture

```text
Texture v1 loaded
↓
修改 source
↓
Texture v2 Publish
```

验证：

```text
Handle 不变
Revision +1
旧 Snapshot 仍可用
新 Resolve 得到 v2
```

---

## Reload Failure

```text
Shader v1 valid
↓
修改出语法错误
↓
compile fails
```

验证：

```text
Shader v1 继续可用
Revision 不变
last_update_failed == true
```

---

## Rapid Save

连续：

```text
v2
v3
v4
```

确保：

```text
最终只能 Publish v4
```

即使 v2 任务最后完成。

---

## Slot Reuse

```text
Asset A
↓
delete
↓
slot reused by Asset B
```

确保旧 A Handle：

```text
IsValid() == false
```

---

## Concurrent Resolve

Thread A：

```text
Resolve v10
```

Thread B：

```text
Publish v11
```

确保：

```text
Thread A 安全继续使用 v10
Thread A 下一次 Resolve 得到 v11
```

---

## DDC Reload

已有 Artifact 时：

```text
DDC
↓
Load candidate
↓
Publish
```

确认不触发 Source Import。

---

# 45. 最终验收架构

完成后系统应接近：

```text
                     Serialized World
                           │
                          Guid
                           │
                           ▼
                    SoftAssetRef<T>
                           │
                           ▼
                     AssetRegistry
                           │
                     AssetHandle<T>
                           │
                           ▼
                       AssetSlot
                  ┌────────┼────────┐
                  │        │        │
                State   Revision  Current
                                    │
                                    ▼
                              Ref<const T>
                                    │
                             Asset Snapshot


Source
  │
  ▼
Importer
  │
  ▼
Artifact
  │
  ▼
DDC
  │
  ▼
AssetHandler
  │
  ▼
Candidate Snapshot
  │
Validate
  │
  ▼
Publish
  │
  ▼
AssetSlot.current
```

---

# 46. 最终架构原则

实现过程中优先遵循以下原则：

```text
1. GUID 是 Persistent Identity

2. AssetHandle 是 Runtime Stable Identity

3. AssetSlot 是 Runtime Mutable State

4. Asset Object 是 Immutable Snapshot

5. Reload 创建新 Snapshot，不原地修改旧 Asset

6. Publish 是 Asset Version 切换的唯一入口

7. Resolve 返回当前 Snapshot

8. Old Snapshot 通过 Ref 自然保持生命周期

9. GPU Resource 使用 Fence Deferred Release

10. Dependency Cache 使用 Revision 判断过期

11. Asset Event 只作为辅助通知

12. Source → Artifact 与 Artifact → Asset 完全分离

13. Reload 失败必须继续使用 Last Known Good Asset

14. Runtime Render Path 不直接访问 Source / DDC / Importer

15. 不为了兼容旧 Ref<Asset> 架构牺牲新模型
```

---

# 47. 本任务暂不实现

本任务不要求同时完成：

```text
Texture Streaming
Virtual Texture
Mesh Streaming
完整 Asset GC
内存 Budget 驱逐
完整 RCU
Hazard Pointer
Prefab Instance Patch
Scene Hot Reload Merge
Network Asset Reload
Remote DDC
```

但当前设计不能阻碍未来增加这些能力。

---

# 48. 最终目标

AiluEngine 的资产系统最终应从：

```text
AssetManager
↓
保存一堆 Ref<Asset>
↓
Reload 时想办法修改原对象
```

升级为：

```text
Stable Handle
↓
Versioned Slot
↓
Immutable Snapshot
↓
Atomic Publish
```

核心思想：

> 资产不是“一个永远不变地址、内部持续变化的对象”。

而是：

> **同一个 Asset Identity 下连续发布的一系列不可变版本。**

这个模型作为后续异步加载、Hot Reload、DDC、Streaming、Memory Budget 和编辑器实时迭代的统一基础。