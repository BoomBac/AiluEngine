# 1. 目标

在保留现有 Lua Runtime 架构的前提下，重做 Lua-facing API，使其适合类似《潜水员戴夫》体量的 2D / 2.5D 游戏开发。

核心目标：

- Lua 不感知 ECS 内部结构。
- 不直接暴露原始 Component。
- 不使用 `SetFloat("xxx")` 一类字符串属性接口。
- Gameplay API 以少量强类型 façade 为主。
- 常用成员使用 Lua property，而不是大量 `GetXXX/SetXXX`。
- Entity 作为 Gameplay API 核心入口。
- Prefab 作为运行时实体创建的主要方式。
- AiluHeadTool 负责自动生成 façade 的 sol2 binding 和 LuaLS declaration。
- AiluHeadTool 不自动决定哪些 Engine 类型应该成为 Gameplay API。
- API 保持轻量，只实现实际游戏需要的能力。

---

# 2. 总体架构

保留现有 Runtime：

```text
ScriptAsset
    ↓
ScriptPrototype
    ↓
ScriptInstance
    ↓
Lua VM
```

Lua API 独立为：

```text
Lua
 │
 ▼
Script API Facade
 │
 ▼
AiluEngine Runtime / ECS / Physics / Render
```

推荐目录：

```text
Engine/Framework/Script/
    ScriptSystem.*
    ScriptRuntime.*

    API/
        ScriptEntity.*
        ScriptTransform.*
        ScriptRigidBody2D.*
        ScriptCollider2D.*
        ScriptSpriteRenderer.*
        ScriptAnimator.*
        ScriptAudioSource.*
        ScriptCamera.*

        ScriptScene.*
        ScriptInput.*
        ScriptPhysics2D.*
        ScriptTime.*
```

Facade 分两类。

对象 View：

```text
Entity
Transform
RigidBody2D
Collider2D
SpriteRenderer
Animator
AudioSource
Camera
```

全局 Service：

```text
scene
input
physics2d
time
audio
```

---

# 3. 保留现有 Runtime

不要重写以下结构，除非适配新 API 必须小范围调整：

```text
ScriptInstanceKey
ScriptInstance
ScriptPrototype

_instances
_prototypes

EnsureComponentReady()
LoadPrototype()
LoadComponentInstance()

InjectScriptProperties()
SynchronizeScriptProperties()

CacheLifecycleFunctions()

InvokeLuaCallback()
faulted/error isolation

RegisterSubscription()
ClearSubscriptions()

ProcessReload()
RebuildInstancesForPrototype()
```

继续保留：

- 单 Lua VM。
- Prototype 缓存。
- 每 Entity 独立 ScriptInstance。
- ScriptAsset。
- ECS::ScriptComponent。
- Inspector exposed properties。
- 热重载。
- Lua error isolation。
- 生命周期 callback。
- subscription 自动清理。

本次属于：

```text
Lua API Redesign
```

不是：

```text
Script Runtime Rewrite
```

---

# 4. 删除旧通用 Component Lua API

删除或停止向 Lua 暴露当前通用：

```text
ScriptComponent facade
```

特别是：

```cpp
SetFloat(...)
SetInt(...)
SetBool(...)
SetString(...)
SetVector2(...)
SetVector3(...)
```

以及：

```cpp
AddEntityBoxShape(...)
SetEntityFloat(...)
SetSprite(...)
```

不再允许：

```lua
entity:get_component("Ailu.ECS.RigidBody2DComponent")
component:set_float("gravity_scale", 1.0)
```

也不要设计：

```lua
entity:set_component_property("xxx", "yyy", value)
```

字符串 Reflection Bridge 不作为 Gameplay API。

`ECS::ScriptComponent` 继续保留，仅负责：

```text
script asset
serialized exposed properties
```

---

# 5. Entity API

新增轻量：

```cpp
struct ScriptEntity
{
    SceneManagement::Scene *_scene = nullptr;
    ECS::Entity _entity = ECS::kInvalidEntity;
};
```

不得直接向 Lua 暴露：

```text
ECS::Entity
Registry
Component*
```

Lua：

```lua
entity.name
entity.tag
entity.active
entity.guid

entity.transform
entity.rigidbody2d
entity.collider2d
entity.sprite
entity.animator
entity.audio

entity:destroy()
```

不存在的可选 Component 返回 `nil`：

```lua
local rb = entity.rigidbody2d

if rb then
    rb.velocity = vec2(5, 0)
end
```

要求：

- 每次访问验证 Scene 和 Entity generation。
- 不长期缓存裸 Component 指针。
- Entity 删除后所有 façade 安全失效。

---

# 6. Transform API

新增：

```cpp
ScriptTransform
```

Lua：

```lua
transform.position
transform.rotation
transform.scale

transform.local_position
transform.local_rotation
transform.local_scale
```

可增加只读：

```lua
transform.forward
transform.right
transform.up
```

底层必须调用正式 Transform API：

```cpp
SetLocalPosition()
SetLocalRotation()
SetLocalScale()
```

禁止直接修改 Component 内部字段。

第一版无需完整 hierarchy 编辑 API。

---

# 7. RigidBody2D API

将当前属于全局 `ScriptPhysics2D`、但实际作用于单个刚体的操作迁移到：

```cpp
ScriptRigidBody2D
```

Lua：

```lua
rb.velocity
rb.angular_velocity
rb.gravity_scale
rb.fixed_rotation

rb:add_force(force)
rb:add_impulse(impulse)
rb:add_torque(torque)
```

目标：

```lua
self.entity.rigidbody2d.velocity = vec2(5, 0)
```

不再使用：

```lua
physics2d.set_linear_velocity(entity, velocity)
```

底层必须通过正式 Physics2D 接口同步物理状态。

---

# 8. Collider2D API

第一版保持极简：

```lua
collider.enabled
collider.is_trigger
```

如果当前 Physics API 已稳定支持运行时修改，可增加：

```lua
collider.layer
collider.mask
```

不要第一版暴露：

```text
shape array
fixture
Box2D internal object
polygon raw vertices
```

Collider 几何主要由 Prefab / Inspector 配置。

---

# 9. Physics2D 全局 API

`physics2d` 只负责 world query。

例如：

```lua
local hit = physics2d.raycast(origin, direction, distance, layer_mask)

if hit then
    print(hit.entity)
    print(hit.point)
    print(hit.normal)
end
```

新增轻量：

```cpp
ASTRUCT(Script)
struct ScriptRaycastHit2D
{
    GENERATED_BODY()

    ScriptEntity _entity;
    Vector2f _point;
    Vector2f _normal;
    f32 _distance;
};
```

第一版支持：

```text
raycast
overlap_circle
overlap_box
```

无需复杂 allocation-free query API。

---

# 10. SpriteRenderer API

新增：

```cpp
ScriptSpriteRenderer
```

Lua：

```lua
sprite.sprite
sprite.visible
sprite.flip_x
sprite.flip_y
sprite.order
sprite.color
```

目标：

```lua
self.entity.sprite.sprite = self.dead_sprite
```

不要：

```lua
self.entity.sprite:set_sprite(self.dead_sprite_guid)
```

Guid 属于底层实现细节。

---

# 11. Animator API

如果当前 Animation 系统已有对应能力，新增：

```cpp
ScriptAnimator
```

Lua 第一版：

```lua
animator:play("idle")
animator:play("swim")
animator:play("attack")

animator.speed
```

如果已有 cross fade：

```lua
animator:cross_fade("attack", 0.1)
```

第一版不要导出完整 Animation State Machine 内部结构。

---

# 12. Audio API

新增：

```cpp
ScriptAudioSource
```

Lua：

```lua
audio_source:play()
audio_source:stop()

audio_source.volume
audio_source.pitch
audio_source.loop
```

可增加全局：

```lua
audio.play_one_shot(clip, position)
```

不要为了脚本接口额外重做音频系统。

---

# 13. Scene API

每个 ScriptInstance 注入：

```lua
scene
```

Lua：

```lua
scene.find(name)
scene.find_guid(guid)

scene.spawn(prefab)

scene.main_camera
```

主要使用：

```lua
local bullet = scene.spawn(self.bullet_prefab)
bullet.transform.position = spawn_position
```

不鼓励：

```lua
scene.create_entity("Bullet")
entity:add_component(...)
```

Prefab 是主要 Runtime Entity 构造方式。

---

# 14. Input API

统一为：

```lua
input.pressed("jump")
input.released("jump")
input.down("attack")

input.axis("zoom")
input.axis2("move")
```

Gameplay 推荐 polling：

```lua
function player:on_update(dt)
    local move = input.axis2("move")

    if input.pressed("attack") then
        self:attack()
    end
end
```

现有 subscription 系统继续保留，但不是 Input 默认使用方式。

---

# 15. Time API

提供：

```lua
time.time
time.delta_time
time.fixed_delta_time
time.render_alpha
```

生命周期已有 `dt` 参数时优先使用：

```lua
function player:on_update(dt)
end
```

---

# 16. Camera API

新增：

```cpp
ScriptCamera
```

Lua：

```lua
camera.orthographic_size
camera.fov

camera:world_to_screen(position)
camera:screen_to_world(position)
```

通过：

```lua
scene.main_camera
```

获得。

---

# 17. 生命周期

新的 Lua 生命周期统一使用 snake_case：

```lua
function script:on_create()
end

function script:on_enable()
end

function script:on_fixed_update(dt)
end

function script:on_update(dt)
end

function script:on_late_update(dt)
end

function script:on_disable()
end

function script:on_destroy()
end

function script:on_reload()
end
```

迁移期 Runtime 可兼容：

```text
on_create
    fallback -> OnCreate
    fallback -> OnInit
```

其他生命周期同理。

新脚本模板和 LuaLS declaration 只使用 snake_case。

---

# 18. Physics Collision 生命周期

Physics2D callback 自动转发给 ScriptInstance：

```lua
function script:on_collision_enter(other, hit)
end

function script:on_collision_exit(other)
end

function script:on_trigger_enter(other)
end

function script:on_trigger_exit(other)
end
```

`other`：

```text
ScriptEntity
```

`hit`：

```lua
hit.point
hit.normal
```

要求：

- Entity 已销毁时不调用。
- Script faulted 时不调用。
- Disabled Script 按现有 enable 规则处理。
- Lua 不直接订阅 PhysicsWorld / Box2D 内部事件。

---

# 19. Script Exposed Properties

继续保留当前：

```text
ScriptPropertyData
```

以及已有类型：

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

不重写 Inspector Runtime。

Lua property schema 继续沿用现有实现。

目标体验：

```lua
local diver = {}

diver.move_speed = 6.0
diver.damage = 20

return diver
```

Asset 类型 Runtime 最终注入 Script Asset Handle，而不是 GUID string。

---

# 20. Asset Handle

新增或复用轻量 Script Asset View。

至少覆盖：

```text
Prefab
Sprite
Texture
Material
AudioClip
AnimationClip
```

Lua 不处理：

```text
Guid string
filesystem path
ResourceMgr
```

目标：

```lua
scene.spawn(self.enemy_prefab)

self.entity.sprite.sprite = self.hit_sprite
```

Asset Handle 只需要包含：

```text
type
guid/internal handle
valid
```

保持轻量 value type。

---

# 21. AiluHeadTool Lua Binding 改造

## 21.1 改造目标

AiluHeadTool 继续负责：

```text
C++ Script Facade
        ↓
generated sol2 binding
        ↓
LuaLS declaration
```

但必须明确：

```text
Reflection 能访问
    ≠
Gameplay Lua 应该访问
```

AHT 只为明确声明的 Script-facing façade 生成 Lua API。

不要自动导出所有：

```text
ACLASS
ASTRUCT
APROPERTY
AFUNCTION
```

---

## 21.2 增加 Script API 类型标记

推荐扩展：

```cpp
ASTRUCT(Script)
struct ScriptEntity
{
};
```

以及：

```cpp
ACLASS(Script)
class SomeScriptFacade
{
};
```

AHT `ClassInfo` 增加：

```cpp
bool _is_script_api = false;
```

解析：

```text
ASTRUCT(Script)
ACLASS(Script)
```

时设置：

```cpp
_is_script_api = true;
```

`GenerateLuaBindings()` 和 `GenerateLuaDeclarations()` 首先判断：

```cpp
if (!type._is_script_api)
    continue;
```

不要再依赖：

```text
只要类型内部存在 AFUNCTION(Script)
就自动把整个类型变成 Lua usertype
```

---

# 22. AiluHeadTool Script Method

普通 Script method 使用：

```cpp
AFUNCTION(Script)
void AddForce(const Vector2f &force) const;
```

自动生成：

```cpp
type_script_rigid_body_2d.set_function("add_force", &ScriptRigidBody2D::AddForce);
```

Lua：

```lua
rb:add_force(vec2(0, 5))
```

AHT 继续负责：

```text
AddForce
    ↓
add_force
```

CamelCase → snake_case 转换。

---

# 23. AiluHeadTool Script Property

新增：

```cpp
AFUNCTION(ScriptProperty)
```

用于声明 Lua property getter/setter。

例如：

```cpp
ASTRUCT(Script)
struct ScriptRigidBody2D
{
    GENERATED_BODY()

    AFUNCTION(ScriptProperty)
    Vector2f GetVelocity() const;

    AFUNCTION(ScriptProperty)
    void SetVelocity(const Vector2f &velocity) const;

    AFUNCTION(Script)
    void AddForce(const Vector2f &force) const;
};
```

AHT 自动识别：

```text
GetVelocity
SetVelocity
```

对应：

```text
velocity
```

生成：

```cpp
type_script_rigid_body_2d["velocity"] =
    sol::property(&ScriptRigidBody2D::GetVelocity, &ScriptRigidBody2D::SetVelocity);
```

Lua：

```lua
rb.velocity = vec2(10, 0)
```

---

## 23.1 只读 Property

例如：

```cpp
AFUNCTION(ScriptProperty)
String GetGuid() const;
```

生成：

```cpp
type_script_entity["guid"] = sol::property(&ScriptEntity::GetGuid);
```

Lua：

```lua
local guid = entity.guid
```

不能写入。

---

## 23.2 Property 配对规则

支持：

```text
GetXxx()
SetXxx(value)
```

自动形成：

```text
xxx
```

也支持：

```text
IsXxx()
SetXxx(bool)
```

形成：

```text
xxx
```

例如：

```cpp
AFUNCTION(ScriptProperty)
bool IsVisible() const;

AFUNCTION(ScriptProperty)
void SetVisible(bool visible) const;
```

生成：

```lua
sprite.visible
```

遇到无法配对的 setter 时 AHT 应报错，而不是静默跳过。

---

# 24. Script API 类型自动注册

当前 `IsSupportedLuaType()` 不应长期维护：

```text
ScriptEntity
ScriptTransform
ScriptScene
ScriptInput
ScriptTime
...
```

这种手写白名单。

改为两部分。

基础类型白名单：

```text
void
bool

f32
f64
float
double

i32
u32
i64
u64

String

Vector2f
Vector3f
Vector4f
Quaternion
Color
```

加：

```text
所有 ASTRUCT(Script)
所有 ACLASS(Script)
```

AHT 在一次扫描完成后建立：

```cpp
HashSet<String> script_api_types;
```

因此以后新增：

```cpp
ASTRUCT(Script)
struct ScriptAnimator
```

其返回值和参数自动成为合法 Lua binding 类型。

无需手动修改：

```cpp
IsSupportedLuaType()
```

---

# 25. 全局 Service Metadata

不要继续在 AHT 中增加：

```cpp
if (type._name == "ScriptEngine")
if (type._name == "ScriptInput")
if (type._name == "ScriptTime")
...
```

推荐支持：

```cpp
ASTRUCT(Script; Global="input")
struct ScriptInput
{
};
```

例如：

```cpp
ASTRUCT(Script; Global="physics2d")
struct ScriptPhysics2D
{
};
```

AHT `ClassInfo` 增加：

```cpp
String _script_global_name;
```

如果非空，自动生成 global service binding。

例如：

```text
Global="input"
```

最终生成：

```lua
input
```

而不是：

```lua
ScriptInput
```

作为主要使用入口。

---

## 25.1 Scene 特殊处理

`scene` 不属于真正的全局 singleton。

每个 ScriptInstance 对应的 Scene 可能不同，因此：

```lua
scene
```

仍由：

```cpp
LoadComponentInstance()
```

注入当前 ScriptInstance。

不要通过 `ASTRUCT(... Global="scene")` 注册成静态全局 singleton。

同理：

```lua
self.entity
```

仍由 ScriptInstance 注入。

---

# 26. LuaLS Declaration 自动生成

`GenerateLuaDeclarations()` 必须使用与 Runtime binding 相同的 metadata。

例如：

```cpp
ASTRUCT(Script)
struct ScriptRigidBody2D
```

生成：

```lua
---@class ScriptRigidBody2D
---@field velocity Vec2
---@field gravity_scale number
local ScriptRigidBody2D = {}

---@param force Vec2
function ScriptRigidBody2D:add_force(force) end
```

Entity：

```lua
---@class ScriptEntity
---@field name string
---@field guid string
---@field transform ScriptTransform
---@field rigidbody2d ScriptRigidBody2D|nil
---@field collider2d ScriptCollider2D|nil
---@field sprite ScriptSpriteRenderer|nil
```

要求：

```text
Runtime API
LuaLS API
```

必须由同一 metadata 生成，避免漂移。

---

# 27. LuaLS 生命周期更新

删除旧 declaration 中硬编码：

```lua
function AiluScript:OnCreate()
function AiluScript:OnUpdate(dt)
```

改为：

```lua
---@class AiluScript
---@field entity ScriptEntity
---@field scene ScriptScene
local AiluScript = {}

function AiluScript:on_create() end

function AiluScript:on_enable() end

---@param dt number
function AiluScript:on_fixed_update(dt) end

---@param dt number
function AiluScript:on_update(dt) end

---@param dt number
---@param render_alpha number
function AiluScript:on_late_update(dt, render_alpha) end

function AiluScript:on_disable() end

function AiluScript:on_destroy() end

function AiluScript:on_reload() end
```

---

# 28. AiluHeadTool 不做的事情

明确不要实现：

```text
Runtime Reflection Invoke
std::any Lua Bridge
string function invocation
string property access
自动导出全部 ACLASS
自动导出全部 ASTRUCT
自动导出全部 APROPERTY
自动暴露 ECS Component
复杂 Lua binding DSL
```

AiluHeadTool 的定位：

```text
人为设计 Gameplay API
        ↓
AHT 自动生成机械 binding
```

而不是：

```text
Engine Reflection
        ↓
自动变成 Gameplay API
```

---

# 29. 推荐 Facade 示例

最终 C++ 应接近：

```cpp
ASTRUCT(Script)
struct AILU_API ScriptEntity
{
    GENERATED_BODY()

    AFUNCTION(ScriptProperty)
    String GetName() const;

    AFUNCTION(ScriptProperty)
    void SetName(const String &name) const;

    AFUNCTION(ScriptProperty)
    ScriptTransform GetTransform() const;

    AFUNCTION(ScriptProperty)
    ScriptRigidBody2D GetRigidBody2D() const;

    AFUNCTION(Script)
    void Destroy() const;
};
```

AHT 自动生成 Lua 体验：

```lua
entity.name = "Player"

entity.transform.position = vec3(1, 2, 0)

local rb = entity.rigidbody2d
if rb then
    rb.velocity = vec2(4, 0)
end

entity:destroy()
```

注意：

如果 Component 不存在，需要 Lua 返回 `nil`，具体 C++ 返回类型可根据 sol2 能力选择：

```text
optional façade
sol::optional
专门 getter lambda
```

不要为了生成器方便返回一个“invalid façade”让 Lua 自己反复 `is_valid()`。

---

# 30. Lua 命名规范

Lua-facing API：

```text
snake_case
```

例如：

```lua
rigidbody.gravity_scale
physics2d.raycast
scene.main_camera
input.axis2
```

C++ 保持：

```cpp
GetPosition()
SetPosition()
AddForce()
```

Binding 层负责转换。

---

# 31. 推荐最终 Lua 示例

## Diver

```lua
local diver = {}

function diver:on_create()
    self.rb = self.entity.rigidbody2d
    self.animator = self.entity.animator
end

function diver:on_update(dt)
    local move = input.axis2("move")

    self.rb.velocity = vec2(move.x * self.move_speed, move.y * self.move_speed)

    if move:length_squared() > 0.01 then
        self.animator:play("swim")
    else
        self.animator:play("idle")
    end

    if input.pressed("harpoon") then
        self:fire_harpoon()
    end
end

function diver:fire_harpoon()
    local harpoon = scene.spawn(self.harpoon_prefab)

    harpoon.transform.position = self.entity.transform.position
    harpoon.rigidbody2d.velocity = self.entity.transform.right.xy * 15.0
end

return diver
```

## Fish

```lua
local fish = {}

function fish:on_create()
    self.rb = self.entity.rigidbody2d
    self.health = self.max_health
end

function fish:on_update(dt)
    local player = scene.find("Player")

    if not player then
        return
    end

    local delta = player.transform.position - self.entity.transform.position
    self.rb.velocity = delta:normalized().xy * self.speed
end

function fish:on_trigger_enter(other)
    if other.tag ~= "Harpoon" then
        return
    end

    self.health = self.health - 25

    if self.health <= 0 then
        self.entity:destroy()
    end
end

return fish
```

示例中当前 Engine 不存在的能力不要为了示例强行补齐。

---

# 32. 实施顺序

## T01：建立新 API 回归基线

保留现有 Script Runtime tests。

新增最小测试覆盖：

```text
prototype
instance
hot reload
property injection
fault isolation
```

---

## T02：AiluHeadTool 基础改造

优先实现：

```text
ASTRUCT(Script)
ACLASS(Script)

AFUNCTION(Script)
AFUNCTION(ScriptProperty)

Script API Type Registry

CamelCase -> snake_case

LuaLS property declaration
```

这一阶段必须先做，避免后续 façade 手写大量 sol2 binding。

验收：

```cpp
ASTRUCT(Script)
struct ScriptTest
```

能够自动生成：

```lua
test.value
test:add_value(...)
```

---

## T03：Entity + Transform

实现：

```text
ScriptEntity
ScriptTransform
```

并完全使用 AHT 自动生成 binding。

验收：

```lua
self.entity.name = "Player"
self.entity.transform.position = vec3(...)
```

---

## T04：Scene + Prefab

实现：

```text
scene.find
scene.find_guid
scene.spawn
scene.main_camera
```

验收：

```lua
local entity = scene.spawn(prefab)
```

---

## T05：Input + Time + Global Service Metadata

实现：

```text
ASTRUCT(Script; Global="input")
ASTRUCT(Script; Global="time")
```

以及：

```text
input.pressed
input.down
input.axis
input.axis2

time.time
time.delta_time
```

---

## T06：RigidBody2D + Physics2D

实现：

```text
ScriptRigidBody2D
ScriptPhysics2D
ScriptRaycastHit2D
```

将刚体操作从全局 Physics API 迁移到 Rigidbody façade。

---

## T07：Physics Callback

实现：

```text
on_collision_enter
on_collision_exit
on_trigger_enter
on_trigger_exit
```

---

## T08：Sprite + Animator + Audio + Camera

按照当前 Engine 已存在的真实功能逐个增加 façade。

不要为了 API 完整度新增大型 Engine 子系统。

---

## T09：Asset Handle

去除 Gameplay API 中的 GUID string。

实现轻量：

```text
PrefabAsset
SpriteAsset
AudioClipAsset
...
```

---

## T10：LuaLS 完整化

补齐：

```text
所有 Script façade
所有 property
所有 method
global services
AiluScript lifecycle
nullable component view
Asset Handle
```

保证实际 binding 与 declaration 一致。

---

## T11：删除旧 Lua API

全部新 API 测试完成后删除：

```text
ScriptComponent generic facade

SetFloat
SetBool
SetVector*

generic component property bridge

旧 Physics entity operation API

旧 Lua getter/setter API

旧 LuaLS declaration
```

---

# 33. 第一版范围限制

不要实现：

```text
Lua 直接访问 ECS Registry

任意 Component 动态创建

任意 Component Reflection 修改

std::any Lua Bridge

Lua 多线程执行

复杂 coroutine scheduler

复杂 async/await

Gameplay Ability System

大型 Gameplay Tag framework

复杂 event bus

Lua inheritance framework

Dependency Injection

复杂 Lua module hot reload dependency graph
```

Lua 原生：

```text
table
function
require
coroutine
```

已经足够。

---

# 34. 验收标准

最终仅使用 Lua 可以完成：

```text
玩家移动
游泳
攻击
发射鱼叉

鱼 AI
目标追踪

受击
死亡

Trigger
Collision

Prefab Spawn

动画切换
音效播放

Camera 简单控制

Input

场景对象查找

Inspector 参数配置

热重载
```

Gameplay Lua 中不出现：

```text
ECS

Registry

Component type string

SetFloat
SetBool
SetVector3

Guid string asset API
```

Lua 主要围绕：

```text
self.entity
scene
input
physics2d
time
```

和少量 Typed View 编写。

---

# 35. Claude 实现要求

基于当前 AiluEngine 最新代码逐任务实施。

约束：

1. 不重写现有 Script Runtime。
2. 优先复用 ScriptPrototype / ScriptInstance / property / reload / error isolation。
3. Lua-facing API 可以全部重做。
4. Lua 不直接暴露 ECS Component 内存。
5. 不实现字符串 Reflection Component Bridge。
6. 不使用 `std::any` 构建通用动态调用。
7. Script façade 使用轻量 `scene + entity` handle。
8. 不长期缓存裸 Component 指针。
9. Lua API 使用 snake_case。
10. C++ 保持 AiluEngine 当前代码规范。
11. AiluHeadTool 在 façade 大量实现前先支持 `ScriptProperty`。
12. Runtime binding 与 LuaLS declaration 必须来自同一 metadata。
13. 每个任务完成后工程必须可编译。
14. 每个任务尽可能独立、可回滚。
15. 不为了 API 完整度实现当前 Engine 不存在的功能。
16. 第一版以支撑类似《潜水员戴夫》的项目为目标，而不是制作通用商业引擎级脚本接口。
17. 每个阶段补充最小 Lua regression test。
18. 删除旧 API 必须放在新 API 完成并通过测试之后。