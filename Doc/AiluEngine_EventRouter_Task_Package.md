# AiluEngine EventRouter 轻量事件路由任务包

## 1. 目标

在现有 `Delegate<Args...>` 基础上新增轻量 `EventRouter<Key, Args...>`，用于支持：

- Input Action 按 action/key 订阅
- Collision Event 按 channel/key 订阅
- Animation Event / Gameplay Event 等后续按 key 分发场景
- Lua 侧统一使用“事件 + key + callback”模型
- 避免 AiluHeadTool 针对 Input、Physics 等业务事件持续增加特化绑定逻辑

核心原则：

```text
Delegate<Args...>          = 单个事件，多播
EventRouter<Key, Args...>  = 一组同类事件，先按 key 路由，再多播
```

不要给现有 `Delegate` 强行增加可选 `event_key`。

---

## 2. 保留现有 Delegate

继续用于不需要路由的事件，例如：

```text
OnEnable
OnDisable
OnDestroy
OnTransformChanged
OnWindowResize
```

建议顺便修正两个问题：

1. `Handle` 从 `1` 开始，`0` 作为无效值。
2. 移除或废弃 `Unsubscribe(HandlerType)`，统一通过 `Handle` 取消订阅。

推荐：

```cpp
using Handle = u32;
static constexpr Handle kInvalidHandle = 0;

Handle _next_id = 1;
```

原因：`std::function::target()` 无法可靠比较捕获 lambda、`std::bind` 等 callable。

---

## 3. 新增 EventRouter

建议接口：

```cpp
template<typename Key, typename... Args>
class EventRouter : public NonCopyable
{
public:
    using HandlerType = std::function<void(Args...)>;
    using Handle = u32;

    Handle Subscribe(const Key& key, HandlerType&& handler);
    void Unsubscribe(Handle handle);
    void Invoke(const Key& key, Args... args);

    class EventView;
    EventView GetEventView();

private:
    struct HandlerEntry
    {
        Handle _handle = 0;
        HandlerType _handler;
    };

    // 实现可根据现有 Ailu 容器选择 HashMap / unordered_map。
};
```

语义上等价于：

```text
Key
 ├─ Jump      -> handlers[]
 ├─ Attack    -> handlers[]
 └─ Interact  -> handlers[]
```

`Invoke(key)` 只遍历对应 key 的监听者，不要遍历全部监听者后逐个比较 key。

---

## 4. Handle 管理

`Unsubscribe` 只接收 Handle：

```cpp
const auto handle = event.Subscribe(key, callback);
event.Unsubscribe(handle);
```

实现需要能够通过 Handle 找到对应 Key。

可以维护：

```cpp
HashMap<Key, Vector<HandlerEntry>> _handlers;
HashMap<Handle, Key> _handle_to_key;
```

删除最后一个监听者后，应清理空的 key bucket。

暂时不需要复杂的 connection/token RAII 系统，后续 Lua 生命周期确有需求时再扩展。

---

## 5. 线程安全与 Invoke 行为

保持现有 `Delegate` 的基本语义：

1. `Subscribe / Unsubscribe / Invoke` 可安全并发访问内部容器。
2. `Invoke` 不要持锁执行用户 callback。
3. 在锁内复制当前 key 对应的 handler 列表。
4. 解锁后逐个调用 callback。

示意：

```cpp
void Invoke(const Key& key, Args... args)
{
    Vector<HandlerEntry> handlers;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        // 找到 key 对应 bucket，并复制
    }

    for (auto& entry : handlers)
        entry._handler(args...);
}
```

当前版本优先保证正确性和接口简洁，不需要为了避免 callback copy 引入复杂生命周期方案。

---

## 6. EventView

沿用现有 `Delegate::EventView` 的思想，对外只暴露订阅能力，不暴露 `Invoke`。

目标接口：

```cpp
auto event = _event_router.GetEventView();

event.Subscribe(key, callback);
event.Unsubscribe(handle);
```

事件拥有者：

```cpp
_event_router.Invoke(key, args...);
```

外部订阅者不能主动广播事件。

## 9. Lua / AiluHeadTool

目标是让生成工具只理解统一事件模型，而不是业务类型。

需要支持两种事件：

```text
Delegate
EventRouter
```

Lua 侧建议统一成：

```lua
event:subscribe(callback)
event:unsubscribe(handle)

event_router:subscribe(key, callback)
event_router:unsubscribe(handle)
```

业务层可以继续包装成更自然的 API：

```lua
input:on_action("jump", callback)
collider:on_collision_enter(CollisionChannel.Enemy, callback)
```

但这些 wrapper 最终都落到同一套 `EventRouter` binding，不要为 Input / Collision 分别写底层绑定机制。

修改目前的所有事件导出

```cpp
        AEVENT(Script, KeyIndex = 0, KeyName = action_name)
        DECLARE_DELEGATE(on_performed, String);
        AEVENT(Script, KeyIndex = 0, KeyName = action_name)
        DECLARE_DELEGATE(on_value_changed, String, f32);
```

改成这样

```cpp
AEVENT(Script, KeyName = action_name)
void OnPerformed(String action_name);

AEVENT(Script, KeyName = action_name)
void OnValueChanged(String action_name, f32 value);
```

普通函数形式，AHT来解析key的类型和参数名



---

## 10. 不做的内容

本任务不要顺手加入：

- 全局 EventBus / MessageBus
- 优先级监听
- 弱引用 listener
- 异步事件队列
- wildcard key
- hierarchical key
- bitmask matcher 泛化
- callback 返回值聚合
- RAII Connection 大规模重构
- Delegate 全量替换

保持 EventRouter 足够小。

---

## 11. 建议文件

尽量复用现有 Delegate 文件与命名结构。

可选：

```text
Framework/Core/Delegate.h
```

直接在同一文件增加：

```cpp
Delegate<Args...>
EventRouter<Key, Args...>
```

如果文件开始明显膨胀，再拆：

```text
Delegate.h
EventRouter.h
```

优先减少无意义文件拆分。

## 13. 最终期望

事件基础设施保持两层：

```text
Delegate<Args...>
    └─ 单事件 multicast

EventRouter<Key, Args...>
    └─ keyed routing
          └─ multicast
```

业务系统：

```text
Input Action      ┐
Animation Event   ├─ EventRouter
Gameplay Event    │
Collision Event   ┘

OnDestroy         ┐
OnEnable          ├─ Delegate
OnChanged         ┘
```

实现重点是：

**统一事件路由模型、降低 Lua/AiluHeadTool 特化数量、保持基础设施轻量，不把 EventRouter 演化成大型 EventBus。**
