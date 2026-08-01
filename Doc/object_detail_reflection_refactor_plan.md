# AiluEngine Object Detail 反射化重构实现方案

## 1. 文档目标

重构 Editor 的 `ObjectDetail`，解决组件绘制代码集中在单个文件、组件类型硬编码、重复 UI 构造和维护成本持续增长的问题。

本次重构必须复用 AiluEngine 已有的反射与 UI 属性构建能力：

- `Type::GetProperties()`；
- `PropertyInfo::Get<T>()`；
- `PropertyInfo::Set<T>()`；
- `PropertyInfo::AddObserver()`；
- `PropertyInfo::EPropertyChangeSource::kUI`；
- `UI::CompositeBuilder::BuildPropertyElement()`；
- `UIElement::AddPropertyObserver()` 管理 Observer 生命周期。

**禁止再实现一套 Property Binding、数据镜像或独立的属性通知系统。**

目标不是一次实现 Unreal Details Panel 那样的大型框架，而是在现有系统上增加少量缺失的组织层，让普通组件自动绘制，复杂组件允许局部定制。

---

## 2. 当前代码现状

### 2.1 Object Detail

相关文件：

```text
Editor/Inc/Widgets/ObjectDetail.h
Editor/Src/Widgets/ObjectDetail.cpp
```

当前 `ObjectDetail.cpp` 主要问题：

1. `ObjectDetail::Update()` 直接识别并绘制每种 ECS 组件；
2. `ObjectDetail.h` 为每种组件保存单独的 `_xxx_block` 成员；
3. 新增组件需要同时修改：
   - 组件菜单；
   - `ObjectDetail.h`；
   - `ObjectDetail::Update()`；
   - 面板构建和清理逻辑；
4. 普通字段 UI、资源选择、特殊行为和组件面板生命周期混在一起；
5. 组件数量增加后，`ObjectDetail` 会继续线性膨胀。

当前已经存在的组件相关块包括：

```text
_transform_block
_light_block
_static_mesh_block
_light_probe_block
_cam_block
_sprite_block
_script_block
```

### 2.2 现有反射系统

相关文件：

```text
Engine/Inc/Objects/Type.h
Engine/Src/Objects/Type.cpp
```

`PropertyInfo` 已支持：

```cpp
property->Get<T>(instance);
property->Set<T>(instance, value, PropertyInfo::EPropertyChangeSource::kUI);
property->AddObserver(instance, callback);
```

因此属性编辑器不应直接写字段地址，也不应建立另一套 getter/setter 绑定。

### 2.3 现有 UI 属性构建器

相关文件：

```text
Engine/Inc/UI/Composite.h
Engine/Src/UI/Composite.cpp
```

现有 `UI::CompositeBuilder` 已经：

- 用 `HashMap<const Type *, Builder>` 注册字段控件；
- 支持 `Vector2f`、`Vector3f`、`Vector4f`、整数向量、`f32`、`i32`、`bool` 等；
- 使用 `PropertyInfo::Set()` 写回；
- 使用 `PropertyInfo::AddObserver()` 更新 UI；
- 使用 `UIElement::AddPropertyObserver()` 托管 Observer 生命周期；
- 对浮点数支持 `FloatFieldParams` 范围参数。

本次实现应把 `CompositeBuilder` 升级为通用的反射属性绘制基础，而不是另建 `PropertyInspectorRegistry`。

---

## 3. 最终架构

```text
ObjectDetail
├── 处理当前选中 Entity
├── 构建对象头部
├── 枚举 Entity 当前拥有的组件
├── 根据 ComponentEditorRegistry 创建组件面板
└── 处理 Add Component 菜单

ComponentEditorRegistry
├── 保存可编辑组件的描述信息
├── 关联 ECS ComponentTypeId 与反射 Type
├── 提供组件存在性、实例获取、添加和删除操作
├── 为普通组件使用默认反射绘制
└── 为复杂组件提供可选的自定义编辑器

ReflectedPropertyPanel
├── 枚举 Type::GetProperties()
├── 读取属性元数据
├── 按 Category 分组
├── 调用 UI::CompositeBuilder::BuildPropertyElement()
└── 不实现额外数据绑定

IComponentEditor（可选）
├── 只处理无法由普通反射属性表达的组件行为
├── 可以混合调用 ReflectedPropertyPanel
└── 不负责通用组件生命周期
```

核心边界：

```text
ObjectDetail              面板编排
ComponentEditorRegistry   组件类型注册与类型擦除
ReflectedPropertyPanel    反射属性枚举、分组和 UI 构造
CompositeBuilder          单个 PropertyInfo 对应的控件
IComponentEditor          少量特殊组件定制
```

---

## 4. 设计原则

### 4.1 普通组件零定制

只要组件具备反射信息，并且属性类型被 `CompositeBuilder` 支持，就应自动显示。

新增普通组件时，不允许再修改 `ObjectDetail.cpp`。

### 4.2 特殊组件只覆盖特殊部分

以下组件可能仍需要自定义编辑器：

- `TransformComponent`：Quaternion 与 Euler 转换、Transform dirty/version；
- `ScriptComponent`：路径变化后重置运行时；
- `LightComponent`：根据灯光类型动态显示不同字段；
- `StaticMeshComponent`：Material slots、Shader properties 等动态列表；
- Camera 组件：字段修改必须经过 Camera setter 时；
- `SpriteRendererComponent`：资源选择器和渲染状态刷新。

自定义编辑器不得重复实现普通 `bool/f32/vector/enum` 字段控件，应尽可能调用反射面板绘制普通部分。

### 4.3 不缓存 ECS 组件裸指针

组件地址可能因 ECS pool 扩容、迁移或删除而失效。

组件编辑器和回调应保存：

```text
Scene 标识或可安全访问的 Scene 指针
Entity
ComponentTypeId
```

需要访问组件时，通过 Registry 重新获取当前实例地址。

如果当前 ECS 明确保证组件地址稳定，也不要让新的架构依赖这一隐含条件。

### 4.4 组件结构变化触发重建，属性变化由 Observer 同步

- 添加、删除组件：重建 Object Detail 组件列表；
- Light 类型等导致字段集合变化：只重建对应组件块，或第一版重建整个 Object Detail；
- 普通属性值变化：依赖 `PropertyInfo::AddObserver()` 更新控件，不需要每帧轮询。

---

## 5. 新增模块与文件

建议新增目录：

```text
Editor/Inc/Inspector/
Editor/Src/Inspector/
```

第一阶段新增文件：

```text
Editor/Inc/Inspector/ComponentEditorRegistry.h
Editor/Src/Inspector/ComponentEditorRegistry.cpp

Editor/Inc/Inspector/ReflectedPropertyPanel.h
Editor/Src/Inspector/ReflectedPropertyPanel.cpp

Editor/Inc/Inspector/IComponentEditor.h

Editor/Inc/Inspector/ComponentEditors/TransformComponentEditor.h
Editor/Src/Inspector/ComponentEditors/TransformComponentEditor.cpp

Editor/Inc/Inspector/ComponentEditors/LightComponentEditor.h
Editor/Src/Inspector/ComponentEditors/LightComponentEditor.cpp

Editor/Inc/Inspector/ComponentEditors/ScriptComponentEditor.h
Editor/Src/Inspector/ComponentEditors/ScriptComponentEditor.cpp

Editor/Inc/Inspector/ComponentEditors/StaticMeshComponentEditor.h
Editor/Src/Inspector/ComponentEditors/StaticMeshComponentEditor.cpp
```

如果项目倾向更少文件，也可先将三个核心类放入：

```text
Editor/Inc/Inspector/ComponentInspector.h
Editor/Src/Inspector/ComponentInspector.cpp
```

但不要继续放进 `ObjectDetail.cpp`。

---

## 6. ComponentEditorRegistry

### 6.1 组件描述

```cpp
namespace Ailu::Editor
{
    struct ComponentEditorContext;
    class IComponentEditor;

    struct ComponentEditorInfo
    {
        ECS::ComponentTypeId _component_type = 0u;
        const Type *_reflection_type = nullptr;
        String _display_name;
        String _category;
        i32 _order = 0;
        bool _allow_add = true;
        bool _allow_remove = true;

        std::function<bool(ECS::Register &, ECS::Entity)> _has_component;
        std::function<void *(ECS::Register &, ECS::Entity)> _get_component;
        std::function<void(ECS::Register &, ECS::Entity)> _add_component;
        std::function<void(ECS::Register &, ECS::Entity)> _remove_component;
        std::function<Scope<IComponentEditor>()> _create_editor;
    };
}
```

说明：

- `_reflection_type` 必须复用现有 `Type`；
- `_create_editor` 为空表示使用默认反射编辑器；
- Add Component 菜单与已有组件枚举都从同一份信息生成；
- 不再保留单独的 `GetComponentMenuItems()` 硬编码列表。

### 6.2 Registry 接口

```cpp
namespace Ailu::Editor
{
    class ComponentEditorRegistry final : public NonCopyable
    {
    public:
        static ComponentEditorRegistry &Get();

        template<typename TComponent>
        void Register(String display_name,
                      String category = "General",
                      i32 order = 0,
                      bool allow_add = true,
                      bool allow_remove = true);

        template<typename TComponent, typename TEditor>
        void RegisterCustom(String display_name,
                            String category = "General",
                            i32 order = 0,
                            bool allow_add = true,
                            bool allow_remove = true);

        const Vector<ComponentEditorInfo> &Components() const;
        const ComponentEditorInfo *Find(ECS::ComponentTypeId component_type) const;

    private:
        Vector<ComponentEditorInfo> _components;
        HashMap<ECS::ComponentTypeId, usize> _component_indices;
    };
}
```

### 6.3 模板注册实现要求

模板中生成类型擦除函数：

```cpp
template<typename TComponent>
void ComponentEditorRegistry::Register(String display_name,
                                       String category,
                                       i32 order,
                                       bool allow_add,
                                       bool allow_remove)
{
    ComponentEditorInfo info;
    info._component_type = ECS::ComponentType<TComponent>();
    info._reflection_type = StaticClass<TComponent>();
    info._display_name = std::move(display_name);
    info._category = std::move(category);
    info._order = order;
    info._allow_add = allow_add;
    info._allow_remove = allow_remove;

    info._has_component = [](ECS::Register &registry, ECS::Entity entity)
    {
        return registry.GetComponent<TComponent>(entity) != nullptr;
    };

    info._get_component = [](ECS::Register &registry, ECS::Entity entity) -> void *
    {
        return registry.GetComponent<TComponent>(entity);
    };

    info._add_component = [](ECS::Register &registry, ECS::Entity entity)
    {
        registry.AddComponent<TComponent>(entity);
    };

    info._remove_component = [](ECS::Register &registry, ECS::Entity entity)
    {
        registry.RemoveComponent<TComponent>(entity);
    };

    RegisterInternal(std::move(info));
}
```

根据工程中实际的 ECS API 调整 `ComponentType<T>()`、`AddComponent()` 和 `RemoveComponent()` 名称。

若删除组件必须使用 `CommandManager` 和 `RemoveComponentCommand<T>` 才能支持 Undo，则 `_remove_component` 不应直接调用 Registry，而应注册一个使用现有 Command 系统的函数。

---

## 7. ComponentEditorContext

```cpp
namespace Ailu::Editor
{
    struct ComponentEditorContext
    {
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;
        const ComponentEditorInfo *_component_info = nullptr;
        UI::VerticalBox *_content = nullptr;
        std::function<void()> _request_rebuild;

        ECS::Register &Registry() const;
        void *ComponentInstance() const;

        template<typename TComponent>
        TComponent *GetComponent() const
        {
            return Registry().GetComponent<TComponent>(_entity);
        }

        void MarkSceneDirty() const;
        void RequestRebuild() const;
    };
}
```

要求：

- `ComponentInstance()` 每次调用 `_component_info->_get_component`；
- 不在 Context 或 ComponentEditor 中永久保存组件实例地址；
- `_request_rebuild` 可先设置 `ObjectDetail::_needs_rebuild = true`；
- `MarkSceneDirty()` 封装当前已有的 `SceneMgr::Get().MarkCurSceneDirty()` 或 Scene 对应接口。

---

## 8. ReflectedPropertyPanel

### 8.1 职责

`ReflectedPropertyPanel` 只负责：

1. 遍历反射类型及其父类型属性；
2. 过滤隐藏属性；
3. 按 Category 和 Order 排序；
4. 根据元数据构造 `CompositeBuilder::Params`；
5. 调用 `UI::CompositeBuilder::BuildPropertyElement()`；
6. 把返回的控件加入父 UI；
7. 允许调用方过滤部分属性。

它不实现字段读取、写回和 Observer，这些已经由 `CompositeBuilder` 完成。

### 8.2 接口

```cpp
namespace Ailu::Editor
{
    class ReflectedPropertyPanel final : public NonCopyable
    {
    public:
        using PropertyFilter = std::function<bool(const PropertyInfo &)>;
        using PropertyChanged = std::function<void(const PropertyInfo &)>;

        struct BuildArgs
        {
            const Type *_type = nullptr;
            void *_instance = nullptr;
            UI::VerticalBox *_parent = nullptr;
            PropertyFilter _filter;
            PropertyChanged _on_property_changed;
        };

        void Build(const BuildArgs &args);
        void Clear();

    private:
        Ref<UI::CompositeBuilder::Params> BuildParams(const PropertyInfo &property) const;
        String ResolveDisplayName(const PropertyInfo &property) const;
        String ResolveCategory(const PropertyInfo &property) const;
    };
}
```

`Ref<CompositeBuilder::Params>` 只是示意；若现有 `Ref` 不适合多态参数，使用 `Scope<CompositeBuilder::Params>`。

### 8.3 属性枚举

```cpp
void ReflectedPropertyPanel::Build(const BuildArgs &args)
{
    Clear();

    if (args._type == nullptr || args._instance == nullptr || args._parent == nullptr)
        return;

    Vector<const PropertyInfo *> properties;

    for (const Type *type = args._type; type != nullptr; type = type->BaseType())
    {
        for (const PropertyInfo &property : type->GetProperties())
        {
            if (args._filter && !args._filter(property))
                continue;

            if (ReadMetaBool(property, "Hidden", false))
                continue;

            properties.emplace_back(&property);
        }
    }

    SortProperties(properties);
    BuildCategories(args, properties);
}
```

根据当前 `Type` 的父类型接口实际名称调整 `BaseType()`。

### 8.4 元数据

第一版仅使用现有或容易补充的元数据：

```text
DisplayName
Category
Order
Hidden
ReadOnly
IsRange
RangeMin
RangeMax
Step
IsColor
```

不要在第一版加入复杂表达式系统，例如 `VisibleWhen="Type == Spot"`。

动态字段由自定义组件编辑器处理。

### 8.5 Composite 参数转换

现有 `CompositeBuilder` 的 `f32/i32` 支持 `FloatFieldParams`：

```cpp
Scope<UI::CompositeBuilder::Params> ReflectedPropertyPanel::BuildParams(const PropertyInfo &property) const
{
    const bool is_range = ReadMetaBool(property, "IsRange", false);
    if (!is_range)
        return nullptr;

    auto params = MakeScope<UI::FloatFieldParams>();
    params->_range.x = ReadMetaFloat(property, "RangeMin", 0.0f);
    params->_range.y = ReadMetaFloat(property, "RangeMax", 1.0f);
    params->_step = ReadMetaFloat(property, "Step", 0.01f);
    return params;
}
```

需要检查当前 `Meta` API 的真实读取方法，不要创建另一份 metadata 容器。

### 8.6 Scene Dirty 与特殊回调

目前 `CompositeBuilder` 调用 `PropertyInfo::Set()` 后不会自动标记 Scene Dirty。

优先选择以下方案之一：

#### 推荐方案：给 CompositeBuilder 增加通用回调参数

扩展：

```cpp
class CompositeBuilder
{
public:
    struct Params
    {
        virtual ~Params() = default;
        std::function<void(PropertyInfo *)> _on_value_changed;
    };
};
```

在所有 builder 调用 `property->Set()` 后统一调用：

```cpp
if (params != nullptr && params->_on_value_changed)
    params->_on_value_changed(property);
```

为减少每个 builder 重复，增加内部函数：

```cpp
template<typename TValue>
void SetPropertyValue(PropertyInfo *property,
                      void *instance,
                      const TValue &value,
                      CompositeBuilder::Params *params)
{
    property->Set<TValue>(instance, value, PropertyInfo::EPropertyChangeSource::kUI);

    if (params != nullptr && params->_on_value_changed)
        params->_on_value_changed(property);
}
```

然后替换 `Composite.cpp` 内部直接的 `property->Set()`。

`ReflectedPropertyPanel` 构造 Params 时设置：

```cpp
params->_on_value_changed = [callback = args._on_property_changed](PropertyInfo *property)
{
    if (callback)
        callback(*property);
};
```

Object Detail 默认回调：

```cpp
context.MarkSceneDirty();
```

特殊组件可追加：

```cpp
if (property.Name() == "_script_path")
    component->ResetRuntime();
```

#### 不推荐方案

不要让 `ReflectedPropertyPanel` 为每个控件再建立一层 UI 回调来猜测属性何时变化，这会重复 `CompositeBuilder` 的职责。

---

## 9. 扩展 CompositeBuilder

### 9.1 必须保留现有架构

继续使用：

```cpp
HashMap<const Type *, Builder> s_builders;
```

不新增第二个属性 Drawer Registry。

### 9.2 增加公开注册接口

当前 `s_builders` 只能在 `CompositeBuilder::InitBuilders()` 内维护。应增加：

```cpp
class CompositeBuilder
{
public:
    using Builder = std::function<Ref<UIElement>(const String &, PropertyInfo *, void *, Params *)>;

    static void RegisterBuilder(const Type *type, Builder builder);

    template<typename TValue>
    static void RegisterBuilder(Builder builder)
    {
        RegisterBuilder(StaticClass<TValue>(), std::move(builder));
    }
};
```

用途：

- Editor 模块注册资源引用字段控件；
- 游戏模块注册自定义类型；
- 避免继续修改 Engine 的 `InitBuilders()` 巨型函数。

注册重复类型时记录警告，并采用明确策略：覆盖或拒绝。建议允许覆盖，以支持 Editor 替换 Runtime 默认控件。

### 9.3 增加枚举控件

如果反射 `Type` 可获取枚举项，`BuildPropertyElement()` 在查不到精确 builder 时应检测：

```cpp
if (property->GetType()->IsEnum())
    return BuildEnumField(label, property, instance, params);
```

枚举写回必须通过 `PropertyInfo::Set()`。

如果当前 `PropertyInfo::Set<T>()` 无法以类型擦除方式写入未知底层类型，可先针对引擎当前枚举底层类型实现，或给 `PropertyInfo` 增加安全的枚举设置接口。

### 9.4 增加 String 控件

注册：

```cpp
s_builders[StaticClass<String>()] = ...;
```

必须使用 `PropertyInfo::Get<String>()/Set<String>()` 和 Observer。

### 9.5 增加颜色控件

如果颜色字段底层也是 `Vector4f`，不能只按 `Type*` 判断普通向量还是颜色。

有两种实现：

1. 为 `Color` 使用独立反射 Type；
2. 在 `Params` 中增加 `_is_color`，由元数据 `IsColor` 驱动。

优先复用现有 Color Picker 控件，禁止在 Object Detail 中复制颜色弹窗逻辑。

### 9.6 增加资源引用控件

资源引用字段通过 Editor 模块调用 `CompositeBuilder::RegisterBuilder()` 注册。

例如：

```text
Ref<Texture2D>
Ref<Material>
Ref<Sprite>
Ref<Mesh>
Guid + AssetType 元数据
```

如果当前反射无法正确获得 `Ref<T>` 的 `Type*`，可使用属性元数据指定：

```text
Editor="Asset"
AssetType="Material"
```

但优先使用现有模板类型信息 `_template_info`，不要用字符串解析 C++ 类型名作为主要方案。

---

## 10. 默认组件编辑器

不需要为每个普通组件创建类。

`ObjectDetail` 在 Registry 中发现 `_create_editor == nullptr` 时：

1. 创建统一组件折叠块；
2. 获取当前组件实例；
3. 构建 `ReflectedPropertyPanel`；
4. 属性修改时标记 Scene Dirty。

建议内部条目：

```cpp
struct ObjectDetailComponentEntry
{
    const ComponentEditorInfo *_component_info = nullptr;
    UI::CollapsibleView *_block = nullptr;
    Scope<IComponentEditor> _custom_editor;
    Scope<ReflectedPropertyPanel> _reflected_panel;
};
```

---

## 11. IComponentEditor

### 11.1 接口

```cpp
namespace Ailu::Editor
{
    class IComponentEditor : public NonCopyable
    {
    public:
        virtual ~IComponentEditor() = default;

        virtual void Build(ComponentEditorContext &context) = 0;

        virtual void Refresh(ComponentEditorContext &context)
        {
        }

        virtual bool NeedsRebuild(const ComponentEditorContext &context) const
        {
            return false;
        }
    };
}
```

`Refresh()` 不用于普通反射属性同步。普通属性由 Observer 更新。

它只用于非反射 UI，例如：

- Material slot 数量改变；
- Script 状态文本；
- Mesh preview；
- 动态警告信息。

### 11.2 混合反射绘制

自定义组件编辑器内部可以拥有：

```cpp
ReflectedPropertyPanel _property_panel;
```

例如过滤掉特殊字段后自动绘制剩余字段：

```cpp
_property_panel.Build({
    ._type = context._component_info->_reflection_type,
    ._instance = context.ComponentInstance(),
    ._parent = context._content,
    ._filter = [](const PropertyInfo &property)
    {
        return property.Name() != "_script_path";
    },
    ._on_property_changed = [&context](const PropertyInfo &)
    {
        context.MarkSceneDirty();
    },
});
```

---

## 12. 特殊组件实施方式

### 12.1 TransformComponentEditor

Transform 不建议直接反射写 `_local_transform` 内部字段，除非现有 Observer 已确保：

- local matrix dirty；
- world matrix dirty；
- transform version 更新；
- motion vector 状态正确。

实现要求：

1. 保留现有 Position、Rotation、Scale 三行 UI；
2. 可继续使用 `CompositeBuilder`，但属性写入必须经过 Transform setter 或现有正确修改入口；
3. Rotation UI 显示 Euler，内部存储 Quaternion；
4. 修复当前 scale 回调使用 `scale_block[1]` 而不是 `scale_block[i]` 的问题；
5. Gizmo 修改 Transform 后，UI 必须同步更新；
6. 若 Transform 字段无法直接由 `PropertyInfo` 表达 setter，Transform 保持完全自定义是可接受的。

### 12.2 LightComponentEditor

实现要求：

1. 普通颜色、强度、阴影参数尽可能走反射；
2. 根据 `_type` 过滤属性或构造不同子区域；
3. `_type` 变化后调用 `context.RequestRebuild()`；
4. 不在回调中捕获长期组件裸指针；
5. Light 特殊 dirty/update 逻辑必须继续执行。

第一版允许 Light 类型变化后重建整个 Object Detail，后续再优化为只重建 Light block。

### 12.3 ScriptComponentEditor

实现要求：

1. Script path 使用现有文件/资源选择 UI；
2. 修改后通过反射 `PropertyInfo::Set()` 或组件正式 setter 写入；
3. 写入后调用现有 `ResetRuntime()`；
4. 标记 Scene Dirty；
5. 其他普通字段走 `ReflectedPropertyPanel`。

### 12.4 StaticMeshComponentEditor

实现要求：

1. Mesh 引用使用注册到 `CompositeBuilder` 的资源字段控件；
2. Material slots 属于动态列表，由自定义编辑器绘制；
3. Shader properties 不应假装为普通 C++ 反射字段；
4. Mesh 或 Material 变化时正确刷新渲染代理；
5. 动态 slot 数量变化时 RequestRebuild。

### 12.5 Camera 和 SpriteRenderer

优先尝试：

- 普通字段反射绘制；
- 资源字段使用资源 Composite builder；
- 特殊 setter/dirty 操作用 Observer 或属性变更回调处理。

如果直接 `PropertyInfo::Set()` 会绕过必要 setter，则保留少量自定义字段，不要为了“全自动”破坏数据一致性。

---

## 13. ObjectDetail 重构

### 13.1 ObjectDetail.h

删除具体组件成员：

```cpp
UI::CollapsibleView *_transform_block;
UI::CollapsibleView *_light_block;
UI::CollapsibleView *_static_mesh_block;
UI::CollapsibleView *_light_probe_block;
UI::CollapsibleView *_cam_block;
UI::CollapsibleView *_sprite_block;
UI::CollapsibleView *_script_block;
UI::InputBlock *_script_path_block;
```

替换为：

```cpp
Vector<ObjectDetailComponentEntry> _component_entries;
ECS::Entity _selected_entity = ECS::kInvalidEntity;
bool _needs_rebuild = true;
```

增加：

```cpp
void Rebuild(ECS::Entity entity);
void ClearComponentEntries();
void BuildComponentEntry(const ComponentEditorInfo &info, ECS::Entity entity);
void RemoveComponent(const ComponentEditorInfo &info, ECS::Entity entity);
```

### 13.2 Update

目标结构：

```cpp
void ObjectDetail::Update(f32 dt)
{
    DockWindow::Update(dt);

    const ECS::Entity selected = ResolveSelectedEntity();
    if (selected == ECS::kInvalidEntity)
    {
        if (_selected_entity != ECS::kInvalidEntity)
        {
            _selected_entity = ECS::kInvalidEntity;
            ClearComponentEntries();
        }
        return;
    }

    if (_selected_entity != selected || _needs_rebuild)
    {
        _selected_entity = selected;
        Rebuild(selected);
        _needs_rebuild = false;
    }

    auto *scene = SceneMgr::Get().ActiveScene();
    if (scene == nullptr)
        return;

    for (ObjectDetailComponentEntry &entry : _component_entries)
    {
        if (entry._custom_editor == nullptr)
            continue;

        ComponentEditorContext context = BuildContext(*entry._component_info, entry._block);
        if (entry._custom_editor->NeedsRebuild(context))
        {
            _needs_rebuild = true;
            break;
        }

        entry._custom_editor->Refresh(context);
    }
}
```

### 13.3 Rebuild

```cpp
void ObjectDetail::Rebuild(ECS::Entity entity)
{
    ClearComponentEntries();

    auto *scene = SceneMgr::Get().ActiveScene();
    if (scene == nullptr)
        return;

    auto &registry = scene->GetRegister();

    for (const ComponentEditorInfo &info : ComponentEditorRegistry::Get().Components())
    {
        if (!info._has_component(registry, entity))
            continue;

        BuildComponentEntry(info, entity);
    }
}
```

此实现遍历已注册组件类型，而不是要求 ECS 第一阶段就提供“枚举 Entity 上所有组件”的新接口。

如果 ECS 已有高效组件枚举接口，优先改成枚举实体实际拥有的组件，再通过 Registry 查描述。

### 13.4 组件标题块

抽出统一函数：

```cpp
UI::CollapsibleView *CreateComponentBlock(const ComponentEditorInfo &info,
                                          ECS::Entity entity,
                                          bool allow_remove);
```

负责：

- 标题；
- 折叠状态；
- Reset/Remove 菜单；
- 通用视觉样式；
- 删除后触发 `_needs_rebuild`。

自定义组件编辑器不得自行重复创建标题块。

---

## 14. Add Component 菜单

删除当前独立维护的 `GetComponentMenuItems()`。

菜单直接遍历：

```cpp
for (const ComponentEditorInfo &info : ComponentEditorRegistry::Get().Components())
```

过滤：

```text
_allow_add == true
当前 Entity 尚未拥有该组件
```

按 `_category` 分组、按 `_order` 排序。

点击后：

1. 通过 `_add_component` 添加；
2. 使用现有 Command 系统支持 Undo；
3. 标记 Scene Dirty；
4. 设置 `_needs_rebuild = true`；
5. 关闭 Popup。

Tag、Transform 等强制组件注册时设置 `_allow_add = false` 或 `_allow_remove = false`。

---

## 15. 组件注册位置

在 Editor 初始化阶段增加：

```cpp
void RegisterComponentEditors()
{
    auto &registry = ComponentEditorRegistry::Get();

    registry.RegisterCustom<ECS::TransformComponent, TransformComponentEditor>(
        "Transform", "Core", 0, false, false);

    registry.RegisterCustom<ECS::ScriptComponent, ScriptComponentEditor>(
        "Script", "Scripting", 100);

    registry.RegisterCustom<ECS::LightComponent, LightComponentEditor>(
        "Light", "Rendering", 200);

    registry.RegisterCustom<ECS::StaticMeshComponent, StaticMeshComponentEditor>(
        "Static Mesh", "Rendering", 210);

    registry.Register<ECS::LightProbeComponent>(
        "Light Probe", "Rendering", 220);

    registry.Register<ECS::CCamera>(
        "Camera", "Rendering", 230);

    registry.Register<ECS::SpriteRendererComponent>(
        "Sprite Renderer", "Rendering", 240);
}
```

根据真实类型名称修正。

调用位置建议放在 Editor App 初始化，而不是 `ObjectDetail` 构造函数中。

必须防止重复注册。

---

## 16. Observer 生命周期与安全要求

现有 `CompositeBuilder` 把 `PropertyObserverHandle` 放入 UI Element，因此销毁组件 UI 时 Observer 会自动移除。这一点应继续使用。

但仍需要处理以下情况：

1. UI 尚未销毁，ECS 组件先被删除；
2. ECS pool 迁移导致 instance 地址变化；
3. 回调捕获旧 instance 地址。

第一版要求：

- 所有组件增删操作在执行后立即设置 `_needs_rebuild`；
- Object Detail 在同一帧或下一帧尽快销毁旧 UI；
- 禁止在面板之外长期持有这些 UI；
- 检查 ECS 是否保证组件地址稳定。

如果 ECS 组件地址可能迁移，现有 `PropertyInfo::AddObserver(instance)` 本身也依赖稳定地址，需要后续从反射 Observer 层解决，而不是在 Object Detail 中另建 Binding 绕过。

可选长期方案：Observer key 使用 Entity + ComponentTypeId，或 ECS 在迁移前通知 Observer 重新绑定。此次重构不强制实现。

---

## 17. PropertyInfo Setter 语义

当前 `PropertyInfo::Set()` 基于字段 offset 直接赋值，然后触发 Notify。

这对纯数据字段有效，但对以下字段可能绕过业务逻辑：

- Transform；
- Camera projection 参数；
- Script path；
- Light type；
- 资源引用；
- 需要设置 dirty/version 的字段。

本次实现采用以下优先级：

1. 若现有属性 Observer 已负责业务更新，继续使用 `PropertyInfo::Set()`；
2. 若组件已有 setter，则特殊组件编辑器调用 setter；
3. 普通反射字段继续直接 Set；
4. 不在 Object Detail 中增加字段名硬编码业务补丁。

长期可给反射系统增加可选 getter/setter，但不属于本次必须项。

建议未来形态：

```cpp
APROPERTY(Getter = NearClip, Setter = SetNearClip)
f32 _near_clip;
```

这样序列化、脚本、Undo 和 Editor 都能共享正式属性入口。

---

## 18. Undo/Redo

本次重构不得削弱现有 Undo 功能。

### 第一阶段最低要求

- Add Component 使用 Command；
- Remove Component 使用 Command；
- 保持当前已有属性 Undo 行为，不额外破坏。

### 后续增强

让 `CompositeBuilder::Params` 支持编辑生命周期：

```cpp
std::function<void(PropertyInfo *)> _on_edit_begin;
std::function<void(PropertyInfo *)> _on_value_changed;
std::function<void(PropertyInfo *)> _on_edit_end;
```

用于：

- Drag/Slider 连续修改合并为一个 Undo command；
- InputBlock focus 开始时保存旧值；
- focus 结束时提交属性命令。

此次不要为了 Undo 再实现一套 Property Binding。

---

## 19. 分阶段实施计划

### 阶段 1：整理 CompositeBuilder

完成：

1. 增加公开 `RegisterBuilder()`；
2. 增加统一 `_on_value_changed` 回调；
3. 抽取内部 `SetPropertyValue()`，减少重复；
4. 补 `String`；
5. 补 enum；
6. 确认 Vector、float、int、bool Observer 正常；
7. 修复输入控件在 Observer 更新时不能再次触发写回。

验收：

- 独立测试对象可通过反射生成基础属性 UI；
- 外部调用 `PropertyInfo::Set()` 后 UI 自动更新；
- UI 修改只触发一次属性通知。

### 阶段 2：实现 ReflectedPropertyPanel

完成：

1. Type 属性遍历；
2. 父类型属性遍历；
3. DisplayName；
4. Category；
5. Order；
6. Hidden；
7. Range params；
8. Scene Dirty 回调；
9. 属性过滤器。

验收：

- 一个简单组件无需手写 UI 即可完整显示；
- 分类和排序稳定；
- 不支持的属性输出一次明确 warning，不崩溃。

### 阶段 3：实现 ComponentEditorRegistry

完成：

1. 组件注册；
2. 类型擦除 Has/Get/Add/Remove；
3. 默认编辑器；
4. Add Component 菜单从 Registry 生成；
5. 通用组件标题与删除菜单。

验收：

- 新注册普通组件无需修改 ObjectDetail；
- Add/Remove 可用；
- 禁止删除的组件不显示 Remove。

### 阶段 4：迁移 ObjectDetail

迁移顺序：

1. LightProbe 或其他简单组件；
2. Camera/SpriteRenderer 中的普通部分；
3. Script；
4. Light；
5. Transform；
6. StaticMesh。

每迁移一个组件，删除 `ObjectDetail` 中对应旧代码和成员。

验收：

- `ObjectDetail.cpp` 不再包含组件类型的字段级 UI 构造；
- `ObjectDetail.h` 不再包含 `_light_block` 等具体组件成员；
- `Update()` 只负责选择变化、重建和自定义编辑器刷新。

### 阶段 5：资源字段与特殊类型

完成：

1. Material picker；
2. Texture/Sprite/Mesh picker；
3. Color picker；
4. Layer mask；
5. 其他引擎自定义值类型。

这些全部通过 `CompositeBuilder::RegisterBuilder()` 扩展。

---

## 20. 验收标准

### 架构验收

- [ ] 不存在新建的 Property Binding 系统；
- [ ] 所有普通字段读写使用 `PropertyInfo::Get/Set`；
- [ ] UI 同步使用现有 `PropertyInfo::AddObserver`；
- [ ] 单字段控件统一由 `CompositeBuilder` 构建；
- [ ] ObjectDetail 不再识别具体字段；
- [ ] Add Component 菜单与组件编辑器使用同一 Registry；
- [ ] 普通组件不需要自定义 editor 类。

### 功能验收

- [ ] 切换选中 Entity 后面板正确更新；
- [ ] 外部修改反射属性后控件自动刷新；
- [ ] 输入属性后 Scene 被标记 Dirty；
- [ ] Add/Remove 组件正确并支持现有 Undo；
- [ ] Light 类型切换后字段集合正确刷新；
- [ ] Transform Gizmo 修改后 UI 正确同步；
- [ ] Script path 修改后 Runtime 正确重置；
- [ ] Mesh/Material/Sprite 资源选择正确；
- [ ] 删除组件后没有失效 Observer 或悬空回调；
- [ ] 不支持的属性类型不会导致崩溃。

### 代码质量验收

- [ ] `ObjectDetail.cpp` 控制在约 200～350 行；
- [ ] 单个自定义组件 Editor 只包含该组件特殊逻辑；
- [ ] 不复制 float/vector/bool 等通用控件代码；
- [ ] 不通过字符串比较组件 C++ 类型名决定逻辑；
- [ ] 回调中不长期捕获 ECS 组件裸指针；
- [ ] 代码按 120 列格式化；
- [ ] 变量名使用小写加下划线；
- [ ] 类成员以下划线开头；
- [ ] 类名和函数名使用驼峰命名。

---

## 21. 明确禁止事项

Claude 实现时不要采用以下方案：

1. 不要新增 `PropertyBinding<T>`、`FloatBinding`、`VectorBinding` 等第二套绑定；
2. 不要在 `ObjectDetail` 中继续增加组件 `if/else`；
3. 不要让 ECS Runtime 组件依赖 Editor 或 ImGui；
4. 不要给每个普通组件都创建空壳自定义 Editor；
5. 不要绕过 `PropertyInfo::Set()` 直接写普通反射字段；
6. 不要复制 `CompositeBuilder` 的字段控件实现到 Editor；
7. 不要用 `TypeName()` 字符串作为主要注册键；
8. 不要一次实现复杂的声明式条件表达式系统；
9. 不要在 UI 回调中永久捕获可能失效的组件指针；
10. 不要为了重构改变现有组件序列化格式。

---

## 22. 建议 Claude 首先阅读的文件

按顺序阅读：

```text
Engine/Inc/Objects/Type.h
Engine/Src/Objects/Type.cpp

Engine/Inc/UI/Composite.h
Engine/Src/UI/Composite.cpp

Editor/Inc/Widgets/ObjectDetail.h
Editor/Src/Widgets/ObjectDetail.cpp

Editor/Inc/Common/Undo.h
Editor/Src/Common/Undo.cpp

Editor/Inc/Common/EditorPopup.h
Editor/Src/Common/EditorPopup.cpp

Editor/Src/Widgets/ResourceBrowser.cpp
Editor/Src/Widgets/EditorLayer.cpp
```

重点确认：

1. `Meta` 当前真实 API；
2. `Type` 的父类型遍历接口；
3. ECS 的 ComponentTypeId 和 Add/Remove API；
4. Scene Dirty 的正式接口；
5. Remove Component Command 的构造方式；
6. `UIElement::AddPropertyObserver()` 的销毁行为；
7. ResourceBrowser 中是否已有可复用资源选择器；
8. Color Picker 是否已有独立通用控件；
9. Transform/Camera/Light 属性修改后的 dirty 更新机制。

---

## 23. 交付要求

Claude 应按阶段提交，避免一次性大改导致难以验证。

每个阶段交付内容：

1. 修改文件列表；
2. 核心设计说明；
3. 完整可编译代码；
4. 已删除的旧路径；
5. 手工测试步骤；
6. 已知限制；
7. 下一阶段依赖。

最终交付应包含：

- 新增 Inspector 模块；
- 扩展后的 `CompositeBuilder`；
- 精简后的 `ObjectDetail.h/.cpp`；
- 组件注册代码；
- 至少 Transform、Light、Script、StaticMesh 的定制实现；
- 简单组件的自动反射绘制示例；
- Add/Remove Component 回归测试结果。

---

## 24. 最终目标示例

普通组件：

```cpp
ASTRUCT()
struct AudioListenerComponent
{
    GENERATED_BODY()

    APROPERTY()
    bool _enabled = true;

    APROPERTY(IsRange, RangeMin = 0.0f, RangeMax = 2.0f)
    f32 _gain = 1.0f;
};
```

只需要注册：

```cpp
registry.Register<ECS::AudioListenerComponent>("Audio Listener", "Audio", 100);
```

之后应自动获得：

- 组件标题；
- Enabled checkbox；
- Gain slider/input；
- PropertyInfo 写回；
- Observer 同步；
- Scene Dirty；
- Add Component 菜单；
- Remove Component；
- 无需修改 `ObjectDetail.cpp`。

最终 `ObjectDetail` 只负责编排，`CompositeBuilder` 负责单属性 UI，现有反射系统负责值访问和通知，复杂组件只保留必要的定制代码。
