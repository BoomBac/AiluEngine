# AiluEngine 2D 物理系统开发方案

## 1. 文档目标

为 AiluEngine 增加一套独立、简洁、现代的 `Physics2D` 系统，用于支持《潜水员戴夫》这类 2D / 2.5D 动作探索项目。

设计重点：

- 复用现有 ECS。
- 复用现有 `TransformComponent`。
- 不增加第二套 `Transform2DComponent`。
- 使用 Box2D 作为底层 2D 物理 Backend。
- AiluEngine 自己负责 ECS、场景、组件、查询、事件、编辑器和脚本 API。
- 不继续扩展当前实验性质的 3D `CRigidBody / CCollider / PhysicsSystem`。
- 第一阶段优先覆盖实际游戏需求，而不是追求通用物理引擎完整度。

最终目标：

```text
Gameplay / Lua
      │
      ▼
Physics2D API
      │
      ▼
Physics2DWorld
      │
      ▼
Box2D
      │
      ▼
TransformComponent
      │
      ▼
SpriteRendererComponent
```

---

# 2. 当前 AiluEngine 状态

AiluEngine 当前已经存在：

```text
ECS
TransformComponent
TransformSystem
Scene
System Phase
ScriptSystem FixedUpdate
SpriteRendererComponent
DebugDrawer
CRigidBody
CCollider
PhysicsSystem
```

当前 `PhysicsSystem` 仍然属于实验性质实现：

- 手工刚体积分。
- Collider 全量遍历。
- 缺少 Broad Phase。
- 碰撞响应逻辑主要针对简单 Y 轴弹力。
- 缺少完整 Trigger 生命周期。
- 缺少查询系统。
- 缺少 CCD。
- 缺少睡眠。
- 缺少稳定 Contact Solver。

因此：

> 不应继续基于当前 `PhysicsSystem` 扩展正式 2D 物理能力。

旧系统暂时保留，用于兼容已有测试场景，但标记为：

```text
Legacy / Experimental
```

---

# 3. 总体架构

Physics2D 使用以下结构：

```text
Scene
 │
 ├ TransformComponent
 ├ RigidBody2DComponent
 └ Collider2DComponent
        │
        ▼
Physics2DSystem
        │
        ▼
Physics2DWorld
        │
        ▼
Box2D World
```

职责划分：

```text
RigidBody2DComponent
    保存刚体配置

Collider2DComponent
    保存碰撞 Shape 配置

Physics2DSystem
    接入 ECS System Phase

Physics2DWorld
    管理 Box2D World
    管理 Entity -> Body 映射
    同步 Transform
    创建/销毁 Body
    执行 Step
    收集 Event
    提供 Query

Physics2D
    面向 Gameplay / Lua 的静态 API

Box2D
    Broad Phase
    Narrow Phase
    Contact
    Solver
    Sensor
    Sleep
    CCD
    RayCast
    ShapeCast
    Joint
```

原则：

> Box2D 类型不得暴露给 Gameplay、Scene、Lua 或 Editor。

---

# 4. 不增加 Transform2DComponent

Physics2D 必须继续复用：

```cpp
TransformComponent
```

2D 物理映射：

```text
Transform.position.x -> Physics X
Transform.position.y -> Physics Y

Transform.rotation.z -> Physics Angle

Transform.position.z -> 仅用于 Render / Sorting
```

现有 `Math::Transform2D` 可以继续作为数学工具使用，但：

```text
Math::Transform2D
```

不得成为新的 ECS Transform Component。

场景中保持：

```text
Entity
 ├ TransformComponent
 ├ SpriteRendererComponent
 ├ RigidBody2DComponent
 └ Collider2DComponent
```

---

# 5. Box2D 集成

建议：

```text
ThirdParty/
    box2d/
```

优先使用 Box2D 3.x。

建议通过 CMake：

```cmake
add_subdirectory(ThirdParty/box2d)
target_link_libraries(AiluEngine PRIVATE box2d)
```

具体按当前 AiluEngine ThirdParty 管理方式适配。

Box2D Header 不应出现在公共 Physics2D Component Header 中。

允许出现在：

```text
Physics2DWorld.cpp
Physics2DBackend.h
```

或者：

```cpp
class Physics2DWorld::Impl;
```

通过 PImpl 完全隐藏。

第一阶段不需要为 Physics Backend 抽象接口。

不要为了未来替换 Box2D 提前设计：

```text
IPhysics2DBackend
Box2DBackend
Jolt2DBackend
...
```

当前只有一个 Backend，直接使用即可。

---

# 6. 目录结构

新增：

```text
Engine/Inc/Physics/2D/
    Physics2D.h
    Physics2DTypes.h
    Physics2DComponents.h
    Physics2DWorld.h
    Physics2DSystem.h

Engine/Src/Physics/2D/
    Physics2D.cpp
    Physics2DWorld.cpp
    Physics2DSystem.cpp
```

后续可增加：

```text
Editor/Inc/Inspector/ComponentEditors/
    RigidBody2DComponentEditor.h
    Collider2DComponentEditor.h

Editor/Src/Inspector/ComponentEditors/
    RigidBody2DComponentEditor.cpp
    Collider2DComponentEditor.cpp
```

第一阶段如果现有 Reflection Inspector 可以自动绘制组件，则优先复用 Reflection，不新增大量专用 Inspector。

---

# 7. RigidBody2DComponent

定义：

```cpp
AENUM()
enum class EBody2DType
{
    kStatic,
    kKinematic,
    kDynamic
};

struct AILU_API RigidBody2DComponent
{
    DECLARE_COMPONENT(RigidBody2DComponent, "Ailu.ECS.RigidBody2DComponent")

    EBody2DType _type = EBody2DType::kDynamic;

    f32 _gravity_scale = 1.0f;

    f32 _linear_damping = 0.0f;
    f32 _angular_damping = 0.0f;

    bool _fixed_rotation = false;
    bool _continuous = false;
    bool _allow_sleep = true;
};
```

组件只保存：

```text
Scene Configuration
```

不得保存：

```text
b2BodyId
b2ShapeId
runtime pointer
contact cache
solver state
temporary velocity cache
```

运行时状态统一放入：

```text
Physics2DWorld
```

---

# 8. Body Type 定义

## 8.1 Static

Static 表示：

```text
不会被物理系统移动
不受重力
不会被碰撞推动
用于提供碰撞
```

典型对象：

```text
洞穴
墙壁
地图
岩石
地面
静态障碍物
```

如果 Entity：

```text
TransformComponent
Collider2DComponent
```

但没有：

```text
RigidBody2DComponent
```

则 Physics2D 自动创建：

```text
Static Body
```

这是默认行为。

---

## 8.2 Kinematic

Kinematic 表示：

> 由 Gameplay 控制运动，但参与物理世界碰撞。

不会自动受到：

```text
Gravity
Force
Impulse
```

通常由：

```text
Velocity
Move
Target Position
```

驱动。

典型对象：

```text
Player
Fish
NPC
Moving Platform
部分 Projectile
```

例如：

```cpp
Physics2D::SetLinearVelocity(scene, player, move_direction * speed);
```

Kinematic 的移动来自 Gameplay，而不是 Solver。

这是 Dave-like 游戏最重要的 Body 类型之一。

---

## 8.3 Dynamic

Dynamic 表示：

```text
完全由物理 Solver 驱动
```

受到：

```text
Gravity
Force
Impulse
Collision Response
Friction
Restitution
```

典型对象：

```text
箱子
掉落道具
碎片
物理摆件
被击飞物
```

Dynamic Body 的位置不得由 Gameplay 每帧直接写 Transform。

需要使用：

```cpp
Physics2D::SetPosition(...)
Physics2D::SetLinearVelocity(...)
Physics2D::AddForce(...)
Physics2D::AddImpulse(...)
```

---

# 9. Body Type 使用建议

Dave-like 游戏推荐：

```text
Player             Kinematic
普通 Fish           Kinematic
NPC                Kinematic
Boss               Kinematic

Projectile         Kinematic / Dynamic

Physics Prop       Dynamic
Debris             Dynamic

World              Static
Trigger Zone       Static
```

项目中 Dynamic Body 不应成为默认选择。

角色运动优先：

```text
Gameplay Controller + Kinematic
```

而不是：

```text
Dynamic Rigidbody + AddForce
```

---

# 10. Collider2DComponent

Ailu ECS 当前一种 Component 每 Entity 一份，因此：

> Collider2DComponent 必须支持多个 Shape。

不要采用：

```text
BoxCollider2DComponent
CircleCollider2DComponent
CapsuleCollider2DComponent
```

这种组件爆炸设计。

定义：

```cpp
AENUM()
enum class ECollider2DShape
{
    kBox,
    kCircle,
    kCapsule,
    kPolygon,
    kChain
};

ASTRUCT()
struct ColliderShape2D
{
    GENERATED_BODY()

    ECollider2DShape _type = ECollider2DShape::kBox;

    Vector2f _center = Vector2f::kZero;
    f32 _rotation = 0.0f;

    Vector2f _size = Vector2f::kOne;
    f32 _radius = 0.5f;
    f32 _height = 1.0f;

    bool _is_trigger = false;

    f32 _density = 1.0f;
    f32 _friction = 0.3f;
    f32 _restitution = 0.0f;

    u8 _layer = 0;
};

struct AILU_API Collider2DComponent
{
    DECLARE_COMPONENT(Collider2DComponent, "Ailu.ECS.Collider2DComponent")

    Vector<ColliderShape2D> _shapes;
};
```

---

# 11. 多 Shape 设计

例如玩家：

```text
Player
 ├ TransformComponent
 ├ RigidBody2DComponent
 └ Collider2DComponent
       ├ Shape 0 : Capsule
       │            Player Body
       │
       ├ Shape 1 : Circle Trigger
       │            Interaction
       │
       └ Shape 2 : Box Trigger
                    HurtBox
```

运行时映射：

```text
Ailu Entity
      │
      ▼
   b2BodyId
      │
      ├ b2ShapeId
      ├ b2ShapeId
      └ b2ShapeId
```

每个 Shape 必须保留：

```text
shape_index
```

以便 Collision Event 和 Query 返回具体 Shape。

---

# 12. 第一阶段 Shape 范围

第一阶段只正式支持：

```text
Box
Circle
Capsule
```

必须完整支持：

```text
Collision
Trigger
Query
Debug Draw
Editor
Serialization
```

暂缓：

```text
Polygon
Chain
```

第二阶段再实现。

Polygon / Chain 主要用于：

```text
复杂洞穴边界
TileMap Collision
静态地图轮廓
```

---

# 13. Physics2D Runtime 数据

定义内部运行时数据：

```cpp
struct PhysicsBody2DRuntime
{
    ECS::Entity _entity = ECS::kInvalidEntity;

    Vector2f _previous_position = Vector2f::kZero;
    Vector2f _current_position = Vector2f::kZero;

    f32 _previous_rotation = 0.0f;
    f32 _current_rotation = 0.0f;

    // Box2D handle hidden in implementation.
};
```

实际 Box2D Handle 建议完全放到 `.cpp` 内部。

例如：

```cpp
struct PhysicsBody2DNative
{
    b2BodyId _body_id = b2_nullBodyId;
    Vector<b2ShapeId> _shape_ids;
};
```

映射：

```text
Entity
  ↓
PhysicsBody2DRuntime
  ↓
Native Body
```

---

# 14. Physics2DWorld

建议：

```cpp
class AILU_API Physics2DWorld
{
public:
    Physics2DWorld();
    ~Physics2DWorld();

    void Initialize();
    void Shutdown();

    void Sync(Register &r, const std::set<Entity> &entities);

    void Step(Register &r, f32 fixed_delta_time);

    void CreateBody(Register &r, Entity entity);
    void DestroyBody(Entity entity);

    void FlushEvents(Register &r);

    bool Raycast(const Raycast2DDesc &desc, RaycastHit2D &hit) const;

    void RaycastAll(const Raycast2DDesc &desc, Vector<RaycastHit2D> &hits) const;

    bool CircleCast(const CircleCast2DDesc &desc, ShapeCastHit2D &hit) const;

    void OverlapCircle(const OverlapCircle2DDesc &desc, Vector<OverlapHit2D> &hits) const;

    void OverlapBox(const OverlapBox2DDesc &desc, Vector<OverlapHit2D> &hits) const;

private:
    struct Impl;
    Scope<Impl> _impl;
};
```

推荐通过 PImpl 隔离 Box2D Header。

---

# 15. Physics2DSystem

Physics2DSystem 尽量保持极薄：

```cpp
class Physics2DSystem final : public System
{
    DECLARE_SYSTEM(Physics2DSystem)

public:
    void Update(Register &r, f32 fixed_delta_time) final;

    ESystemPhase GetPhase() const final
    {
        return ESystemPhase::kPhysics;
    }

    Physics2DWorld &World()
    {
        return _world;
    }

private:
    Physics2DWorld _world;
};
```

Update：

```cpp
void Physics2DSystem::Update(Register &r, f32 fixed_delta_time)
{
    PROFILE_BLOCK_CPU("Physics2DSystem::Update")

    _world.Sync(r, _entities);
    _world.Step(r, fixed_delta_time);
    _world.FlushEvents(r);
}
```

不要把复杂碰撞逻辑直接写入 `Physics2DSystem`。

复杂度全部限制在：

```text
Physics2DWorld
```

中。

---

# 16. Scene 注册

Scene 初始化时增加：

```cpp
_register.RegisterComponent<ECS::RigidBody2DComponent>();
_register.RegisterComponent<ECS::Collider2DComponent>();
```

Physics2DSystem Signature：

```cpp
ECS::Signature physics_2d_sig;

physics_2d_sig.set(_register.GetComponentTypeID<ECS::TransformComponent>(), true);
physics_2d_sig.set(_register.GetComponentTypeID<ECS::Collider2DComponent>(), true);

_register.RegisterSystem<ECS::Physics2DSystem>(physics_2d_sig);
```

注意：

```text
RigidBody2DComponent
```

不是必须组件。

因此：

```text
Transform + Collider2D
```

自动成为 Static Body。

---

# 17. Transform Authority

必须明确物理和 Transform 的数据所有权。

---

## 17.1 Static

Static：

```text
Transform
    ↓
Physics
```

Transform 是 Authority。

编辑器修改：

```text
Transform
```

Physics2DWorld 检测 Transform 变化后：

```text
Update Box2D Static Body Transform
```

---

## 17.2 Kinematic

Kinematic：

```text
Gameplay
    ↓
Physics Velocity / Move Target
    ↓
Physics
    ↓
Transform
```

运行时不建议 Gameplay 每帧直接：

```cpp
transform->SetLocalPosition(...)
```

而应：

```cpp
Physics2D::SetLinearVelocity(...)
```

或：

```cpp
Physics2D::Move(...)
```

---

## 17.3 Dynamic

Dynamic：

```text
Physics
    ↓
Transform
```

Physics 是唯一 Authority。

不允许 Gameplay 每帧直接修改 Transform。

如果需要瞬移：

```cpp
Physics2D::SetPosition(...)
```

Physics2D 同时更新：

```text
Box2D Body
TransformComponent
interpolation state
```

---

# 18. Hierarchy 规则

Dynamic / Kinematic Body 必须成为：

```text
Physics Root
```

允许：

```text
Player
 ├ RigidBody2D
 ├ Collider2D
 │
 ├ Weapon
 ├ Shadow
 └ VFX
```

不建议支持：

```text
MovingParent
 └ DynamicRigidBody
```

原因：

```text
Parent Transform
```

与：

```text
Physics World Transform
```

会产生双重 Authority。

第一阶段规则：

### Static Collider

允许存在 Parent。

### Dynamic Body

禁止父级 Transform 驱动。

### Kinematic Body

同样建议作为 Physics Root。

如果检测到：

```text
Dynamic/Kinematic RigidBody2D
+
CHierarchy parent
```

运行时：

```text
LOG_WARNING
```

编辑器中也应提示。

第一阶段不需要自动修复。

---

# 19. Fixed Update

Physics2D 只能运行在：

```text
FixedUpdate
```

中。

禁止使用普通 Frame Delta 执行 Box2D Step。

推荐：

```text
fixed_delta_time = 1 / 60
```

系统顺序：

```text
Fixed Frame
│
├ Script FixedUpdate
│
├ Gameplay FixedUpdate
│
├ kPrePhysics
│
├ kPhysics
│    └ Physics2DWorld::Step
│
├ kPostPhysics
│    └ Physics Event Dispatch
│
└ Transform Sync
```

Physics2D 内部不得：

```cpp
delta_time *= time_scale;
```

重复处理时间缩放。

时间缩放由上层统一 FixedUpdate Scheduler 负责。

---

# 20. Fixed Step 插值

Render Frame 可能高于 Physics Fixed Frame。

因此 Dynamic / Kinematic Body 保存：

```cpp
_previous_position
_current_position

_previous_rotation
_current_rotation
```

渲染插值：

```cpp
render_position = Lerp(previous_position, current_position, render_alpha);
```

已有 TransformComponent 中已经存在：

```text
prev world
render world
render position
```

Physics2D 应尽量复用现有 Transform 插值体系，而不是再维护第二套 Renderer Transform。

目标：

```text
Physics 60 Hz
Render 120/144 Hz
```

运动仍然平滑。

---

# 21. Physics2D Gameplay API

新增：

```cpp
namespace Ailu::Physics2D
{
    bool IsValidBody(SceneManagement::Scene &scene, ECS::Entity entity);

    void SetPosition(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &position);

    Vector2f GetPosition(SceneManagement::Scene &scene, ECS::Entity entity);

    void SetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &velocity);

    Vector2f GetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity);

    void SetAngularVelocity(SceneManagement::Scene &scene, ECS::Entity entity, f32 velocity);

    void AddForce(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &force);

    void AddImpulse(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &impulse);
}
```

禁止 Gameplay 获取：

```text
b2BodyId
b2ShapeId
b2WorldId
```

---

# 22. Physics Query

对于 Dave-like 游戏，Physics Query 是核心能力。

第一阶段实现：

```text
Raycast
RaycastAll

CircleCast

OverlapCircle
OverlapBox
```

第二阶段：

```text
BoxCast
CapsuleCast
OverlapCapsule
```

---

# 23. PhysicsQueryFilter

定义：

```cpp
struct PhysicsQueryFilter
{
    u32 _layer_mask = 0xffffffffu;

    bool _hit_trigger = false;
};
```

以后如果需要再增加：

```text
ignore entity
predicate callback
category filtering
```

第一阶段不要过度设计。

---

# 24. Raycast

定义：

```cpp
struct Raycast2DDesc
{
    Vector2f _origin = Vector2f::kZero;
    Vector2f _direction = Vector2f(1.0f, 0.0f);

    f32 _distance = 0.0f;

    PhysicsQueryFilter _filter;
};

struct RaycastHit2D
{
    ECS::Entity _entity = ECS::kInvalidEntity;

    u16 _shape_index = 0;

    Vector2f _point = Vector2f::kZero;
    Vector2f _normal = Vector2f::kZero;

    f32 _distance = 0.0f;
    f32 _fraction = 0.0f;
};
```

RaycastHit2D 只返回：

```text
Entity
Shape Index
Point
Normal
Distance
```

不得返回：

```text
Collider Pointer
Box2D Handle
Native Shape
```

---

# 25. Overlap

定义：

```cpp
struct OverlapHit2D
{
    ECS::Entity _entity = ECS::kInvalidEntity;
    u16 _shape_index = 0;
};
```

接口：

```cpp
void OverlapCircle(..., Vector<OverlapHit2D> &results);

void OverlapBox(..., Vector<OverlapHit2D> &results);
```

使用场景：

```text
玩家 Interaction
鱼攻击范围
AOE
拾取
近战
Boss HitBox
AI 感知
```

---

# 26. CircleCast

CircleCast 用于：

```text
Projectile
Character Movement
Obstacle Avoidance
Fast Hit Detection
```

定义：

```cpp
struct CircleCast2DDesc
{
    Vector2f _origin = Vector2f::kZero;

    f32 _radius = 0.5f;

    Vector2f _direction = Vector2f(1.0f, 0.0f);

    f32 _distance = 0.0f;

    PhysicsQueryFilter _filter;
};
```

返回：

```cpp
struct ShapeCastHit2D
{
    ECS::Entity _entity = ECS::kInvalidEntity;

    u16 _shape_index = 0;

    Vector2f _point = Vector2f::kZero;
    Vector2f _normal = Vector2f::kZero;

    f32 _distance = 0.0f;
    f32 _fraction = 0.0f;
};
```

---

# 27. Collision Layer

增加项目级 Physics Layer。

最大：

```text
32 Layers
```

例如：

```text
0 Default
1 Player
2 Enemy
3 World
4 PlayerProjectile
5 EnemyProjectile
6 Pickup
7 Interaction
8 PlayerHurtBox
9 EnemyHurtBox
```

组件中保存：

```cpp
u8 _layer;
```

Project Settings 保存：

```text
32 × 32 Collision Matrix
```

例如：

```text
                    Player Enemy World Pickup
Player                -      X     X     X
Enemy                 X      -     X     -
Projectile            X      X     X     -
Pickup                 -      -     -     -
```

Physics2DWorld 创建 Shape 时根据 Layer Matrix 转换成 Box2D：

```text
category bits
mask bits
```

Gameplay 不直接操作原始 bit filter。

---

# 28. Collision / Trigger Event

定义：

```cpp
AENUM()
enum class EPhysicsContact2DType
{
    kCollisionBegin,
    kCollisionEnd,

    kTriggerBegin,
    kTriggerEnd
};
```

事件：

```cpp
struct PhysicsContact2D
{
    EPhysicsContact2DType _type;

    ECS::Entity _entity_a = ECS::kInvalidEntity;
    ECS::Entity _entity_b = ECS::kInvalidEntity;

    u16 _shape_a = 0;
    u16 _shape_b = 0;

    Vector2f _point = Vector2f::kZero;
    Vector2f _normal = Vector2f::kZero;
};
```

事件流程：

```text
Box2D Step
   ↓
Collect Box2D Events
   ↓
Convert to PhysicsContact2D
   ↓
Event Queue
   ↓
PostPhysics
   ↓
Gameplay / Lua
```

第一阶段只实现：

```text
Begin
End
```

不要实现：

```text
CollisionStay
TriggerStay
```

需要持续检测的 Gameplay 应自己维护：

```text
Begin -> insert
End   -> erase
```

---

# 29. Event 安全要求

禁止在 Box2D Step 内直接：

```text
Destroy Entity
Destroy Body
Add Component
Remove Component
Load Scene
```

所有 Physics Event 必须：

```text
Step 完成
    ↓
收集
    ↓
统一 Dispatch
```

如果 Gameplay 在 Event 中销毁 Entity，则继续使用现有 ECS Deferred Destroy。

---

# 30. Trigger

ColliderShape2D：

```cpp
bool _is_trigger;
```

Trigger 在 Box2D 中映射为 Sensor。

Trigger：

```text
不产生碰撞阻挡
产生 Begin / End Event
可以被 Query 检测
```

典型用途：

```text
Interaction Area
Water Area
Cutscene Trigger
Quest Trigger
Pickup
Attack HitBox
HurtBox
Detection Radius
```

---

# 31. Character Movement

第一阶段：

> 不实现完整 CharacterController2D Component。

Player / Fish 使用：

```text
Kinematic RigidBody2D
```

Gameplay 决定：

```text
desired velocity
```

例如：

```cpp
Vector2f velocity = input * move_speed;

Physics2D::SetLinearVelocity(scene, player, velocity);
```

不要使用：

```cpp
AddForce(...)
```

驱动普通角色移动。

---

# 32. 后续 KinematicMotor2D

如果后续角色需要：

```text
Slide
Step
Depenetration
Stable Ground
Slope
```

再增加：

```cpp
class KinematicMotor2D
{
public:
    MoveResult2D Move(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &delta);
};
```

内部使用：

```text
ShapeCast
Slide
Depenetration
```

但：

> KinematicMotor2D 不属于第一阶段必须内容。

Dave-like 的水下角色移动第一阶段直接使用 Kinematic Body 即可。

---

# 33. 水下玩法

不要实现：

```text
Fluid Physics
```

水下效果通过 Gameplay 参数模拟：

```text
Gravity Scale
Linear Damping
Move Acceleration
Max Speed
Buoyancy
Current Force
```

例如：

```text
WaterVolume Trigger
      ↓
Player enters
      ↓
WaterMovementState
      ↓
gravity_scale
drag
movement_speed
```

水流：

```cpp
Physics2D::AddForce(scene, entity, current_direction * current_strength);
```

视觉 Water Renderer 与 Physics2D 完全解耦。

---

# 34. Projectile

高速 Projectile 推荐两种方式。

## 简单 Projectile

Gameplay 直接：

```text
Raycast
CircleCast
```

每 FixedUpdate：

```text
old_position
    ↓
ShapeCast
    ↓
new_position
```

适合：

```text
鱼叉
子弹
高速攻击
```

---

## Dynamic Projectile

需要：

```text
Gravity
Bounce
Physical Interaction
```

时使用：

```text
Dynamic Body
Continuous Collision
```

不要默认所有 Projectile 都创建 Dynamic Rigidbody。

---

# 35. Joint

第一阶段：

```text
不实现 Editor Joint Component
```

第二阶段只增加：

```text
DistanceJoint2D
RevoluteJoint2D
```

用途：

```text
鱼叉绳
吊挂物
摆动物
机关
```

暂不支持：

```text
Prismatic
Wheel
Motor
Weld
Ragdoll
```

除非实际项目出现需求。

---

# 36. Debug Draw

Physics2D 必须支持 Debug Draw。

颜色建议：

```text
Static Collider     Green
Kinematic Collider  Cyan
Dynamic Collider    Blue
Trigger             Yellow
Sleeping Dynamic    Gray
Collision Contact   Red
```

支持绘制：

```text
Box
Circle
Capsule
Contact Point
Contact Normal
AABB optional
```

优先复用：

```text
Ailu::DebugDrawer
```

不要单独创建 Physics Debug Renderer。

---

# 37. Editor Inspector

## RigidBody2D

第一版显示：

```text
Body Type

Gravity Scale

Linear Damping
Angular Damping

Fixed Rotation
Continuous
Allow Sleep
```

---

## Collider2D

显示：

```text
Shapes
    Add Shape

Shape 0
    Type
    Center
    Rotation

    Size / Radius / Height

    Trigger

    Layer

    Density
    Friction
    Restitution

    Remove
```

优先通过 Reflection 自动 Inspector 实现。

只在：

```text
Shape 编辑
Scene Gizmo
特殊 Dropdown
```

需要时增加 Custom Component Editor。

---

# 38. Scene Gizmo

选中带 Collider2D 的 Entity 时：

```text
Scene View
```

绘制所有 Shape。

后续支持：

```text
拖动 Box Size
拖动 Circle Radius
拖动 Capsule Height
拖动 Center
```

第一阶段允许只有 Debug Wireframe，不强制实现交互 Handle。

---

# 39. Runtime Body 创建

Physics2DWorld 每次 Sync：

检测：

```text
Entity alive?
Has Transform?
Has Collider2D?
Body exists?
```

如果：

```text
Collider2D exists
Body runtime not exists
```

则：

```text
CreateBody
```

如果：

```text
Collider2D removed
Entity destroyed
```

则：

```text
DestroyBody
```

RigidBody2DComponent 增删：

```text
Body Type 更新
Body 重新创建
```

第一阶段可简单采用：

```text
配置发生结构变化 -> Recreate Body
```

不要为了避免一次 Body Recreate 增加大量增量同步复杂度。

---

# 40. Shape 配置变化

第一阶段策略：

如果以下字段变化：

```text
Shape Type
Shape Size
Center
Radius
Height
Trigger
Layer
Density
```

直接：

```text
Recreate Shapes
```

甚至允许：

```text
Recreate Body
```

编辑器性能完全足够。

运行时频繁修改 Shape 不属于第一阶段目标。

---

# 41. Transform 同步

Physics2DWorld::Sync 负责：

### Static

检测 Transform world version：

```text
changed
    ↓
Update Body Transform
```

### Kinematic

如果 Body 由 Physics API 驱动：

```text
不从 ECS Transform 覆盖
```

### Dynamic

完全：

```text
Physics -> Transform
```

如果 Editor 非 Play Mode：

允许：

```text
Transform -> Body
```

用于 Scene Editing。

---

# 42. Play Mode / Editor Mode

非 Play Mode：

```text
Physics 不 Step
```

但允许：

```text
Collider Debug Draw
Editor Transform 更新 Shape Preview
```

Play Mode：

```text
Fixed Physics Step
```

Simulate Mode：

如果当前引擎 Simulate Mode 已存在，也应支持 Physics2D Step。

---

# 43. Lua API

后续绑定：

```lua
Physics2D.SetLinearVelocity(entity, x, y)

Physics2D.AddImpulse(entity, x, y)

Physics2D.Raycast(...)

Physics2D.OverlapCircle(...)
```

建议结果：

```lua
local hit = Physics2D.Raycast(...)

if hit then
    print(hit.entity)
    print(hit.point.x)
    print(hit.point.y)
end
```

不要向 Lua 导出：

```text
Box2D Handle
Native Pointer
Physics2DWorld Internal Object
```

---

# 44. 性能目标

Dave-like 第一阶段目标：

```text
Static Collider      数千
Active Dynamic       数百以内
Kinematic            数百
Trigger              数百
Queries / frame      数十到数百
```

不以：

```text
100k dynamic body
```

为目标。

优先正确性、简单性和稳定性。

---

# 45. Multithreading

第一阶段：

```text
Physics2D 单线程
```

不要接入 Ailu JobSystem。

原因：

```text
项目体量不需要
增加生命周期复杂度
增加 ECS 同步复杂度
增加 Debug 难度
```

Box2D 后续确有性能压力时再评估多线程。

---

# 46. Serialization

需要为以下组件支持 Scene Serialization：

```text
RigidBody2DComponent
Collider2DComponent
ColliderShape2D
```

仅序列化配置。

不得序列化：

```text
runtime velocity
runtime contact
sleep state
Box2D handle
broadphase proxy
```

SaveGame 如果需要保存物理对象状态：

Gameplay 层主动保存：

```text
position
rotation
velocity
custom state
```

不要直接 dump Physics World。

---

# 47. Scene Clone

Scene Clone 后：

```text
Physics2D runtime handles 必须重新创建
```

不得复制：

```text
b2WorldId
b2BodyId
b2ShapeId
```

新 Scene 创建：

```text
New Physics2DWorld
```

然后从 ECS Component 重建。

---

# 48. Entity 生命周期

Entity Destroy：

```text
ECS DeferredDestroy
    ↓
Physics2D detects removal
    ↓
Destroy Box2D Body
```

必须保证：

```text
Body 不引用已失效 Entity
```

Box2D user data 中如果保存 Entity，应使用完整：

```text
ECS::Entity
```

即：

```text
index + generation
```

不要只保存 EntityIndex。

---

# 49. Runtime Mapping

Box2D Shape 必须能够反查：

```text
Entity
Shape Index
```

建议 Native User Data：

```cpp
struct PhysicsShape2DUserData
{
    ECS::Entity _entity = ECS::kInvalidEntity;
    u16 _shape_index = 0;
};
```

注意生命周期：

UserData 地址必须稳定。

不要把指针指向可能因：

```text
Vector realloc
```

移动的对象。

推荐：

```text
stable pool
deque
unique_ptr
```

或使用 Box2D 支持的整数 user data 机制进行编码。

---

# 50. Collision Event 去重

Physics2DWorld 应依赖 Box2D 提供的 Begin/End Event。

不要自己通过：

```text
上一帧 Pair Set
当前帧 Pair Set
```

重新计算 Contact 生命周期。

只在 Ailu 层做：

```text
Native Event -> Ailu Event
```

转换。

---

# 51. Physics Material

第一阶段不创建：

```text
PhysicsMaterial2D Asset
```

直接在 Shape：

```text
_density
_friction
_restitution
```

中保存。

只有项目出现大量共享材质需求时，再增加：

```text
PhysicsMaterial2D
```

例如：

```text
Ice
Rubber
Stone
Wood
```

避免提前资产化。

---

# 52. 第一阶段 API 范围

必须实现：

```text
RigidBody
    Static
    Kinematic
    Dynamic

SetPosition
GetPosition

SetLinearVelocity
GetLinearVelocity

SetAngularVelocity

AddForce
AddImpulse

Box Collider
Circle Collider
Capsule Collider

Multi Shape

Trigger

Collision Begin
Collision End

Trigger Begin
Trigger End

Layer Matrix

Raycast
RaycastAll

CircleCast

OverlapCircle
OverlapBox

Fixed Step

Transform Sync

Debug Draw

Serialization
```

---

# 53. 第一阶段明确不实现

```text
自研 Solver

自研 Broad Phase

Soft Body

Fluid Simulation

Cloth

Ragdoll

Rollback Physics

Deterministic Networking

Polygon Editor

Composite Collider

TileMap Collider

PhysicsMaterial Asset

Full CharacterController2D

Joint Editor

Multithread Physics

Runtime Shape Animation

Destructible Physics

Physics Scene Streaming
```

---

# 54. 实现阶段

## Phase 1：Box2D Foundation

完成：

```text
ThirdParty Box2D
Physics2DWorld
World Create / Destroy
Fixed Step
```

验收：

```text
可以创建 Box2D World
可以运行稳定 60Hz Step
无 Body 时正常运行
World 销毁无泄漏
```

---

## Phase 2：Components

完成：

```text
RigidBody2DComponent
Collider2DComponent
ColliderShape2D

Box
Circle
Capsule
```

完成 Reflection / Serialization。

验收：

```text
场景保存加载组件数据一致
```

---

## Phase 3：Entity Mapping

完成：

```text
Entity -> Body
Body -> Entity
Shape -> Entity + ShapeIndex

CreateBody
DestroyBody
```

验收：

```text
创建实体 -> 自动创建 Body
销毁实体 -> 自动销毁 Body
Scene reload -> Body 正确重建
```

---

## Phase 4：Transform Sync

完成：

```text
Static
Kinematic
Dynamic
```

Authority。

验收：

```text
Static Transform 修改正确同步

Kinematic Velocity 正确移动

Dynamic Gravity 正常下落

Dynamic Body 回写 Transform
```

---

## Phase 5：Collision

完成：

```text
Collision
Trigger
Begin
End
```

验收：

```text
Dynamic 撞 Static 正常

Kinematic 接触 Static 正常

Trigger 不阻挡

Begin 只触发一次

End 只触发一次
```

---

## Phase 6：Physics Query

完成：

```text
Raycast
RaycastAll
CircleCast
OverlapCircle
OverlapBox
```

验收：

```text
Layer Mask 正确

Trigger Filter 正确

Hit 返回正确 Entity

ShapeIndex 正确
```

---

## Phase 7：Layer Matrix

完成：

```text
32 Layer
Collision Matrix
Box2D Filter
```

验收：

```text
禁用 Layer Pair 后不产生碰撞
重新启用后恢复
Query Layer Mask 正确
```

---

## Phase 8：Debug Draw

完成：

```text
Collider Wireframe
Trigger Wireframe
Contact Point
Contact Normal
```

验收：

```text
Scene View 可直观看到 Physics Shape
```

---

## Phase 9：Lua

完成：

```text
SetLinearVelocity
AddImpulse
Raycast
OverlapCircle
```

以及：

```text
Collision / Trigger Event
```

最小 Lua 接入。

---

# 55. 单元测试

建议增加：

```text
Test/Physics2D/
```

至少：

```text
StaticBodyTest

DynamicGravityTest

KinematicMoveTest

BoxCollisionTest

CircleCollisionTest

CapsuleCollisionTest

TriggerBeginEndTest

LayerFilterTest

RaycastTest

CircleCastTest

OverlapCircleTest

MultiShapeTest

DestroyEntityTest

SceneCloneTest
```

---

# 56. Regression Test

增加一个 Physics2D 测试场景：

```text
Physics2DRegression
```

内容：

```text
Static Ground

Dynamic Box

Dynamic Circle

Kinematic Player

Trigger Area

Multi Shape Entity

Raycast Debug

Layer Filtering
```

运行：

```text
30 seconds
```

确认：

```text
无崩溃
无 NaN
无 Body 泄漏
无 Entity Handle 失效访问
```

---

# 57. Debug / Profiler

增加：

```text
PROFILE_BLOCK_CPU("Physics2DWorld::Sync")
PROFILE_BLOCK_CPU("Physics2DWorld::Step")
PROFILE_BLOCK_CPU("Physics2DWorld::FlushEvents")
```

Debug Statistics：

```text
Body Count
Static Count
Kinematic Count
Dynamic Count
Shape Count
Contact Count
Trigger Count
Query Count
```

第一阶段只需要 Debug UI 显示，不需要复杂 Physics Profiler。

---

# 58. Coding Requirements

代码遵循 AiluEngine 当前命名风格。

变量：

```cpp
snake_case
```

成员：

```cpp
_member_name
```

静态变量：

```cpp
s_name
```

常量：

```cpp
kConstantName
```

类名：

```cpp
Physics2DWorld
ColliderShape2D
```

函数名：

```cpp
CreateBody()
DestroyBody()
SetLinearVelocity()
```

代码按：

```text
120 columns
```

格式化。

避免无意义的小函数拆分。

---

# 59. 错误处理

以下情况不得 Crash：

```text
Entity 已销毁

Collider 没有 Transform

无效 Body Handle

无效 Shape

Physics API 传入无 Physics Body Entity

Zero Direction Raycast

Negative Radius

Invalid Layer
```

Debug Build：

```text
LOG_WARNING / AL_ASSERT
```

Release Build：

安全返回。

例如：

```cpp
if (!world.IsValidBody(entity))
{
    LOG_WARNING("Physics2D::SetLinearVelocity: entity {} has no valid physics body", entity);
    return;
}
```

---

# 60. 设计约束

整个 Physics2D 实现过程中必须遵循：

### 1

不要修改现有 3D PhysicsSystem 使其承担 2D 功能。

### 2

Physics2D 与旧 PhysicsSystem 独立。

### 3

不要增加 Transform2DComponent。

### 4

不要暴露 Box2D Handle。

### 5

不要把 Runtime Handle 放进 ECS Component。

### 6

不要将角色默认设计成 Dynamic Rigidbody。

### 7

不要实现自研 Solver。

### 8

不要提前抽象多 Backend。

### 9

不要第一阶段实现复杂 Joint。

### 10

不要第一阶段实现 Fluid Physics。

---

# 61. Dave-like 使用示例

## Player

```text
Player
 ├ TransformComponent
 ├ SpriteRendererComponent
 ├ ScriptComponent
 ├ RigidBody2DComponent
 │    type = Kinematic
 │    fixed_rotation = true
 │
 └ Collider2DComponent
      ├ Capsule
      │    layer = Player
      │
      └ Circle Trigger
           layer = Interaction
```

移动：

```cpp
Physics2D::SetLinearVelocity(scene, player, input * move_speed);
```

---

## Fish

```text
Fish
 ├ Transform
 ├ SpriteRenderer
 ├ FishAI
 ├ RigidBody2D
 │    type = Kinematic
 │
 └ Collider2D
      ├ Circle Body
      └ Circle HurtBox
```

AI：

```cpp
Physics2D::SetLinearVelocity(scene, fish, swim_direction * swim_speed);
```

---

## Rock

```text
Rock
 ├ Transform
 ├ SpriteRenderer
 └ Collider2D
      └ Box
```

没有 RigidBody：

```text
Automatic Static Body
```

---

## Pickup

```text
Pickup
 ├ Transform
 ├ SpriteRenderer
 └ Collider2D
      └ Circle
           trigger = true
           layer = Pickup
```

Player 进入：

```text
TriggerBegin
    ↓
PickupSystem
```

---

## Harpoon

建议：

```text
Gameplay Projectile
```

每 FixedUpdate：

```text
CircleCast
```

命中：

```text
Fish Entity
    ↓
Damage
    ↓
Attach Harpoon
```

无需创建 Dynamic Rigidbody。

---

# 62. 最终目标结构

最终 AiluEngine Physics2D 应保持：

```text
                    Gameplay / Lua
                         │
              ┌──────────┼───────────┐
              │          │           │
           Query      Velocity     Impulse
              │          │           │
              └──────────┼───────────┘
                         ▼
                    Physics2D API
                         │
                         ▼
                  Physics2DWorld
                ┌────────┴────────┐
                │                 │
          Entity Mapping      Event Queue
                │                 │
                ▼                 ▼
              Box2D          PostPhysics
                │                 │
                ▼                 ▼
             Solver          Gameplay
                │
                ▼
         TransformComponent
                │
                ▼
       SpriteRendererComponent
```

整个正式 Physics2D 模块的核心应该保持：

```text
2 个 ECS Component

1 个 Physics2DSystem

1 个 Physics2DWorld

1 组 Physics2D Gameplay API

1 套 Query

1 套 Event
```

不要让 Physics2D 扩散成大量：

```text
Manager
Subsystem
Service
Backend
Proxy
Controller
Adapter
Wrapper
```

层级。

---

# 63. 最终验收标准

完成第一阶段后，需要能够使用 AiluEngine 制作如下测试场景：

```text
玩家在二维水下场景中自由移动。

玩家使用 Kinematic Body。

洞穴边界使用 Static Collider。

鱼使用 Kinematic Body。

鱼与玩家发生碰撞。

攻击区域使用 Trigger。

鱼叉使用 CircleCast。

道具使用 Trigger 检测拾取。

箱子使用 Dynamic Body。

箱子受到重力并与地形碰撞。

Layer Matrix 可以禁止特定碰撞组合。

Lua 可以进行 Raycast 和控制 Velocity。

Scene View 可以显示 Collider。

保存场景重新打开后所有 Physics2D 配置正确恢复。
```

满足以上条件，即认为 Physics2D 第一阶段完成。

此时不要继续扩充物理功能。

下一步应直接进入实际 Dave-like Gameplay Vertical Slice，通过真实玩法需求决定是否补充：

```text
KinematicMotor2D
Polygon / Chain
TileMap Collision
Distance Joint
Revolute Joint
Physics Material
```

而不是继续以“完善物理引擎”为目标开发。