# AiluEngine 2D Collision Channel / Response 开发任务

## 目标

为现有 `Physics2D` 增加一套类似 UE、但更轻量的碰撞过滤与响应系统，解决：

- 角色站在地面上也会收到普通 Collision 事件，脚本需要大量手动过滤。
- 不同类型对象之间需要配置 `Ignore / Overlap / Block`。
- Collider 编辑器面板需要直观配置碰撞行为。
- Lua 层能够获取碰撞对象类型，但不应承担主要过滤职责。

整体设计要求：

- 轻量。
- 数据驱动。
- 适合“潜水员戴夫”体量项目。
- 不复用 Render Layer，Physics Collision Channel 独立存在。
- 尽量复用现有 `Collider2D / PhysicsWorld2D / ContactEvent` 架构。

---

## 1. 核心概念

每个 Collider2D 包含：

1. `Object Type`
2. 对其他 Object Type 的 `Collision Response`

Response 共三种：

```cpp
enum class ECollisionResponse2D : uint8_t
{
    kIgnore,
    kOverlap,
    kBlock
};
```

基础 Channel：

```cpp
enum class ECollisionChannel2D : uint8_t
{
    kWorldStatic,
    kWorldDynamic,
    kPlayer,
    kEnemy,
    kProjectile,
    kTrigger,
    kPickup,

    kCount
};
```

后续可继续扩展，但第一版不要做动态 Channel 注册系统。

---

## 2. Collision Profile

建议数据结构：

```cpp
struct CollisionProfile2D
{
    ECollisionChannel2D _object_type = ECollisionChannel2D::kWorldDynamic;

    std::array<ECollisionResponse2D, static_cast<size_t>(ECollisionChannel2D::kCount)> _responses;
};
```

提供接口：

```cpp
ECollisionResponse2D GetResponse(ECollisionChannel2D channel) const;
void SetResponse(ECollisionChannel2D channel, ECollisionResponse2D response);
```

Collider2D 持有：

```cpp
CollisionProfile2D _collision_profile;
```

不要与 Entity Rendering Layer 共用。

---

## 3. 双方 Response 合并规则

两个 Collider 相遇时，需要同时考虑双方配置。

例如：

```text
Enemy -> Player = Block
Player -> Enemy = Overlap
```

最终应该为：

```text
Overlap
```

规则采用“较弱响应优先”：

```text
Ignore < Overlap < Block
```

即：

```cpp
ECollisionResponse2D ResolveCollisionResponse(ECollisionResponse2D lhs, ECollisionResponse2D rhs);
```

逻辑：

```text
Block   + Block   -> Block
Block   + Overlap -> Overlap
Block   + Ignore  -> Ignore

Overlap + Overlap -> Overlap
Overlap + Ignore  -> Ignore

Ignore  + 任意     -> Ignore
```

实现时不要依赖 enum 数值隐式比较，建议明确实现。

---

## 4. Box2D 映射

### Ignore

双方不应产生有效物理交互。

尽可能通过 Box2D collision filter 提前过滤：

```text
Ignore
    -> 不生成 contact
    -> 不上报 Collision / Trigger
```

如果静态 filter 无法完整表达动态双向 response，再通过自定义过滤 callback 补充。

### Block

正常 Box2D Contact：

```text
Block
    -> 保留 contact constraint
    -> 产生物理阻挡
    -> 上报 CollisionBegin / CollisionEnd
```

映射现有：

```cpp
b2World_GetContactEvents(...)
```

事件类型：

```cpp
EPhysicsContact2DType::kCollisionBegin
EPhysicsContact2DType::kCollisionEnd
```

### Overlap

不能简单通过：

```cpp
shape_def.isSensor = true;
```

实现所有 Overlap。

原因：

一个 Collider 可能：

```text
Enemy vs Ground   = Block
Enemy vs Trigger  = Overlap
```

而 Box2D Sensor 是 shape 自身属性，无法根据另一个对象的 Channel 动态决定。

因此分两类处理。

#### 显式 Sensor Collider

例如 TriggerBox / AttackArea：

继续使用 Box2D Sensor：

```text
is_sensor = true
```

通过：

```cpp
b2World_GetSensorEvents(...)
```

上报：

```cpp
kTriggerBegin
kTriggerEnd
```

#### 普通 Collider 的 Channel Overlap

当两个普通 Shape 的最终 Response 为：

```text
Overlap
```

应在 Box2D contact 生成后禁用实际 contact constraint，但仍保留 overlap/event 信息。

推荐通过 PreSolve / Contact Filter 之类机制：

```text
ResolveResponse == Overlap
    -> 禁止物理求解
    -> 记录为 Trigger/Overlap 关系
```

最终对游戏层统一上报：

```cpp
kTriggerBegin
kTriggerEnd
```

不要让 Lua 层区分：

```text
Box2D Sensor Trigger
Channel Overlap Trigger
```

脚本只看到统一 Trigger 事件。

---

## 5. Physics Event 过滤

现有：

```cpp
append_event(shape_a, shape_b, type);
```

在 append 前或者内部加入 Response 判断。

伪代码：

```cpp
const auto response = ResolveCollisionResponse(shape_a, shape_b);

switch (response)
{
case ECollisionResponse2D::kIgnore:
    break;

case ECollisionResponse2D::kOverlap:
    AppendTriggerEvent(...);
    break;

case ECollisionResponse2D::kBlock:
    AppendCollisionEvent(...);
    break;
}
```

目标是：

**脚本默认只收到真正有意义的事件。**

不要让 Lua 普遍写：

```lua
if entity.type ~= xxx then
    return
end
```

来承担底层碰撞过滤。

---

## 6. Contact2D 数据扩展

现有 Lua：

```lua
c:on_collision_enter(function(entity, hit2d)
end)
```

建议 `PhysicsHit2D / Contact2D` 增加：

```cpp
ECollisionChannel2D _other_object_type;
```

可以进一步包含：

```cpp
Vector2f _point;
Vector2f _normal;
float _impulse;
```

按现有系统已有数据决定，不要求本任务一次全部补齐。

Lua 可访问：

```lua
hit2d.object_type
```

例如：

```lua
c:on_collision_enter(function(entity, hit2d)
    if hit2d.object_type ~= CollisionChannel2D.Player then
        return
    end

    self.entity.sprite.color = Color(1, 0, 0, 1)
end)
```

该能力用于特殊游戏逻辑，不作为基础碰撞过滤的主要方案。

---

## 7. Collision Preset

为了避免每个 Collider 手动配置完整矩阵，增加 Preset。

第一版内置：

```text
Default
Player
Enemy
WorldStatic
WorldDynamic
Projectile
Trigger
Pickup
Custom
```

例如：

### Enemy

```text
Object Type: Enemy

WorldStatic   Block
WorldDynamic  Block
Player        Block
Enemy         Block
Projectile    Block
Trigger       Overlap
Pickup        Ignore
```

### Trigger

```text
Object Type: Trigger

WorldStatic   Ignore
WorldDynamic  Ignore
Player        Overlap
Enemy         Overlap
Projectile    Ignore
Trigger       Ignore
Pickup        Ignore
```

### Projectile

示例：

```text
WorldStatic   Block
WorldDynamic  Block
Player        Block / Ignore
Enemy         Block
Projectile    Ignore
Trigger       Ignore
Pickup        Ignore
```

具体默认值可根据项目当前需求调整。

---

## 8. Preset 行为

Collider 保存：

```cpp
ECollisionPreset2D _preset;
CollisionProfile2D _collision_profile;
```

当选择：

```text
Enemy
```

自动覆盖 CollisionProfile。

如果用户手动修改任何 response：

```text
Preset -> Custom
```

行为参考 UE。

不需要第一版做复杂的继承、Profile Asset 或项目级配置文件。

---

## 9. Editor Inspector

Collider2D Inspector 增加：

```text
Collision
─────────────────────────────

Collision Preset    Enemy ▼

Object Type         Enemy ▼

Responses
                  Ignore  Overlap  Block
WorldStatic                          ●
WorldDynamic                         ●
Player                               ●
Enemy                                ●
Projectile                           ●
Trigger                      ●
Pickup              ●
```

要求：

- 三列 Radio Button。
- 单行只能选择一种 Response。
- 修改 Object Type 或 Response 后 Preset 自动变成 `Custom`。
- 选择预设后自动更新整套配置。
- UI 尽量紧凑。

---

## 10. Collider 与 Trigger 职责

### Character Body Collider

负责：

```text
角色移动
地面碰撞
墙体阻挡
其他实体阻挡
```

典型：

```text
Enemy Body

WorldStatic -> Block
Player      -> Block
Trigger     -> Overlap
```

### HitBox / HurtBox / Detection Area

单独使用 Trigger Collider：

```text
AttackHitBox
InteractionArea
PickupArea
EnemyDetectionArea
```

只产生 Trigger：

```lua
collider:on_trigger_enter(function(entity, hit2d)
end)
```

而不是让角色 Body Collider 的：

```lua
on_collision_enter
```

承担攻击检测。

这是推荐的最终使用模式。

---

## 11. Lua API

保持当前接口：

```lua
collider:on_collision_enter(callback)
collider:on_collision_exit(callback)

collider:on_trigger_enter(callback)
collider:on_trigger_exit(callback)
```

补充 CollisionChannel enum 导出，例如：

```lua
CollisionChannel2D.WorldStatic
CollisionChannel2D.WorldDynamic
CollisionChannel2D.Player
CollisionChannel2D.Enemy
CollisionChannel2D.Projectile
CollisionChannel2D.Trigger
CollisionChannel2D.Pickup
```

可选提供：

```lua
collider.object_type
```

以及：

```lua
collider:get_collision_response(channel)
collider:set_collision_response(channel, response)
```

第一版如果没有运行时修改需求，可以只读，不必全部暴露。

---

## 12. 不要与 Render Layer 合并

明确保持两套概念：

```text
Entity / Render Layer
    Camera Culling
    Rendering
    Editor visibility
```

和：

```text
Collision Channel
    Physics Filter
    Block / Overlap / Ignore
```

不要为了减少字段而复用同一套 LayerMask。

---

## 13. 推荐内部执行流程

两个 Shape 可能接触：

```text
Shape A
Shape B
    ↓
读取双方 CollisionProfile
    ↓
ResolveCollisionResponse()
    ↓
┌──────────┬──────────────┬─────────────┐
│ Ignore   │ Overlap      │ Block       │
├──────────┼──────────────┼─────────────┤
│ 丢弃     │ 禁止求解     │ 正常求解    │
│ 无事件   │ Trigger事件  │ Collision事件│
└──────────┴──────────────┴─────────────┘
```

显式 Box2D Sensor：

```text
Sensor Event
    ↓
直接映射 TriggerBegin / TriggerEnd
```

---

## 14. 当前问题修改后的效果

当前代码：

```lua
c:on_collision_enter(function(entity, hit2d)
    engine.log_error("hit start" .. entity.name)
    self.entity.sprite.color = Color(1, 0, 0, 1)
end)
```

敌人站在 Ground 上：

```text
Enemy vs WorldStatic = Block
```

因此仍然产生 Collision，这是正确行为，因为物理系统需要地面接触。

如果“变红”代表受到 Player 攻击，则不应该依赖 Body Collision。

推荐新增攻击 Trigger：

```text
AttackHitBox

Player / Enemy -> Overlap
WorldStatic    -> Ignore
```

脚本：

```lua
attack_collider:on_trigger_enter(function(entity, hit2d)
    self.entity.sprite.color = Color(1, 0, 0, 1)
end)
```

这样 Ground 根本不会进入对应回调。

---

## 15. 第一阶段实现范围

必须完成：

- `ECollisionChannel2D`
- `ECollisionResponse2D`
- `CollisionProfile2D`
- 双向 Response Resolve
- Ignore 过滤
- Block 正常 Collision
- Overlap 行为
- Collider2D 序列化
- Inspector Collision 面板
- Collision Preset
- Lua `hit2d.object_type`
- Trigger / Collision 事件统一整理

暂不实现：

- 用户自定义无限 Channel
- 项目设置中的 Channel 编辑器
- Collision Profile Asset
- 复杂 Physics Material
- Contact Modify 高级接口
- Blueprint 风格 Event Graph
- Render Layer 与 Physics Layer 联动

---

## 16. 验收测试

至少添加以下场景测试。

### Enemy vs Ground

```text
Enemy       -> WorldStatic = Block
WorldStatic -> Enemy       = Block
```

预期：

```text
产生物理阻挡
产生 CollisionBegin
产生 CollisionEnd
```

### Trigger vs Enemy

```text
Trigger -> Enemy   = Overlap
Enemy   -> Trigger = Overlap
```

预期：

```text
无物理阻挡
TriggerBegin
TriggerEnd
```

### Enemy vs Pickup

```text
Enemy  -> Pickup = Ignore
Pickup -> Enemy  = Ignore
```

预期：

```text
无阻挡
无事件
```

### Block + Overlap

```text
A -> B = Block
B -> A = Overlap
```

预期：

```text
最终 Overlap
无物理阻挡
产生 Trigger
```

### Block + Ignore

```text
A -> B = Block
B -> A = Ignore
```

预期：

```text
最终 Ignore
无阻挡
无事件
```

### Preset 修改

选择：

```text
Enemy
```

然后手动修改：

```text
Player: Block -> Ignore
```

预期：

```text
Preset 自动变为 Custom
```

---

## 17. 实现原则

优先复用 AiluEngine 当前：

- `Collider2D`
- `PhysicsWorld2D`
- Box2D shape/user data 映射
- Contact/Sensor event 收集逻辑
- Reflection
- Inspector
- Lua binding

不要为了该功能引入新的平行组件体系。

最终目标是让游戏侧只需要思考：

```text
我是什么 Object Type？
我对其他类型是 Ignore / Overlap / Block？
```

而不是在每一个 Lua 碰撞回调里手动判断地面、玩家、敌人、子弹。
