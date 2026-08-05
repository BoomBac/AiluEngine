# AiluEngine Entity GUID 应用重构方案

## 1. 文档目标

把已经存在的 `ECS::PersistentIdComponent` 真正接入场景实体生命周期，使 Entity GUID 成为实体跨保存、加载和重建后的稳定身份。

本方案基于 GitHub `BoomBac/AiluEngine` 的 `master` 分支，检查基线提交：

```text
c31eda6f49d4ecd9d3c702731d3ac6f42f473f36
```

本次重构必须保持以下边界：

- `ECS::Entity` 继续作为运行时高频句柄，保留现有 `index + generation` 设计；
- `Guid` 只用于持久化边界、跨生命周期引用和编辑器诊断；
- Entity GUID 独立随机生成，不允许根据组件属性、名称、Transform 或资源引用计算；
- 复制 Entity 必须生成新 GUID；
- 保存再加载必须保留原 GUID；
- 不把 GUID 引入渲染、Transform、Physics 等热路径。

---

## 2. 当前代码结论

### 2.1 已有基础

`Engine/Inc/Scene/Component.h` 已有：

```cpp
struct AILU_API PersistentIdComponent
{
    DECLARE_COMPONENT(PersistentIdComponent, "Ailu.ECS.PersistentIdComponent")
    Guid _guid;

    PersistentIdComponent() : _guid(Guid::Generate()) {}
    explicit PersistentIdComponent(Guid guid) : _guid(std::move(guid)) {}
};
```

`Scene::AddObject()` 的三个重载和 `Scene::DuplicateEntity()` 已添加该组件，其中复制路径会生成新 GUID。

### 2.2 当前缺口

1. `SceneAssetHandler::Load()` 直接调用 `reg.Create()`，没有恢复或添加 `PersistentIdComponent`；
2. `SceneAssetHandler::Save()` 没有保存 Entity GUID；
3. `SceneEntityDocument` 使用 `_entity_id` 保存运行时 `ECS::Entity`；
4. `SceneHierarchyComponentDocument` 保存 `_parent/_first_child/_prev_sibling/_next_sibling` 等运行时句柄；
5. 场景内没有 `Guid -> Entity` 查询表；
6. 加载后同一个逻辑实体会获得新运行时句柄，同时也丢失原 GUID；
7. 当前场景文件暴露了 ECS 层级链表的内部实现，未来修改 `CHierarchy` 会直接影响文件格式；
8. `SceneMgr::EnterPlayMode()` 使用 `new Scene(*_p_current)` 复制场景，新增身份索引时必须兼容该复制路径；
9. `Guid` 当前缺少统一的有效性判断和通用哈希器。

### 2.3 最终身份模型

```text
Scene 文件 / 存档 / Prefab / 脚本持久引用
                    │
                    ▼
               Entity Guid
                    │
            Scene::FindEntity()
                    │
                    ▼
       ECS::Entity(index + generation)
                    │
                    ▼
         ECS / Render / Physics 热路径
```

Entity GUID 是持久身份，`ECS::Entity` 是当前 Scene 实例中的临时句柄，两者不能互相替代。

---

## 3. 最终场景格式

新格式只保存稳定身份和逻辑层级关系，不保存运行时句柄或层级链表节点。

```json
{
    "_scene_format_version": 2,
    "_entities": [
        {
            "_entity_guid": "3a205637-4414-49a5-a1e9-f7dc04549723",
            "_tag_component": {
                "_name": "Player",
                "_layer_mask": 0
            },
            "_transform_component": {
                "_position": [0, 0, 0],
                "_rotation": [0, 0, 0, 1],
                "_scale": [1, 1, 1]
            },
            "_hierarchy_component": {
                "_parent_guid": "null",
                "_sibling_index": 0,
                "_inv_matrix_attach": "..."
            }
        }
    ]
}
```

要求：

- `_entity_id` 不再写入新文件；
- `_parent/_first_child/_prev_sibling/_next_sibling/_children_num` 不再写入新文件；
- `_parent_guid` 表示逻辑父节点；
- `_sibling_index` 保留同一父节点下的显示与遍历顺序；
- `CHierarchy` 的链表字段在加载时通过 `Scene::Reparent()` 重建；
- 旧字段只用于兼容读取，不再输出。

不要修改全局 `kSerializedAssetDocumentVersion`。在 `SceneAssetDocument` 内增加独立的场景格式版本，避免所有 Asset 因场景格式变化一起失效。

---

# 4. 实现任务包

## 任务包 1：完善 Guid 基础能力

### 目标

让 `Guid` 可以安全地作为场景索引键，并统一判断空值或非法值。

### 修改文件

```text
Engine/Inc/Framework/Math/Guid.h
Engine/Src/Framework/Math/Guid.cpp
Engine/Inc/Graph/GraphDocument.h
Engine/Src/Graph/GraphDocument.cpp
```

### 实现内容

1. 为 `Guid` 增加：

```cpp
bool IsEmpty() const;
bool IsValid() const;
```

2. `IsEmpty()` 至少兼容：

```text
空字符串
Guid::EmptyGuid() 当前使用的 "null"
```

3. 增加引擎通用哈希器：

```cpp
struct AILU_API GuidHasher
{
    usize operator()(const Guid &guid) const noexcept;
};
```

4. `GuidHasher` 直接哈希 `guid.ToString()`，不要自行解析 UUID 位段；
5. 用通用 `GuidHasher` 替换 `GraphGuidHasher`，避免重复实现；
6. 不修改现有 GUID 字符串格式，不修改 `Guid::EmptyGuid()` 的序列化表现。

### 验收标准

- `HashMap<Guid, ECS::Entity, GuidHasher>` 可以编译和使用；
- 空字符串与 `"null"` 都被识别为空 GUID；
- 已有 Asset GUID 和 Graph GUID 文件保持兼容。

---

## 任务包 2：为 Scene 增加 Entity GUID 索引

### 目标

让 Scene 提供稳定身份查询，建立以下双向关系中的核心方向：

```text
Guid -> ECS::Entity
ECS::Entity -> PersistentIdComponent::_guid
```

反向查询直接读取组件，不额外维护第二张表。

### 修改文件

```text
Engine/Inc/Scene/Scene.h
Engine/Src/Scene/Scene.cpp
```

### 建议接口

```cpp
public:
    ECS::Entity FindEntity(const Guid &guid) const;
    const Guid &GetEntityGuid(ECS::Entity entity) const;
    bool HasEntityGuid(const Guid &guid) const;
    bool ValidateEntityGuidIndex() const;

private:
    Guid GenerateUniqueEntityGuid() const;
    bool RegisterEntityGuid(ECS::Entity entity, const Guid &guid);
    void UnregisterEntityGuid(ECS::Entity entity);
    void RebuildEntityGuidIndex();

private:
    HashMap<Guid, ECS::Entity, GuidHasher> _guid_to_entity;
```

### 实现规则

1. `FindEntity()` 找不到、GUID 为空或句柄已失效时返回 `ECS::kInvalidEntity`；
2. `GetEntityGuid()` 对非法 Entity 返回 `Guid::EmptyGuid()`；
3. `RegisterEntityGuid()` 必须拒绝：
   - 空 GUID；
   - 已被其他存活 Entity 使用的 GUID；
   - 非存活 Entity；
4. `GenerateUniqueEntityGuid()` 使用 `Guid::Generate()`，并检查 `_guid_to_entity` 冲突；
5. `ValidateEntityGuidIndex()` 同时检查：
   - 所有索引项指向存活 Entity；
   - Entity 拥有 `PersistentIdComponent`；
   - 组件 GUID 与索引键一致；
   - 不存在重复 GUID；
6. `RebuildEntityGuidIndex()` 用于加载完成、调试恢复和场景复制后的防御性重建。

### 重要限制

不要通过 `RegisterOnComponentAdd<PersistentIdComponent>()` 注册捕获 `this` 的回调。

当前 `ECS::Register` 的复制构造会复制组件回调，而 `SceneMgr::EnterPlayMode()` 会复制整个 Scene。回调捕获原 Scene 指针后会在运行时副本中指向错误对象。第一版直接由 Scene 生命周期函数维护索引，更简单且安全。

### 验收标准

- 任意存活场景 Entity 可通过 GUID O(1) 找回；
- 删除 Entity 后 GUID 查询立即失效；
- Play Mode 场景副本中的索引与副本 Register 一致；
- Debug 构建中 `ValidateEntityGuidIndex()` 可发现人为破坏的数据。

---

## 任务包 3：统一 Entity 创建与销毁入口

### 目标

保证所有 Scene Entity 从创建开始就具备唯一 `PersistentIdComponent`，不再由调用方手动拼装基础组件。

### 修改文件

```text
Engine/Inc/Scene/Scene.h
Engine/Src/Scene/Scene.cpp
Engine/Src/Assets/AssetHandlers.cpp
```

### 建议内部接口

```cpp
private:
    ECS::Entity CreateEntityInternal(String name, const Guid &requested_guid);
```

约定：

- `requested_guid.IsEmpty()`：新建 Entity，生成新 GUID；
- `requested_guid.IsValid()`：场景加载，保留指定 GUID；
- GUID 冲突：由调用方先决定报错或修复，不允许静默覆盖索引。

### 实现内容

1. `CreateEntityInternal()` 统一创建基础组件：

```text
PersistentIdComponent
TagComponent
TransformComponent
CHierarchy
```

2. `Scene::AddObject()` 三个重载改为调用该函数；
3. `Scene::DuplicateEntity()` 调用该函数且不传源 Entity GUID；
4. `SceneAssetHandler::Load()` 不再直接调用 `reg.Create()`；
5. `DeletePendingEntities()` 在 `_register.Destory(actor)` 前调用 `UnregisterEntityGuid(actor)`；
6. `Scene::Clear()` 同时清空 `_guid_to_entity`；
7. 保留 `GetRegister()` 用于组件操作，但禁止新增业务代码绕过 Scene 直接创建场景 Entity；
8. `PersistentIdComponent` 继续禁止在 Inspector 中添加和删除。

### 验收标准

- 搜索 Scene 与 AssetHandler 代码后，场景实体创建只剩统一入口；
- 所有 `AddObject()` 返回的 Entity 都拥有有效 GUID；
- Duplicate 后源和目标 GUID 不同；
- 删除父节点及其子树后，所有对应 GUID 都无法查询。

---

## 任务包 4：新增 Scene Document V2

### 目标

让场景文件使用 GUID，而不是运行时 Entity 句柄。

### 修改文件

```text
Engine/Inc/Assets/AssetDocument.h
Engine/Inc/Assets/generated/AssetDocument.gen.h
Engine/Inc/Assets/generated/AssetDocument.gen.cpp
```

生成文件应通过现有反射代码生成流程更新，不要手工长期维护。

### 数据结构调整

`SceneAssetDocument` 增加：

```cpp
inline static constexpr u32 kCurrentSceneFormatVersion = 2u;

APROPERTY()
u32 _scene_format_version = 1u;
```

默认值设为 `1u`，这样旧场景缺少该字段时会自然进入迁移路径；保存时显式写入 `2u`。

`SceneEntityDocument` 增加：

```cpp
Guid _entity_guid;
```

`SceneHierarchyComponentDocument` 新格式字段：

```cpp
Guid _parent_guid;
u32 _sibling_index = 0u;
String _inv_matrix_attach;
```

保留以下旧字段作为反序列化兼容数据，但新格式不再写出：

```text
SceneEntityDocument::_entity_id
SceneHierarchyComponentDocument::_first_child
SceneHierarchyComponentDocument::_prev_sibling
SceneHierarchyComponentDocument::_next_sibling
SceneHierarchyComponentDocument::_parent
SceneHierarchyComponentDocument::_children_num
```

### 序列化要求

1. 为 `SceneEntityDocument` 和 `SceneHierarchyComponentDocument` 使用自定义 `Serialize/Deserialize`；
2. `Serialize()` 只写 V2 字段；
3. `Deserialize()` 使用 `JsonArchive::HasField()` 判断 V2 或旧格式；
4. 不依赖全局 Asset 格式版本判断场景层级格式；
5. 不把 `_has_xxx_component` 标记重新写回，新格式继续使用“字段存在即表示组件存在”的当前方案。

### 验收标准

- 新保存的场景 JSON 中不存在 `_entity_id`；
- 新保存的层级数据中不存在任何运行时 Entity 数值；
- 旧场景仍可被反序列化到兼容字段；
- 生成代码正常编译。

---

## 任务包 5：重写场景加载流程

### 目标

以 GUID 为主键加载场景，并通过 Scene API 重建运行时 Entity 与层级。

### 修改文件

```text
Engine/Src/Assets/AssetHandlers.cpp
Engine/Inc/Scene/Scene.h
Engine/Src/Scene/Scene.cpp
```

### V2 加载流程

#### 第一遍：创建 Entity 与身份索引

对每个 `SceneEntityDocument`：

1. 校验 `_entity_guid`；
2. GUID 为空时生成新 GUID并记录修复日志；
3. GUID 重复时：
   - 第一个 Entity 保留原 GUID；
   - 后续冲突 Entity 生成新 GUID；
   - 记录明确错误日志；
4. 调用 Scene 统一创建入口；
5. 建立文档 GUID 到运行时 Entity 的映射；
6. 加载 Tag 基础数据。

#### 第二遍：加载业务组件

复用现有组件加载逻辑，但通过当前 Entity GUID 找到运行时 Entity，不再使用 `_entity_id`。

#### 第三遍：重建层级

1. 所有 Entity 已经由统一入口拥有默认 `CHierarchy`；
2. 按 `_parent_guid` 分组；
3. 每组按 `_sibling_index` 稳定排序；
4. 依次调用：

```cpp
scene->Reparent(child, parent, false);
```

5. 父 GUID 为空时保持根节点；
6. 父 GUID 不存在、自引用或形成环时：
   - 当前节点降级为根节点；
   - 输出错误日志；
   - 不允许加载崩溃；
7. `Reparent()` 完成后，如仍需保留旧 `_inv_matrix_attach`，再覆盖该字段。

不要直接写 `CHierarchy` 的链表字段。

### V1 旧场景迁移流程

1. 第一遍按旧 `_entity_id` 建立：

```text
legacy entity id -> new ECS::Entity
legacy entity id -> generated Guid
```

2. 每个旧 Entity 生成一次 GUID；
3. 根据旧 `_parent` 解析父节点；
4. 根据每个父节点的 `_first_child -> _next_sibling` 链推导 `_sibling_index`；
5. 使用与 V2 相同的 `Reparent()` 流程重建层级；
6. 标记场景为 Dirty，使下一次保存自动升级为 V2；
7. 迁移只发生在内存中，不在加载过程中直接覆盖原文件。

### 验收标准

- V2 场景加载后 Entity GUID 与文件完全一致；
- V1 场景可以加载，保存后自动转为 V2；
- 重复 GUID、丢失父节点、环形父子关系不会导致崩溃；
- 层级顺序与保存前一致；
- 加载代码不再把运行时句柄直接写入 `CHierarchy`。

---

## 任务包 6：重写场景保存流程

### 目标

保存前验证 Entity 身份，输出确定性的 V2 场景文件。

### 修改文件

```text
Engine/Src/Assets/AssetHandlers.cpp
```

### 实现内容

1. 保存前调用 `ValidateEntityGuidIndex()`；
2. Release 构建发现缺失 GUID 时执行修复并输出错误日志，Debug 构建额外触发断言；
3. `SceneEntityDocument::_entity_guid` 来自 `PersistentIdComponent`；
4. 层级只保存：

```text
_parent_guid
_sibling_index
_inv_matrix_attach
```

5. `_sibling_index` 从当前兄弟链顺序计算，不使用 Entity 数值排序；
6. 文档中的 Entity 建议按 GUID 字符串排序，减少 ECS dense storage 调整导致的无意义文件 diff；
7. 保存时设置：

```cpp
doc._scene_format_version = SceneAssetDocument::kCurrentSceneFormatVersion;
```

8. 不在保存流程中修改正常 Entity 的 GUID；只有缺失或冲突数据才允许修复。

### 验收标准

- 同一场景连续保存两次，文件内容完全一致；
- 删除并新增其他 Entity 不会改变未修改 Entity 的 GUID；
- 移动、改名、修改组件属性不会改变 Entity GUID；
- Duplicate 后新实体拥有新 GUID；
- Save -> Load -> Save 后 GUID 和层级关系保持一致。

---

## 任务包 7：编辑器最小接入

### 目标

让 GUID 可诊断、可复制，但不把编辑器实时操作全部改成 GUID。

### 修改文件

```text
Editor/Src/Inspector/ComponentEditorRegistration.cpp
Editor/Inc/Common/Selection.h
Editor/Src/Widgets/WorldOutline.cpp
Editor/Src/Widgets/ObjectDetail.cpp
```

### 实现内容

1. `PersistentIdComponent` 保持不可添加、不可删除；
2. 为 Persistent ID 提供只读展示，增加 `Copy GUID` 操作；
3. World Outline 右键菜单可增加 `Copy Entity GUID`；
4. 不允许在 Inspector 中直接编辑或重新生成 GUID；
5. `Selection`、`TreeItemId`、Gizmo、Pick 和当前 Undo Command 第一阶段继续保存 `ECS::Entity`；
6. 场景切换或选中 Entity 已失效时清理 Selection，避免另一个 Scene 中相同运行时句柄被误认为原对象；
7. 可增加调试辅助：

```cpp
Guid Selection::FirstEntityGuid();
```

该接口每次通过 Active Scene 查询，不在 Selection 中维护第二份真值。

### 为什么第一阶段不全面迁移 Selection

Selection、Pick、Gizmo 和 World Outline 都只服务当前存活 Scene 实例，使用运行时 Entity 更直接。GUID 的第一优先级是持久化和跨重建引用，不应为“统一类型”牺牲热路径和现有简洁性。

### 验收标准

- Inspector 可以查看和复制 GUID；
- 用户无法误改 GUID；
- 场景切换后不会保留指向错误 Entity 的旧 Selection；
- Editor 现有 Pick、Gizmo、Outline 和组件编辑行为不回归。

---

## 任务包 8：新增持久 Entity 引用类型

### 目标

为后续组件间引用、Prefab、存档和脚本字段提供统一类型，避免业务代码直接保存运行时 Entity。

本任务包在任务包 1～7 完成后实施，不要与基础迁移混在同一个提交中。

### 建议新增文件

```text
Engine/Inc/Scene/EntityReference.h
Engine/Src/Scene/EntityReference.cpp
```

### 建议结构

```cpp
struct AILU_API EntityReference
{
    Guid _scene_guid;
    Guid _entity_guid;

    ECS::Entity Resolve(const SceneManagement::Scene &scene) const;
    bool IsValid() const;
};
```

### 规则

1. 同场景引用可以允许 `_scene_guid` 为空；
2. 跨场景引用必须同时保存 Scene Asset GUID 与 Entity GUID；
3. `Resolve()` 最终调用 `Scene::FindEntity()`；
4. 可以缓存运行时 Entity，但缓存不是序列化数据，也不能作为真值；
5. 组件、Lua、存档系统未来需要长期保存 Entity 时统一使用该类型；
6. 渲染和系统遍历继续使用 `ECS::Entity`。

### 验收标准

- Scene 重新加载后，持久引用仍可解析到同一逻辑 Entity；
- 目标删除后安全返回 `ECS::kInvalidEntity`；
- 引用数据中不出现运行时 Entity 数值。

---

## 任务包 9：测试与迁移验证

### 必须覆盖的自动化测试

1. 连续创建至少 10,000 个 Entity，GUID 不重复；
2. `FindEntity(GetEntityGuid(entity)) == entity`；
3. Duplicate 后 GUID 不同；
4. 删除 Entity 后旧 GUID 无法解析；
5. 删除父节点时所有子节点 GUID 索引同步移除；
6. Save -> Load 后每个 Entity GUID 保持一致；
7. Save -> Load -> Save 文件稳定；
8. 父子层级和兄弟顺序保持一致；
9. 旧 `_entity_id` 场景可加载并升级；
10. 重复 GUID 自动修复且有日志；
11. 缺失父 GUID时节点降级为根节点；
12. 环形父子关系被拒绝；
13. Enter Play Mode 复制 Scene 后 GUID 与索引仍一致；
14. Exit Play Mode 后编辑场景 GUID 不受运行时副本修改影响。

### 手工验证

```text
1. 创建三个同名、同组件、同 Transform 的 Entity；
2. 确认三个 GUID 不同；
3. 移动、改名、修改组件并保存；
4. 关闭并重新打开场景；
5. 确认 GUID、层级和兄弟顺序不变；
6. Duplicate 一个 Entity；
7. 确认副本 GUID 与源不同；
8. 检查场景 JSON 不含运行时 Entity 句柄。
```

---

## 5. 推荐提交顺序

每个任务包独立提交，便于 Claude 实现后逐步编译和回归：

```text
Commit 1  Guid validity and common hasher
Commit 2  Scene entity GUID index and query API
Commit 3  Centralize scene entity lifecycle
Commit 4  Add Scene Document V2 schema
Commit 5  Load scenes by persistent Entity GUID
Commit 6  Save deterministic GUID-based scene documents
Commit 7  Add read-only Entity GUID editor integration
Commit 8  Add persistent EntityReference
Commit 9  Add migration and identity regression tests
```

任务包 8 可以延期；任务包 1～7 和 9 构成本次 Entity GUID 重构的完整闭环。

---

## 6. 明确禁止事项

- 不允许根据组件属性计算 Entity GUID；
- 不允许复制 Entity 时复用源 GUID；
- 不允许把 `ECS::Entity` 写入新场景文件；
- 不允许把 `CHierarchy` 的运行时链表字段作为 V2 文件格式；
- 不允许普通属性修改触发 GUID 变化；
- 不允许在渲染、物理、Transform 遍历中用 GUID 替换 Entity；
- 不允许加载 GUID 冲突时静默覆盖已有索引；
- 不允许 Inspector 提供任意编辑或重新生成 GUID 的入口；
- 不允许为 Scene 的 GUID 索引注册会在 Scene 复制后捕获错误 `this` 的 Register 回调。

---

## 7.5 执行状态（2026-08-05 更新）

| 任务包 | 状态 | 说明 |
|---|---|---|
| 1 Guid 基础能力 | ✅ | `IsEmpty/IsValid/GuidHasher`，`GraphGuidHasher` 已替换 |
| 2 Scene GUID 索引 | ✅ | `_guid_to_entity` + 查询/校验/重建 API |
| 3 统一实体生命周期 | ✅ | `CreateEntityInternal`，`AddObject` 三重载与 Duplicate 统一入口，删除/清理同步索引 |
| 4 Scene Document V2 | ✅ | 新字段 + 自定义 `Serialize/Deserialize`；**额外修复**：`SceneAssetDocument` 增加类级自定义序列化，用 `HasField` 守卫 `_scene_format_version`，避免旧场景缺失该字段时 JsonArchive 写入未初始化值（否则 V1 场景会被误判为 V2） |
| 5 加载重写 | ✅ | 三遍流程（GUID 身份 / 组件 / Reparent 层级），V1 迁移路径，冲突修复与日志 |
| 6 保存重写 | ✅ | 保存前 `ValidateEntityGuidIndex` + 修复，GUID 排序输出，仅存 `_parent_guid/_sibling_index/_inv_matrix_attach` |
| 7 编辑器最小接入 | ✅ | `PersistentIdComponentEditor` 只读展示 + Copy GUID；World Outline 右键 Copy Entity GUID；场景切换/Play 模式清理 Selection；`Selection::FirstEntityGuid()` |
| 8 EntityReference | ✅ | `Scene/EntityReference.h/.cpp`（`_scene_guid` + `_entity_guid`，`Resolve/IsValid`，`SerializerWrapper` 特化支持序列化）；Scene 增加 `AssetGuid()` 并在 Load 中设置；`Resolve` 对跨场景引用按 Asset GUID 校验。**注意**：新头文件注释曾用中文导致 MSVC codepage 936 误读、成员解析错乱，已改为纯 ASCII 注释 |
| 9 测试 | 🟡 部分 | 已自动化：Guid 有效性/哈希、SceneDocument V2 往返、V1 旧格式反序列化、EntityReference IsValid/序列化往返（Test 目标 6 项全过）；场景级测试需 GPU 图形环境，Test 目标 headless 无法构造 Scene，按 §4.9 手工清单验证 |

构建验证：`Engine_d.dll` / `Editor.exe` / `Test.exe` Debug 全部编译通过；`Test.exe` 运行 4/4 Entity GUID 测试通过，原有 Allocator 24/24 通过。

---

## 7. 完成定义

满足以下条件后，本次重构才算完成：

```text
Entity 创建       自动获得唯一 GUID
Entity 复制       获得新 GUID
Entity 保存       写入原 GUID
Entity 加载       恢复原 GUID
Entity 删除       GUID 索引同步失效
Hierarchy 保存    只保存 parent GUID 和 sibling index
Hierarchy 加载    通过 Scene API 重建
旧场景            可加载并在下次保存时升级
运行时系统        继续使用 ECS::Entity
持久引用          可通过 Scene::FindEntity(Guid) 解析
```
