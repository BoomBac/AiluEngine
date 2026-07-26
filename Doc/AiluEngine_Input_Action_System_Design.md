# AiluEngine Input Action 系统设计文档

## 1. 文档目的

本文档用于指导 AiluEngine 实现一套基于 Action 的输入系统。

系统需要完成从平台原始输入到游戏语义输入的完整转换，并支持：

- 键盘、鼠标、手柄等多种输入设备。
- Button、1D Axis、2D Axis 等不同输入值类型。
- 一个 Action 绑定多个设备输入。
- WASD 等复合输入绑定。
- Dead Zone、Scale、Invert 等输入值处理。
- Press、Release、Hold、Tap、MultiTap 等输入交互。
- Gameplay、UI、Vehicle、Editor 等 Action Map。
- 输入上下文切换、优先级、阻塞和消费。
- 运行时查询和事件回调两种使用方式。
- 输入配置资源化。
- 编辑器 Scene View、Game View、文本输入之间的输入隔离。

系统的核心目标是让游戏逻辑只依赖诸如 `Move`、`Jump`、`Fire` 这样的语义输入，而不直接依赖
`Keyboard::Space`、`Mouse::LeftButton` 或平台 API。

---

## 2. 总体数据流

```text
Platform Input API
        ↓
InputDevice
        ↓
InputControl
        ↓
InputBinding
        ↓
InputProcessor
        ↓
InputInteraction
        ↓
InputAction
        ↓
InputActionMap
        ↓
InputContext
        ↓
Gameplay / UI / Editor
```

各层职责必须保持清晰：

```text
InputDevice      负责采集设备状态。
InputControl     表示设备上的具体控件。
InputBinding     将控件连接到 Action。
InputProcessor   变换输入值。
InputInteraction 解释输入时序。
InputAction      表示游戏语义输入。
InputActionMap   组织一组 Action。
InputContext     决定哪些 ActionMap 当前有效。
InputSystem      统一调度整个输入系统。
```

---

## 3. 设计原则

### 3.1 平台输入和游戏语义分离

错误示例：

```cpp
if (keyboard->IsKeyDown(EKey::kSpace))
    character->Jump();
```

正确示例：

```cpp
if (_jump_action->WasPerformedThisFrame())
    character->Jump();
```

Gameplay、UI 和 Editor 代码不应直接读取平台键盘或鼠标状态。

### 3.2 资源描述和运行时状态分离

配置资源中保存：

- Action 名称。
- Binding 路径。
- Processor 参数。
- Interaction 参数。
- Action Map 和 Context 关系。

运行时对象中保存：

- 已解析的设备和 Control 索引。
- 当前值和上一帧值。
- Interaction 计时状态。
- Action 当前阶段。
- 本帧事件标记。

### 3.3 字符串只用于加载和编辑阶段

以下路径适合资源序列化：

```text
<Keyboard>/space
<Mouse>/left_button
<Gamepad>/left_stick
```

运行时不应每帧解析字符串。资源加载或 Binding Resolve 阶段应将路径解析为设备和 Control 索引。

### 3.4 Processor 和 Interaction 分离

```text
Processor:
    输入值如何变换。

Interaction:
    输入在什么时间和条件下触发。
```

例如：

```text
StickDeadZoneProcessor:
    将摇杆死区内输入变为 0。

HoldInteraction:
    持续按住超过指定时间后触发。
```

---

# 4. 核心枚举和基础类型

## 4.1 输入设备类型

```cpp
enum class EInputDeviceType : u8
{
    kKeyboard,
    kMouse,
    kGamepad,
    kTouch,
    kVirtual
};
```

## 4.2 输入值类型

```cpp
enum class EInputValueType : u8
{
    kButton,
    kAxis1D,
    kAxis2D,
    kAxis3D
};
```

## 4.3 Action 类型

```cpp
enum class EInputActionType : u8
{
    kButton,
    kValue,
    kPassThrough
};
```

含义：

- `kButton`：离散输入，例如 Jump、Fire、Submit。
- `kValue`：持续值，例如 Move、Look、Throttle。
- `kPassThrough`：不进行 Binding 竞争，直接透传输入事件。

## 4.4 Action 阶段

```cpp
enum class EInputActionPhase : u8
{
    kDisabled,
    kWaiting,
    kStarted,
    kPerformed,
    kCanceled
};
```

含义：

- `kDisabled`：Action 未启用。
- `kWaiting`：等待输入。
- `kStarted`：交互已经开始。
- `kPerformed`：交互满足触发条件。
- `kCanceled`：交互开始后未满足条件便结束。

## 4.5 输入值

```cpp
struct InputValue
{
    EInputValueType _type = EInputValueType::kButton;
    Vector3f _value = Vector3f::kZero;

    bool AsButton(f32 threshold = 0.5f) const
    {
        return _value.x >= threshold;
    }

    f32 AsAxis1D() const
    {
        return _value.x;
    }

    Vector2f AsAxis2D() const
    {
        return {_value.x, _value.y};
    }

    Vector3f AsAxis3D() const
    {
        return _value;
    }

    f32 Magnitude() const
    {
        switch (_type)
        {
        case EInputValueType::kButton:
        case EInputValueType::kAxis1D:
            return Math::Abs(_value.x);
        case EInputValueType::kAxis2D:
            return Vector2f(_value.x, _value.y).Length();
        case EInputValueType::kAxis3D:
            return _value.Length();
        default:
            return 0.0f;
        }
    }
};
```

---

# 5. InputDevice

## 5.1 职责

`InputDevice` 表示一个实际或虚拟输入设备，例如：

- Keyboard
- Mouse
- Xbox Gamepad
- DualSense
- Touch Screen
- Virtual Device

它负责：

1. 从平台 API 采集原始输入。
2. 保存当前帧和上一帧的控件状态。
3. 对外提供统一的 Control 读取接口。
4. 管理设备连接、断开和重连。
5. 保存设备能力信息。
6. 生成设备级输入事件或状态快照。

它不负责：

- 判断 Space 是否表示 Jump。
- 判断输入是否构成长按或双击。
- 决定当前处于 Gameplay 还是 UI。
- 处理游戏状态。

## 5.2 推荐接口

```cpp
class InputDevice
{
public:
    virtual ~InputDevice() = default;

    virtual void Poll() = 0;
    virtual InputValue ReadControl(u16 control_index) const = 0;
    virtual EInputDeviceType GetDeviceType() const = 0;
    virtual bool IsConnected() const = 0;

    u32 GetDeviceId() const
    {
        return _device_id;
    }

    const String &GetDeviceName() const
    {
        return _device_name;
    }

protected:
    u32 _device_id = 0;
    String _device_name;
};
```

## 5.3 Keyboard 示例

```cpp
class KeyboardDevice final : public InputDevice
{
public:
    void Poll() override
    {
        _previous_keys = _current_keys;
        PlatformInput::GetKeyboardState(_current_keys.data(), static_cast<u32>(_current_keys.size()));
    }

    InputValue ReadControl(u16 control_index) const override
    {
        InputValue value;
        value._type = EInputValueType::kButton;
        value._value.x = _current_keys[control_index] ? 1.0f : 0.0f;
        return value;
    }

    EInputDeviceType GetDeviceType() const override
    {
        return EInputDeviceType::kKeyboard;
    }

    bool IsConnected() const override
    {
        return true;
    }

private:
    Array<bool, 256> _current_keys{};
    Array<bool, 256> _previous_keys{};
};
```

## 5.4 设备实例和设备类型

系统需要区分：

```text
Device Type:
    Gamepad

Device Instance:
    Gamepad 0
    Gamepad 1
```

Binding 默认可以绑定某类设备：

```text
<Gamepad>/button_south
```

也可以在运行时绑定到具体玩家设备实例。

---

# 6. InputControl

## 6.1 职责

`InputControl` 表示设备上的一个具体输入控件。

示例：

```text
Keyboard
├── w
├── a
├── s
├── d
└── space

Mouse
├── position
├── delta
├── wheel
├── left_button
└── right_button

Gamepad
├── left_stick
├── right_stick
├── left_trigger
├── right_trigger
├── button_south
└── dpad
```

## 6.2 推荐结构

```cpp
struct InputControlDesc
{
    String _name;
    EInputValueType _value_type = EInputValueType::kButton;
    u16 _index = 0;
};
```

运行时解析结果：

```cpp
struct ResolvedInputControl
{
    InputDevice *_device = nullptr;
    u16 _control_index = 0;
    EInputValueType _value_type = EInputValueType::kButton;

    bool IsValid() const
    {
        return _device != nullptr;
    }

    InputValue Read() const
    {
        return _device != nullptr ? _device->ReadControl(_control_index) : InputValue{};
    }
};
```

## 6.3 Control 路径解析

资源中的路径：

```text
<Keyboard>/space
<Gamepad>/left_stick
<Mouse>/delta
```

加载时解析为：

```cpp
ResolvedInputControl InputSystem::ResolveControlPath(StringView path);
```

解析过程：

1. 提取设备类型。
2. 查找匹配设备。
3. 查找设备上的 Control 名称。
4. 保存设备指针和 Control 索引。
5. 验证值类型是否兼容。

---

# 7. InputProcessor

## 7.1 职责

`InputProcessor` 对 Binding 输出值进行无状态或轻状态变换。

常见 Processor：

- Dead Zone
- Scale
- Invert
- Normalize
- Clamp
- Axis To Button
- Sensitivity
- Curve

Processor 不应负责：

- 长按。
- 双击。
- 超时。
- Action Phase。
- Context 优先级。

## 7.2 推荐接口

```cpp
class InputProcessor
{
public:
    virtual ~InputProcessor() = default;
    virtual InputValue Process(const InputValue &value) const = 0;
};
```

## 7.3 ScaleProcessor

```cpp
class ScaleProcessor final : public InputProcessor
{
public:
    InputValue Process(const InputValue &value) const override
    {
        InputValue result = value;
        result._value.x *= _scale.x;
        result._value.y *= _scale.y;
        result._value.z *= _scale.z;
        return result;
    }

private:
    Vector3f _scale = Vector3f::kOne;
};
```

## 7.4 StickDeadZoneProcessor

```cpp
class StickDeadZoneProcessor final : public InputProcessor
{
public:
    InputValue Process(const InputValue &value) const override
    {
        const Vector2f axis = value.AsAxis2D();
        const f32 length = axis.Length();

        InputValue result = value;
        if (length <= _min_dead_zone)
        {
            result._value = Vector3f::kZero;
            return result;
        }

        const f32 normalized_length = Math::Saturate((length - _min_dead_zone) / (_max_dead_zone - _min_dead_zone));
        const Vector2f normalized_axis = axis.Normalized() * normalized_length;
        result._value = {normalized_axis.x, normalized_axis.y, 0.0f};
        return result;
    }

private:
    f32 _min_dead_zone = 0.125f;
    f32 _max_dead_zone = 1.0f;
};
```

---

# 8. InputBinding

## 8.1 职责

`InputBinding` 将一个或多个物理 Control 映射到一个 `InputAction`。

例如：

```text
Jump
├── <Keyboard>/space
└── <Gamepad>/button_south

Fire
├── <Mouse>/left_button
└── <Gamepad>/right_trigger
```

它负责：

1. 保存 Control 路径。
2. 保存已解析的 Control。
3. 读取原始值。
4. 执行 Processor。
5. 保存 Interaction 配置。
6. 支持 Composite Binding。
7. 支持设备组和控制方案。
8. 支持用户重绑定。

## 8.2 推荐结构

```cpp
struct InputBinding
{
    String _name;
    String _control_path;
    String _groups;

    Vector<Own<InputProcessor>> _processors;
    Vector<Own<InputInteraction>> _interactions;

    ResolvedInputControl _resolved_control;

    bool _is_composite = false;
    bool _is_part_of_composite = false;
    String _composite_part_name;

    InputValue Evaluate() const
    {
        InputValue value = _resolved_control.Read();

        for (const Own<InputProcessor> &processor : _processors)
            value = processor->Process(value);

        return value;
    }
};
```

## 8.3 一个 Action 多个 Binding

```text
Jump Action
├── Keyboard Space Binding
├── Gamepad South Button Binding
└── Touch Jump Button Binding
```

Action 和 Binding 的职责不能混淆：

```text
Binding:
    输入来源。

Action:
    游戏语义。
```

## 8.4 Binding 合并规则

多个 Binding 同时输出时，Action 需要定义合并规则。

推荐默认策略：选择幅值最大的 Binding。

```cpp
InputValue InputAction::EvaluateBindings() const
{
    InputValue result;
    f32 max_magnitude = 0.0f;

    for (const InputBinding &binding : _bindings)
    {
        const InputValue value = binding.Evaluate();
        const f32 magnitude = value.Magnitude();

        if (magnitude > max_magnitude)
        {
            max_magnitude = magnitude;
            result = value;
        }
    }

    return result;
}
```

可选策略：

- 最大幅值。
- 累加后钳制。
- 最近输入设备优先。
- Pass Through。
- 指定设备优先。

建议初版只实现：

- Button：最大值。
- Axis：最大幅值。
- Pass Through：逐 Binding 发送事件。

---

# 9. Composite Binding

## 9.1 职责

Composite Binding 将多个 Control 合成为一个输入值。

常见 Composite：

- 1D Axis。
- 2D Vector。
- Button With Modifier。
- Chord。
- DPad。

## 9.2 WASD 示例

```text
2DVector Composite
├── Up    = <Keyboard>/w
├── Down  = <Keyboard>/s
├── Left  = <Keyboard>/a
└── Right = <Keyboard>/d
```

## 9.3 推荐接口

```cpp
class InputCompositeBinding
{
public:
    virtual ~InputCompositeBinding() = default;
    virtual InputValue Evaluate() const = 0;
};
```

## 9.4 Axis2DCompositeBinding

```cpp
class Axis2DCompositeBinding final : public InputCompositeBinding
{
public:
    InputValue Evaluate() const override
    {
        Vector2f result = Vector2f::kZero;

        result.y += _up.Evaluate().AsButton() ? 1.0f : 0.0f;
        result.y -= _down.Evaluate().AsButton() ? 1.0f : 0.0f;
        result.x -= _left.Evaluate().AsButton() ? 1.0f : 0.0f;
        result.x += _right.Evaluate().AsButton() ? 1.0f : 0.0f;

        if (_normalize && result.LengthSquared() > 1.0f)
            result = result.Normalized();

        InputValue value;
        value._type = EInputValueType::kAxis2D;
        value._value = {result.x, result.y, 0.0f};
        return value;
    }

private:
    InputBinding _up;
    InputBinding _down;
    InputBinding _left;
    InputBinding _right;
    bool _normalize = true;
};
```

---

# 10. InputInteraction

## 10.1 职责

`InputInteraction` 解释输入的时序和触发规则。

它负责回答：

```text
输入是否刚开始？
是否满足触发条件？
是否超时？
是否提前释放？
是否需要取消？
```

常见 Interaction：

- Press
- Release
- Hold
- Tap
- Slow Tap
- Multi Tap
- Chord
- Sequence

## 10.2 Interaction 上下文

```cpp
struct InputInteractionContext
{
    InputValue _value;
    f64 _time = 0.0;
    f32 _delta_time = 0.0f;
    bool _was_actuated = false;
    bool _is_actuated = false;
};
```

## 10.3 Interaction 结果

```cpp
struct InputInteractionResult
{
    EInputActionPhase _phase = EInputActionPhase::kWaiting;
    bool _phase_changed = false;
};
```

## 10.4 推荐接口

```cpp
class InputInteraction
{
public:
    virtual ~InputInteraction() = default;

    virtual InputInteractionResult Process(const InputInteractionContext &context) = 0;
    virtual void Reset() = 0;
};
```

## 10.5 PressInteraction

推荐定义为按下边沿触发：

```cpp
class PressInteraction final : public InputInteraction
{
public:
    InputInteractionResult Process(const InputInteractionContext &context) override
    {
        if (!context._was_actuated && context._is_actuated)
            return {EInputActionPhase::kPerformed, true};

        return {};
    }

    void Reset() override
    {
    }
};
```

## 10.6 ReleaseInteraction

```cpp
class ReleaseInteraction final : public InputInteraction
{
public:
    InputInteractionResult Process(const InputInteractionContext &context) override
    {
        if (context._was_actuated && !context._is_actuated)
            return {EInputActionPhase::kPerformed, true};

        return {};
    }

    void Reset() override
    {
    }
};
```

## 10.7 HoldInteraction

行为：

```text
按下：
    Started

达到持续时间：
    Performed

达到持续时间前松开：
    Canceled

Performed 后松开：
    Waiting
```

```cpp
class HoldInteraction final : public InputInteraction
{
public:
    InputInteractionResult Process(const InputInteractionContext &context) override
    {
        if (!_is_holding && !context._was_actuated && context._is_actuated)
        {
            _is_holding = true;
            _has_performed = false;
            _start_time = context._time;
            return {EInputActionPhase::kStarted, true};
        }

        if (_is_holding && !_has_performed && context._is_actuated &&
            context._time - _start_time >= static_cast<f64>(_duration))
        {
            _has_performed = true;
            return {EInputActionPhase::kPerformed, true};
        }

        if (_is_holding && !context._is_actuated)
        {
            const bool was_performed = _has_performed;
            Reset();
            return was_performed ? InputInteractionResult{} :
                                   InputInteractionResult{EInputActionPhase::kCanceled, true};
        }

        return {};
    }

    void Reset() override
    {
        _start_time = 0.0;
        _is_holding = false;
        _has_performed = false;
    }

private:
    f64 _start_time = 0.0;
    f32 _duration = 0.5f;
    bool _is_holding = false;
    bool _has_performed = false;
};
```

## 10.8 TapInteraction

推荐规则：

```text
按下：
    Started

在最大时间内松开：
    Performed

超过最大时间：
    Canceled
```

## 10.9 Interaction 所属层级

初版建议 Interaction 挂在 Binding 上。

原因：

```text
同一个 Action 的不同 Binding 可以使用不同交互方式。
```

例如：

```text
Interact Action
├── Keyboard E       → Press
└── Touch UI Button  → Hold 0.3s
```

Action 也可以提供默认 Interaction。当 Binding 没有自定义 Interaction 时，使用 Action 默认值。

---

# 11. InputAction

## 11.1 职责

`InputAction` 表示一个游戏语义输入。

示例：

- Move
- Look
- Jump
- Fire
- Sprint
- Interact
- OpenInventory
- Submit
- Cancel

它负责：

1. 保存 Action 名称和稳定 ID。
2. 定义 Action 类型和值类型。
3. 持有多个 Binding。
4. 聚合 Binding 输出。
5. 驱动 Interaction 状态机。
6. 保存当前值和上一帧值。
7. 保存当前 Action Phase。
8. 发送 Started、Performed、Canceled 事件。
9. 提供本帧查询接口。
10. 支持启用和禁用。

## 11.2 推荐接口

```cpp
struct InputActionEvent
{
    class InputAction *_action = nullptr;
    InputValue _value;
    EInputActionPhase _phase = EInputActionPhase::kWaiting;
    f64 _time = 0.0;
};

class InputAction
{
public:
    void Enable();
    void Disable();
    void Update(const struct InputActionUpdateContext &context);

    const String &GetName() const
    {
        return _name;
    }

    EInputActionPhase GetPhase() const
    {
        return _phase;
    }

    const InputValue &GetValue() const
    {
        return _current_value;
    }

    bool WasStartedThisFrame() const
    {
        return _started_this_frame;
    }

    bool WasPerformedThisFrame() const
    {
        return _performed_this_frame;
    }

    bool WasCanceledThisFrame() const
    {
        return _canceled_this_frame;
    }

private:
    void ResetFrameState();
    void ApplyInteractionResult(const InputInteractionResult &result, f64 time);

private:
    String _name;
    u32 _id = 0;

    EInputActionType _action_type = EInputActionType::kButton;
    EInputValueType _value_type = EInputValueType::kButton;
    EInputActionPhase _phase = EInputActionPhase::kDisabled;

    Vector<InputBinding> _bindings;

    InputValue _current_value;
    InputValue _previous_value;

    bool _enabled = false;
    bool _started_this_frame = false;
    bool _performed_this_frame = false;
    bool _canceled_this_frame = false;
};
```

## 11.3 Button Action 默认行为

没有显式 Interaction 时，Button Action 默认使用 Press 行为：

```text
未按下 → 按下:
    Performed
```

## 11.4 Value Action 默认行为

Value Action 在有效值变化时更新 `_current_value`。

建议提供：

```cpp
bool IsActuated() const;
bool ValueChangedThisFrame() const;
```

Move 示例：

```cpp
const Vector2f move_input = _move_action->GetValue().AsAxis2D();
_character->Move(move_input);
```

## 11.5 Action 更新示例

```cpp
void InputAction::Update(const InputActionUpdateContext &context)
{
    ResetFrameState();

    if (!_enabled)
        return;

    _previous_value = _current_value;
    _current_value = EvaluateBindings();

    const bool was_actuated = _previous_value.Magnitude() >= context._actuation_threshold;
    const bool is_actuated = _current_value.Magnitude() >= context._actuation_threshold;

    ProcessInteractions(context, was_actuated, is_actuated);
}
```

---

# 12. InputActionMap

## 12.1 职责

`InputActionMap` 是一组逻辑相关的 Action。

示例：

```text
Gameplay
├── Move
├── Look
├── Jump
├── Fire
├── Sprint
└── Interact

UI
├── Navigate
├── Submit
├── Cancel
├── Point
└── Click

Vehicle
├── Steer
├── Throttle
├── Brake
└── ExitVehicle

EditorScene
├── CameraMove
├── CameraRotate
├── FocusSelection
└── SelectObject
```

它负责：

1. 组织 Action。
2. 批量启用和禁用 Action。
3. 提供按名称或 ID 查询。
4. 保存 Action 名称到索引的映射。
5. 作为 Context 的基本组合单位。
6. 支持资源序列化。

## 12.2 推荐接口

```cpp
class InputActionMap
{
public:
    void Enable()
    {
        if (_enabled)
            return;

        _enabled = true;
        for (InputAction &action : _actions)
            action.Enable();
    }

    void Disable()
    {
        if (!_enabled)
            return;

        _enabled = false;
        for (InputAction &action : _actions)
            action.Disable();
    }

    InputAction *FindAction(StringView name);
    const InputAction *FindAction(StringView name) const;

    bool IsEnabled() const
    {
        return _enabled;
    }

private:
    String _name;
    u32 _id = 0;
    Vector<InputAction> _actions;
    HashMap<String, u32> _action_name_to_index;
    bool _enabled = false;
};
```

## 12.3 Action Map 不是优先级系统

`InputActionMap` 只负责组织和批量开关。

以下职责属于 `InputContext`：

- Map 之间谁优先。
- 是否阻塞下层。
- 是否消费输入。
- 当前处于哪个输入模式。

---

# 13. InputContext

## 13.1 职责

`InputContext` 表示当前输入环境。

它负责解决：

```text
当前哪些 ActionMap 生效？
UI 和 Gameplay 同时绑定 Escape 时谁优先？
高优先级输入是否阻塞低优先级输入？
输入是否会被消费？
打开菜单后角色是否还能移动？
文本框获得焦点后快捷键是否仍生效？
```

## 13.2 常见 Context

```text
Global
Gameplay
UI
PauseMenu
Inventory
Dialogue
Vehicle
Console
Editor
EditorScene
EditorTextInput
GameView
```

## 13.3 推荐结构

```cpp
class InputContext
{
public:
    const String &GetName() const
    {
        return _name;
    }

    i32 GetPriority() const
    {
        return _priority;
    }

    bool BlocksLowerContexts() const
    {
        return _block_lower_contexts;
    }

private:
    String _name;
    i32 _priority = 0;

    bool _consume_input = true;
    bool _block_lower_contexts = false;
    bool _active = false;

    Vector<Ref<InputActionMap>> _action_maps;
};
```

## 13.4 Context 栈

推荐 InputSystem 维护一个激活 Context 列表或栈：

```cpp
class InputContextStack
{
public:
    void Push(Ref<InputContext> context);
    void Pop(StringView context_name);
    void Clear();
    void SortByPriority();

private:
    Vector<Ref<InputContext>> _contexts;
};
```

处理顺序：

```text
高优先级 Context
        ↓
低优先级 Context
```

## 13.5 阻塞和消费

### 阻塞下层

`_block_lower_contexts = true` 表示当前 Context 之后的低优先级 Context 不再更新。

适用场景：

- 暂停菜单。
- 模态窗口。
- 控制台。
- 全屏设置界面。

### 消费输入

高优先级 Context 处理某个 Control 后，将该 Control 标记为已消费。

低优先级 Context 中依赖相同 Control 的 Binding 不再生效。

推荐消费 Control，而不是只消费 Action。

原因：

```text
Escape 被 UI 消费后，Gameplay 中所有使用 Escape 的 Action 都应停止触发。
```

## 13.6 输入消费状态

```cpp
struct InputConsumptionState
{
    HashSet<u64> _consumed_control_ids;

    bool IsConsumed(u64 control_id) const
    {
        return _consumed_control_ids.contains(control_id);
    }

    void Consume(u64 control_id)
    {
        _consumed_control_ids.insert(control_id);
    }

    void Reset()
    {
        _consumed_control_ids.clear();
    }
};
```

---

# 14. InputActionAsset

## 14.1 职责

`InputActionAsset` 是输入配置资源。

它负责保存：

- Action Map。
- Action。
- Binding。
- Processor 参数。
- Interaction 参数。
- Context。
- Control Scheme。

```cpp
ACLASS()
class AILU_API InputActionAsset : public Object
{
    GENERATED_BODY()

public:
    InputActionMap *FindActionMap(StringView name);
    InputContext *FindContext(StringView name);

private:
    APROPERTY()
    Vector<InputActionMap> _action_maps;

    APROPERTY()
    Vector<InputContext> _contexts;
};
```

## 14.2 资源实例化

资源对象不应直接保存运行时设备指针。

推荐流程：

```text
InputActionAsset
        ↓ Instantiate
InputActionRuntime
        ↓ Resolve Bindings
InputSystem
```

资源对象保存描述，运行时实例保存状态。

如果初版不希望增加额外 Runtime 类型，也必须保证：

- 资源模板不会被多个玩家共享可变状态。
- Interaction 状态不会写回资产。
- Binding 的设备指针不会序列化。

---

# 15. InputSystem

## 15.1 职责

`InputSystem` 是整个输入系统的入口和调度器。

它负责：

1. 注册和移除 InputDevice。
2. Poll 所有设备。
3. 解析 Control Path。
4. 管理激活 Context。
5. 按优先级更新 Context。
6. 更新 Action Map 和 Action。
7. 处理输入消费。
8. 记录最后活跃设备。
9. 提供 Action 查询。
10. 处理设备连接事件。
11. 支持用户重绑定。

## 15.2 推荐接口

```cpp
class InputSystem
{
public:
    void RegisterDevice(Own<InputDevice> device);
    void RemoveDevice(u32 device_id);

    void PushContext(Ref<InputContext> context);
    void PopContext(StringView context_name);

    ResolvedInputControl ResolveControlPath(StringView path);

    void Update(f32 delta_time);

    InputDevice *GetLastActiveDevice() const
    {
        return _last_active_device;
    }

private:
    void PollDevices();
    void UpdateContexts(f32 delta_time);
    void ResetFrameState();

private:
    Vector<Own<InputDevice>> _devices;
    Vector<Ref<InputContext>> _active_contexts;

    InputConsumptionState _consumption_state;

    InputDevice *_last_active_device = nullptr;
    f64 _current_time = 0.0;
};
```

---

# 16. 每帧更新流程

## 16.1 总体顺序

```text
1. 重置本帧输入事件和消费状态。
2. Poll 所有 InputDevice。
3. 检测设备连接和断开。
4. 检测最后活跃设备。
5. 按优先级排序激活 Context。
6. 从高优先级 Context 开始更新。
7. 更新 Context 中的 ActionMap。
8. 读取 Binding。
9. 执行 Processor。
10. 聚合 Action 值。
11. 执行 Interaction。
12. 派发 Action 事件。
13. 标记已消费 Control。
14. 根据 Context 配置决定是否阻塞下层。
```

## 16.2 示例伪代码

```cpp
void InputSystem::Update(f32 delta_time)
{
    _current_time += delta_time;
    _consumption_state.Reset();

    PollDevices();

    Sort(_active_contexts, [](const Ref<InputContext> &lhs, const Ref<InputContext> &rhs) {
        return lhs->GetPriority() > rhs->GetPriority();
    });

    for (const Ref<InputContext> &context : _active_contexts)
    {
        UpdateContext(*context, delta_time);

        if (context->BlocksLowerContexts())
            break;
    }
}
```

---

# 17. 完整场景示例

## 17.1 Gameplay

Action Map：

```text
Gameplay
├── Move
├── Look
├── Jump
├── Fire
├── Sprint
└── Pause
```

Context：

```text
GameplayContext
Priority: 10
Block Lower: false
```

## 17.2 暂停菜单

Action Map：

```text
UI
├── Navigate
├── Submit
└── Cancel
```

Context：

```text
PauseMenuContext
Priority: 100
Block Lower: true
```

打开暂停菜单：

```cpp
void GameMode::OpenPauseMenu()
{
    _input_system->PushContext(_pause_menu_context);
    _pause_menu->SetVisible(true);
}
```

关闭暂停菜单：

```cpp
void GameMode::ClosePauseMenu()
{
    _input_system->PopContext("PauseMenu");
    _pause_menu->SetVisible(false);
}
```

当 PauseMenuContext 激活时：

- WASD 不移动角色。
- Enter 触发 Submit。
- Escape 触发 Cancel。
- GameplayContext 不再更新。

## 17.3 蓄力攻击

配置：

```text
Action:
    ChargedAttack

Binding:
    <Mouse>/left_button

Interaction:
    Hold
    Duration = 1.0
```

时间线：

```text
0.0 秒按下:
    Started

0.4 秒松开:
    Canceled
```

或者：

```text
0.0 秒按下:
    Started

1.0 秒仍按住:
    Performed

1.3 秒松开:
    回到 Waiting
```

游戏代码：

```cpp
void PlayerController::OnChargedAttackStarted(const InputActionEvent &event)
{
    _character->BeginCharge();
}

void PlayerController::OnChargedAttackPerformed(const InputActionEvent &event)
{
    _character->ReleaseChargedAttack();
}

void PlayerController::OnChargedAttackCanceled(const InputActionEvent &event)
{
    _character->CancelCharge();
}
```

## 17.4 编辑器 Scene View

Action Map：

```text
EditorScene
├── CameraMove
├── CameraRotate
├── CameraZoom
├── FocusSelection
├── SelectObject
└── DeleteSelection
```

Context：

```text
EditorSceneContext
Priority: 20
```

Scene View 获得焦点时：

```cpp
void SceneView::OnFocus()
{
    _input_system->PushContext(_scene_view_context);
}
```

Scene View 失去焦点时：

```cpp
void SceneView::OnLostFocus()
{
    _input_system->PopContext("EditorScene");
}
```

文本框获得焦点时压入：

```text
EditorTextInputContext
Priority: 1000
Block Lower: false
Consume Keyboard: true
```

这样可以防止：

- 输入 `W` 时切换编辑器移动工具。
- 按 Delete 删除文本时误删场景对象。
- 按 Space 输入空格时触发 Scene View 操作。

---

# 18. 配置资源示例

```json
{
    "action_maps": [
        {
            "name": "Gameplay",
            "actions": [
                {
                    "name": "Move",
                    "action_type": "Value",
                    "value_type": "Axis2D",
                    "bindings": [
                        {
                            "type": "2DVectorComposite",
                            "up": "<Keyboard>/w",
                            "down": "<Keyboard>/s",
                            "left": "<Keyboard>/a",
                            "right": "<Keyboard>/d"
                        },
                        {
                            "control_path": "<Gamepad>/left_stick",
                            "processors": [
                                {
                                    "type": "StickDeadZone",
                                    "min": 0.125,
                                    "max": 1.0
                                }
                            ]
                        }
                    ]
                },
                {
                    "name": "Jump",
                    "action_type": "Button",
                    "value_type": "Button",
                    "bindings": [
                        {
                            "control_path": "<Keyboard>/space",
                            "interactions": [
                                {
                                    "type": "Press"
                                }
                            ]
                        },
                        {
                            "control_path": "<Gamepad>/button_south",
                            "interactions": [
                                {
                                    "type": "Press"
                                }
                            ]
                        }
                    ]
                },
                {
                    "name": "ChargedAttack",
                    "action_type": "Button",
                    "value_type": "Button",
                    "bindings": [
                        {
                            "control_path": "<Mouse>/left_button",
                            "interactions": [
                                {
                                    "type": "Hold",
                                    "duration": 1.0
                                }
                            ]
                        }
                    ]
                }
            ]
        },
        {
            "name": "UI",
            "actions": [
                {
                    "name": "Submit",
                    "action_type": "Button",
                    "value_type": "Button",
                    "bindings": [
                        {
                            "control_path": "<Keyboard>/enter"
                        },
                        {
                            "control_path": "<Gamepad>/button_south"
                        }
                    ]
                },
                {
                    "name": "Cancel",
                    "action_type": "Button",
                    "value_type": "Button",
                    "bindings": [
                        {
                            "control_path": "<Keyboard>/escape"
                        },
                        {
                            "control_path": "<Gamepad>/button_east"
                        }
                    ]
                }
            ]
        }
    ],
    "contexts": [
        {
            "name": "Gameplay",
            "priority": 10,
            "block_lower_contexts": false,
            "action_maps": [
                "Gameplay"
            ]
        },
        {
            "name": "PauseMenu",
            "priority": 100,
            "block_lower_contexts": true,
            "action_maps": [
                "UI"
            ]
        }
    ]
}
```

---

# 19. 建议目录结构

```text
Runtime/Input/
├── InputSystem.h
├── InputSystem.cpp
├── InputTypes.h
├── InputValue.h
├── InputDevice.h
├── InputControl.h
├── InputBinding.h
├── InputBinding.cpp
├── InputCompositeBinding.h
├── InputProcessor.h
├── InputInteraction.h
├── InputInteraction.cpp
├── InputAction.h
├── InputAction.cpp
├── InputActionMap.h
├── InputActionMap.cpp
├── InputContext.h
├── InputContext.cpp
└── Devices/
    ├── KeyboardDevice.h
    ├── KeyboardDevice.cpp
    ├── MouseDevice.h
    ├── MouseDevice.cpp
    ├── GamepadDevice.h
    └── GamepadDevice.cpp

Runtime/Asset/
├── InputActionAsset.h
└── InputActionAsset.cpp

Editor/Input/
├── InputActionAssetEditor.h
├── InputActionAssetEditor.cpp
├── InputBindingDrawer.h
└── InputBindingDrawer.cpp

Platform/Windows/Input/
├── WindowsKeyboardDevice.cpp
├── WindowsMouseDevice.cpp
└── WindowsGamepadDevice.cpp
```

---

# 20. 第一阶段实现范围

第一阶段只实现核心可用功能。

## 必须实现

- KeyboardDevice。
- MouseDevice。
- InputControl。
- Button、Axis1D、Axis2D。
- 普通 InputBinding。
- 2D Vector Composite。
- Scale、Invert、DeadZone Processor。
- Press、Release、Hold Interaction。
- InputAction。
- InputActionMap。
- InputContext。
- Context 优先级。
- Context 阻塞下层。
- Control 消费。
- Action 轮询查询。
- Action 事件回调。
- JSON 或现有资产格式序列化。

## 暂不实现

- 多玩家设备配对。
- 手柄热插拔高级逻辑。
- MultiTap。
- Sequence。
- 复杂 Chord。
- 输入录制和回放。
- 网络输入预测。
- 完整重绑定 UI。
- 平台触摸输入。
- 输入宏系统。

---

# 21. 第二阶段扩展

- GamepadDevice。
- Control Scheme。
- 用户重绑定。
- Binding Override。
- 多玩家设备配对。
- 最后活跃设备检测。
- 键鼠和手柄提示自动切换。
- Tap、SlowTap、MultiTap。
- Chord Binding。
- 输入录制和回放。
- Input Debugger。
- Input Action Asset Editor。
- 冲突 Binding 检测。
- 输入事件时间戳。
- Fixed Update 输入缓存。

---

# 22. Fixed Update 支持

输入设备通常按渲染帧采集，但 Gameplay 可能在 Fixed Update 中消费。

必须避免某个短按发生在两个 Fixed Tick 之间而丢失。

建议 InputSystem 保存边沿事件队列：

```cpp
struct InputActionFrameEvent
{
    u32 _action_id = 0;
    EInputActionPhase _phase = EInputActionPhase::kWaiting;
    InputValue _value;
    f64 _time = 0.0;
};
```

渲染帧中产生的 `Performed` 事件进入队列。

Fixed Update 消费尚未处理的事件。

持续值如 Move 可以直接读取最近状态，离散事件如 Jump 应通过事件队列消费。

---

# 23. 生命周期与重置规则

## Action Disable

Action 被禁用时：

1. 如果处于 Started 状态，应触发 Canceled。
2. 重置所有 Interaction。
3. 清空当前值。
4. 设置 Phase 为 Disabled。
5. 清空本帧标记。

## Context Pop

Context 被移除时：

1. 禁用其独占启用的 ActionMap。
2. 重置其中 Action 的 Interaction 状态。
3. 清除与该 Context 相关的临时消费状态。
4. 不应保留半完成 Hold 或 Tap。

## Device Disconnect

设备断开时：

1. 其所有 Control 视为回到默认值。
2. 由该设备启动但未完成的 Interaction 应取消。
3. 重新 Resolve 受影响 Binding。
4. 通知上层设备断开事件。

---

# 24. 调试支持

建议增加 Input Debugger 面板。

显示内容：

```text
Devices
├── Keyboard
├── Mouse
└── Gamepad 0

Active Contexts
├── EditorTextInput  Priority 1000
├── EditorScene      Priority 20
└── Global           Priority 0

Actions
├── Move             Value (0.0, 1.0)
├── Jump             Waiting
├── Fire             Performed
└── ChargedAttack    Started 0.46 / 1.00

Consumed Controls
└── <Keyboard>/escape
```

每个 Action 显示：

- 当前 Phase。
- 当前值。
- 上一帧值。
- Active Binding。
- Active Device。
- Interaction 状态。
- 本帧是否 Started、Performed、Canceled。
- 是否被 Context 屏蔽。
- 是否因 Control 被消费而跳过。

---

# 25. 单元测试要求

## InputValue

- Button 转换。
- Axis1D 转换。
- Axis2D 转换。
- Magnitude。
- 默认值。

## Processor

- Scale。
- Invert。
- Dead Zone 边界。
- Normalize。
- Processor 链顺序。

## Binding

- Control 解析成功。
- Control 解析失败。
- 多 Binding 最大幅值。
- Composite WASD。
- 对角线归一化。
- 被消费 Control 不产生值。

## Interaction

- Press 仅在按下边沿触发一次。
- Release 仅在释放边沿触发一次。
- Hold 达到持续时间后触发。
- Hold 提前释放时取消。
- Hold 完成后释放不会重复 Performed。
- Disable 时重置 Interaction。
- Device Disconnect 时取消。

## Action

- Enable 和 Disable。
- 当前值和上一帧值。
- Started、Performed、Canceled 标记只持续一帧。
- 默认 Button Interaction。
- Value Action 连续更新。
- 多 Binding 聚合。

## Context

- 高优先级先更新。
- Block Lower Contexts。
- Control 消费。
- Push 和 Pop。
- Pop 时取消进行中的 Interaction。
- 文本输入 Context 屏蔽编辑器快捷键。

---

# 26. 验收标准

实现完成后应满足以下条件：

1. Gameplay 代码可以完全通过 Action 查询输入，不直接访问平台键盘和鼠标。
2. Jump 可以同时绑定 Space 和 Gamepad South Button。
3. Move 可以同时支持 WASD 和左摇杆。
4. WASD 对角线输入可以归一化。
5. 摇杆可以应用 Dead Zone。
6. Hold Interaction 可以正确产生 Started、Performed 和 Canceled。
7. 打开暂停菜单后 Gameplay 输入停止。
8. UI 使用 Escape 后 Gameplay 中相同按键不会同时触发。
9. 编辑器文本框获得焦点时，字符输入不会触发 Scene View 快捷键。
10. Binding Path 在加载阶段解析，运行时不进行字符串查找。
11. Action Disable 和 Context Pop 不会留下未完成 Interaction 状态。
12. 短按事件不会因为 Fixed Update 频率不同而丢失。
13. Input Debugger 可以观察设备、Context、Action、Binding 和 Interaction 状态。
14. 所有核心行为都有单元测试覆盖。

---

# 27. 职责边界总结

| 类型 | 核心职责 | 不应负责 |
|---|---|---|
| `InputDevice` | 采集设备状态 | 游戏语义和交互时序 |
| `InputControl` | 标识设备控件 | Action 逻辑 |
| `InputBinding` | Control 到 Action 的连接 | 游戏状态 |
| `InputProcessor` | 输入值变换 | 长按、双击、超时 |
| `InputInteraction` | 输入时序和触发规则 | Context 路由 |
| `InputAction` | 游戏语义、值和 Phase | 平台输入采集 |
| `InputActionMap` | 组织 Action | 优先级和消费 |
| `InputContext` | 输入模式、优先级、阻塞和消费 | 设备读取 |
| `InputActionAsset` | 配置和序列化 | 运行时状态 |
| `InputSystem` | 生命周期和统一调度 | 具体 Gameplay 行为 |

最终必须遵守：

```text
InputDevice 不理解 Jump。
InputBinding 不保存角色状态。
InputProcessor 不处理时间。
InputInteraction 不决定当前游戏模式。
InputAction 不直接访问平台 API。
InputActionMap 不处理优先级。
InputContext 不采集设备状态。
InputActionAsset 不保存运行时设备指针。
Gameplay 不依赖具体设备按键。
```
