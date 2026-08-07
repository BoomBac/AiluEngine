# AiluEngine Lua 脚本系统任务包

> 目标：将 AiluEngine 当前 Lua + sol2 测试实现，逐步演进为可用于实际游戏开发的脚本系统。
>
> 本文按“可独立实现、可独立验收、可独立回滚”的方式拆分任务，适合逐包交给 Claude 实现。
>
> 基于当前仓库 `BoomBac/AiluEngine` 的现状整理，重点涉及：
>
> - `Engine/Inc/Framework/Script/ScriptSystem.h`
> - `Engine/Src/Framework/Script/ScriptSystem.cpp`
> - `Engine/Inc/Scene/Component.h`
> - `Engine/Inc/Scene/Entity.h`
> - `Engine/Src/Scene/Scene.cpp`
> - `Editor/Src/Inspector/ComponentEditors/ScriptComponentEditor.cpp`
> - `Engine/Inc/Objects/Type.h`
> - `Engine/Inc/Framework/Core/ReflectionMacros.h`
> - `AiluHeadTool/AiluHeadTool.h`
> - `AiluHeadTool/AiluHeadTool.cpp`
> - `Engine/Inc/Assets/AssetDocument.h`

---

# 1. 总体目标

最终脚本架构建议为：

```text
C++ Engine Internal
        │
        │ selected safe API
        ▼
Script-facing Facade
        │
        │ AiluHeadTool code generation
        ▼
Generated sol2 Bindings
        │
        ▼
      Lua VM
        │
        ├── ScriptPrototype
        │       └── 每个 Lua 文件共享函数代码
        │
        └── ScriptInstance
                ├── Entity A 独立状态
                ├── Entity B 独立状态
                └── Entity C 独立状态
```

核心原则：

1. ECS Component 只保存可序列化数据，不持有 `sol::table` 等 Lua runtime 对象。
2. 不直接向 Lua 暴露原始 ECS Component 内存。
3. 常用 Gameplay API 通过安全的 Script Facade 暴露。
4. Reflection 决定“允许导出什么”，AiluHeadTool 生成强类型 sol2 binding。
5. 不实现一个基于 `std::any` / `PropertyInfo` 的 Lua 通用动态调用器。
6. 同一个 Lua 文件只加载/执行一次形成 Prototype。
7. 每个 Entity 拥有独立 ScriptInstance 状态。
8. ScriptAsset、Entity 引用最终均使用稳定 Guid，而不是路径或 runtime Entity 值。
9. 热重载只重新构建 Prototype，然后重建/迁移 Instance。
10. Lua 单个 Instance 出错后应隔离，不允许每帧重复刷相同错误。

---

# 2. 当前基线

当前代码已经具备：

- `ScriptSystem` 单 Lua VM。
- `ScriptEntityHandle`。
- `ScriptComponent`。
- `OnInit`。
- `OnFixedUpdate`。
- `OnUpdate`。
- `OnLateUpdate`。
- `OnDestroy`。
- `sol::protected_function` 错误保护。
- Lua 文件变化监听和版本号。
- Script Component Inspector。
- Scene 对 Script Component 的序列化。
- ECS Entity index + generation。
- Scene Persistent Entity Guid。
- Reflection：
  - `ACLASS`
  - `ASTRUCT`
  - `AENUM`
  - `APROPERTY`
  - `AFUNCTION`
- AiluHeadTool 已能解析 class/property/function 基础信息。

当前主要技术债：

```text
ScriptComponent
    └── std::optional<sol::table> _instance
```

以及：

```text
每个 Entity 创建 ScriptInstance
    ↓
load_file()
    ↓
执行 Lua 文件
```

以及：

```text
RegisterCoreBindings()
    ↓
手写 new_usertype / set_function
```

这些是本次任务拆分重点。

---

# 3. 全局实现约束

以下约束适用于所有任务包。

## 3.1 不允许直接暴露内部 Component 数据

禁止设计：

```lua
entity:get_component("TransformComponent")._local_transform._position.x = 1
```

原因：

- 会绕过 Transform dirty/version。
- 会破坏 System 数据一致性。
- 会让 Lua API 与 ECS 内部布局强绑定。

推荐：

```lua
entity.transform.position = vec3(1, 2, 3)
```

底层必须调用正式引擎 API，例如：

```cpp
TransformComponent::SetLocalPosition(...)
```

---

## 3.2 不允许 Lua runtime 类型进入 ECS Component

最终 `Component.h` 中的 `ScriptComponent` 不应依赖：

```cpp
#include <sol/sol.hpp>
```

也不应包含：

```cpp
sol::table
sol::function
sol::object
```

---

## 3.3 不做万能 Reflection Lua Bridge

本阶段不要实现：

```text
Lua
 ↓
string member name
 ↓
Type
 ↓
PropertyInfo / FunctionInfo
 ↓
std::any
 ↓
dynamic Invoke
```

AiluHeadTool 应生成：

```cpp
lua.new_usertype<T>(...);
```

这样的强类型绑定。

---

## 3.4 保留现有能力直到替代方案完成

迁移过程中：

- `RunFile()` 可暂时保留。
- `RunString()` 可暂时保留。
- `GetGlobalInt/Number/Bool()` 可暂时保留。
- `OnInit()` 可兼容。
- `_script_path` 在 ScriptAsset GUID 化完成之前继续使用。

不要在同一个任务包中提前删除仍被后续任务依赖的旧接口。

---

# 4. 推荐任务顺序

```text
T00 现有脚本系统回归基线
 │
 ▼
T01 Runtime 与 ECS Component 分离
 │
 ▼
T02 ScriptPrototype 缓存
 │
 ├───────────────┐
 ▼               ▼
T03 Script Facade API   T04 AHT 自动生成 sol2 Binding
 │               │
 └───────┬───────┘
         ▼
T05 LuaLS / EmmyLua 自动补全生成
         │
         ▼
T06 ScriptAsset Guid 化
         │
         ▼
T07 Script Exposed Properties + Inspector
         │
         ▼
T08 Hot Reload 正式化
         │
         ▼
T09 生命周期、错误隔离与最终回归
```

---

# 5. T00：建立脚本系统回归基线

## 目标

在开始重构之前建立可重复执行的测试基线，确保后续每个任务包都能确认没有破坏当前能力。

## 修改范围

优先新增测试脚本和最小自动化测试，不修改核心架构。

建议目录：

```text
Tests/
Scripts/
    script_basic.lua
    script_lifecycle.lua
    script_instance_state.lua
    script_error.lua
```

如果当前测试工程组织不同，以仓库现有测试结构为准。

## 测试内容

### Basic

验证：

```lua
engine.log(...)
engine.time()
engine.delta_time()
```

以及：

```text
Entity.is_valid
Entity.get_name
Entity.get_position
Entity.set_position
```

### Lifecycle

记录：

```text
OnInit
OnFixedUpdate
OnUpdate
OnLateUpdate
OnDestroy
```

被调用次数和顺序。

### Multiple Instances

同一个脚本挂到两个 Entity：

```lua
self.counter = self.counter + 1
```

确认两个实例状态互不影响。

### Error

故意在 `OnUpdate` 中抛错，确认：

- Engine 不崩溃。
- 能打印 Lua 错误和 traceback。

## 验收标准

- [ ] Lua backend 开启时测试可执行。
- [ ] `RunString()` 正常。
- [ ] `RunFile()` 正常。
- [ ] ScriptComponent 生命周期正常。
- [ ] 两个 Entity 的脚本状态独立。
- [ ] Lua runtime error 不导致引擎崩溃。
- [ ] 后续所有任务包必须重新跑此基线。

## Claude 任务指令

```text
请先不要重构脚本架构。

基于当前 AiluEngine ScriptSystem，建立一组最小 Lua 脚本回归测试，覆盖：
1. RunFile / RunString；
2. engine.log/time/delta_time；
3. ScriptEntityHandle；
4. OnInit/OnFixedUpdate/OnUpdate/OnLateUpdate/OnDestroy；
5. 同一脚本挂载多个 Entity 时实例状态独立；
6. Lua error 不导致引擎崩溃。

尽量复用现有测试框架，不引入新的第三方测试库。
完成后给出测试入口和执行结果。
```

---

# 6. T01：将 Script Runtime 从 ScriptComponent 中剥离

## 目标

让 ECS `ScriptComponent` 恢复为纯数据组件。

当前类似：

```cpp
struct ScriptComponent
{
    String _script_path;
    bool _is_initialized;
    String _resolved_script_path;
    u32 _loaded_script_version;

#if AILU_ENABLE_LUA_SCRIPTING
    std::optional<sol::table> _instance;
#endif
};
```

目标第一阶段：

```cpp
struct ScriptComponent
{
    DECLARE_COMPONENT(ScriptComponent, "Ailu.ECS.ScriptComponent")

    String _script_path;
    bool _enabled = true;
};
```

暂时继续保留 `_script_path`，Guid 化留到 T06。

## 新增 Runtime 数据

建议在 Script 模块内部：

```cpp
struct ScriptInstanceKey
{
    SceneManagement::Scene *_scene = nullptr;
    ECS::Entity _entity = ECS::kInvalidEntity;

    bool operator==(const ScriptInstanceKey &other) const = default;
};
```

以及：

```cpp
struct ScriptInstance
{
    SceneManagement::Scene *_scene = nullptr;
    ECS::Entity _entity = ECS::kInvalidEntity;

    String _script_path;
    String _resolved_script_path;

    u32 _loaded_script_version = 0u;
    bool _is_initialized = false;
    bool _faulted = false;

#if AILU_ENABLE_LUA_SCRIPTING
    sol::table _instance;
#endif
};
```

存储于：

```cpp
ScriptSystem
    └── HashMap<ScriptInstanceKey, ScriptInstance>
```

具体容器可根据 AiluEngine HashMap 能力调整。

## 关键要求

### ScriptComponent copy

Component copy/clone 不再需要特殊处理 Lua 对象。

### Component.h

应尽可能移除：

```cpp
#include <sol/sol.hpp>
```

至少 ScriptComponent 不再依赖它。

### 生命周期

以下入口继续保持现有对外行为：

```cpp
FixedUpdateComponent(...)
UpdateComponent(...)
LateUpdateComponent(...)
DestroyComponent(...)
```

内部改为查找 Runtime `ScriptInstance`。

### Entity 删除

Entity 或 ScriptComponent 被删除时必须删除对应 ScriptInstance。

### Scene 删除

Scene 销毁时不能留下指向 Scene 的 ScriptInstance。

## 不包含

本任务不做：

- ScriptPrototype。
- ScriptAsset Guid。
- 自动 Binding。
- Inspector 属性。
- 热重载架构重写。

## 验收标准

- [ ] `ScriptComponent` 不再持有任何 sol2 类型。
- [ ] `ScriptComponent` copy/clone 只复制纯数据。
- [ ] Script 生命周期行为与 T00 基线一致。
- [ ] 同一 Entity 只存在一个对应 ScriptInstance。
- [ ] Entity 销毁后 Runtime Instance 被删除。
- [ ] Scene 销毁后无悬空 ScriptInstance。
- [ ] Lua VM Finalize 前正确清理所有 ScriptInstance。
- [ ] T00 全部通过。

## Claude 任务指令

```text
实现 AiluEngine Script Runtime 与 ECS ScriptComponent 分离。

要求：
1. ScriptComponent 只保留可序列化数据；
2. sol::table、初始化状态、脚本版本等 runtime 数据迁移到 ScriptSystem；
3. Runtime Instance 必须由 scene + entity 唯一定位；
4. 保持现有 FixedUpdateComponent/UpdateComponent/LateUpdateComponent/DestroyComponent 行为兼容；
5. 正确处理 Entity 删除、ScriptComponent 删除、Scene 销毁、ScriptSystem Finalize；
6. 本任务不要实现 ScriptPrototype、ScriptAsset Guid、自动 Binding 或 Inspector 新功能；
7. 完成后运行现有脚本回归测试。
```

---

# 7. T02：引入 ScriptPrototype，Lua 文件只执行一次

## 目标

解决当前多个 Entity 使用同一 Lua 文件时重复：

```text
load_file
execute chunk
return table
```

的问题。

目标：

```text
Script File
    ↓
load/execute once
    ↓
ScriptPrototype
    ├── Instance A
    ├── Instance B
    └── Instance C
```

## 新增结构

建议：

```cpp
struct ScriptPrototype
{
    String _resolved_path;
    u32 _version = 0u;

#if AILU_ENABLE_LUA_SCRIPTING
    sol::table _prototype;
#endif
};
```

ScriptSystem：

```cpp
HashMap<String, ScriptPrototype> _prototypes;
```

T02 阶段仍以 normalized path 作为 key。

## Lua Instance 创建

假设脚本：

```lua
local Player = {}

function Player:OnUpdate(dt)
end

return Player
```

加载一次得到：

```text
prototype = Player
```

每个 Entity 创建独立 table：

```lua
instance = {
    entity = ...
}
```

并设置：

```lua
setmetatable(instance, {
    __index = prototype
})
```

C++ 可使用 sol2 API 完成。

## 生命周期函数缓存

在创建 Instance 时缓存：

```cpp
sol::protected_function _on_init;
sol::protected_function _on_fixed_update;
sol::protected_function _on_update;
sol::protected_function _on_late_update;
sol::protected_function _on_destroy;
```

避免每帧：

```cpp
instance["OnUpdate"]
```

字符串查询。

如果 Prototype 中不存在对应函数：

```text
function invalid
```

直接跳过。

## Prototype 加载失败

如果脚本：

- 语法错误。
- chunk 执行失败。
- 没有返回 table。

则 Prototype 状态失败，不创建 Instance。

## 不包含

本任务不做：

- Hot Reload 状态迁移。
- ScriptAsset Guid。
- LuaLS。
- Script exposed properties。

## 验收标准

- [ ] 同一个 Lua 文件挂载 N 个 Entity 时，只执行文件顶层 chunk 一次。
- [ ] 每个 Entity 拥有独立 instance table。
- [ ] 生命周期函数由 Instance 创建阶段缓存。
- [ ] `OnUpdate` 不再每帧通过字符串查找函数。
- [ ] Prototype load error 不创建半初始化 Instance。
- [ ] 两个 Entity 的普通 Lua 字段互不影响。
- [ ] Prototype 中共享函数。
- [ ] T00 全部通过。

## 额外测试

Lua：

```lua
local top_level_execute_count = (global_counter or 0) + 1
global_counter = top_level_execute_count

local Test = {}

function Test:OnInit()
    self.value = 0
end

function Test:OnUpdate(dt)
    self.value = self.value + 1
end

return Test
```

挂载 10 个 Entity：

```text
global_counter == 1
```

而不是 10。

## Claude 任务指令

```text
在 T01 Runtime Instance Registry 基础上实现 ScriptPrototype。

要求：
1. 同一路径 Lua 文件只 load/execute 一次；
2. Lua 文件必须返回 prototype table；
3. 每个 Entity 创建独立 instance table；
4. instance 通过 metatable __index 指向 prototype；
5. 创建 instance 时缓存 生命周期函数，避免每帧字符串查表；
6. prototype load 失败时不得产生半初始化 instance；
7. 暂时继续使用 normalized script path 作为 prototype key；
8. 不做 ScriptAsset Guid、Inspector property 或复杂 hot reload；
9. 增加测试证明 10 个 Entity 使用同一脚本时顶层 chunk 只执行一次。
```

---

# 8. T03：建立第一批 Script-facing Facade API

## 目标

建立稳定 Lua Gameplay API，不直接暴露 ECS Component。

第一批建议：

```text
ScriptEntity
ScriptTransform
ScriptScene
ScriptTime
ScriptInput
```

第二批后续再增加：

```text
ScriptCamera
ScriptRigidBody
ScriptAnimator
ScriptAudio
```

## 8.1 ScriptEntity

建议 API：

```cpp
class ScriptEntity
{
public:
    bool IsValid() const;
    String GetName() const;
    void SetName(const String &name) const;

    ScriptTransform GetTransform() const;

    Guid GetGuid() const;

    bool HasParent() const;
    ScriptEntity GetParent() const;

    void Destroy() const;
};
```

Lua：

```lua
self.entity.name
self.entity.guid
self.entity.transform
self.entity:destroy()
```

不要向 Lua 暴露：

```text
EntityIndex
EntityGeneration
raw ECS::Entity mutation
Registry*
```

---

## 8.2 ScriptTransform

建议第一版：

```cpp
class ScriptTransform
{
public:
    bool IsValid() const;

    Vector3f GetLocalPosition() const;
    void SetLocalPosition(const Vector3f &position) const;

    Quaternion GetLocalRotation() const;
    void SetLocalRotation(const Quaternion &rotation) const;

    Vector3f GetLocalScale() const;
    void SetLocalScale(const Vector3f &scale) const;

    Vector3f GetPosition() const;
    Quaternion GetRotation() const;
    Vector3f GetScale() const;
};
```

所有 setter 必须调用：

```cpp
TransformComponent::SetLocalPosition
TransformComponent::SetLocalRotation
TransformComponent::SetLocalScale
```

或同等正式入口。

禁止直接写：

```cpp
transform->_local_transform._position = ...
```

---

## 8.3 ScriptScene

第一版：

```cpp
class ScriptScene
{
public:
    bool IsValid() const;

    ScriptEntity FindEntity(const Guid &guid) const;
    ScriptEntity FindEntityByName(const String &name) const;
    ScriptEntity CreateEntity(const String &name) const;
};
```

`FindEntityByName` 可作为方便接口，但持久引用必须使用 Guid。

---

## 8.4 ScriptTime

建议：

```lua
time.delta_time
time.fixed_delta_time
time.render_alpha
time.time
```

替代逐渐扩大的：

```lua
engine.delta_time()
engine.fixed_delta_time()
```

旧接口可暂时保留兼容。

---

## 8.5 ScriptInput

基于当前 Input Action 系统优先设计：

```lua
input:is_pressed("Jump")
input:is_down("Fire")
input:get_float("MoveX")
input:get_vector2("Move")
```

第一版只实现当前 InputSystem 能稳定支持的最小集合。

不要把内部 Input storage 或 device state 直接暴露给 Lua。

## 句柄语义

所有 Script Facade handle 推荐保存：

```cpp
Scene *_scene;
Entity _entity;
```

每次访问前验证：

```cpp
scene != nullptr
scene->IsValidEntity(entity)
```

## 不包含

本任务不要：

- 改 AiluHeadTool。
- 做自动 binding。
- 做 ScriptAsset。
- 做通用 GetComponent raw API。

Facade 第一版仍可在 `RegisterCoreBindings()` 中手动注册，用于先验证 API 设计。

## 验收标准

- [ ] Lua 不需要直接访问 raw `TransformComponent`。
- [ ] Transform setter 正确维护 dirty/version。
- [ ] Entity 销毁后旧 ScriptEntity/ScriptTransform handle 返回 invalid，不崩溃。
- [ ] ScriptScene 能通过 Guid 查找 Entity。
- [ ] ScriptScene 能按名称做方便查询。
- [ ] Time API 工作。
- [ ] Input API 至少有一个 Action 查询可用。
- [ ] 现有脚本可迁移到新 API。
- [ ] T00 全部通过。

## Claude 任务指令

```text
为 AiluEngine Lua 建立第一批稳定 Script-facing Facade：
ScriptEntity、ScriptTransform、ScriptScene、ScriptTime、ScriptInput。

原则：
1. 不直接暴露 ECS Component；
2. Transform setter 必须走正式 Transform API，不能直接写内部字段；
3. Entity handle 使用 Scene* + ECS::Entity，并利用 generation 做有效性验证；
4. 跨帧 handle 失效后必须安全返回 invalid；
5. Scene 持久引用以 Entity Guid 为主；
6. 第一版仍允许在 RegisterCoreBindings 中手写 sol2 binding；
7. 不修改 AiluHeadTool；
8. 不实现通用 raw GetComponent；
9. 给出 Lua 示例并运行回归测试。
```

---

# 9. T04：AiluHeadTool 自动生成 sol2 Binding

## 目标

结束不断扩大的：

```cpp
ScriptSystem::RegisterCoreBindings()
```

手写 binding。

目标：

```text
Reflection Annotation
        ↓
AiluHeadTool
        ↓
*.lua.gen.cpp
        ↓
RegisterGeneratedLuaBindings(lua)
```

## 9.1 Metadata

复用现有：

```text
ACLASS
ASTRUCT
AENUM
APROPERTY
AFUNCTION
```

建议增加 Script metadata。

示例：

```cpp
ACLASS()
class ScriptTransform
{
    GENERATED_BODY()

public:
    AFUNCTION(Script)
    Vector3f GetLocalPosition() const;

    AFUNCTION(Script)
    void SetLocalPosition(const Vector3f &position) const;
};
```

或通过现有 metadata parser 支持：

```text
Script
ScriptReadOnly
ScriptName=...
```

具体语法应尽量复用现有 Reflection metadata 格式，不创建独立 Lua annotation parser。

## 9.2 生成代码

例如：

```cpp
void RegisterScriptTransformLuaBinding(sol::state &lua)
{
    auto type = lua.new_usertype<ScriptTransform>("Transform");

    type.set_function("get_local_position", &ScriptTransform::GetLocalPosition);
    type.set_function("set_local_position", &ScriptTransform::SetLocalPosition);
}
```

以及统一入口：

```cpp
void RegisterGeneratedLuaBindings(sol::state &lua)
{
    RegisterScriptEntityLuaBinding(lua);
    RegisterScriptTransformLuaBinding(lua);
    RegisterScriptSceneLuaBinding(lua);
}
```

## 9.3 类型白名单

第一版只支持明确类型：

```text
void
bool

i8/i16/i32/i64
u8/u16/u32/u64

f32/f64

String

Vector2f
Vector3f
Vector4f
Quaternion
Color
Guid

已标记 Script 的 user type
```

遇到不支持类型必须：

```text
AiluHeadTool generation error
```

而不是生成可能编译失败或行为不明的代码。

## 9.4 Overload

第一版可以限制：

```text
同一个 Script type 中不允许同名 overload 自动导出
```

遇到 overload：

- 要求指定 ScriptName。
- 或暂时报 generation error。

不要为了 overload 立刻实现复杂模板推导。

## 9.5 Property

第一版允许生成：

```cpp
sol::property(getter, setter)
```

但只有明确标记的 Script API。

不要自动导出所有 `APROPERTY()`。

## 9.6 Builtin Binding

保留一个很小的：

```cpp
RegisterBuiltinBindings();
```

只负责：

- Lua 基础模块。
- `engine.log`。
- 必要 VM helper。
- Generated binding 入口。

## 验收标准

- [ ] 新增 Script API 不需要修改 `ScriptSystem.cpp`。
- [ ] AiluHeadTool 能识别 Script-marked type/function/property。
- [ ] 能生成至少 ScriptEntity/ScriptTransform binding。
- [ ] 生成代码成功编译。
- [ ] 不支持的参数/返回类型有明确生成期错误。
- [ ] 未标记 Script 的 C++ API 不会进入 Lua。
- [ ] 不使用 `std::any` 做 Lua runtime dispatch。
- [ ] `RegisterCoreBindings()` 明显缩小。
- [ ] T00/T03 测试通过。

## Claude 任务指令

```text
扩展 AiluHeadTool，为标记为 Script 的 Reflection API 自动生成 sol2 binding。

要求：
1. 复用现有 ACLASS/ASTRUCT/APROPERTY/AFUNCTION 解析结果；
2. 不创建第二套 Reflection 元数据系统；
3. 生成强类型 lua.new_usertype / set_function / sol::property；
4. 只导出显式 Script 标记的 API；
5. 第一版使用明确类型白名单；
6. 不支持的签名必须在生成阶段报错；
7. overload 第一版可以要求 ScriptName 或拒绝生成；
8. 生成统一 RegisterGeneratedLuaBindings(sol::state&)；
9. ScriptSystem 只保留 Builtin binding + Generated binding 注册入口；
10. 不实现基于 PropertyInfo/std::any 的动态 Lua bridge。
```

---

# 10. T05：生成 LuaLS / EmmyLua 类型声明

## 目标

让 VS Code 中 Lua 脚本拥有：

- 自动补全。
- 参数提示。
- 返回值类型。
- Entity/Transform/Scene 等类型跳转。
- 生命周期函数提示。

目标生成目录可为：

```text
<Project>/.ailu/lua/
    ailu_engine.lua
    entity.lua
    transform.lua
    scene.lua
    input.lua
```

也可以生成单文件，第一版以简单稳定为主。

## 数据源

必须与 T04 使用相同 Reflection/AHT metadata。

禁止维护另一份人工 Lua API schema。

## 示例输出

```lua
---@class Entity
---@field name string
---@field guid Guid
---@field transform Transform
local Entity = {}

---@return boolean
function Entity:is_valid() end

---@class Transform
local Transform = {}

---@return Vector3
function Transform:get_local_position() end

---@param value Vector3
function Transform:set_local_position(value) end
```

Script 生命周期：

```lua
---@class AiluScript
---@field entity Entity
local AiluScript = {}

function AiluScript:OnCreate() end

---@param dt number
function AiluScript:OnUpdate(dt) end

---@param dt number
function AiluScript:OnFixedUpdate(dt) end
```

## VS Code 配置

如果项目已有 VS Code project generation，可自动加入 LuaLS library path。

如果没有，生成文档说明：

```json
{
    "Lua.workspace.library": [
        ".ailu/lua"
    ]
}
```

不要强行覆盖用户完整 settings.json。

## 验收标准

- [ ] LuaLS 文件由 AiluHeadTool 自动生成。
- [ ] 与 sol2 binding 使用同一 metadata。
- [ ] ScriptEntity/Transform/Scene 至少能自动补全。
- [ ] 函数参数和返回值类型正确。
- [ ] 生命周期可自动补全。
- [ ] 新增一个 Script API 后重新跑 AHT，binding 和 LuaLS 同时更新。
- [ ] 不需要手工维护第二份声明。

## Claude 任务指令

```text
在 T04 AiluHeadTool Script metadata 基础上生成 LuaLS/EmmyLua 类型声明。

要求：
1. sol2 binding 与 LuaLS 使用完全相同的 Reflection metadata；
2. 支持 Script class、function、property、参数、返回值；
3. 生成 Entity/Transform/Scene/Input/Time 的声明；
4. 生成基础 Script 生命周期函数声明；
5. 输出到项目或引擎约定的 .ailu/lua 目录；
6. 不覆盖用户已有 VS Code settings，只提供或增量接入 Lua.workspace.library；
7. 添加测试：新增一个 Script API 后，binding 和 LuaLS 均自动出现。
```

---

# 11. T06：将脚本引用从 Path 升级为 ScriptAsset Guid

## 目标

当前：

```cpp
ScriptComponent::_script_path
```

会导致 Lua 文件移动/重命名时 Scene 引用失效。

最终：

```cpp
ScriptComponent
{
    Guid _script_asset;
}
```

路径由 AssetDatabase / Resource system 解析。

## 新增 ScriptAssetDocument

建议：

```cpp
ACLASS()
class ScriptAssetDocument : public Object
{
    GENERATED_BODY()

public:
    APROPERTY()
    AssetDocumentHeader _header;

    APROPERTY()
    String _file;
};
```

也可以让 `.lua` 本身直接拥有 `.meta` Guid，而不额外包装 AssetDocument。

具体选择必须与当前 AiluEngine Asset Guid 体系一致。

原则：

```text
Scene / Component
    ↓ Guid
Script Asset
    ↓
Current File Path
```

## Scene 序列化

当前类似：

```cpp
struct SceneScriptComponentDocument
{
    String _script_path;
};
```

升级：

```cpp
struct SceneScriptComponentDocument
{
    Guid _script_asset;
};
```

如果需要兼容旧 Scene：

```text
先读 _script_asset
不存在则读 _script_path
然后尝试迁移为 Guid
```

## Inspector

Script Component Inspector 不再要求手输：

```text
Scripts/foo.lua
```

目标变成 Asset picker / drag-drop。

第一版如果 Asset picker 基础设施不足，可以：

- 显示当前 Guid。
- 提供文件选择并转换成 Asset Guid。

但最终不应让路径成为 Scene 的持久引用。

## Prototype key

完成本任务后：

```text
ScriptPrototype key
```

从：

```text
normalized path
```

改为：

```text
ScriptAsset Guid
```

## 验收标准

- [ ] Scene ScriptComponent 保存 ScriptAsset Guid。
- [ ] Lua 文件移动后 Scene 引用仍有效。
- [ ] Prototype cache 使用 ScriptAsset Guid。
- [ ] Inspector 不再依赖纯文本路径作为最终数据源。
- [ ] 旧 scene 如需要可兼容迁移。
- [ ] 脚本热重载可通过 Guid 找到对应 Prototype。
- [ ] T00-T05 回归通过。

## Claude 任务指令

```text
将 AiluEngine ScriptComponent 的持久脚本引用从 String path 升级为 ScriptAsset Guid。

要求：
1. 遵循现有 AssetDocument/Asset Guid 体系；
2. Scene 只持久化 Guid，不持久化 runtime path；
3. Runtime 通过 AssetDatabase/Resource system 从 Guid 解析 Lua 文件；
4. Prototype cache key 改为 ScriptAsset Guid；
5. Inspector 改为资产引用语义；
6. 如当前已有旧 scene 文件，提供最小兼容迁移路径；
7. Lua 文件移动/重命名后，Scene 引用必须仍有效；
8. 不在本任务实现 Script exposed property。
```

---

# 12. T07：Script Exposed Properties 与 Inspector

## 目标

支持类似 Unity MonoBehaviour 的：

```text
Script: PlayerController

Speed      3.0
Target     Player
CanJump    true
```

但数据定义来自 Lua Script Prototype。

## Lua 声明方案

第一版建议显式 schema：

```lua
local Player = {}

Player.__properties = {
    speed = {
        type = "float",
        default = 3.0
    },

    can_jump = {
        type = "bool",
        default = true
    },

    target = {
        type = "entity"
    }
}

return Player
```

不要通过执行随机 Lua 代码猜字段。

## 支持类型

第一版：

```text
bool
int
float
string

vec2
vec3
vec4
color

entity
asset
```

后续再增加：

```text
enum
array
struct
```

## C++ 持久化数据

建议：

```cpp
enum class EScriptPropertyType : u8
{
    kBool,
    kInt,
    kFloat,
    kString,
    kVector2,
    kVector3,
    kVector4,
    kColor,
    kEntity,
    kAsset,
};
```

以及：

```cpp
struct ScriptPropertyData
{
    String _name;
    EScriptPropertyType _type;
    ScriptValue _value;
};
```

`ScriptValue` 可使用：

```text
variant
```

或与当前 Serialize 系统一致的结构。

## Entity 属性

不能保存：

```cpp
ECS::Entity
```

必须保存：

```cpp
Guid
```

运行时创建 Instance 时：

```text
Guid
 ↓
Scene::FindEntity
 ↓
ScriptEntity handle
```

## Asset 属性

保存：

```cpp
Guid
```

不保存 raw pointer。

## Instance 初始化

创建 ScriptInstance：

```text
create instance table
 ↓
copy property values from ScriptComponent
 ↓
instance.entity = ScriptEntity(...)
 ↓
OnCreate
```

## Schema 变化

脚本新增字段：

```text
Scene 中没有
    ↓
使用 default
```

脚本删除字段：

第一版：

```text
可以继续保留 serialized orphan property
但 Inspector 不显示
```

或加载时清理。

推荐先保留，减少 hot reload/rollback 数据丢失。

类型改变：

```text
float -> vec3
```

如果无法安全转换：

- 使用新 default。
- 打 warning。
- 保留旧 serialized value 作为 orphan 或丢弃。

## Inspector

ScriptComponentEditor：

1. Script Asset。
2. 读取 Prototype property schema。
3. 为每个 property 使用 Editor 现有 Property Drawer。
4. 修改后：
   - 写 ScriptComponent serialized property。
   - Mark scene dirty。
5. Play mode 中可选择同步当前 Runtime Instance。

## 验收标准

- [ ] Lua 能声明 property schema。
- [ ] Inspector 自动显示 schema 字段。
- [ ] 修改数值后 Scene 保存/重载不丢失。
- [ ] 两个 Entity 使用同一脚本可有不同属性值。
- [ ] Entity property 持久化为 Guid。
- [ ] Asset property 持久化为 Guid。
- [ ] Script 新增字段使用 default。
- [ ] Script 删除字段不会造成加载崩溃。
- [ ] Script property 在 `OnCreate()` 前写入 instance。
- [ ] T00-T06 回归通过。

## Claude 任务指令

```text
为 AiluEngine Lua Script 实现 exposed properties + Inspector。

要求：
1. Lua prototype 使用显式 __properties schema；
2. 第一版支持 bool/int/float/string/vec2/vec3/vec4/color/entity/asset；
3. ScriptComponent 持久化每个 Entity 自己的 property value；
4. Entity property 使用 Persistent Entity Guid；
5. Asset property 使用 Asset Guid；
6. 创建 ScriptInstance 时先注入 property，再执行 OnCreate；
7. ScriptComponentEditor 根据 schema 自动构建 UI；
8. 属性变化必须 Mark Scene Dirty；
9. schema 新增/删除/类型变化必须安全处理；
10. 不要将 ECS::Entity 或资源裸指针写入 Scene。
```

---

# 13. T08：正式化 Script Hot Reload

## 目标

把当前：

```text
file changed
 ↓
RunFile()
 ↓
Component version check
 ↓
各自 load_file()
```

改为：

```text
Asset changed
 ↓
Compile / Execute New Prototype
 ↓
prototype.version++
 ↓
mark instances stale
 ↓
safe-point recreate / migrate instances
```

## Reload 流程

建议：

```text
FileWatcher
 ↓
Queue ScriptAsset Guid
 ↓
ScriptSystem::ProcessReloadQueue
 ↓
LoadNewPrototype
 ↓
成功？
 ├── No  → 保留旧 Prototype / Instance
 └── Yes
       ↓
   replace Prototype
       ↓
   rebuild instances
```

关键原则：

**新 Prototype 编译失败时，旧游戏逻辑继续运行。**

不要：

```text
syntax error
 ↓
把旧 prototype 删掉
 ↓
所有 instance 失效
```

## Instance 状态迁移

第一版至少迁移 exposed properties。

可选迁移普通 Lua state：

```lua
function Player:OnBeforeReload()
    return {
        runtime_value = self.runtime_value
    }
end

function Player:OnReload(state)
    self.runtime_value = state.runtime_value
end
```

建议普通 Lua runtime state migration 放第二阶段。

第一版只保证：

```text
serialized exposed properties
```

不丢。

## Reload 回调

可增加：

```lua
function Script:OnReload()
end
```

语义：

```text
新 instance 已创建
properties 已注入
然后 OnReload
```

不要同时执行 `OnCreate` 与 `OnReload`，除非定义明确。

推荐：

```text
首次创建    -> OnCreate
热重载重建  -> OnReload
```

如果没有 `OnReload`：

```text
fallback OnCreate
```

也可，但必须文档明确。

## 验收标准

- [ ] 修改 Lua 文件只重新执行一次 Prototype chunk。
- [ ] 100 个 instance 不会重新 `load_file()` 100 次。
- [ ] 新 Prototype 编译失败时旧 Prototype 继续工作。
- [ ] 成功重载后所有相关 instance 使用新函数。
- [ ] exposed properties 保持原值。
- [ ] hot reload 不产生悬空 sol::object。
- [ ] reload 在安全帧点执行，不与 instance callback 并发修改。
- [ ] 错误日志能指出 ScriptAsset/文件。
- [ ] T00-T07 回归通过。

## Claude 任务指令

```text
重构 AiluEngine Script Hot Reload，使 reload 以 ScriptPrototype 为单位。

要求：
1. FileWatcher 只产生 reload request，不直接执行 gameplay script；
2. 一个 ScriptAsset reload 时只重新 load/execute prototype 一次；
3. 新 prototype 成功后再替换旧 prototype；
4. 编译/执行失败时旧 prototype 和旧 instance 必须继续可用；
5. 成功替换后安全重建相关 ScriptInstance；
6. exposed properties 必须保留；
7. reload 在明确 safe point 执行；
8. 第一版不要求迁移任意 Lua runtime 字段；
9. 增加 100 instance reload 测试，证明顶层 chunk 只执行一次。
```

---

# 14. T09：生命周期、错误隔离与最终 Harden

## 目标

将脚本系统从“能运行”提升到可长期用于 gameplay。

## 14.1 生命周期统一

最终建议：

```text
OnCreate
OnEnable
OnDisable
OnFixedUpdate
OnUpdate
OnLateUpdate
OnDestroy
OnReload
```

兼容：

```text
OnInit -> OnCreate
```

旧脚本迁移期可 fallback。

## 14.2 Enabled

`ScriptComponent`：

```cpp
bool _enabled = true;
```

状态变化：

```text
false -> true
    OnEnable

true -> false
    OnDisable
```

disabled 时：

```text
不执行 FixedUpdate / Update / LateUpdate
```

## 14.3 Fault Isolation

ScriptInstance：

```cpp
bool _faulted = false;
```

如果：

```text
OnUpdate error
```

则：

```text
instance._faulted = true
```

之后不再每帧调用。

恢复条件：

```text
Script reload 成功
或 Editor 手动 Restart Script
```

避免：

```text
60 FPS
 ×
同一个 Lua error
 =
每秒 60 条日志
```

## 14.4 Callback Helper 收敛

避免重复实现：

```cpp
InvokeComponentMethod(...)
InvokeComponentMethod(..., f32)
InvokeComponentMethod(..., f32, f32)
```

可统一成模板：

```cpp
template<typename... Args>
bool InvokeScriptCallback(ScriptInstance &instance,
                          sol::protected_function &callback,
                          Args &&...args);
```

保持调用路径简单。

## 14.5 Phase

脚本更新应明确接入现有 Scene/ECS phase。

建议：

```text
OnFixedUpdate
    → fixed gameplay phase

OnUpdate
    → gameplay phase

OnLateUpdate
    → transform/gameplay 后的 late phase
```

不要让 ScriptSystem 形成与 ECS Scheduler 完全独立且顺序模糊的更新体系。

## 14.6 Profiling

建议每个 Script callback 至少能统计：

```text
Script Update Total
Script FixedUpdate Total
Script LateUpdate Total
Script Reload
```

不要默认为每个 Entity 创建昂贵动态 profiler name。

可后续做：

```text
per-script asset profiler
```

## 14.7 Finalize 顺序

必须明确：

```text
Destroy all ScriptInstance
 ↓
Destroy all ScriptPrototype
 ↓
clear protected functions
 ↓
destroy sol::state
```

避免：

```text
sol::table 析构时 Lua VM 已不存在
```

## 验收标准

- [ ] `OnCreate/OnEnable/OnDisable/OnDestroy` 语义明确。
- [ ] disabled script 不执行 update。
- [ ] 单个 Instance error 后被 fault isolation。
- [ ] 一个错误脚本不会影响其他 ScriptInstance。
- [ ] reload 成功后 faulted instance 可恢复。
- [ ] callback 调用逻辑无大量重复 overload。
- [ ] 生命周期函数接入明确的 Scene/System phase。
- [ ] Finalize 无 use-after-free。
- [ ] Debug/Release 均跑通过脚本测试。
- [ ] 完整 T00-T09 回归通过。

## Claude 任务指令

```text
对 AiluEngine Lua ScriptSystem 做最终 lifecycle/error hardening。

要求：
1. 生命周期规范为 OnCreate/OnEnable/OnDisable/OnFixedUpdate/OnUpdate/OnLateUpdate/OnDestroy/OnReload；
2. OnInit 保留兼容 fallback；
3. ScriptComponent 支持 enabled；
4. 单个 callback 抛错后仅 fault 当前 ScriptInstance；
5. faulted instance 不再每帧重复调用和刷日志；
6. reload 成功可恢复 faulted instance；
7. 收敛重复 InvokeComponentMethod overload；
8. 将 Script lifecycle 明确接入现有 Scene/ECS 更新 phase；
9. 明确 ScriptSystem Finalize 的 Lua 对象销毁顺序；
10. 增加最终回归测试和 profiler 基础统计。
```

---

# 15. 第二批 Facade API 建议

T03 完成并稳定后再扩展。

## ScriptCamera

```lua
camera.fov
camera.near_clip
camera.far_clip
camera.orthographic_size
```

setter 调用 Camera 正式接口。

## ScriptRigidBody

```lua
rigidbody.velocity
rigidbody.mass
rigidbody:add_force(...)
```

不要直接把 Physics backend handle 暴露给 Lua。

## ScriptAnimator

```lua
animator:play("Idle")
animator:set_float("Speed", 1.0)
animator:set_bool("Grounded", true)
```

## ScriptAudio

```lua
audio:play()
audio:stop()
audio.volume = 0.8
```

## Asset Handle

统一：

```text
ScriptTexture
ScriptMaterial
ScriptMesh
ScriptAudioAsset
```

Gameplay 只获得受控 handle，不获得底层 RHI/Resource pointer。

---

# 16. 暂时不要做的功能

以下内容不建议混入第一轮脚本系统：

## 16.1 任意 C++ Reflection 动态调用

不要：

```lua
obj:invoke("AnyFunction", ...)
```

## 16.2 Lua 直接操作 Render/RHI

不要：

```lua
device:create_texture(...)
command_list:draw(...)
```

## 16.3 任意 ECS raw component memory

不要：

```lua
entity:get_raw_component(...)
```

## 16.4 Script 多线程执行

第一阶段继续：

```text
单 Lua VM
单线程 gameplay script
```

等 Script API 和状态边界稳定以后，再讨论多 VM 或 worker VM。

## 16.5 Coroutine Scheduler

Lua coroutine 可以继续由 Lua 自己使用，但不要立刻设计完整 Unity-style coroutine scheduler。

## 16.6 Network Replication

Script property replication 应在游戏网络模型确定后单独设计。

---

# 17. 最终目标代码边界

建议最终目录趋向：

```text
Engine/
├── Inc/
│   └── Framework/
│       └── Script/
│           ├── ScriptSystem.h
│           ├── ScriptInstance.h
│           ├── ScriptPrototype.h
│           ├── ScriptValue.h
│           └── Api/
│               ├── ScriptEntity.h
│               ├── ScriptTransform.h
│               ├── ScriptScene.h
│               ├── ScriptTime.h
│               └── ScriptInput.h
│
├── Src/
│   └── Framework/
│       └── Script/
│           ├── ScriptSystem.cpp
│           ├── ScriptInstance.cpp
│           ├── ScriptPrototype.cpp
│           ├── ScriptBindings.cpp
│           ├── Generated/
│           │   └── ScriptBindings.gen.cpp
│           └── Api/
│               ├── ScriptEntity.cpp
│               ├── ScriptTransform.cpp
│               ├── ScriptScene.cpp
│               ├── ScriptTime.cpp
│               └── ScriptInput.cpp
```

实际目录可根据当前工程 CMake/VS project 组织调整，不要求为了符合本文而大规模搬文件。

---

# 18. 最终 ScriptSystem 职责

最终 `ScriptSystem` 建议只承担：

```text
ScriptSystem
 ├── Lua VM lifecycle
 ├── Builtin binding registration
 ├── Generated binding registration
 ├── ScriptPrototype cache
 ├── ScriptInstance registry
 ├── Script lifecycle dispatch
 ├── Script property injection
 ├── Hot reload
 └── Error reporting / fault isolation
```

不负责：

```text
ECS raw component reflection
Renderer internals
Asset import implementation
Editor Widget implementation
```

---

# 19. 最终 Lua 使用体验示例

```lua
local PlayerController = {}

PlayerController.__properties = {
    speed = {
        type = "float",
        default = 5.0
    },

    target = {
        type = "entity"
    }
}

function PlayerController:OnCreate()
    self.transform = self.entity.transform
end

function PlayerController:OnUpdate(dt)
    local position = self.transform.position

    if input:is_down("MoveForward") then
        position.z = position.z + self.speed * dt
    end

    self.transform.position = position
end

function PlayerController:OnDestroy()
end

return PlayerController
```

Inspector：

```text
Script
    PlayerController.lua

Properties

Speed
    5.0

Target
    Player
```

场景内部持久化：

```text
Script Asset
    → Asset Guid

Target Entity
    → Persistent Entity Guid
```

runtime：

```text
Script Asset Guid
    ↓
ScriptPrototype
    ↓
ScriptInstance
    ↓
ScriptEntity
    ↓
Scene + runtime ECS Entity
```

---

# 20. 每个任务包统一验收模板

Claude 完成每个任务后，要求按以下格式汇报：

```text
## 修改文件

- ...
- ...

## 新增文件

- ...
- ...

## 核心设计

...

## 与旧实现的兼容性

...

## 删除/废弃内容

...

## 测试

### Build

Debug:
PASS / FAIL

Release:
PASS / FAIL

### Script Regression

T00 Basic:
PASS / FAIL

T00 Lifecycle:
PASS / FAIL

T00 Multiple Instance:
PASS / FAIL

T00 Error:
PASS / FAIL

### 本任务新增测试

...

## 已知问题

...

## 未完成内容

...

## 是否满足任务包验收标准

逐条列出：
- [x] ...
- [ ] ...
```

不要只给：

```text
Implemented successfully.
```

必须逐项给出可验证结果。

---

# 21. 推荐实际执行节奏

第一轮建议只交给 Claude：

```text
T00
T01
T02
T03
```

完成后人工检查 Lua 使用体验。

确认 Facade API 方向满意，再做：

```text
T04
T05
```

因为 AHT 自动生成一旦确定下来，会成为长期基础设施。

之后再做资产/编辑器层：

```text
T06
T07
```

最后：

```text
T08
T09
```

这种顺序可以避免在 Script API 尚未稳定时就提前把错误模型固化进代码生成器、Asset 格式和 Inspector。

---

# 22. 最终完成定义

整个 Lua Script System 第一阶段完成时，应满足：

- [ ] ECS ScriptComponent 为纯可序列化数据。
- [ ] Lua runtime state 全部由 ScriptSystem 管理。
- [ ] 同一脚本文件只拥有一个共享 Prototype。
- [ ] 每个 Entity 拥有独立 ScriptInstance。
- [ ] 常用 gameplay API 通过安全 Facade 暴露。
- [ ] Transform 等关键状态修改不会绕过 Engine invariants。
- [ ] Script API 由 Reflection metadata 选择。
- [ ] sol2 binding 由 AiluHeadTool 自动生成。
- [ ] LuaLS 声明由同一 metadata 自动生成。
- [ ] Script Component 使用 Asset Guid。
- [ ] Script property 可在 Inspector 编辑和序列化。
- [ ] Entity property 使用 Persistent Entity Guid。
- [ ] Hot Reload 以 Prototype 为单位。
- [ ] Hot Reload 失败不破坏旧运行状态。
- [ ] 单个 Lua Instance error 被隔离。
- [ ] VS Code 中 Script API 具备基本自动补全。
- [ ] Debug / Release 构建和脚本回归测试全部通过。

完成这些之后，AiluEngine 的 Lua 层才适合继续扩展：

```text
Physics callback
Animation event
Gameplay event bus
Coroutine
Prefab integration
SaveGame integration
AI scripting
Editor custom script tools
```

而不会继续把临时 API 堆进 `RegisterCoreBindings()`。
