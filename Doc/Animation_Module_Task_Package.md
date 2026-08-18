# AiluEngine 现代轻量动画模块开发任务包

## 1. 目标

重构 AiluEngine 动画模块，建立一套 **2D / 3D 共用核心架构**。

核心目标：

- 2D Sprite 动画与 3D Skeleton 动画共用：
  - Animator
  - AnimationController
  - State Machine
  - Transition
  - Parameter
  - CrossFade
  - BlendSpace 控制逻辑
  - Animation Event
- 2D / 3D 仅在最终 Binding / Sampling 阶段分离。
- AnimationController 不依赖 ECS、Skeleton、SpriteRenderer、Physics、Input。
- AnimationSystem 不负责 Skinning。
- Runtime 禁止每帧字符串查找或 Reflection Property 查找。
- 保持实现轻量，覆盖类似《潜水员戴夫》体量的项目即可。
- 优先复用现有：
  - `Track.hpp`
  - `TransformTrack`
  - `Skeleton`
  - `Pose`
  - ECS
  - Asset / Guid
  - Graph Editor 基础设施
- 不实现 UE AnimGraph / Unity Animator 的完整复杂度。

---

# 2. 当前问题

当前动画实现存在以下耦合。

## 2.1 AnimationClip 与 Skeleton 强绑定

当前：

```cpp
class AnimationClip
{
    Vector<TransformTrack> _tracks;

public:
    f32 Sample(Pose &pose, f32 time);
};
```

`AnimationClip` 直接输出 `Pose`，因此实际是 Skeleton Animation Clip，无法自然支持 Sprite 动画。

目标：

- `AnimationClip` 只描述动画数据。
- Clip 不直接输出 Skeleton Pose。
- Sampling / Binding 决定动画如何应用到目标。

---

## 2.2 CSkeletonMesh 混入动画播放职责

当前 `CSkeletonMesh` 中包含：

```cpp
Ref<AnimationClip> _anim_clip;
f32 _anim_time;
Ref<AnimationClip> _blend_anim_clip;
BlendSpace _blend_space;
i16 _anim_type;
```

这些字段应从 Renderer Component 中移除。

目标：

```text
3D Entity
    TransformComponent
    CSkeletonMesh
    AnimatorComponent

2D Entity
    TransformComponent
    SpriteRendererComponent
    AnimatorComponent
```

2D / 3D 使用同一个 `AnimatorComponent`。

---

## 2.3 AnimationSystem 承担职责过多

当前 AnimationSystem 同时负责：

- Animation time
- Clip sampling
- Pose
- CrossFade
- BlendSpace
- Matrix Palette
- CPU Skinning
- ThreadPool Skinning Task
- Skeleton Debug Draw

需要拆分。

目标：

```text
AnimationSystem
    ↓
Controller / Player / Sampling
    ↓
Animation Output
    ↓
SkeletonPose / Sprite Result

SkinningSystem / Renderer
    ↓
SkeletonPose → Matrix Palette → GPU Skinning
```

Animation 模块不能访问 VertexBuffer。

---

## 2.4 Runtime Pose 错误绑定 Mesh Asset

当前 Pose / CrossFadeController 等 runtime 数据以 `mesh_id` 为 key。

这是错误模型。

同一 Mesh Asset 的两个 Entity 必须能播放完全不同的动画。

Runtime 动画状态必须属于 **Animator Instance / Entity**，而不是 Mesh Asset。

---

# 3. 总体架构

推荐结构：

```text
Gameplay / Script
       │
       │ SetFloat / SetBool / SetTrigger
       ▼
AnimatorComponent
       │
       ▼
AnimationInstance
       │
       ▼
AnimationController
    State Machine
    Transition
    Parameters
       │
       ▼
AnimationEvaluation
 clip + time + weight
       │
       ├────────────────────┐
       ▼                    ▼
Skeleton Binding        Sprite Binding
       │                    │
       ▼                    ▼
SkeletonPose        SpriteRendererComponent
       │
       ▼
Skinning / Renderer
```

核心原则：

> Controller 只决定“当前有哪些 Motion / Clip，各自 time / weight 是多少”。

Controller 不负责：

- Pose Sampling
- Sprite Frame Sampling
- Skeleton
- Renderer
- Entity
- Physics
- Gameplay Event 行为

---

# 4. 核心类型

## 4.1 AnimatorComponent

新增：

```cpp
struct AnimatorComponent
{
    DECLARE_COMPONENT(AnimatorComponent, "Ailu.ECS.AnimatorComponent")

    Guid _controller;
    f32 _speed = 1.0f;
    bool _play_on_awake = true;

    AnimationInstanceHandle _instance;
};
```

要求：

- `_controller` 为 `AnimationControllerAsset` Guid。
- `_instance` 为 runtime only，不参与 Scene 序列化。
- 删除 `CSkeletonMesh` 中 animation runtime 字段。

---

# 5. AnimationController Asset

Controller 是静态 Asset，多 Entity 共用。

```cpp
class AnimationControllerAsset : public Object, public IPersistentable
{
private:
    Vector<AnimationParameterDesc> _parameters;
    Vector<AnimationState> _states;
    Vector<AnimationTransition> _transitions;

    Vector<u16> _any_state_transitions;

    u16 _entry_state = kInvalidState;
};
```

Controller Asset 保存：

- Parameters
- States
- Transitions
- Entry State
- Any State Transitions

Controller Asset 不保存 runtime 状态。

---

# 6. AnimationInstance

每个 Animator Entity 有独立 Instance。

推荐：

```cpp
struct AnimationInstance
{
    u16 _current_state = kInvalidState;
    u16 _next_state = kInvalidState;

    f32 _state_time = 0.0f;
    f32 _next_state_time = 0.0f;

    f32 _transition_time = 0.0f;
    f32 _transition_duration = 0.0f;

    Vector<f32> _float_parameters;
    Vector<i32> _int_parameters;
    Vector<u8> _bool_parameters;

    BitSet _triggers;

    SkeletonPose _skeleton_pose;

    bool _in_transition = false;
};
```

注意：

- 2D Animator 不应强制分配 `SkeletonPose`。
- 实际实现可将 Skeleton Runtime 数据放入专属 binding/runtime cache。
- `AnimationInstance` 应通过 Pool 管理，避免 ECS Component 内放大量动态容器。

推荐：

```cpp
class AnimationInstancePool
{
public:
    AnimationInstanceHandle Create(const AnimationControllerAsset &controller);
    void Destroy(AnimationInstanceHandle handle);

    AnimationInstance &Get(AnimationInstanceHandle handle);
    const AnimationInstance &Get(AnimationInstanceHandle handle) const;
};
```

---

# 7. Parameters

V1 支持：

```cpp
enum class EAnimationParameterType : u8
{
    kFloat,
    kInt,
    kBool,
    kTrigger
};
```

描述：

```cpp
struct AnimationParameterDesc
{
    String _name;
    u32 _name_hash = 0;
    EAnimationParameterType _type;
};
```

Runtime 不允许：

```cpp
parameters["speed"]
```

Controller Asset 加载 / Compile 后，应转换成连续 index：

```text
speed     -> parameter_index 0
grounded  -> parameter_index 1
attack    -> parameter_index 2
```

Runtime：

```cpp
instance._float_parameters[0] = speed;
```

API：

```cpp
void SetFloat(AnimationParameterId id, f32 value);
void SetInt(AnimationParameterId id, i32 value);
void SetBool(AnimationParameterId id, bool value);
void SetTrigger(AnimationParameterId id);
void ResetTrigger(AnimationParameterId id);
```

允许提供：

```cpp
AnimationParameterId GetParameterId(StringView name);
```

但该函数只能用于初始化 / Script API 边界，不得在 AnimationSystem 每帧内部执行字符串查找。

---

# 8. State

一个 State 指向一个 Motion。

```cpp
enum class EAnimationMotionType : u8
{
    kClip,
    kBlendSpace
};

struct AnimationMotion
{
    EAnimationMotionType _type = EAnimationMotionType::kClip;
    Guid _asset;
};

struct AnimationState
{
    String _name;
    AnimationMotion _motion;

    f32 _speed = 1.0f;
    bool _loop = true;

    Vector<u16> _transitions;
};
```

不要让 State 只绑定 Clip。

这样可支持：

```text
Idle
    motion = idle.anim

Locomotion
    motion = locomotion.blendspace

Attack
    motion = attack.anim
```

---

# 9. Transition

```cpp
enum class EAnimationConditionOp : u8
{
    kEqual,
    kNotEqual,

    kGreater,
    kGreaterEqual,
    kLess,
    kLessEqual,

    kTriggered
};
```

Condition：

```cpp
struct AnimationCondition
{
    u16 _parameter_index = 0;
    EAnimationConditionOp _op = EAnimationConditionOp::kEqual;

    f32 _float_value = 0.0f;
    i32 _int_value = 0;
    bool _bool_value = false;
};
```

Transition：

```cpp
struct AnimationTransition
{
    u16 _from_state = kInvalidState;
    u16 _to_state = kInvalidState;

    Vector<AnimationCondition> _conditions;

    f32 _duration = 0.15f;

    bool _has_exit_time = false;
    f32 _exit_time = 1.0f;
};
```

规则：

- 一个 Transition 内多个 Conditions 使用 AND。
- V1 不实现 OR Group。
- 一个 State 可以有多个 Transition。
- 按数组顺序作为优先级。
- 每帧只允许开始一次新的 Transition。

---

# 10. Entry / Any State

支持：

```text
Entry -> Idle
```

Asset：

```cpp
u16 _entry_state;
```

支持 Any State：

```text
Any State -> Death
Any State -> HitReact
```

每帧判断顺序：

```text
1. Any State transitions
2. Current State transitions
```

如果 Any State 命中，则不继续检查 Current State Transition。

V1 不实现：

- Exit Node
- Sub-State Machine
- State Machine Nesting

---

# 11. AnimationController Runtime

Controller 动态输入只有：

```text
Parameters
delta_time
```

禁止：

```cpp
Update(entity, skeleton, sprite_renderer, rigid_body, input, dt);
```

推荐接口：

```cpp
class AnimationController
{
public:
    void Update(AnimationInstance &instance, f32 delta_time) const;

    AnimationEvaluation Evaluate(const AnimationInstance &instance) const;

private:
    bool CheckTransition(const AnimationInstance &instance, const AnimationTransition &transition) const;
    bool CheckCondition(const AnimationInstance &instance, const AnimationCondition &condition) const;
};
```

流程：

```text
Advance State Time
    ↓
Check Any State
    ↓
Check Current State Transitions
    ↓
Start / Update Transition
    ↓
Evaluate Current Motion
    ↓
Output AnimationEvaluation
```

---

# 12. AnimationEvaluation

Controller / Motion 的统一输出。

```cpp
struct AnimationSample
{
    Guid _clip;
    f32 _time = 0.0f;
    f32 _weight = 1.0f;
};

struct AnimationEvaluation
{
    Array<AnimationSample, 4> _samples;
    u8 _sample_count = 0;
};
```

普通状态：

```text
Run
weight = 1.0
```

CrossFade：

```text
Walk
weight = 0.4

Run
weight = 0.6
```

原则：

> Controller 输出 AnimationSample，不输出 Pose。

---

# 13. CrossFade

Transition：

```text
Walk -> Run
duration = 0.2s
```

如果：

```text
transition_time = 0.05
```

则：

```text
t = 0.05 / 0.2 = 0.25
```

输出：

```text
Walk weight = 0.75
Run  weight = 0.25
```

过渡结束：

```text
current_state = Run
next_state = invalid
in_transition = false
```

V1 使用线性 Blend 即可。

暂不实现：

- Custom Blend Curve
- Sync Marker
- Inertialization

---

# 14. BlendSpace

现有 `BlendSpace` 直接持有：

- Skeleton
- Pose

需要重构。

新的 BlendSpace 只负责：

> 根据参数位置计算多个 Clip 的权重。

结构：

```cpp
struct BlendSpaceSample
{
    Guid _clip;
    Vector2f _position;
};

class BlendSpaceAsset : public Object, public IPersistentable
{
private:
    Vector<BlendSpaceSample> _samples;

    Vector2f _x_range;
    Vector2f _y_range;

    bool _is_2d = false;
};
```

输出仍然是：

```cpp
AnimationEvaluation
```

例如 1D：

```text
speed

0.0 -> idle
1.5 -> walk
4.0 -> run
```

输入：

```text
speed = 2.5
```

输出：

```text
walk 0.6
run  0.4
```

BlendSpace 不允许依赖：

- Skeleton
- Pose
- Sprite

V1 可以先只实现 1D BlendSpace。

2D BlendSpace 可放 V2。

---

# 15. AnimationClip

将当前 Skeleton-only Clip 重构为通用动画 Clip。

推荐第一阶段支持：

```text
Transform Track
Sprite Track
Event Track
```

可预留：

```text
Float Track
Color Track
```

但 V1 不必实现通用 Reflection Property Animation。

---

# 16. Transform Track

现有：

```cpp
TransformTrack
```

尽量复用。

现有 Track 使用 `_id` 表示 joint。

建议逐步抽象成 binding id，而不是直接理解为 Joint Index。

例如：

```cpp
enum class EAnimationBindingType : u8
{
    kRootTransform,
    kSkeletonJoint,
    kSprite
};

struct AnimationBinding
{
    EAnimationBindingType _type;
    u32 _target;
};
```

Skeleton Joint：

```text
_target = Hash("arm_l")
```

Clip 加载 / Animator 初始化时 Resolve：

```text
Hash("arm_l")
    ↓
joint index 23
```

Runtime 缓存：

```cpp
struct ResolvedAnimationBinding
{
    u16 _track_index = 0;
    u16 _target_index = 0;
};
```

每帧禁止 joint name 字符串搜索。

---

# 17. SpriteAnimationTrack

新增：

```cpp
struct SpriteKeyFrame
{
    f32 _time = 0.0f;
    Guid _sprite;
};

class SpriteAnimationTrack
{
public:
    const SpriteKeyFrame &Sample(f32 time) const;

private:
    Vector<SpriteKeyFrame> _frames;
};
```

例如：

```text
swim.anim

0.00 -> swim_0.sprite
0.10 -> swim_1.sprite
0.20 -> swim_2.sprite
0.30 -> swim_3.sprite
```

输出应用到：

```cpp
SpriteRendererComponent::_sprite
```

Sprite 属于离散属性。

CrossFade 时 V1 规则：

> 使用权重最高的 Clip 对 Sprite Track 的采样结果。

不对两个 Sprite 做插值。

---

# 18. SkeletonPose

现有 `Pose` 保留，但建议重命名：

```cpp
SkeletonPose
```

因为 Pose 是 Skeleton 专属概念。

保留：

```cpp
Vector<Transform> _joints;
Vector<u16> _parents;
```

保留现有：

- Bind Pose
- Rest Pose
- Local / Global Transform
- Matrix Palette
- Pose Blend

但全部移动到：

```text
Animation/Skeleton/
```

---

# 19. Skeleton Binding

新增专属 Binding：

```cpp
class SkeletonAnimationBinding
{
public:
    void Resolve(const AnimationClip &clip, const Skeleton &skeleton);

    void Evaluate(const AnimationEvaluation &evaluation,
                  const Skeleton &skeleton,
                  SkeletonPose &out_pose);
};
```

职责：

- Resolve Clip Track -> Skeleton Joint Index
- Sample Transform Tracks
- Blend 多个 Samples
- 输出 `SkeletonPose`

不负责：

- Skinning
- VertexBuffer
- Renderer

---

# 20. Sprite Binding

新增：

```cpp
class SpriteAnimationBinding
{
public:
    void Evaluate(const AnimationEvaluation &evaluation,
                  SpriteRendererComponent &renderer);
};
```

职责：

- 选择 dominant sample
- Sample Sprite Track
- 写入 SpriteRenderer

以后可以扩展：

- Color
- Flip
- Transform
- Material Parameter

V1 只实现 Sprite 即可。

---

# 21. Animation Event

Animation Event 属于 Clip 时间轴。

不要放进 Controller。

结构：

```cpp
enum class EAnimationEventKind : u8
{
    kGameplay,
    kCosmetic
};

struct AnimationEvent
{
    f32 _time = 0.0f;
    u32 _event_id = 0;

    EAnimationEventKind _kind = EAnimationEventKind::kCosmetic;
};
```

Clip：

```cpp
class AnimationClip
{
private:
    Vector<AnimationEvent> _events;
};
```

Event Asset 中不保存：

- C++ callback
- `std::function`
- Lua callback
- Entity pointer
- Gameplay object pointer

只保存：

```text
time
event id
event kind
```

---

# 22. Event ID

编辑器层可以用字符串：

```text
footstep
attack_hit_begin
attack_hit_end
```

Asset 加载 / Compile 后转换成 hash：

```cpp
u32 _event_id;
```

允许脚本 API 将 Hash 再映射为可读字符串。

Runtime AnimationSystem 不允许每帧字符串比较。

---

# 23. Animation Event 检测

每个 active Clip 必须记录：

```text
previous_time
current_time
```

检测：

```text
(previous_time, current_time]
```

范围内经过的 Event。

Loop 必须正确处理。

例如：

```text
duration = 1.0
previous = 0.95
current = 0.05
```

需要检测：

```text
(0.95, 1.0]
+
[0.0, 0.05]
```

推荐：

```cpp
void CollectEvents(const AnimationClip &clip,
                   f32 previous_time,
                   f32 current_time,
                   Vector<AnimationEvent> &out_events);
```

---

# 24. CrossFade Event 规则

需要区分：

```cpp
enum class EAnimationEventKind : u8
{
    kGameplay,
    kCosmetic
};
```

规则：

## Gameplay Event

例如：

```text
hit_begin
hit_end
enable_hitbox
disable_hitbox
```

所有 active motion 都必须正常检测并派发。

不能因为 Clip 权重较低而丢失，否则可能产生 Gameplay 状态卡死。

## Cosmetic Event

例如：

```text
footstep
dust
cloth_sound
```

CrossFade 时只派发 dominant clip 的 Cosmetic Event。

避免：

```text
Walk Footstep
Run Footstep
```

同时触发。

---

# 25. Animation Event Queue

Animation 模块只产生事件，不执行 Gameplay。

```cpp
struct AnimationEventMessage
{
    ECS::Entity _entity = ECS::kInvalidEntity;
    u32 _event_id = 0;

    EAnimationEventKind _kind = EAnimationEventKind::kCosmetic;
};
```

事件进入：

```cpp
class AnimationEventQueue
{
public:
    void Push(const AnimationEventMessage &event);

    Span<const AnimationEventMessage> Events() const;

    void Clear();
};
```

流程：

```text
AnimationSystem
    ↓
AnimationEventQueue
    ↓
ScriptSystem / GameplaySystem
```

禁止：

```cpp
AnimationClip::Sample()
{
    gameplay->Attack();
}
```

---

# 26. State Event

Controller 可在 runtime 发出：

```text
State Enter
State Exit
State Changed
```

V1 不需要在 Controller Editor 中编辑复杂 State Event。

只提供 runtime callback / message 即可。

例如：

```cpp
struct AnimationStateChangedMessage
{
    ECS::Entity _entity;
    u16 _from_state;
    u16 _to_state;
};
```

---

# 27. Seek / Editor Preview Event

编辑器拖动时间轴时不能触发 Gameplay Event。

增加：

```cpp
enum class EAnimationAdvanceMode : u8
{
    kPlayback,
    kSeek
};
```

只有：

```text
kPlayback
```

才派发 Animation Event。

Editor Timeline Scrub 使用：

```text
kSeek
```

---

# 28. AnimationSystem

新 AnimationSystem 只负责 runtime animation evaluation。

建议职责：

```text
for Animator Entity:

1. Resolve / Create AnimationInstance
2. Advance parameter/state/controller time
3. Check State Transition
4. Evaluate Motion
5. Produce AnimationEvaluation
6. Collect Clip Events
7. Dispatch to SkeletonBinding / SpriteBinding
```

AnimationSystem 不负责：

- Skinning
- VertexBuffer
- Render Command
- Skeleton Debug Drawing

---

# 29. SkinningSystem

将现有：

```cpp
SkinTask(...)
```

和 Matrix Palette / VertexBuffer 更新从 `AnimationSystem` 移出去。

推荐：

```text
SkeletonAnimationBinding
    ↓
SkeletonPose
    ↓
SkinningSystem
    ↓
Matrix Palette
    ↓
Renderer / GPU Skinning
```

优先为未来 GPU Skinning 留出接口。

V1 若暂时继续 CPU Skinning，也必须独立为：

```cpp
SkinningSystem
```

不得继续放在 AnimationSystem 内。

---

# 30. Editor：AnimationController Editor

推荐复用 AiluEngine 现有 Graph 基础设施。

布局：

```text
┌────────────────────────────────────────────────────────┐
│ Animation Controller                                   │
├──────────────┬────────────────────────────┬─────────────┤
│ Parameters   │                            │ Inspector   │
│              │       State Graph          │             │
│ speed float  │                            │             │
│ grounded bool│   Entry -> Idle -> Walk    │             │
│ attack trig  │                  ↓         │             │
│              │                 Run        │             │
│              │                            │             │
└──────────────┴────────────────────────────┴─────────────┘
```

---

# 31. Parameters Panel

支持：

```text
+ Float
+ Int
+ Bool
+ Trigger
```

操作：

```text
Add
Rename
Delete
```

修改 Parameter 时必须自动修复 / 标记所有引用 Transition Condition。

禁止静默产生 dangling parameter index。

---

# 32. State Graph

右键：

```text
Create State
```

Node：

```text
┌──────────────────┐
│ Walk             │
├──────────────────┤
│ walk.anim        │
└──────────────────┘
```

State Inspector：

```text
Name
Motion
Speed
Loop
```

支持：

```text
Set As Entry
Make Transition
Delete
```

特殊 Node：

```text
Entry
Any State
```

---

# 33. Transition Editor

点击 State Link，在 Inspector 编辑：

```text
Transition

Duration
    0.15

Has Exit Time
    false

Exit Time
    1.0

Conditions
    speed Greater 3.0

+ Condition
```

根据 Parameter Type 限制 Operator。

例如：

```text
float:
    Greater
    GreaterEqual
    Less
    LessEqual
    Equal
    NotEqual

int:
    Greater
    GreaterEqual
    Less
    LessEqual
    Equal
    NotEqual

bool:
    Equal
    NotEqual

trigger:
    Triggered
```

---

# 34. AnimationClip Editor

Animation Event 直接编辑在 Clip Timeline。

例如：

```text
Attack.anim

0.0          0.3          0.6          1.0
 |------------|------------|------------|

             ▲        ▲
         hit_begin  hit_end
```

点击 Event Marker：

```text
Time
Event Name
Kind
```

Sprite Clip Timeline 同时可展示 Sprite Frame：

```text
0.00 sprite_0
0.10 sprite_1
0.20 sprite_2
```

3D Clip Timeline 可以先只展示：

```text
Transform Tracks
Events
```

V1 不要求制作完整 DCC Curve Editor。

---

# 35. 目录建议

```text
Engine/Inc/Animation/
    AnimationClip.h
    AnimationController.h
    AnimationControllerAsset.h
    AnimationInstance.h
    AnimationInstancePool.h
    AnimationState.h
    AnimationTransition.h
    AnimationEvaluation.h
    AnimationEvent.h
    AnimationEventQueue.h
    BlendSpace.h

    Track/
        Track.hpp
        TransformTrack.h
        SpriteAnimationTrack.h

    Skeleton/
        Skeleton.h
        SkeletonPose.h
        SkeletonAnimationBinding.h
        Solver.h

    Sprite/
        SpriteAnimationBinding.h
```

Src 对应拆分。

Editor：

```text
Editor/Inc/Animation/
    AnimationControllerEditor.h
    AnimationClipEditor.h

Editor/Src/Animation/
    AnimationControllerEditor.cpp
    AnimationClipEditor.cpp
```

具体路径可结合当前项目目录风格调整。

---

# 36. Script API

Lua 层只暴露 Animator 语义。

推荐：

```lua
animator:set_float("speed", speed)
animator:set_bool("grounded", grounded)
animator:set_trigger("attack")
```

事件：

```lua
animator:on_event("attack_hit", function()
end)
```

或沿用项目当前统一 EventRouter / AEVENT 机制。

底层必须转换：

```text
String -> ParameterId / EventId
```

并缓存。

不要让 Lua 每帧直接操作：

- Clip
- Pose
- Skeleton Joint
- Transition
- AnimationEvaluation

---

# 37. Asset 与 Runtime 分离

编辑 Asset 允许：

```text
String Name
Guid
Readable State Name
Readable Parameter Name
```

Runtime Compile / Resolve 后全部转成：

```text
u16 State Index
u16 Transition Index
u16 Parameter Index
u16 Joint Index
u32 Event Hash
```

原则：

> 编辑数据强调稳定和可读；runtime 数据强调连续和 index 化。

---

# 38. V1 范围

V1 必须实现：

- `AnimatorComponent`
- `AnimationControllerAsset`
- `AnimationInstance`
- Parameter
  - Float
  - Int
  - Bool
  - Trigger
- State
- Transition
- Entry
- Any State
- Exit Time
- CrossFade
- Clip Motion
- 1D BlendSpace
- `AnimationEvaluation`
- 通用 `AnimationClip`
- Transform Track
- Sprite Track
- Event Track
- Skeleton Binding
- Sprite Binding
- SkeletonPose
- AnimationEventQueue
- AnimationController Editor
- AnimationClip Event Editor
- 2D Sprite Animation
- 3D Skeleton Animation
- 从 AnimationSystem 拆出 Skinning

---

# 39. V1 明确不做

不要实现：

- UE AnimGraph
- Unity Playables
- Sub-State Machine
- Animation Layer
- Avatar Mask
- Additive Animation
- Animation Retarget
- Sync Group
- Sync Marker
- Montage
- Slot
- State Behaviour Script
- Generic Reflection Property Animation
- Root Motion
- Motion Matching
- 2D BlendSpace
- Blend Tree Graph
- Complex Curve Editor
- Animation Compression
- GPU Animation Sampling
- Inertialization

这些全部留给后续需求驱动。

---

# 40. V2 候选

后续按需求增加：

- 2D BlendSpace
- Additive Animation
- Layer / Mask
- Root Motion
- Generic Float / Color Property Track
- Animation Retarget
- Sync Marker
- GPU Skinning
- Animation Compression

---

# 41. 迁移现有代码

## Step 1

新增：

```text
AnimatorComponent
AnimationControllerAsset
AnimationInstance
AnimationEvaluation
```

先不要删旧逻辑。

---

## Step 2

将现有：

```cpp
AnimationClip::Sample(Pose &, f32)
```

中的 Transform Track Sampling 逻辑迁移到：

```cpp
SkeletonAnimationBinding
```

AnimationClip 不再知道 Pose。

---

## Step 3

将现有：

```text
Pose
Skeleton
Solver
```

移动到 Skeleton 子模块。

可先保留兼容 alias，减少一次性改动。

---

## Step 4

将：

```cpp
CSkeletonMesh::_anim_clip
CSkeletonMesh::_anim_time
CSkeletonMesh::_blend_anim_clip
CSkeletonMesh::_blend_space
CSkeletonMesh::_anim_type
```

删除。

由 `AnimatorComponent` 接管动画入口。

---

## Step 5

删除 AnimationSystem 中：

```text
static Map<mesh_id, Pose>
static Map<mesh_id, CrossFadeController>
static Map<mesh_id, MatrixPalette>
```

改用 Animator Instance。

---

## Step 6

将 SkinTask / Skinning 拆到独立 `SkinningSystem`。

AnimationSystem 最终不得包含 VertexBuffer 访问。

---

## Step 7

新增 SpriteAnimationTrack + SpriteAnimationBinding。

验证同一个 Controller / State Machine 架构可以：

```text
SpriteRendererComponent
CSkeletonMesh
```

分别正常工作。

---

## Step 8

实现 Animation Event。

验证：

- 普通播放
- Loop
- CrossFade
- Gameplay Event
- Cosmetic Event
- Editor Seek

---

## Step 9

实现 AnimationController Editor。

优先完成：

```text
Parameter
State
Transition
Entry
Any State
Inspector
```

不要先做复杂美术效果。

---

# 42. Runtime 更新顺序

建议系统顺序：

```text
Gameplay / Script
    ↓
写 Animator Parameters
    ↓
AnimationSystem
    ↓
Controller / Event / Binding
    ↓
TransformSystem（若动画修改 Entity Transform）
    ↓
SkinningSystem
    ↓
Render
```

如果动画暂时不支持 Root Transform Track，则 AnimationSystem 与 TransformSystem 的具体前后关系可保持当前架构。

---

# 43. Trigger 生命周期

Trigger 建议：

```text
SetTrigger()
    ↓
本次 AnimationSystem Update 可见
    ↓
被 Transition 消费后立即 Reset
```

如果 Trigger 未命中任何 Transition：

- 推荐仍保留到下一帧。
- 可增加最多一帧 / 一次消费策略，但不要第一版引入复杂 Trigger Queue。

优先采用：

> Transition 命中时消费 Trigger。

---

# 44. Exit Time

`_exit_time` 使用 normalized time：

```text
0.0 ~ 1.0
```

例如：

```text
Attack -> Idle

has_exit_time = true
exit_time = 0.9
```

当：

```text
normalized_state_time >= 0.9
```

才允许 Transition。

Condition 与 Exit Time 同时存在时：

```text
Exit Time AND All Conditions
```

---

# 45. State Time

每个 State 自己维护：

```text
state_time
```

Motion 的实际采样：

```text
sample_time = state_time * state_speed * animator_speed
```

Loop Motion：

```text
sample_time = fmod(sample_time, duration)
```

非 Loop：

```text
sample_time = min(sample_time, duration)
```

Transition 时必须分别维护：

```text
current_state_time
next_state_time
```

不要两个 State 共用一个播放时间。

---

# 46. 性能要求

运行时设计目标：

- 每 Entity Update 不做字符串 map lookup。
- 不做 Reflection Path Resolve。
- 不做 Asset Guid 查找。
- 不动态分配临时 Vector。
- `AnimationEvaluation` 使用固定小数组。
- Binding 缓存 index。
- Controller Transition 使用连续数组。
- Event Track 按时间排序。
- Sprite Track 按时间排序。
- Transform Track 尽量复用现有采样实现。
- Skeleton Pose / Matrix Palette 内存可复用。

---

# 47. 测试要求

至少增加以下自动测试。

## Controller

```text
Entry State
Float Condition
Bool Condition
Int Condition
Trigger Condition
Any State
Transition Priority
Exit Time
Trigger Consume
```

## CrossFade

```text
0%
25%
50%
100%
```

权重必须正确。

## Animation Event

```text
Normal Range
Loop Wrap
No Duplicate Event
Gameplay Event During CrossFade
Cosmetic Event Dominant Only
Seek Does Not Fire Event
```

## Sprite

```text
Frame Sampling
Loop
Non Loop Clamp
CrossFade Dominant Sprite
```

## Skeleton

```text
Single Clip Pose
Two Clip Blend
Two Entities Same Mesh Different Animation
```

重点验证：

> 两个 Entity 使用同一个 SkeletonMesh Asset 时，Pose 和 State 必须完全独立。

---

# 48. 验收场景

## 2D

创建 Sprite Character：

```text
TransformComponent
SpriteRendererComponent
AnimatorComponent
```

Controller：

```text
Entry -> Idle

Idle -> Run
    speed > 0.1

Run -> Idle
    speed <= 0.1

Any State -> Attack
    attack Trigger
```

Clip：

```text
idle.sprite_anim
run.sprite_anim
attack.sprite_anim
```

Event：

```text
attack_hit_begin
attack_hit_end
```

要求全部正常工作。

---

## 3D

创建 Skeleton Character：

```text
TransformComponent
CSkeletonMesh
AnimatorComponent
```

复用完全相同的 Controller 概念：

```text
Idle
Run
Attack
```

仅 Clip Asset 类型不同。

要求：

- State Machine 正常。
- CrossFade 正常。
- SkeletonPose 正常。
- Skinning 正常。
- Event 正常。

---

# 49. 最终边界总结

必须保持：

```text
AnimationController
    负责 State / Transition / Parameter / Motion Selection

AnimationClip
    负责 Animation Data

AnimationEvaluation
    负责 Clip + Time + Weight

SkeletonAnimationBinding
    负责 Evaluation -> SkeletonPose

SpriteAnimationBinding
    负责 Evaluation -> SpriteRenderer

AnimationEventQueue
    负责 Animation -> Gameplay / Script Event Bridge

SkinningSystem
    负责 SkeletonPose -> Render
```

禁止重新演变成：

```text
AnimationSystem
    Controller
    Pose
    Skinning
    VertexBuffer
    Sprite
    Event Callback
    Debug Draw
    Gameplay
```

全部塞在一个系统里的结构。

---

# 50. 实现优先级

建议按以下顺序实现：

```text
P0
    AnimatorComponent
    AnimationControllerAsset
    AnimationInstance
    State / Transition / Parameter
    AnimationEvaluation

P1
    SkeletonAnimationBinding
    现有 Skeleton Animation 迁移
    SkinningSystem 拆分

P2
    SpriteAnimationTrack
    SpriteAnimationBinding
    2D Animation

P3
    AnimationEvent / EventQueue

P4
    Controller Editor
    Clip Event Timeline Editor

P5
    1D BlendSpace
```

每个阶段完成后保持工程可编译、可运行。

不要一次性全盘删除旧动画代码后再重建。

---

# 51. Claude 实现约束

实现时遵循：

- 尽量复用 AiluEngine 现有容器、Guid、Object、Serialize、ECS、Graph。
- 不引入新的第三方动画库。
- 不重复实现已有 Reflection / Asset / Graph 基础设施。
- 所有 runtime 数据与 Asset 数据分离。
- 避免虚函数层级和过度抽象。
- 优先普通 struct + enum + index。
- API 保持小而明确。
- 先完成 V1，不主动增加本文“明确不做”的能力。
- 修改过程中同步清理旧 `CSkeletonMesh` 动画字段与旧 AnimationSystem 职责。
- 保持现有代码命名与工程风格一致。
