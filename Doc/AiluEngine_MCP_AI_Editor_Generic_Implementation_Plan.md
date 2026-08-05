# AiluEngine MCP 与通用 AI 编辑器自动化实现方案

## 1. 文档目标

本文档用于指导 AiluEngine 建立一套通用的 Editor Automation 基础设施，并通过 MCP 暴露给 Claude、Codex
或其他支持 MCP 的 AI 客户端。

设计重点不是为某一种组件、资源或具体操作编写专用逻辑，而是建立一套可以持续扩展的通用框架，使 AI 能够：

- 查询当前项目、场景、选中对象和资源；
- 按名称、路径、类型、组件和标签定位目标；
- 检查 Entity、Component、Object 和 Asset 的结构与属性；
- 修改普通反射属性；
- 调用经过注册的编辑器动作；
- 执行创建、删除、复制、重命名和挂接等结构修改；
- 将多步修改组合为事务；
- 支持 Undo/Redo、预览、回滚和版本冲突检测；
- 控制返回上下文规模，避免无意义的 Token 消耗；
- 同时服务于外部 MCP 客户端和未来的编辑器内置 AI 面板。

典型指令：

```text
找到场景中名为 MainLight 的对象，将颜色改成暖色。
找到 Player 节点下所有渲染组件，将投射阴影关闭。
创建一个空对象，将它放到当前选中对象下，并添加 AudioSourceComponent。
查找项目中所有使用指定材质的对象。
把当前选中对象的位置复制到另一个对象。
检查场景中是否存在没有 Mesh 的 StaticMeshComponent。
```

---

## 2. 核心设计原则

### 2.1 MCP 只是协议适配层

```text
MCP Server
    负责协议、Tool Schema、stdio 和请求转发

Editor Automation
    负责对象查询、参数校验、命令执行和结果构造

Engine / Editor
    负责真实数据、反射、资源、场景和 UI
```

MCP Server 不负责场景遍历、属性规则、Undo、资源加载或渲染刷新。

### 2.2 Automation 与模型厂商无关

Automation API 中不应出现 Claude、Codex、OpenAI、Anthropic、Prompt 或 LLM 等概念。

未来编辑器内置 AI 面板应直接调用 `EditorAutomationService`，不经过 MCP 回环。

### 2.3 查询、检查和修改分离

不要设计：

```text
find_and_modify_light
find_and_delete_entity
find_and_move_object
```

应拆为：

```text
query
inspect
modify
```

这样更容易处理多结果、确认、预览、事务、权限和错误恢复。

### 2.4 反射负责通用数据，Adapter 负责特殊行为

普通属性通过反射自动读取和写入。

特殊组件或资产通过 Adapter 处理：

- Setter 副作用；
- 运行时缓存失效；
- 颜色空间转换；
- 资源引用解析；
- 动态数组；
- Material Slot；
- Transform dirty；
- Script runtime reset；
- Camera matrix rebuild。

### 2.5 所有写操作进入 Command

任何 AI 修改都必须可验证、可撤销、可记录、可回滚并可检测冲突。

Automation Handler 不应直接修改组件字段。

### 2.6 外部只使用稳定标识

禁止暴露：

```text
裸指针
ECS::Entity runtime handle
ComponentTypeId runtime id
对象池索引
内存地址
```

外部使用：

```text
Project Id
Scene Guid
Entity Guid
Asset Guid
稳定组件类型名
稳定属性路径
稳定动作名
```

---

## 3. 当前仓库可复用基础

### 3.1 Editor 主线程入口

相关文件：

```text
Editor/Inc/EditorApp.h
Editor/Src/EditorApp.cpp
```

建议：

```cpp
void EditorApp::Tick(f32 delta_time)
{
    _automation_service->Tick();
    Application::Tick(delta_time);
}
```

所有 Scene、ECS、ResourceMgr、Selection 和 UI 操作必须在 Editor 主线程执行。

### 3.2 Entity Guid

当前已有 `PersistentIdComponent`。需要补充：

```cpp
ECS::Entity FindEntityByGuid(const Guid &guid) const;
const Guid *FindEntityGuid(ECS::Entity entity) const;
```

长期维护：

```cpp
HashMap<Guid, ECS::Entity> _entity_guid_map;
```

### 3.3 稳定组件类型

对外使用：

```text
Ailu.ECS.TransformComponent
Ailu.ECS.LightComponent
Ailu.ECS.StaticMeshComponent
```

而不是运行时 `ComponentTypeId`。

### 3.4 反射系统

复用 `Type`、`PropertyInfo` 和 Metadata 生成：

- Property Schema；
- Property Value；
- 编辑权限；
- Display Name；
- Category；
- Range；
- Enum；
- Asset Reference 信息。

建议增加：

```cpp
PropertyInfo::EPropertyChangeSource::kAutomation
```

### 3.5 ComponentEditorRegistry

当前 Registry 已包含组件类型、Reflection Type、Add/Remove、实例获取和 Custom Editor。

长期建议抽出更底层的 `ComponentDescriptorRegistry`，由 Inspector 和 Automation 共用。

### 3.6 Undo/Redo

现有系统主要缺口：

- Execute/Undo 没有结果；
- 部分命令保存裸指针；
- 没有稳定 Guid 定位；
- 没有事务；
- 没有预验证；
- 没有冲突检测；
- 没有统一通知。

---

## 4. 总体架构

```text
Claude / Codex / MCP Host
            │ MCP stdio
            ▼
     AiluMcpServer.exe
            │ Named Pipe + JSON
            ▼
NamedPipeAutomationTransport
            │
            ▼
 EditorAutomationService
            │
            ├── EditorAutomationRegistry
            ├── AutomationQueryService
            ├── AutomationReflectionService
            ├── AutomationAssetService
            ├── AutomationAdapterRegistry
            ├── AutomationActionRegistry
            └── EditorCommandManager
            │
            ▼
 Scene / ECS / Object / ResourceMgr / Selection / UI
```

未来内置 AI：

```text
AI Assistant DockWindow
            │
            ▼
 EditorAutomationService
```

---

## 5. 通用目标引用

```cpp
enum class EAutomationTargetKind
{
    kProject,
    kScene,
    kEntity,
    kComponent,
    kObject,
    kAsset,
    kEditor,
};

struct AutomationTargetRef
{
    EAutomationTargetKind _kind = EAutomationTargetKind::kEntity;

    Guid _project_guid;
    Guid _scene_guid;
    Guid _entity_guid;
    Guid _asset_guid;

    String _component_type;
    String _object_type;
};
```

只有与目标类型相关的字段才使用。

---

## 6. AutomationValue

```cpp
using AutomationArray = Vector<AutomationValue>;
using AutomationObject = HashMap<String, AutomationValue>;

class AutomationValue
{
public:
    using Storage = Variant<
        std::nullptr_t,
        bool,
        i64,
        u64,
        f64,
        String,
        AutomationArray,
        AutomationObject>;

    template<typename T>
    explicit AutomationValue(T value)
        : _value(std::move(value))
    {
    }

private:
    Storage _value;
};
```

逻辑类型至少包括：

```text
Null
Bool
Integer
Float
String
Guid
Enum
Vector
Quaternion
Color
Array
Struct
Object Reference
Asset Reference
Entity Reference
```

复杂值在 JSON 中应携带类型信息：

```json
{
  "type": "color",
  "space": "srgb",
  "value": [1.0, 0.5, 0.25, 1.0]
}
```

```json
{
  "type": "entity_ref",
  "scene_guid": "...",
  "entity_guid": "..."
}
```

---

## 7. 请求、结果和错误

```cpp
struct AutomationRequest
{
    u64 _request_id = 0u;
    String _method;
    AutomationObject _arguments;
    AutomationRequestContext _context;
};

struct AutomationRequestContext
{
    String _caller;
    bool _allow_write = false;
    bool _allow_destructive = false;
    bool _interactive = true;
};

struct AutomationError
{
    String _code;
    String _message;
    AutomationObject _details;
};

struct AutomationResult
{
    bool _success = false;
    AutomationValue _data;
    AutomationError _error;
};
```

固定错误码：

```text
editor_not_ready
editor_shutting_down
editor_in_play_mode
project_not_open
scene_not_open
scene_not_found
scene_revision_conflict
target_not_found
entity_not_found
asset_not_found
component_not_found
property_not_found
property_not_editable
action_not_found
invalid_argument
invalid_guid
invalid_type
ambiguous_target
permission_denied
command_failed
transaction_failed
transport_error
internal_error
```

---

## 8. EditorAutomationService

职责：

- 接收 Transport 请求；
- 排入线程安全队列；
- 在主线程执行；
- 调用 Registry；
- 管理 Ready/Shutdown；
- 控制单帧请求数量；
- 记录日志；
- 返回 Future。

```cpp
class EditorAutomationService final : public NonCopyable
{
public:
    void Initialize();
    void Finalize();
    void Tick();

    Future<AutomationResult> Submit(AutomationRequest request);

private:
    void DrainRequests();
    AutomationResult Execute(const AutomationRequest &request);

private:
    EditorAutomationRegistry _registry;
    ConcurrentQueue<PendingAutomationRequest> _pending_requests;
    bool _is_initialized = false;
    bool _is_shutting_down = false;
    u32 _max_requests_per_tick = 16u;
};
```

Transport 线程只负责 JSON 和队列，不得访问引擎对象。

---

## 9. EditorAutomationRegistry

```cpp
using AutomationHandler = std::function<AutomationResult(
    EditorAutomationContext &,
    const AutomationObject &)>;

enum class EAutomationPermission
{
    kReadOnly,
    kSafeWrite,
    kDestructiveWrite,
    kExternalEffect,
};

struct AutomationMethodDesc
{
    String _name;
    String _description;
    EAutomationPermission _permission = EAutomationPermission::kReadOnly;
    AutomationSchema _input_schema;
    AutomationSchema _output_schema;
};

class EditorAutomationRegistry final : public NonCopyable
{
public:
    bool Register(String method, AutomationMethodDesc desc, AutomationHandler handler);

    AutomationResult Invoke(
        StringView method,
        EditorAutomationContext &context,
        const AutomationObject &arguments) const;
};
```

同一份 Method 描述用于生成 MCP Tool Schema。

---

## 10. 通用查询

### 10.1 Entity Query

Tool：

```text
scene.query_entities
```

输入：

```json
{
  "name": "Player",
  "name_match": "exact",
  "required_components": [
    "Ailu.ECS.TransformComponent"
  ],
  "path_prefix": "/Gameplay",
  "offset": 0,
  "limit": 20
}
```

返回摘要：

```json
{
  "scene_guid": "...",
  "scene_revision": 42,
  "items": [
    {
      "entity_guid": "...",
      "name": "Player",
      "hierarchy_path": "/Gameplay/Player",
      "component_types": [
        "Ailu.ECS.TagComponent",
        "Ailu.ECS.TransformComponent"
      ]
    }
  ],
  "has_more": false
}
```

默认不返回组件完整属性。

### 10.2 Asset Query

Tool：

```text
asset.query
```

输入：

```json
{
  "name": "M_Character",
  "type": "Ailu.Render.Material",
  "path_prefix": "Assets/Materials",
  "limit": 20
}
```

返回：

```json
{
  "items": [
    {
      "asset_guid": "...",
      "name": "M_Character",
      "type": "Ailu.Render.Material",
      "path": "Assets/Materials/M_Character.alasset"
    }
  ]
}
```

---

## 11. 通用 Inspect

Tool：

```text
scene.inspect_entity
```

第一步只看结构：

```json
{
  "entity_guid": "...",
  "include_components": true,
  "include_properties": false
}
```

需要属性时显式指定：

```json
{
  "entity_guid": "...",
  "components": [
    {
      "type": "Ailu.ECS.TransformComponent",
      "property_paths": [
        "_local_transform._position",
        "_local_transform._rotation"
      ]
    }
  ]
}
```

避免默认返回全部字段。

---

## 12. Property Schema

Tool：

```text
reflection.get_type_schema
```

输入：

```json
{
  "type": "Ailu.ECS.TransformComponent"
}
```

返回：

```json
{
  "type": "Ailu.ECS.TransformComponent",
  "properties": [
    {
      "path": "_local_transform._position",
      "display_name": "Position",
      "value_type": "Vector3f",
      "editable": true,
      "category": "Transform"
    }
  ]
}
```

Schema 由反射与 Adapter 合并生成。

---

## 13. Property Path

稳定属性路径示例：

```text
_local_transform._position
_camera._fov
_light._light_color
_materials[0]
```

第一阶段支持：

```text
Struct 嵌套字段
基础类型
Enum
Vector
Color
Guid
```

后续支持：

```text
Array Index
Map Key
Object Reference
Asset Reference
Optional
Variant
```

---

## 14. Automation Adapter

```cpp
class IAutomationTypeAdapter
{
public:
    virtual ~IAutomationTypeAdapter() = default;

    virtual AutomationResult Describe(
        AutomationDescribeContext &context,
        const AutomationTargetRef &target) = 0;

    virtual AutomationResult ReadProperty(
        AutomationReadContext &context,
        const AutomationTargetRef &target,
        StringView property_path) = 0;

    virtual Scope<IEditorCommand> BuildSetPropertyCommand(
        AutomationWriteContext &context,
        const AutomationTargetRef &target,
        StringView property_path,
        const AutomationValue &value) = 0;
};
```

优先级：

```text
专用 Adapter
    ↓
通用反射 Adapter
    ↓
不支持
```

可能需要专用 Adapter 的类型：

```text
TransformComponent
LightComponent
Camera
StaticMeshComponent
ScriptComponent
Material
SpriteRendererComponent
Graph Asset
UI Style Asset
```

灯光不再是架构中心，只是专用 Adapter 的一个例子。

---

## 15. 通用属性写入

Tool：

```text
scene.set_property
```

输入：

```json
{
  "target": {
    "kind": "component",
    "scene_guid": "...",
    "entity_guid": "...",
    "component_type": "Ailu.ECS.TransformComponent"
  },
  "property_path": "_local_transform._position",
  "value": {
    "type": "vector3",
    "value": [1.0, 2.0, 3.0]
  },
  "expected_scene_revision": 42
}
```

执行流：

```text
解析 Target
    ↓
检查 Revision
    ↓
选择 Adapter
    ↓
解析 Property Path
    ↓
转换 AutomationValue
    ↓
构建 Editor Command
    ↓
CommandManager Execute
    ↓
更新 Dirty / Revision
    ↓
发布 Property Changed Event
    ↓
返回旧值与新值
```

Handler 不直接调用 `PropertyInfo::Set()`。

---

## 16. 通用结构操作

```text
scene.create_entity
scene.delete_entity
scene.duplicate_entity
scene.rename_entity
scene.reparent_entity
scene.add_component
scene.remove_component
```

全部使用 Guid 和稳定类型名，全部通过 Command。

---

## 17. Editor Action

非数据编辑操作通过 Action Registry：

```text
选择对象
聚焦 Scene View
打开指定编辑器
刷新资源
重新导入资产
生成预览图
```

统一 Tool：

```text
editor.invoke_action
```

示例：

```json
{
  "action": "editor.selection.set",
  "arguments": {
    "entity_guids": ["..."]
  }
}
```

避免每个 Editor Action 都成为独立 MCP Tool。

---

## 18. Command 系统

```cpp
struct EditorCommandResult
{
    bool _success = false;
    String _error_code;
    String _message;
};

class IEditorCommand
{
public:
    virtual ~IEditorCommand() = default;

    virtual EditorCommandResult Validate(EditorCommandContext &context) = 0;
    virtual EditorCommandResult Execute(EditorCommandContext &context) = 0;
    virtual EditorCommandResult Undo(EditorCommandContext &context) = 0;

    virtual StringView Name() const = 0;
};
```

命令保存：

```text
Scene Guid
Entity Guid
Asset Guid
Component Stable Type
Property Path
Old AutomationValue
New AutomationValue
```

禁止保存长期裸指针。

通用命令：

```cpp
class SetPropertyCommand final : public IEditorCommand
{
private:
    AutomationTargetRef _target;
    String _property_path;
    AutomationValue _old_value;
    AutomationValue _new_value;
    bool _has_old_value = false;
};
```

实际读写由 Adapter 完成。

---

## 19. Scene Revision

区分：

```text
Structure Revision
    Entity、Component、Hierarchy 变化

Edit Revision
    任意用户可观察修改
```

规则：

```text
结构修改：
    Structure Revision + 1
    Edit Revision + 1

属性修改：
    Edit Revision + 1

查询：
    不修改
```

写 Tool 使用 `expected_scene_revision` 防止基于旧上下文操作。

---

## 20. Property Changed 事件

```cpp
struct AutomationPropertyChangedEvent
{
    Guid _scene_guid;
    Guid _entity_guid;
    Guid _asset_guid;

    String _target_type;
    String _property_path;

    AutomationValue _old_value;
    AutomationValue _new_value;
};
```

第一阶段可让 Inspector 重建对应目标面板；后续使用 Observer 只刷新具体控件。

Automation 不直接依赖 `ObjectDetail`。

---

## 21. 事务

复杂指令转换为操作列表：

```json
{
  "expected_scene_revision": 42,
  "operations": [
    {
      "operation": "scene.create_entity",
      "temporary_id": "new_entity",
      "arguments": {
        "name": "AudioRoot"
      }
    },
    {
      "operation": "scene.reparent_entity",
      "arguments": {
        "entity": "$new_entity",
        "new_parent_guid": "..."
      }
    },
    {
      "operation": "scene.add_component",
      "arguments": {
        "entity": "$new_entity",
        "component_type": "Ailu.ECS.AudioSourceComponent"
      }
    }
  ]
}
```

事务要求：

1. 全部预验证；
2. 支持临时结果引用；
3. 生成修改摘要；
4. 整体执行；
5. 失败整体回滚；
6. 整组只生成一个 Undo 单元；
7. Edit Revision 只增加一次；
8. 返回每一步结果。

Tool：

```text
editor.preview_transaction
editor.apply_transaction
```

---

## 22. MCP Server

```text
Claude / Codex
    ↓ stdio
AiluMcpServer.exe
    ↓ Named Pipe
Editor.exe
```

Session 文件：

```text
<Project>/.ailu/editor_session.json
```

```json
{
  "pid": 12345,
  "session_id": "...",
  "pipe_name": "\\.\pipe\ailu_editor_...",
  "project_path": "F:/Projects/Example",
  "editor_version": "0.1.0"
}
```

MCP Server 禁止包含场景规则、属性规则、Undo、Dirty 和渲染刷新逻辑。

---

## 23. 推荐 MCP Tool 集合

第一阶段控制在 10 到 15 个：

```text
editor.get_state
editor.get_selection
editor.invoke_action
editor.undo
editor.redo
editor.preview_transaction
editor.apply_transaction

scene.query_entities
scene.inspect_entity
scene.set_property
scene.create_entity
scene.delete_entity
scene.add_component
scene.remove_component

asset.query
asset.inspect
asset.set_property

reflection.get_type_schema
```

可根据客户端 Tool 数量限制进一步合并。

---

## 24. Token 控制

主要消耗来自 Tool Schema、参数、返回结果和历史 Tool Result。

禁止：

```text
scene.dump_scene
asset.dump_database
reflection.dump_all_types
```

推荐流程：

```text
query
    ↓
summary
    ↓
inspect selected target
    ↓
modify
```

所有列表 Tool 支持：

```text
offset
limit
has_more
continuation_token
```

Inspect 支持 Property Filter。

Tool 描述保持简短，详细引擎文档不放入 Schema。

---

## 25. 权限模型

```text
ReadOnly
SafeWrite
DestructiveWrite
ExternalEffect
```

示例：

```text
scene.query_entities       ReadOnly
scene.inspect_entity       ReadOnly
scene.set_property         SafeWrite
scene.delete_entity        DestructiveWrite
scene.save                 DestructiveWrite
process.run                ExternalEffect
```

第一阶段只开放 ReadOnly 和少量 SafeWrite。

---

## 26. 编辑模式策略

```text
Edit Mode
    允许读
    允许 SafeWrite

Play Mode
    允许读
    禁止写

Simulate Mode
    允许读
    默认禁止写
```

后续再支持显式 `target_world`。

---

## 27. 通用调用链示例

用户：

```text
找到场景中名为 xx 的一盏灯，将颜色改成 #FF8040。
```

底层仍走通用链路：

```text
scene.query_entities
    ↓
scene.inspect_entity
    ↓
scene.set_property
    ↓
LightComponentAutomationAdapter
    ↓
Editor Command
    ↓
Undo / Dirty / Revision / Inspector Event
```

其中灯光 Adapter 仅负责颜色空间和当前特殊数据布局。

其他指令同样复用该链路：

```text
关闭多个对象的阴影
修改 Transform
修改 Camera FOV
替换 Material
修改 Sprite 颜色
修改 AudioSource 音量
修改 UI Style
修改资产导入设置
```

---

## 28. 分阶段实施计划

### 阶段 0：稳定标识与 Revision

```text
Scene Guid
Entity Guid Index
Asset Guid 统一接口
Edit Revision
Automation Change Source
```

### 阶段 1：Automation 核心

```text
AutomationValue
AutomationRequest
AutomationResult
AutomationSchema
EditorAutomationRegistry
EditorAutomationService
```

### 阶段 2：通用只读查询

```text
editor.get_state
scene.query_entities
scene.inspect_entity
asset.query
asset.inspect
reflection.get_type_schema
```

### 阶段 3：Adapter

```text
GenericReflectionAdapter
Transform Adapter
Light Adapter
Camera Adapter
StaticMesh Adapter
Asset Adapter
```

### 阶段 4：Named Pipe

```text
Session 文件
Pipe Server
JSON
Timeout
Reconnect
Shutdown
```

### 阶段 5：只读 MCP

```text
C# MCP Server
stdio
Tool Schema
MCP Inspector 测试
```

### 阶段 6：Command 升级

```text
Validate
Execute
Undo
Redo
稳定 TargetRef
事件通知
```

### 阶段 7：通用安全写入

```text
scene.set_property
asset.set_property
scene.rename_entity
scene.add_component
scene.remove_component
editor.undo
editor.redo
```

### 阶段 8：事务

```text
Preview
Apply
Temporary Id
原子执行
回滚
单 Undo
```

### 阶段 9：破坏性操作

```text
delete entity
delete asset
save scene
reimport asset
```

### 阶段 10：内置 AI

```text
AI DockWindow
Provider
Tool Loop
Selection Context
Preview UI
Apply / Reject
History
Cancel
```

---

## 29. 推荐提交拆分

```text
1. editor: add stable automation target identifiers
2. editor: add scene edit revision
3. editor: add automation value and schema
4. editor: add main-thread automation service
5. editor: add generic entity and asset queries
6. editor: add reflection automation adapter
7. editor: add type-specific automation adapters
8. editor: add named-pipe transport
9. tools: add read-only Ailu MCP server
10. editor: add stable command interface
11. editor: add generic set-property command
12. mcp: expose safe write tools
13. editor: add transaction preview and apply
14. editor: add destructive operation approval
15. editor: add built-in AI assistant
```

---

## 30. 测试建议

```text
Tests/EditorAutomation/
├── AutomationValueTests.cpp
├── AutomationSchemaTests.cpp
├── EntityQueryTests.cpp
├── AssetQueryTests.cpp
├── PropertyPathTests.cpp
├── ReflectionAdapterTests.cpp
├── CommandTests.cpp
├── TransactionTests.cpp
└── TransportTests.cpp
```

重点覆盖：

```text
Guid 稳定性
查询过滤与分页
Property Path
普通反射
特殊 Adapter
命令失败
Undo/Redo
事务回滚
Revision 冲突
Token 返回规模
Transport 断线与退出
```

---

## 31. 第一阶段最终验收

只读闭环：

```text
Claude/Codex
    ↓
scene.query_entities
    ↓
scene.inspect_entity
    ↓
reflection.get_type_schema
```

写入闭环：

```text
Claude/Codex
    ↓
scene.query_entities
    ↓
scene.set_property
    ↓
Automation Adapter
    ↓
Editor Command
    ↓
Undo
```

事务闭环：

```text
Claude/Codex
    ↓
editor.preview_transaction
    ↓
用户确认
    ↓
editor.apply_transaction
    ↓
单次 Undo
```

---

## 32. 第一阶段不实现

```text
任意代码执行
任意磁盘访问
任意网络访问
全场景 Dump
自动保存
自动批量删除
运行时世界修改
完整自主 Agent
远程 HTTP MCP
多用户协同编辑
```

---

## 33. 核心结论

推荐顺序：

```text
稳定标识
    ↓
Automation 数据模型
    ↓
通用查询
    ↓
反射与 Adapter
    ↓
主线程安全写入
    ↓
Command / Undo
    ↓
Named Pipe
    ↓
MCP
    ↓
事务
    ↓
内置 AI
```

系统边界：

```text
MCP Server
    负责协议

EditorAutomationService
    负责调度

Automation Query / Adapter
    负责通用数据访问和特殊规则

EditorCommand
    负责安全修改、Undo 和回滚

Scene / ECS / Object / Asset
    负责真实数据
```

Transform、Light、Camera、Material、Audio、UI 等都只是通用框架中的目标类型或 Adapter，
不应成为整体架构中心。
