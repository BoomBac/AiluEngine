# AiluEngine 存档系统设计与实现方案

## 1. 文档目标

本文档用于指导 AiluEngine 在当前架构基础上实现一套可扩展、可版本迁移、支持 ECS 世界状态与游戏全局状态的存档系统。

设计目标：

- 复用现有反射、Archive、Asset GUID、ECS 和 Scene 系统；
- 不直接把 Scene Asset 当作游戏存档；
- 使用稳定 GUID 标识跨加载实体；
- 支持场景对象、运行时生成对象和非 ECS 子系统状态；
- 支持组件级与文件级版本迁移；
- 支持原子写入、备份和损坏检测；
- 为后续自动存档、检查点、云存档和二进制格式保留扩展空间。

## 2. 当前引擎基础

当前 AiluEngine 已具备以下可复用能力：

- `JsonArchive`；
- `FStructedArchive`；
- `PropertyInfo::Serialize()` / `Deserialize()`；
- 反射生成的基础类型、结构体和容器序列化；
- Asset GUID 引用体系；
- ECS 稳定组件类型 ID；
- Entity 的 index + generation 运行时句柄；
- `PersistentIdComponent`；
- `Project::SavedDirectory()`；
- Scene Asset 的实体创建、组件恢复和实体重映射流程。

当前主要缺口：

1. Scene Asset 没有稳定持久化实体 GUID；
2. Scene 组件序列化仍依赖手工组件分支；
3. ECS 缺少按运行时组件类型执行 Add/Get/Remove 的统一描述；
4. 反射属性缺少 SaveGame、Transient 等序列化策略；
5. 缺少游戏级存档槽、版本、迁移和原子写入设施；
6. 缺少非 ECS 游戏系统的统一保存接口。

## 3. 核心概念划分

### 3.1 Scene Asset

Scene Asset 是编辑器创作的初始世界，负责预放置实体、默认组件数据、初始层级和资源引用。它不是玩家存档。

### 3.2 Save Game

Save Game 是玩家运行过程中的持久化状态，负责场景实体变化、运行时生成或销毁的实体、玩家状态、背包、任务、剧情、世界时间和脚本变量。

### 3.3 Runtime Snapshot

Runtime Snapshot 用于编辑器 Play 状态恢复、回滚、临时检查点、热重载和调试快照。它可以保存更多运行时字段，但通常不要求长期版本兼容。

## 4. 总体架构

```text
SaveGameManager
├── SaveSlotManager
├── WorldStateSerializer
│   ├── EntitySnapshotSerializer
│   ├── ComponentTypeRegistry
│   └── EntityReferenceResolver
├── SaveSubsystemRegistry
├── SaveMigrationRegistry
└── SaveGameArchive
```

### 4.1 SaveGameManager

```cpp
class SaveGameManager
{
public:
    static SaveGameManager &Get();

    SaveGameResult SaveSlot(StringView slot_name, const SaveGameRequest &request);
    SaveGameResult LoadSlot(StringView slot_name, const LoadGameRequest &request);

    bool HasSlot(StringView slot_name) const;
    bool DeleteSlot(StringView slot_name);
    Vector<SaveSlotInfo> EnumerateSlots() const;

private:
    SaveSlotManager _slot_manager;
    WorldStateSerializer _world_serializer;
    SaveSubsystemRegistry _subsystem_registry;
    SaveMigrationRegistry _migration_registry;
};
```

职责：

- 组织完整保存和加载流程；
- 管理当前保存/加载阶段；
- 调用世界序列化器和子系统；
- 聚合错误结果；
- 控制 Tick 暂停与恢复。

### 4.2 SaveSlotManager

负责存档目录、槽位枚举、临时文件、原子替换、备份、元数据和校验。

### 4.3 WorldStateSerializer

负责收集可持久化实体、序列化组件、创建实体、恢复组件、解析实体引用和执行 PostLoad。

### 4.4 ComponentTypeRegistry

连接 ECS 稳定组件类型 ID、反射 `Type*`、Add/Get/Remove 操作、组件版本和持久化策略。

### 4.5 SaveSubsystemRegistry

负责背包、任务、剧情、世界时间、队伍、解锁内容和全局随机种子等非 ECS 数据。

### 4.6 SaveMigrationRegistry

负责文件格式、组件和子系统版本迁移。

## 5. 存档目录

使用：

```cpp
Project::SavedDirectory()
```

推荐目录结构：

```text
<Project>/Saved/
├── SaveGames/
│   ├── slot_00/
│   │   ├── meta.json
│   │   ├── state.json
│   │   ├── thumbnail.png
│   │   └── backup/
│   │       └── state.previous.json
│   ├── slot_01/
│   └── autosave_00/
├── Checkpoints/
└── Temp/
```

第一版可以暂时不生成缩略图，但建议保留目录结构。

## 6. 存档文档结构

### 6.1 顶层 JSON

```json
{
    "_header": {
        "_magic": "AILU_SAVE_GAME",
        "_format_version": 1,
        "_build_version": "0.1.0",
        "_slot_name": "slot_00",
        "_display_name": "Chapter 2",
        "_timestamp_utc": "2026-08-03T02:00:00Z",
        "_play_time_seconds": 4821.5,
        "_active_scene": "scene-guid",
        "_checkpoint": "village_entrance",
        "_payload_hash": 0,
        "_payload_size": 0
    },
    "_world": {
        "_scenes": []
    },
    "_subsystems": {}
}
```

### 6.2 Header

```cpp
ASTRUCT()
struct SaveGameHeader
{
    GENERATED_BODY()

    APROPERTY()
    String _magic = "AILU_SAVE_GAME";

    APROPERTY()
    u32 _format_version = 1u;

    APROPERTY()
    String _build_version;

    APROPERTY()
    String _slot_name;

    APROPERTY()
    String _display_name;

    APROPERTY()
    String _timestamp_utc;

    APROPERTY()
    f64 _play_time_seconds = 0.0;

    APROPERTY()
    Guid _active_scene;

    APROPERTY()
    String _checkpoint;

    APROPERTY()
    u64 _payload_hash = 0u;

    APROPERTY()
    u64 _payload_size = 0u;
};
```

### 6.3 SaveGameDocument

```cpp
ACLASS()
class SaveGameDocument : public Object
{
    GENERATED_BODY()

public:
    APROPERTY()
    SaveGameHeader _header;

    APROPERTY()
    SaveWorldDocument _world;

    APROPERTY()
    Vector<SaveSubsystemDocument> _subsystems;
};
```

## 7. 实体稳定身份

### 7.1 禁止保存 ECS::Entity

`ECS::Entity` 是 index + generation 组成的运行时句柄，仅在当前 Registry 生命周期内有效。长期存档必须保存 `Guid`。

### 7.2 PersistentIdComponent

所有可持久化实体必须具备：

```cpp
struct PersistentIdComponent
{
    Guid _guid;

    PersistentIdComponent()
        : _guid(Guid::Generate())
    {
    }
};
```

规则：

- 新建实体：生成新 GUID；
- 加载 Scene Asset：恢复原 GUID；
- 加载 Save Game：按原 GUID 匹配实体；
- 复制实体：生成新 GUID；
- 删除后重新创建的不同对象：使用新 GUID；
- Runtime Spawn 恢复：沿用存档 GUID。

### 7.3 Scene Asset 持久化 GUID

Scene 文档增加：

```cpp
APROPERTY()
Guid _persistent_guid;
```

保存时：

```cpp
const auto *persistent_id = registry.GetComponent<ECS::PersistentIdComponent>(entity);
if (persistent_id == nullptr)
{
    LOG_ERROR("Persistent entity {} has no PersistentIdComponent", entity);
    continue;
}

entity_document._persistent_guid = persistent_id->_guid;
```

加载时：

```cpp
Guid persistent_guid = entity_document._persistent_guid;
if (!persistent_guid.IsValid())
    persistent_guid = Guid::Generate();

registry.AddComponent<ECS::PersistentIdComponent>(entity, persistent_guid);
```

旧场景无 GUID 时应自动生成并标记 Scene Dirty。

### 7.4 Scene GUID 查询

```cpp
ECS::Entity Scene::FindEntity(const Guid &guid) const;
```

推荐缓存：

```cpp
HashMap<Guid, ECS::Entity> _persistent_entity_map;
```

创建、删除、复制、Scene 加载和 PersistentId 变化时维护该映射。

## 8. 实体文档

```cpp
enum class ESavedEntityOperation : u8
{
    kModify,
    kSpawn,
    kDestroy,
};
```

```cpp
ASTRUCT()
struct SaveEntityDocument
{
    GENERATED_BODY()

    APROPERTY()
    Guid _entity_guid;

    APROPERTY()
    Guid _scene_guid;

    APROPERTY()
    Guid _parent_guid;

    APROPERTY()
    i32 _sibling_index = 0;

    APROPERTY()
    Guid _prefab_guid;

    APROPERTY()
    ESavedEntityOperation _operation = ESavedEntityOperation::kModify;

    APROPERTY()
    Vector<SaveComponentDocument> _components;
};
```

层级只保存 `_parent_guid` 与 `_sibling_index`，不保存 `_first_child`、`_prev_sibling`、`_next_sibling` 和 `_children_num`。这些内部派生数据在加载后通过 Scene 层级 API 重建。

## 9. 实体持久化分类

```cpp
enum class EEntityPersistence : u8
{
    kNone,
    kScenePlaced,
    kRuntimeSpawned,
    kSessionOnly,
};
```

可通过组件描述：

```cpp
struct SaveGameEntityComponent
{
    EEntityPersistence _persistence = EEntityPersistence::kScenePlaced;
    bool _save_transform = true;
};
```

### 9.1 ScenePlaced

门、宝箱、NPC、机关和可交互物。它们存在于 Scene Asset 中，存档保存运行时变化。

### 9.2 RuntimeSpawned

掉落物、建造物、玩家放置对象、动态 NPC 等。需要保存 Entity GUID、Prefab GUID、Transform 和 SaveGame 组件。

### 9.3 SessionOnly

粒子、音效代理、伤害数字、临时渲染实体、普通弹道等，不进入存档。

## 10. 组件描述注册表

### 10.1 统一描述

```cpp
struct ComponentTypeDescriptor
{
    ECS::StableComponentTypeId _stable_type_id = 0u;
    String _stable_name;
    const Type *_type = nullptr;

    ComponentOperations _operations;
    ComponentEditorDescriptor _editor;
    ComponentPersistenceDescriptor _persistence;
};
```

Object Detail、Scene Serializer 和 SaveGame 共用该注册表。

### 10.2 类型擦除操作

```cpp
struct ComponentOperations
{
    std::function<bool(ECS::Register &, ECS::Entity)> _has;
    std::function<void *(ECS::Register &, ECS::Entity)> _get;
    std::function<void *(ECS::Register &, ECS::Entity)> _add;
    std::function<void(ECS::Register &, ECS::Entity)> _remove;
};
```

```cpp
template<typename T>
ComponentOperations MakeComponentOperations()
{
    return {
        ._has = [](ECS::Register &registry, ECS::Entity entity)
        {
            return registry.HasComponent<T>(entity);
        },
        ._get = [](ECS::Register &registry, ECS::Entity entity) -> void *
        {
            return registry.GetComponent<T>(entity);
        },
        ._add = [](ECS::Register &registry, ECS::Entity entity) -> void *
        {
            return &registry.AddComponent<T>(entity);
        },
        ._remove = [](ECS::Register &registry, ECS::Entity entity)
        {
            registry.RemoveComponent<T>(entity);
        },
    };
}
```

### 10.3 持久化策略

```cpp
enum class EComponentPersistence : u8
{
    kNone,
    kScene,
    kSaveGame,
    kSceneAndSaveGame,
    kRuntimeSnapshot,
};
```

```cpp
struct ComponentPersistenceDescriptor
{
    EComponentPersistence _persistence = EComponentPersistence::kNone;
    u32 _version = 1u;

    std::function<bool(void *, FStructedArchive &, const SerializationContext &)> _save;
    std::function<bool(void *, FStructedArchive &, const SerializationContext &, u32)> _load;
    std::function<void(void *, const SaveLoadContext &)> _post_load;
};
```

### 10.4 注册示例

```cpp
RegisterComponentType<ECS::TransformComponent>({
    ._stable_name = "Ailu.ECS.TransformComponent",
    ._persistence = {
        ._persistence = EComponentPersistence::kSceneAndSaveGame,
        ._version = 1u,
    },
});
```

## 11. 反射属性持久化策略

### 11.1 SaveGame 使用白名单

不能默认保存所有 `APROPERTY`。

```cpp
APROPERTY(SaveGame)
f32 _health = 100.0f;

APROPERTY(Transient)
Matrix4x4f _cached_matrix;
```

### 11.2 Property Flags

```cpp
enum EPropertyFlags : u32
{
    kPropertyNone = 0u,
    kPropertySerialize = 1u << 0u,
    kPropertySaveGame = 1u << 1u,
    kPropertyTransient = 1u << 2u,
    kPropertyEditorOnly = 1u << 3u,
    kPropertyRuntimeOnly = 1u << 4u,
    kPropertyDeprecated = 1u << 5u,
};
```

`PropertyInfo` 增加：

```cpp
bool HasFlag(EPropertyFlags flag) const;
```

### 11.3 建议语义

| 属性标记 | Scene Asset | Save Game | Runtime Snapshot |
|---|---:|---:|---:|
| 普通 Serialize | 是 | 否 | 是 |
| SaveGame | 可选 | 是 | 是 |
| Transient | 否 | 否 | 否 |
| EditorOnly | 是 | 否 | 否 |
| RuntimeOnly | 否 | 可选 | 是 |
| Deprecated | 只读取 | 只读取 | 否 |

### 11.4 序列化用途

```cpp
enum class ESerializationPurpose : u8
{
    kAsset,
    kScene,
    kSaveGame,
    kRuntimeSnapshot,
    kNetwork,
};
```

```cpp
struct SerializationContext
{
    ESerializationPurpose _purpose = ESerializationPurpose::kAsset;
    u32 _document_version = 0u;

    ResourceMgr *_resource_mgr = nullptr;
    SceneManagement::Scene *_scene = nullptr;
};
```

Archive 应暴露：

```cpp
const SerializationContext &FArchive::Context() const;
```

## 12. 组件文档格式

```cpp
ASTRUCT()
struct SaveComponentDocument
{
    GENERATED_BODY()

    APROPERTY()
    String _type_name;

    APROPERTY()
    u64 _stable_type_id = 0u;

    APROPERTY()
    u32 _version = 1u;

    JsonValue _data;
};
```

推荐 JSON：

```json
{
    "_type_name": "Ailu.ECS.TransformComponent",
    "_stable_type_id": 123456789,
    "_version": 1,
    "_data": {
        "_local_transform": {
            "_position": [0, 1, 0],
            "_rotation": [0, 0, 0, 1],
            "_scale": [1, 1, 1]
        }
    }
}
```

不要把 `_data` 再编码为 JSON 字符串，避免双重 JSON。

## 13. 默认反射组件序列化

```cpp
bool SaveReflectedComponent(void *component,
                            const Type *type,
                            FStructedArchive &archive,
                            const SerializationContext &context)
{
    for (const PropertyInfo &property : type->GetProperties())
    {
        if (context._purpose == ESerializationPurpose::kSaveGame &&
            !property.HasFlag(kPropertySaveGame))
        {
            continue;
        }

        if (property.HasFlag(kPropertyTransient))
            continue;

        property.Serialize(component, archive);
    }

    return true;
}
```

需要递归遍历基类属性。

## 14. 特殊组件处理

### 14.1 TransformComponent

只保存 Local Position、Local Rotation 和 Local Scale。

不保存矩阵、上一帧矩阵、版本号、Dirty Flag 和缓存世界坐标。

加载后：

```cpp
component->_local_dirty = true;
component->_world_dirty = true;
component->_world_to_local_dirty = true;
```

由 `TransformSystem` 重建派生数据。

### 14.2 CameraComponent

保存 Projection Type、Near、Far、FOV、Orthographic Size、Clear Flags 和 Culling Mask。

不保存投影矩阵、视图矩阵、Frustum Cache 和临时 Render Target。

加载后调用正式 setter 或标记投影缓存 Dirty。

### 14.3 ScriptComponent

保存脚本 Asset GUID 或稳定脚本路径，只用于恢复脚本类型。运行时变量由脚本显式接口控制：

```lua
function OnSave()
    return {
        health = self.health,
        quest_state = self.quest_state
    }
end

function OnLoad(data)
    self.health = data.health
    self.quest_state = data.quest_state
end
```

第一版不要自动保存整个 Lua Table。

### 14.4 StaticMeshComponent / SpriteRendererComponent

保存 Mesh、Sprite、Material 的 GUID 和必要的逻辑属性。

不保存裸指针、GPU Resource、Descriptor、Render Proxy 和临时 Material Instance。

### 14.5 Physics Components

保存逻辑状态，例如 Transform、Linear Velocity、Angular Velocity 和 Awake 状态。加载后重新创建 Physics Body，再应用速度。

## 15. Asset 引用

所有资源引用保存 GUID。

禁止保存裸资源指针、系统绝对路径、临时加载地址和 GPU Handle。

```cpp
template<typename T>
struct AssetRef
{
    Guid _guid;

    Ref<T> Resolve(ResourceMgr &resource_mgr) const;
};
```

资源缺失时应记录日志并使用默认资源或空引用。只有 Required 资源缺失才导致加载失败。

## 16. Entity 引用

所有跨实体引用保存 GUID。

```cpp
struct EntityRef
{
    Guid _guid = Guid::EmptyGuid();
    mutable ECS::Entity _cached_entity = ECS::kInvalidEntity;
    mutable u64 _cached_scene_revision = 0u;

    ECS::Entity Resolve(const SceneManagement::Scene &scene) const;
    bool IsValid() const;
};
```

适用于 Parent、AI Target、Quest Target、Trigger Owner、Interaction Target 和 Attached Entity。

## 17. 世界存档模型

长期推荐：

```text
Scene Asset 基线 + SaveGame 运行时差异
```

加载顺序：

1. 加载 Scene Asset；
2. 建立 Persistent GUID 映射；
3. 应用 `kDestroy`；
4. 应用 `kModify`；
5. 创建 `kSpawn`；
6. 恢复组件；
7. 重建层级；
8. 恢复子系统；
9. 执行 PostLoad。

第一版可以完整保存所有标记实体，但文档结构应保留 `_operation`。

## 18. 非 ECS 子系统

```cpp
class ISaveGameSubsystem
{
public:
    virtual ~ISaveGameSubsystem() = default;

    virtual StringView SaveKey() const = 0;
    virtual u32 SaveVersion() const = 0;

    virtual void BeforeSave(const SaveGameContext &context)
    {
    }

    virtual void Save(FStructedArchive &archive) = 0;
    virtual void Load(FStructedArchive &archive, u32 version) = 0;

    virtual void AfterLoad(const SaveGameContext &context)
    {
    }
};
```

未知 Subsystem 记录 Warning 后跳过，不阻断整个存档加载。

## 19. 保存流程

```text
1. 进入 Saving 状态
2. 暂停结构性世界修改
3. Flush DeferredDestroy
4. Process Scene Commands
5. 调用 Subsystem::BeforeSave
6. 收集 SaveGameDocument
7. 序列化世界
8. 序列化 Subsystem
9. 写入临时文件
10. 重新读取并校验
11. 旧文件移动为备份
12. 临时文件原子替换正式文件
13. 退出 Saving 状态
```

世界快照收集必须在主线程完成。

## 20. 加载流程

```text
1. 进入 Loading 状态
2. 读取文件
3. 验证 magic
4. 验证 size/hash
5. 检查格式版本
6. 执行文件迁移
7. 停止当前世界 Tick
8. 清理当前运行状态
9. 加载基础 Scene Asset
10. 第一遍创建运行时实体
11. 第二遍创建/恢复组件
12. 第三遍解析 EntityRef 和层级
13. 恢复 Subsystem
14. 调用 Component PostLoad
15. 重建 Transform / Physics / Render
16. 调用 Subsystem::AfterLoad
17. 恢复世界 Tick
18. 退出 Loading 状态
```

```cpp
enum class ESaveLoadPhase : u8
{
    kIdle,
    kPreparing,
    kLoadingScene,
    kCreatingEntities,
    kLoadingComponents,
    kResolvingReferences,
    kLoadingSubsystems,
    kPostLoad,
};
```

## 21. 多阶段世界恢复

### 第一遍：实体

```cpp
HashMap<Guid, ECS::Entity> guid_to_entity;
```

ScenePlaced 从 Scene 查找，RuntimeSpawned 创建新实体，Destroy 记录待删除对象。

### 第二遍：组件

按 Component Descriptor 查找类型、检查版本、Add/Get 组件并调用 Load。

### 第三遍：引用与层级

解析 EntityRef、Parent、Sibling Order、AI Target 和脚本对象引用。

### 第四遍：PostLoad

重建缓存、Render Proxy、Physics Body、Script Runtime 和事件连接。

## 22. 原子写入

禁止直接覆盖正式存档。

```text
state.json
    ↓
state.tmp
    ↓ 完整写入并重新读取校验
state.json → state.previous.json
state.tmp → state.json
```

Windows：

```cpp
MoveFileExW(
    temp_path.c_str(),
    target_path.c_str(),
    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
```

第一版可使用 xxHash64 或 CRC32。

## 23. 错误处理

```cpp
enum class ESaveGameError : u8
{
    kNone,
    kSlotNotFound,
    kOpenFailed,
    kReadFailed,
    kWriteFailed,
    kParseFailed,
    kInvalidMagic,
    kUnsupportedVersion,
    kHashMismatch,
    kSceneLoadFailed,
    kMissingRequiredAsset,
    kComponentLoadFailed,
    kSubsystemLoadFailed,
    kAtomicReplaceFailed,
};
```

```cpp
struct SaveGameResult
{
    bool _success = false;
    ESaveGameError _error = ESaveGameError::kNone;
    String _message;
};
```

不要只返回 `bool`。

## 24. 版本与迁移

需要三层版本：

- 文件格式版本；
- 组件版本；
- 子系统版本。

```cpp
class ISaveMigration
{
public:
    virtual ~ISaveMigration() = default;

    virtual u32 FromVersion() const = 0;
    virtual u32 ToVersion() const = 0;
    virtual bool Migrate(JsonValue &document) const = 0;
};
```

迁移按顺序执行：

```text
V1 → V2 → V3
```

字段改名、类型变化、Enum 变化、组件拆分或合并都必须显式迁移。

## 25. SceneAssetHandler 迁移方向

最终 SceneAssetHandler 只负责组装上下文并调用通用 SceneSerializer：

```cpp
bool SceneAssetHandler::Save(const AssetSaveContext &context)
{
    auto *scene = context._asset->As<Scene>();
    if (scene == nullptr)
        return false;

    SceneAssetDocument document;
    document._header = MakeAssetDocumentHeader(context._asset);

    SerializationContext serialization_context{
        ._purpose = ESerializationPurpose::kScene,
        ._resource_mgr = context._resource_mgr,
        ._scene = scene,
    };

    return SceneSerializer::Get().Save(*scene, document, serialization_context);
}
```

Scene 与 SaveGame 共用 Persistent GUID、ComponentTypeDescriptor、反射属性筛选、AssetRef、EntityRef、PostLoad 和组件版本。

## 26. 线程模型

第一版：

- 世界快照收集：主线程；
- JSON 编码：可后续放后台；
- 文件写入：可后续放后台；
- ECS 遍历期间禁止并发结构修改。

正确的异步演进方式：主线程先生成纯 `SaveGameDocument`，后台线程只做编码和磁盘写入。

## 27. 建议目录结构

```text
Engine/
├── Inc/
│   └── SaveGame/
│       ├── SaveGameManager.h
│       ├── SaveGameDocument.h
│       ├── SaveGameTypes.h
│       ├── SaveSlotManager.h
│       ├── WorldStateSerializer.h
│       ├── ComponentTypeDescriptor.h
│       ├── ComponentTypeRegistry.h
│       ├── SaveSubsystem.h
│       ├── SaveSubsystemRegistry.h
│       ├── SaveMigration.h
│       ├── SaveMigrationRegistry.h
│       ├── EntityRef.h
│       └── AssetRef.h
└── Src/
    └── SaveGame/
        ├── SaveGameManager.cpp
        ├── SaveSlotManager.cpp
        ├── WorldStateSerializer.cpp
        ├── ComponentTypeRegistry.cpp
        ├── SaveSubsystemRegistry.cpp
        ├── SaveMigrationRegistry.cpp
        ├── EntityRef.cpp
        └── AssetRef.cpp
```

## 28. 分阶段实施计划

### 阶段一：稳定实体身份

- Scene Asset 保存和恢复 Persistent GUID；
- 实现 `Scene::FindEntity(Guid)`；
- Duplicate 生成新 GUID；
- 层级逐步切换到 GUID；
- 旧 Scene 自动迁移。

验收：重启 Editor、保存并重开 Scene 后 GUID 不变。

### 阶段二：统一组件描述

- 实现 ComponentTypeDescriptor；
- 注册 stable ID、Type、Add/Get/Remove；
- 添加 Scene/SaveGame 策略和版本；
- Object Detail、Scene Serializer、SaveGame 共用 Registry。

### 阶段三：反射持久化标记

- 增加 EPropertyFlags；
- 增加 SaveGame/Transient；
- 增加 SerializationPurpose；
- 默认反射保存器按用途筛选属性。

### 阶段四：基础 SaveGame

- SaveGameManager；
- SaveSlotManager；
- Header 和 Document；
- 世界完整保存；
- 原子写入；
- Hash 校验；
- 槽位枚举和删除。

### 阶段五：引用与特殊组件

- EntityRef；
- AssetRef；
- 多阶段加载；
- Transform、Camera、Render、Physics、Script 特殊处理。

### 阶段六：SaveSubsystem

- Quest；
- Inventory；
- WorldTime；
- 子系统版本迁移。

### 阶段七：高级能力

- 差异存档；
- Runtime Spawn；
- Destroy 标记；
- 自动存档轮换；
- Checkpoint；
- Thumbnail；
- 后台编码与写入；
- 压缩和云同步。

## 29. 第一版建议范围

第一版只实现：

- 单场景；
- 手动存档槽；
- JSON；
- Persistent GUID；
- Transform；
- 玩家状态；
- 一个 Runtime Spawn 测试实体；
- 一个测试 SaveSubsystem；
- 原子写入；
- 文件版本；
- Hash 校验。

暂不实现云存档、加密、增量块、多场景、自动保存轮换、后台 ECS 快照、Prefab 差异和 Lua Table 自动反射。

## 30. 禁止事项

- 保存 `ECS::Entity` 作为长期引用；
- memcpy 整个 Component；
- 保存裸资源指针；
- 保存 GPU 或物理内部对象；
- 保存 Transform 派生矩阵；
- 默认保存全部 `APROPERTY`；
- 直接覆盖正式存档；
- 后台线程直接遍历活动 ECS；
- 把玩家存档写回 Scene Asset；
- 用默认成员值代替显式版本迁移；
- 创建重复的 Editor、Scene 和 SaveGame 组件 Registry。

## 31. 测试计划

### Persistent GUID

- Scene 保存/加载后 GUID 不变；
- Duplicate 生成新 GUID；
- 删除后新建不复用；
- 旧 Scene 自动补 GUID。

### 基础存档

- 空场景；
- 单实体和多实体；
- 父子层级；
- Runtime Spawn；
- Destroy；
- 多组件。

### 引用

- EntityRef 正常；
- 指向已销毁实体；
- AssetRef 正常；
- Asset 缺失；
- 循环实体引用。

### 版本

- 当前版本；
- V1 到 V2；
- 未来版本拒绝；
- 组件旧版本；
- Subsystem 旧版本。

### 文件安全

- 写入中断；
- 临时文件残留；
- 主文件损坏；
- Previous 恢复；
- Hash 不匹配；
- 磁盘空间不足；
- 目录无权限。

## 32. 验收标准

1. Scene 中可持久化实体具有稳定 GUID；
2. 存档不包含运行时 Entity ID；
3. Entity 与 Asset 引用通过 GUID 恢复；
4. SaveGame 属性使用显式白名单；
5. Transient 和派生缓存不保存；
6. 组件与 Subsystem 支持独立版本；
7. 正式存档采用原子替换；
8. 损坏文件不会覆盖上一份有效存档；
9. 加载分为创建实体、恢复组件、解析引用和 PostLoad；
10. 新增普通反射组件不需要修改 SaveGameManager；
11. Scene 和 SaveGame 共享 ComponentTypeDescriptor；
12. 存档失败具有明确错误码和日志。

## 33. 最终原则

```text
Scene Asset
    提供初始世界基线

PersistentIdComponent
    提供实体稳定身份

ComponentTypeDescriptor
    连接 ECS、反射、编辑器和持久化

WorldStateSerializer
    保存实体与组件状态

AssetRef / EntityRef
    提供稳定跨加载引用

ISaveGameSubsystem
    保存非 ECS 游戏状态

SaveMigrationRegistry
    保证长期兼容

SaveSlotManager
    保证文件安全
```

最优先的工作不是存档 UI，而是确保每个可持久化场景实体的 `PersistentIdComponent::_guid` 能在 Scene Asset、运行时和 Save Game 之间保持稳定。
