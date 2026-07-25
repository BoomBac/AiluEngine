# AiluEngine 阶段驱动更新系统改造方案

## 1. 改造目标

将 AiluEngine 当前依赖硬编码调用顺序的更新流程，逐步重构为显式的阶段驱动系统。

当前主要问题：

- `Application::LogicLoop()` 同时负责输入、资源、Layer、场景、脚本、渲染和 Present。
- `Scene::Update()` 同时负责脚本、物理、Transform、动画、AABB、Camera、BVH 和 GPU Scene 更新。
- ECS System 的执行顺序依赖具体类型判断。
- `PhysicsSystem` 和 `TransformSystem` 在 `Scene::Update()` 中被特殊处理。
- 当前存在 `_update_lag`，但真正的固定时间步更新未启用。
- `ScriptSystem` 同时存在全局 `Tick()` 和逐组件 `UpdateComponent()`，职责边界不明确。

本次改造应满足：

1. 保持现有行为不变，先完成结构拆分。
2. ECS System 通过 `phase + order` 声明执行位置。
3. 移除对具体 System 类型名称的特殊判断。
4. 为后续 FixedUpdate、并行调度和脚本生命周期扩展提供基础。
5. 不引入每个 Component 单独虚调用的更新模式。

---

# 2. 设计原则

## 2.1 Component 只保存数据

保持 ECS 数据导向设计：

```text
Component = 数据
System = 行为
Phase = 调度位置
```

不要为普通 ECS Component 增加：

```cpp
FixedUpdate();
Update();
LateUpdate();
```

原因：

- 会产生大量虚函数调用。
- 会破坏相同组件的连续遍历。
- 不利于后续 JobSystem 并行。
- System 之间的依赖关系难以统一管理。

Lua 脚本组件可以由 `ScriptSystem` 批量分发生命周期。

---

## 2.2 阶段顺序必须显式

阶段顺序必须由调度系统集中管理，不允许继续通过下面的方式表达依赖：

```cpp
if (type == PhysicsSystem::TypeName())
{
    // ...
}
```

或：

```cpp
if (type != TransformSystem::TypeName())
{
    // ...
}
```

System 只声明：

```cpp
phase
order
enabled
```

调度器负责执行。

---

## 2.3 第一阶段只重构，不改变行为

初次改造必须保持现有执行顺序。

不要在同一次提交中同时完成：

- 阶段系统重构；
- FixedUpdate 接入；
- Physics 行为调整；
- Lua 生命周期函数扩展；
- 多线程调度。

应拆分为多个可验证阶段。

---

# 3. 当前更新流程

## 3.1 Application 层

当前 `Application::LogicLoop()` 大致执行：

```text
BeforeUpdate
TimeMgr::Tick
Input::BeginFrame
UIManager::Update
Event Dispatch
ResourceMgr::Tick
Layer::OnUpdate
SceneMgr::Tick
ScriptSystem::Tick
RenderPipeline::Render
ImGui
Present
FrameCleanup
AfterUpdate
```

## 3.2 Scene 层

当前 `Scene::Update()` 大致执行：

```text
ProcessSceneCommands
ScriptComponent Update
PhysicsSystem
TransformSystem
其他 ECS System
StaticMesh Bounds Update
SkeletonMesh Bounds Update
Camera Sync
RebuildBVHTree
DeletePendingEntities
UpdateGpuScene
```

阶段驱动改造应将这些隐式阶段显式化。

---

# 4. 目标架构

更新系统分为两层。

## 4.1 Engine Phase

负责整个应用帧：

```cpp
enum class EEnginePhase : u8
{
    kBeginFrame,
    kInput,
    kResourceUpdate,
    kLayerUpdate,
    kSceneUpdate,
    kPreRender,
    kRender,
    kEditorRender,
    kPresent,
    kEndFrame,
};
```

首版不要求所有模块动态注册到 Engine Phase。

优先将 `Application::LogicLoop()` 拆成明确函数。

---

## 4.2 Scene System Phase

负责场景内部 ECS System 调度：

```cpp
namespace Ailu::ECS
{
    enum class ESystemPhase : u8
    {
        kPrePhysics,
        kPhysics,
        kPostPhysics,
        kTransform,
        kAnimation,
        kPostAnimation,
        kGameplay,
        kRenderData,
    };
}
```

首版阶段顺序固定为：

```text
PrePhysics
Physics
PostPhysics
Transform
Animation
PostAnimation
Gameplay
RenderData
```

---

# 5. 第一阶段：拆分 Application::LogicLoop

## 5.1 目标

将当前较大的 `Application::LogicLoop()` 拆成具名函数，但保持调用顺序和行为不变。

## 5.2 新增函数

在 `Application` 中增加：

```cpp
void BeginFrame();
void UpdateInputAndEvents();
void UpdateResources(f32 delta_time);
void UpdateLayers(f32 delta_time);
void UpdateScenes(f32 delta_time);
void PrepareRender();
void RenderFrame();
void RenderEditor();
void PresentFrame();
void EndFrame();
```

## 5.3 重构后的 LogicLoop

```cpp
void Application::LogicLoop()
{
    const f32 delta_time = TimeMgr::s_delta_time;

    if (_state == EApplicationState::EApplicationState_Pause)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return;
    }

    BeginFrame();
    UpdateInputAndEvents();

    if (_state != EApplicationState::EApplicationState_Running)
        return;

    UpdateResources(delta_time);
    UpdateLayers(delta_time);
    UpdateScenes(delta_time);
    PrepareRender();
    RenderFrame();
    RenderEditor();
    PresentFrame();
    EndFrame();
}
```

## 5.4 BeginFrame

应包含：

```text
BeforeUpdate delegate
TimeMgr::Tick
Input::BeginFrame
BeginCursorFrame
Profiler::BeginFrame
```

注意保持当前 Profiler delegate 行为，不要重复调用。

## 5.5 UpdateInputAndEvents

应包含：

```text
UIManager::Update
RawEventQueue 消费
Layer::OnEvent
拖放文件事件
```

## 5.6 UpdateResources

应包含：

```cpp
ResourceMgr::Get().Tick(delta_time);
```

后续可以增加：

```text
资源异步完成处理
热重载
脚本重载请求
```

## 5.7 UpdateLayers

保持当前行为：

```cpp
for (Layer *layer : *_layer_stack)
    layer->OnUpdate(delta_time);
```

不要继续传入固定的 `1.0f`。

如果现有 Layer 逻辑依赖 `1.0f`，先保留旧行为，并添加 TODO，后续单独修复时间单位。

## 5.8 UpdateScenes

首版保持：

```cpp
SceneManagement::SceneMgr::Get().Tick(delta_time);
ScriptSystem::Get().Tick(delta_time);
```

后续再拆分 ScriptSystem 职责。

## 5.9 RenderFrame

```cpp
Render::RenderPipeline::Get().Render();
```

## 5.10 RenderEditor

包含 ImGui：

```cpp
_p_imgui_layer->Begin();

for (Layer *layer : *_layer_stack)
    layer->OnImguiRender();

_p_imgui_layer->End();
```

## 5.11 PresentFrame

```cpp
g_pGfxContext->Present();
Render::RenderPipeline::Get().FrameCleanup();
```

## 5.12 EndFrame

包含：

```text
AfterUpdate delegate
Profiler::EndFrame
ThreadPool records 清理
Frame count 增加
Tracy FrameMark
```

---

# 6. 第二阶段：为 ECS System 增加阶段信息

## 6.1 修改 System 基类

为 ECS System 基类增加：

```cpp
namespace Ailu::ECS
{
    class System
    {
    public:
        virtual ~System() = default;

        virtual void Update(Register &reg, f32 delta_time) = 0;

        virtual ESystemPhase GetPhase() const
        {
            return ESystemPhase::kGameplay;
        }

        virtual i32 GetOrder() const
        {
            return 0;
        }

        virtual bool IsEnabled() const
        {
            return true;
        }
    };
}
```

如果当前 System 基类名称不是 `System`，应用到实际基类中。

---

## 6.2 各 System 阶段定义

### PhysicsSystem

```cpp
ESystemPhase GetPhase() const override
{
    return ESystemPhase::kPhysics;
}
```

### TransformSystem

```cpp
ESystemPhase GetPhase() const override
{
    return ESystemPhase::kTransform;
}
```

### AnimationSystem

```cpp
ESystemPhase GetPhase() const override
{
    return ESystemPhase::kAnimation;
}
```

### LightingSystem

如果 LightingSystem 依赖最终世界矩阵，设置为：

```cpp
ESystemPhase GetPhase() const override
{
    return ESystemPhase::kRenderData;
}
```

### 默认 System

未指定阶段的 System 默认放入：

```cpp
ESystemPhase::kGameplay
```

---

# 7. 第三阶段：在 Register 中建立调度缓存

## 7.1 新增 SystemEntry

```cpp
namespace Ailu::ECS
{
    struct SystemEntry
    {
        System *_system = nullptr;
        ESystemPhase _phase = ESystemPhase::kGameplay;
        i32 _order = 0;
    };
}
```

---

## 7.2 Register 新增成员

```cpp
HashMap<ESystemPhase, Vector<SystemEntry>> _system_schedule;
bool _system_schedule_dirty = true;
```

如果项目现有 HashMap 不支持 enum hash，则：

- 为 `ESystemPhase` 增加 hash；
- 或使用定长数组。

更推荐定长数组：

```cpp
constexpr u32 kSystemPhaseCount = static_cast<u32>(ESystemPhase::kRenderData) + 1u;

Array<Vector<SystemEntry>, kSystemPhaseCount> _system_schedule;
```

这样避免 enum 的 unordered_map 开销。

---

## 7.3 RebuildSystemSchedule

```cpp
void Register::RebuildSystemSchedule()
{
    for (auto &entries : _system_schedule)
        entries.clear();

    for (auto &[type, system] : _systems)
    {
        if (system == nullptr)
            continue;

        SystemEntry entry;
        entry._system = system.get();
        entry._phase = system->GetPhase();
        entry._order = system->GetOrder();

        const u32 phase_index = static_cast<u32>(entry._phase);
        _system_schedule[phase_index].emplace_back(entry);
    }

    for (auto &entries : _system_schedule)
    {
        std::ranges::stable_sort(entries, [](const SystemEntry &lhs, const SystemEntry &rhs)
        {
            return lhs._order < rhs._order;
        });
    }

    _system_schedule_dirty = false;
}
```

要求：

- 使用 `stable_sort`。
- 相同 Phase、相同 Order 时保持注册顺序。
- 不允许每帧重新排序。
- 只在 System 注册、删除或配置变化时标记 dirty。

---

## 7.4 ExecutePhase

```cpp
void Register::ExecutePhase(ESystemPhase phase, f32 delta_time)
{
    if (_system_schedule_dirty)
        RebuildSystemSchedule();

    const u32 phase_index = static_cast<u32>(phase);
    auto &entries = _system_schedule[phase_index];

    for (const SystemEntry &entry : entries)
    {
        if (entry._system == nullptr || !entry._system->IsEnabled())
            continue;

        entry._system->Update(*this, delta_time);
    }
}
```

---

# 8. 第四阶段：重构 Scene::Update

## 8.1 新增 Scene 私有函数

```cpp
void BeginUpdate();
void UpdateScripts(f32 delta_time);
void UpdateBounds();
void UpdateCameras();
void UpdateAccelerationStructures();
void UpdateGpuSceneIfNeeded();
void EndUpdate();
```

---

## 8.2 新的 Scene::Update

首版保持现有行为顺序：

```cpp
void Scene::Update(f32 delta_time)
{
    BeginUpdate();
    UpdateScripts(delta_time);

    _register.ExecutePhase(ECS::ESystemPhase::kPrePhysics, delta_time);
    _register.ExecutePhase(ECS::ESystemPhase::kPhysics, delta_time);
    _register.ExecutePhase(ECS::ESystemPhase::kPostPhysics, delta_time);
    _register.ExecutePhase(ECS::ESystemPhase::kTransform, delta_time);
    _register.ExecutePhase(ECS::ESystemPhase::kAnimation, delta_time);
    _register.ExecutePhase(ECS::ESystemPhase::kPostAnimation, delta_time);
    _register.ExecutePhase(ECS::ESystemPhase::kGameplay, delta_time);
    _register.ExecutePhase(ECS::ESystemPhase::kRenderData, delta_time);

    UpdateBounds();
    UpdateCameras();
    UpdateAccelerationStructures();
    UpdateGpuSceneIfNeeded();
    EndUpdate();
}
```

---

## 8.3 BeginUpdate

```cpp
void Scene::BeginUpdate()
{
    ProcessSceneCommands();
}
```

Scene Command 必须在系统更新之前处理。

原因：

- Reparent 会修改 Hierarchy。
- TransformSystem 本帧需要看到最新父子关系。
- 编辑器 Undo/Redo 不应延迟到渲染之后。

---

## 8.4 UpdateScripts

```cpp
void Scene::UpdateScripts(f32 delta_time)
{
    auto &reg = _register;
    u32 index = 0;

    for (auto &component : reg.View<ECS::ScriptComponent>())
    {
        const ECS::Entity entity = reg.GetEntity<ECS::ScriptComponent>(index++);
        ScriptSystem::Get().UpdateComponent(this, entity, component, delta_time);
    }
}
```

脚本必须位于 Physics 和 Transform 之前，以便脚本本帧修改 Transform。

---

## 8.5 UpdateBounds

将当前 StaticMesh 和 SkeletonMesh 的 `_transformed_aabbs` 更新逻辑移动到此函数。

首版只移动代码，不改变算法。

后续再单独转为 `BoundsSystem`。

---

## 8.6 UpdateCameras

将当前：

```text
Transform world matrix
    ->
Camera Position
Camera Rotation
Camera RecalculateMatrix
```

移动到此函数。

Camera 同步必须发生在 TransformSystem 之后。

---

## 8.7 UpdateAccelerationStructures

首版包含：

```cpp
RebuildBVHTree();
```

但增加 TODO：

```text
当前每帧重建 BVH，后续必须改为 Dirty 驱动。
```

---

## 8.8 UpdateGpuSceneIfNeeded

```cpp
void Scene::UpdateGpuSceneIfNeeded()
{
    if (!_dirty)
        return;

    UpdateGpuScene();
    _dirty = false;
}
```

后续将 `_dirty` 替换为更细粒度的 DirtyFlags。

---

## 8.9 EndUpdate

```cpp
void Scene::EndUpdate()
{
    DeletePendingEntities();
}
```

禁止在 System 遍历组件池期间立即销毁实体。

---

# 9. 删除具体 System 类型特判

完成调度缓存后，删除 `Scene::Update()` 中所有类似逻辑：

```cpp
for (auto &it : _register.SystemView())
{
    auto &[type, system] = it;

    if (type != ECS::PhysicsSystem::TypeName())
        continue;

    system->Update(_register, delta_time);
}
```

删除：

```cpp
if (auto *transform_system = _register.GetSystem<ECS::TransformSystem>())
    transform_system->Update(_register, delta_time);
```

删除：

```cpp
if (type == ECS::TransformSystem::TypeName() || type == ECS::PhysicsSystem::TypeName())
    continue;
```

统一替换为：

```cpp
_register.ExecutePhase(ECS::ESystemPhase::kPhysics, delta_time);
_register.ExecutePhase(ECS::ESystemPhase::kTransform, delta_time);
```

验收标准：

- `Scene::Update()` 不再引用任何具体 ECS System 类型。
- 新增 System 不需要修改 `Scene::Update()`。
- 调整阶段和 order 不需要修改主循环。

---

# 10. 第五阶段：拆分 ScriptSystem 职责

当前存在：

```cpp
ScriptSystem::Get().UpdateComponent(...);
ScriptSystem::Get().Tick(delta_time);
```

需要明确两者职责。

建议改为：

```cpp
void ScriptSystem::BeginFrame(f32 delta_time);
void ScriptSystem::UpdateScene(Scene *scene, f32 delta_time);
void ScriptSystem::EndFrame();
```

或者更具体：

```cpp
void ScriptSystem::ProcessReloadRequests();
void ScriptSystem::UpdateComponent(Scene *scene, ECS::Entity entity, ScriptComponent &component, f32 delta_time);
void ScriptSystem::CollectGarbage();
```

推荐职责：

## BeginFrame

处理：

```text
Lua VM 准备
脚本热重载
待绑定类型更新
异步加载完成
```

## Scene Update

处理：

```text
ScriptComponent OnUpdate
```

## EndFrame

处理：

```text
延迟销毁
Lua 引用清理
可选 GC
```

禁止继续保留语义不明确的全局 `Tick()`。

---

# 11. 第六阶段：接入 FixedUpdate

此阶段必须在普通阶段驱动稳定后再实施。

## 11.1 Application 新增成员

```cpp
f32 _fixed_accumulator = 0.0f;
```

常量：

```cpp
constexpr f32 kFixedDeltaTime = 1.0f / 60.0f;
constexpr u32 kMaxFixedStepsPerFrame = 4u;
```

---

## 11.2 更新流程

```cpp
void Application::UpdateScenes(f32 frame_delta_time)
{
    _fixed_accumulator += frame_delta_time;

    u32 fixed_step_count = 0u;
    while (_fixed_accumulator >= kFixedDeltaTime && fixed_step_count < kMaxFixedStepsPerFrame)
    {
        SceneManagement::SceneMgr::Get().FixedUpdate(kFixedDeltaTime);
        _fixed_accumulator -= kFixedDeltaTime;
        ++fixed_step_count;
    }

    if (fixed_step_count == kMaxFixedStepsPerFrame && _fixed_accumulator >= kFixedDeltaTime)
        _fixed_accumulator = std::fmod(_fixed_accumulator, kFixedDeltaTime);

    SceneManagement::SceneMgr::Get().Update(frame_delta_time);

    const f32 render_alpha = _fixed_accumulator / kFixedDeltaTime;
    SceneManagement::SceneMgr::Get().LateUpdate(frame_delta_time, render_alpha);
}
```

---

## 11.3 Scene 生命周期

增加：

```cpp
void Scene::FixedUpdate(f32 fixed_delta_time);
void Scene::Update(f32 delta_time);
void Scene::LateUpdate(f32 delta_time, f32 render_alpha);
```

建议首版划分：

### FixedUpdate

```text
Script FixedUpdate
PrePhysics
Physics
PostPhysics
```

### Update

```text
Script Update
Gameplay
AI
Animation
```

### LateUpdate

```text
Transform
PostAnimation
Bounds
Camera
Lighting
RenderData
```

---

## 11.4 Physics 与 Transform 数据流

PhysicsSystem 不直接调用 TransformSystem。

推荐数据流：

```text
Script / Gameplay
    修改 TransformComponent::_local_transform
    设置 dirty

PrePhysics
    将 kinematic transform 写入物理世界

Physics
    模拟刚体

PostPhysics
    将 dynamic rigid body 结果写回 local transform
    设置 dirty

Transform
    根据 local transform 和 hierarchy 更新 world matrix
```

---

## 11.5 防止死亡螺旋

必须限制：

```cpp
kMaxFixedStepsPerFrame
```

否则严重卡顿时，固定更新会持续补帧，导致主线程无法恢复。

推荐默认值：

```cpp
constexpr u32 kMaxFixedStepsPerFrame = 4u;
```

---

# 12. 第七阶段：Lua 生命周期函数

为 Lua 脚本增加：

```lua
function OnFixedUpdate(fixed_delta_time)
end

function OnUpdate(delta_time)
end

function OnLateUpdate(delta_time)
end
```

调用阶段：

```text
OnFixedUpdate
    FixedUpdate 阶段

OnUpdate
    普通 Update 阶段

OnLateUpdate
    LateUpdate 阶段
```

要求：

- 缺少某个函数时不报错。
- 生命周期函数句柄应缓存，不要每帧按字符串查找。
- 脚本禁用或实体无效时跳过。
- 脚本销毁延迟到安全阶段。

---

# 13. RenderData 系统化

完成基础阶段驱动后，将 Scene 中的后处理逻辑逐步迁为 System。

建议增加：

```text
BoundsSystem
CameraSystem
LightingSystem
RenderSceneSystem
```

推荐顺序：

```cpp
BoundsSystem      -> kRenderData, order 0
CameraSystem      -> kRenderData, order 100
LightingSystem    -> kRenderData, order 200
RenderSceneSystem -> kRenderData, order 300
```

首版不强制完成这一部分。

---

# 14. DirtyFlags 改造

当前 Scene 使用单一 `_dirty`，无法区分更新类型。

后续增加：

```cpp
enum class ESceneDirtyFlags : u8
{
    kNone = 0,
    kTransform = 1u << 0u,
    kBounds = 1u << 1u,
    kRenderable = 1u << 2u,
    kAccelerationStructure = 1u << 3u,
    kGpuScene = 1u << 4u,
};
```

用途：

```text
Transform dirty
    触发 world matrix 更新

Bounds dirty
    触发 AABB 更新

AccelerationStructure dirty
    触发 BVH / TLAS 更新

GpuScene dirty
    触发 GPU Scene 上传
```

`RebuildBVHTree()` 不应继续每帧执行。

---

# 15. System Order 使用规则

同一 Phase 内通过 `GetOrder()` 排序。

推荐规则：

```text
负数    提前执行
0       默认
正数    延后执行
```

示例：

```cpp
i32 GetOrder() const override
{
    return 100;
}
```

禁止大量使用无意义的连续数字：

```text
1
2
3
4
```

建议预留间隔：

```text
-200
-100
0
100
200
```

便于后续插入。

---

# 16. System 注册规则

System 注册后：

```cpp
_system_schedule_dirty = true;
```

System 删除后：

```cpp
_system_schedule_dirty = true;
```

修改阶段或 order 后：

```cpp
_system_schedule_dirty = true;
```

当前阶段不要求运行时频繁修改阶段。

---

# 17. Profiler 接入

每个阶段增加 CPU Profile：

```cpp
CPUProfileBlock profile_block("Scene::Physics");
```

或使用现有宏。

建议 Profile 名称：

```text
Application::BeginFrame
Application::Input
Application::Resources
Application::Layers
Application::Scenes
Application::PreRender
Application::Render
Application::EditorRender
Application::Present
Application::EndFrame

Scene::ScriptUpdate
Scene::PrePhysics
Scene::Physics
Scene::PostPhysics
Scene::Transform
Scene::Animation
Scene::Gameplay
Scene::RenderData
Scene::Bounds
Scene::Camera
Scene::AccelerationStructure
Scene::GpuScene
```

不要在每个实体级别创建 ProfileBlock。

---

# 18. 文件建议

建议新增：

```text
Engine/Inc/Scene/SystemPhase.h
Engine/Inc/Scene/SystemScheduler.h
Engine/Src/Scene/SystemScheduler.cpp
```

如果调度逻辑直接属于 Register，也可以只增加：

```text
Engine/Inc/Scene/SystemPhase.h
```

并将实现放入现有 Register 文件。

推荐职责：

## SystemPhase.h

包含：

```cpp
ESystemPhase
kSystemPhaseCount
SystemEntry
```

## Register

包含：

```cpp
RebuildSystemSchedule
ExecutePhase
MarkSystemScheduleDirty
```

不要新增功能重复的独立全局 Scheduler 单例。

System Scheduler 应属于每个 Scene 的 ECS Register。

---

# 19. 实施顺序

## Commit 1：拆分 Application::LogicLoop

只拆函数，不改变行为。

验收：

- 帧执行结果不变。
- Profiler 顺序不变。
- Editor 和 Player 正常运行。

---

## Commit 2：拆分 Scene::Update

新增：

```text
BeginUpdate
UpdateScripts
UpdateBounds
UpdateCameras
UpdateAccelerationStructures
UpdateGpuSceneIfNeeded
EndUpdate
```

验收：

- 场景行为不变。
- Transform、动画、Camera 和渲染结果不变。

---

## Commit 3：增加 ESystemPhase

为 System 增加：

```text
GetPhase
GetOrder
IsEnabled
```

暂时不替换旧调用。

验收：

- 所有 System 正常编译。
- 默认 System 进入 Gameplay。

---

## Commit 4：增加 Register 调度缓存

实现：

```text
RebuildSystemSchedule
ExecutePhase
```

验收：

- System 顺序稳定。
- 相同 order 保持注册顺序。
- 不发生每帧重建调度表。

---

## Commit 5：移除 System 类型特判

删除 Physics 和 Transform 的特殊判断。

统一调用：

```cpp
ExecutePhase(...)
```

验收：

- `Scene::Update()` 不引用具体 System 类型。
- Physics 在 Transform 前执行。
- Animation 顺序正确。

---

## Commit 6：拆分 ScriptSystem Tick

明确：

```text
BeginFrame
Scene Update
EndFrame
```

验收：

- Lua 热重载正常。
- 脚本只更新一次。
- 脚本销毁正常。

---

## Commit 7：引入 FixedUpdate

增加 accumulator 和最大补帧限制。

验收：

- 30 FPS 下每帧约执行两次 Physics。
- 120 FPS 下部分帧不执行 Physics。
- 严重卡顿时不会无限补帧。
- Physics delta time 恒定。

---

## Commit 8：增加 Lua FixedUpdate / LateUpdate

验收：

- 生命周期函数顺序正确。
- 缺失函数不报错。
- 函数查找结果被缓存。

---

## Commit 9：RenderData 和 DirtyFlags

增加：

```text
BoundsSystem
CameraSystem
SceneDirtyFlags
BVH Dirty Update
```

验收：

- 静止场景不再每帧重建 BVH。
- Camera 始终读取最终 Transform。
- GPU Scene 仅在必要时上传。

---

# 20. 测试要求

## 20.1 System 顺序测试

注册多个测试 System：

```text
Physics order 0
Physics order 100
Transform order 0
Gameplay order -100
Gameplay order 0
```

验证执行顺序：

```text
Physics 0
Physics 100
Transform 0
Gameplay -100
Gameplay 0
```

---

## 20.2 Schedule Dirty 测试

验证：

- 注册 System 后调度表重建一次。
- 连续多帧不会重复重建。
- 删除 System 后重新构建。
- 修改 order 后重新构建。

---

## 20.3 Scene 安全删除测试

在脚本和 System 更新期间请求删除实体。

验证：

- 当前遍历不失效。
- 实体在 EndUpdate 删除。
- 子实体递归删除正常。
- ScriptComponent 正确销毁。

---

## 20.4 Transform 顺序测试

测试：

```text
Script 修改 local transform
Physics 写回 rigid body transform
TransformSystem 生成 world matrix
Camera 读取 world matrix
```

Camera 本帧必须读取最终位置。

---

## 20.5 FixedUpdate 测试

模拟：

```text
16.6 ms
33.3 ms
8.3 ms
100 ms
500 ms
```

验证：

- accumulator 正确。
- 最大补帧次数生效。
- 不出现死亡螺旋。
- `render_alpha` 范围为 `[0, 1)`。

---

# 21. 明确禁止

禁止将所有 Component 改成虚函数生命周期：

```cpp
component->Update();
```

禁止每帧重建 System 调度表。

禁止使用 System 类型名决定执行顺序。

禁止让 PhysicsSystem 直接调用 TransformSystem。

禁止让任意 System 在遍历期间直接销毁实体。

禁止首个改造提交同时修改时间步和 Physics 行为。

禁止使用全局单例 System Scheduler 管理所有 Scene。

禁止继续让 `Application::LogicLoop()` 和 `Scene::Update()` 承担所有模块逻辑。

---

# 22. 最终目标流程

```text
Application::BeginFrame
    Time
    Profiler
    Input

Application::ProcessEvents
    Window
    Layer
    UI

Application::UpdateResources
    Resource
    Script Reload

Application::FixedUpdateScenes [0..N]
    Script FixedUpdate
    PrePhysics
    Physics
    PostPhysics

Application::UpdateScenes [1]
    Script Update
    Gameplay
    AI
    Animation

Application::LateUpdateScenes [1]
    Transform
    PostAnimation
    Bounds
    Camera
    Lighting
    RenderData

Application::PreRender
    GPU Upload
    RenderGraph Prepare

Application::Render
    RenderPipeline

Application::EditorRender
    ImGui

Application::Present
    Present
    FrameCleanup

Application::EndFrame
    Deferred Destroy
    Profiler End
```

---

# 23. 完成标准

改造完成后应满足：

- `Scene::Update()` 不知道具体 System 类型。
- System 通过 `GetPhase()` 和 `GetOrder()` 声明执行顺序。
- Register 维护稳定的阶段调度缓存。
- Physics 使用固定时间步。
- Transform、Camera 和 RenderData 顺序明确。
- Script 生命周期包含 FixedUpdate、Update 和 LateUpdate。
- 实体和资源销毁发生在安全阶段。
- 静止场景不再无条件重建 BVH。
- 新增 System 不需要修改 Application 或 Scene 主循环。
- 后续可以按 Phase 构建 JobSystem 依赖和并行调度。