# AiluEngine Widget Editor 开发方案

> 面向实现者：Claude / Codex / 人工开发者  
> 目标：为 AiluEngine 增加一个类似 Unreal Engine UMG Designer 的轻量级游戏内 UI 可视化编辑器。  
> 仓库：`BoomBac/AiluEngine`  
> 基于：2026-08-12 `master` 分支当前架构整理。

---

## 1. 文档目标

本文档用于直接指导实现，不讨论完整 UE UMG/Widget Blueprint 的全部功能。

本次目标是实现一个 **轻量、现代、与 AiluEngine 当前 UI Runtime 深度复用** 的 Widget Editor，使用户可以：

- 创建 Widget UI Asset。
- 在编辑器中通过 Palette 添加 UI 控件。
- 在 Hierarchy 中查看、选择、重排、重设父级。
- 在 Designer 中可视化预览 UI。
- 对 Canvas 子元素进行拖动和缩放。
- 在 Details 中编辑 `UIElement` 和对应 `UISlot` 的反射属性。
- Undo / Redo。
- 保存、重新加载 Widget Asset。
- 在 Design / Preview 模式间切换。
- 最终在游戏运行时实例化 Widget Asset。

明确不做：

- UE Blueprint Graph。
- Visual Scripting。
- UI Animation Timeline。
- Multi-user editing。
- Prefab override 系统。
- 完整响应式布局预设系统。
- 复杂约束求解器。
- 编辑器专用 Widget 节点数据模型。

核心原则：

> **Widget Editor 只是一层可视化编辑工具。UIElement、UISlot、Widget、布局、序列化和反射仍由 Runtime 系统负责。**

---

# 2. 当前 AiluEngine 可复用能力

实现前必须先阅读并理解以下现有代码，不要重复实现已有能力。

## 2.1 UIElement

重点文件：

```text
Engine/Inc/UI/UIElement.h
Engine/Src/UI/UIElement.cpp
```

当前 `UIElement` 已具备：

- `SerializeObject` 基类。
- 多态 UI 控件。
- `_children` 树。
- `AddChild()` / `RemoveChild()`。
- Parent 关系。
- `UISlot`。
- `HitTest()`。
- `Arrange()`。
- Layout / Paint / Transform / Hierarchy invalidation。
- 事件系统。
- `PostDeserialize()`。
- 反射属性。

因此：

> **禁止新建 WidgetNode / WidgetElementData 等与 UIElement 一一对应的 Editor 数据模型。**

Widget Asset 直接保存 UIElement 模板树。

---

## 2.2 UISlot

重点文件：

```text
Engine/Inc/UI/UISlot.h
Engine/Src/UI/UISlot.cpp
```

当前已有：

```text
UISlot
├── CanvasSlot
└── LinearSlot
```

`CanvasSlot` 已包含：

- `_anchor`
- `_position`
- `_size`
- `_size_to_content`
- `_alignment_h`
- `_alignment_v`
- `_margin`

`LinearSlot` 已包含：

- `_size`
- `_margin`
- `_size_policy_h`
- `_size_policy_v`
- `_fill_rate`
- `_cross_align`

Parent Container 已负责创建对应 Slot。

因此 Designer 的编辑规则必须遵守：

```text
Canvas child
    -> 可自由 Move / Resize

VerticalBox / HorizontalBox child
    -> 不允许自由改 Position
    -> 通过 Margin / SizePolicy / FillRate / Alignment / sibling order 编辑
```

**不要使用 UIElement Transform 替代布局属性。**

---

## 2.3 Widget

重点文件：

```text
Engine/Inc/UI/Widget.h
Engine/Src/UI/Widget.cpp
```

当前 `Widget` 已负责：

- Root UIElement。
- UI 生命周期。
- UI Render。
- HitTest / Event dispatch。
- Focus。
- Widget Size / Position。
- RenderTexture output。
- `BindOutput()`。
- Paint cache invalidation。

Editor Preview 应复用真正的 Runtime Widget，而不是在 Editor 中模拟 UI。

---

## 2.4 TreeView

重点文件：

```text
Engine/Inc/UI/TreeView.h
Engine/Src/UI/TreeView.cpp
Editor/Inc/Widgets/WorldOutline.h
Editor/Src/Widgets/WorldOutline.cpp
```

现有 `TreeView` 已具备：

- DataSource。
- Selection。
- Expand / Collapse。
- Drag。
- Drop。
- Context Menu。
- ScrollIntoView。

Widget Hierarchy 必须直接复用 `TreeView`。

---

## 2.5 ReflectedPropertyPanel

重点文件：

```text
Editor/Inc/Inspector/ReflectedPropertyPanel.h
Editor/Src/Inspector/ReflectedPropertyPanel.cpp
```

当前接口：

```cpp
struct BuildArgs
{
    const Type *_type = nullptr;
    void *_instance = nullptr;
    UI::VerticalBox *_parent = nullptr;
    PropertyFilter _filter;
    PropertyChanged _on_property_changed;
};
```

Widget Details 必须复用此模块。

禁止：

```cpp
if (type == Button)
    DrawButtonProperties();
else if (type == Image)
    DrawImageProperties();
...
```

正确做法：

```text
Selected UIElement
    -> ReflectedPropertyPanel

Selected UIElement.GetSlot()
    -> ReflectedPropertyPanel
```

特殊属性以后通过 Reflection metadata 或少量 custom property editor 扩展。

---

## 2.6 Asset Editor

重点文件：

```text
Editor/Inc/Widgets/AssetEditorRegistry.h
Editor/Inc/Editors/SpriteAssetEditor.h
Editor/Src/Editors/SpriteAssetEditor.cpp
```

现有架构已经支持：

```text
Asset
    -> AssetEditorRegistry
    -> DockWindow
```

`WidgetEditor` 必须沿用该模式。

Sprite Editor 可以作为以下功能的实现参考：

- Asset Editor 生命周期。
- Toolbar。
- SplitView。
- Preview area。
- Pan / Zoom。
- Undo / Redo。
- Apply / Revert。
- 自定义 Preview UIElement。

---

# 3. 总体架构

最终结构：

```text
WidgetAsset
    |
    | Clone / Instantiate
    v
Runtime UI::Widget
    |
    +-------------------------+
    |                         |
    v                         v
WidgetHierarchyPanel     WidgetDesignerView
    |                         |
    +-----------+-------------+
                |
                v
          WidgetSelection
                |
                v
       WidgetDetailsPanel
                |
                v
      UIElement / UISlot
```

编辑器 UI：

```text
+-----------------------------------------------------------------------+
| Widget Editor | Save | Undo | Redo | Design/Preview | 1920x1080 | Fit |
+---------------+-------------------------------------+-----------------+
| Palette       |                                     | Details         |
|               |                                     |                 |
| Basic         |            Designer                 | Slot            |
|  Text         |                                     |  Position       |
|  Image        |       +---------------------+       |  Size           |
|  Button       |       |                     |       |  Anchor         |
|               |       |     Game UI         |       |                 |
| Layout        |       |                     |       | Element         |
|  Canvas       |       +---------------------+       |  Padding        |
|  VBox         |                                     |  Visibility     |
|  HBox         |                                     |  Style          |
+---------------+                                     |                 |
| Hierarchy     |                                     |                 |
| Canvas        |                                     |                 |
| |- Header     |                                     |                 |
| |- Content    |                                     |                 |
| `- Button     |                                     |                 |
+---------------+-------------------------------------+-----------------+
```

---

# 4. Widget Asset

## 4.1 不直接把 UI::Widget 当 Asset

`UI::Widget` 包含很多 Runtime 状态：

- RenderTarget。
- Window。
- event receiving。
- popup。
- sort order。
- runtime focus / hover / click 状态。
- runtime render cache。

这些不应该全部成为 Asset 模板数据。

新增：

```cpp
ACLASS()
class AILU_API WidgetAsset : public Asset
{
    GENERATED_BODY()

public:
    Ref<UI::Widget> CreateInstance() const;

    Vector2f DesignSize() const { return _design_size; }
    UI::UIElement *Root() const { return _root.get(); }

private:
    APROPERTY()
    Vector2f _design_size = {1920.0f, 1080.0f};

    APROPERTY()
    Ref<UI::UIElement> _root;
};
```

实际基类、构造方式和 Asset metadata 请遵循当前 Asset 实现习惯。

---

## 4.2 WidgetAssetDocument

放入当前 Asset Document 系统：

```cpp
ACLASS()
class AILU_API WidgetAssetDocument : public Object
{
    GENERATED_BODY()

public:
    APROPERTY()
    AssetDocumentHeader _header;

    APROPERTY()
    Vector2f _design_size = {1920.0f, 1080.0f};

    APROPERTY()
    Ref<UI::UIElement> _root;
};
```

不要为每一个控件建立：

```text
ButtonDocument
ImageDocument
TextDocument
CanvasDocument
...
```

现有 SerializeObject / Reflection 已具备多态对象序列化能力，应直接使用。

---

## 4.3 默认 Root

新建 Widget Asset 时默认：

```text
WidgetAsset
└── Canvas
```

Canvas 名称：

```text
RootCanvas
```

Design Size 默认：

```text
1920 x 1080
```

---

## 4.4 Runtime 实例化

API：

```cpp
Ref<UI::Widget> WidgetAsset::CreateInstance() const;
```

概念实现：

```cpp
Ref<UI::Widget> WidgetAsset::CreateInstance() const
{
    auto widget = MakeRef<UI::Widget>();
    widget->SetSize(_design_size);
    widget->AddToWidget(CloneUIElementTree(_root));
    return widget;
}
```

必须保证：

```text
Asset Template Tree
        !=
Runtime Instance Tree
```

禁止 Runtime 直接修改 Asset 中的 `_root`。

---

# 5. UIElement Clone

## 5.1 第一版方案

为了减少初期工作量，可以复用现有序列化系统：

```text
UIElement
   -> Memory Archive
   -> Deserialize
   -> cloned UIElement
```

提供统一函数：

```cpp
Ref<UI::UIElement> CloneUIElementTree(const Ref<UI::UIElement> &source);
```

只要 UI 实例创建不是每帧高频操作，该方案的性能足够。

---

## 5.2 后续优化

后续如需要优化，可以实现 Reflection Deep Clone：

```text
Type::CreateInstance()
    -> copy reflected properties
    -> recursive clone children
```

但 **MVP 不要求**。

---

# 6. 稳定 Element Guid

建议给 `UIElement` 增加稳定 Guid。

```cpp
protected:
    APROPERTY()
    Guid _guid;
```

增加：

```cpp
const Guid &GuidValue() const;
```

构造时：

```text
if guid empty
    -> generate guid
```

Deserialize 时保留保存的 Guid。

Clone Runtime Instance 时：

- Asset template 的 Guid 保留。
- 同一 Widget instance 内 Guid 与模板对应。
- 不使用 `Object::_id` 作为持久化 Element identity。

用途：

```text
Hierarchy selection
Undo / Redo
future animation track
Lua widget lookup
event binding
future prefab / variant
```

建议提供：

```cpp
UI::UIElement *FindElementByGuid(const Guid &guid);
```

可以放在 `WidgetAsset` / Editor helper，或以后放入 `Widget`。

---

# 7. WidgetEditor

新增：

```text
Editor/Inc/Editors/Widget/WidgetEditor.h
Editor/Src/Editors/Widget/WidgetEditor.cpp
```

建议类：

```cpp
class WidgetEditor : public DockWindow
{
public:
    WidgetEditor();
    ~WidgetEditor() override;

    void Open(WidgetAsset *asset);
    void Update(f32 dt) override;

private:
    void BuildUI();
    void BuildToolbar(UI::HorizontalBox *toolbar);
    void Save();
    void RebuildPreview();
    void SetSelectedElement(UI::UIElement *element);
    void SetSelectedGuid(const Guid &guid);

private:
    WidgetAsset *_asset = nullptr;
    Ref<UI::Widget> _preview_widget;

    Guid _selected_guid = Guid::EmptyGuid();
    UI::UIElement *_selected_element = nullptr;

    WidgetPalettePanel *_palette = nullptr;
    WidgetHierarchyPanel *_hierarchy = nullptr;
    WidgetDesignerView *_designer = nullptr;
    WidgetDetailsPanel *_details = nullptr;
};
```

---

# 8. AssetEditorRegistry 注册

沿用现有 `AssetEditorRegistry`。

概念代码：

```cpp
AssetEditorRegistry::Get().RegisterEditor(
    StaticClass<WidgetAsset>(),
    [](Asset *asset)
    {
        auto editor = MakeRef<WidgetEditor>();
        editor->Open(static_cast<WidgetAsset *>(asset));
        return editor;
    });
```

AssetBrowser 双击 Widget Asset 后进入 Widget Editor。

---

# 9. Editor 内部状态与 Asset 状态

第一版推荐：

```text
WidgetAsset
    -> 保存的数据

_preview_widget
    -> WidgetAsset 的 working copy
```

打开 Asset：

```text
WidgetAsset
    -> Clone
    -> Preview Widget
```

编辑操作全部修改：

```text
_preview_widget.Root()
```

Save 时：

```text
Preview Tree
    -> clone back
    -> WidgetAsset._root
    -> ResourceMgr / AssetHandler Save
```

这样可以自然支持：

```text
dirty
revert
undo
cancel
```

并避免编辑过程中直接污染已经加载的 Runtime Asset。

---

# 10. WidgetDesignerView

新增：

```text
Editor/Inc/Editors/Widget/WidgetDesignerView.h
Editor/Src/Editors/Widget/WidgetDesignerView.cpp
```

继承：

```cpp
class WidgetDesignerView : public UI::UIElement
```

职责只包含：

```text
Preview drawing
Pan
Zoom
Fit
Grid
HitTest
Selection Overlay
Move
Resize
Anchor Overlay
Design / Preview input routing
Drag & Drop destination
```

不要负责：

```text
Asset Save
Hierarchy construction
Reflection property UI
Undo manager ownership
Asset creation
```

---

# 11. Preview Render

推荐：

```text
Preview UI::Widget
    -> RenderTexture
    -> WidgetDesignerView draws RenderTexture
```

原因：

如果直接：

```text
Editor UI
└── Designer
    └── Game UI
```

游戏控件自身：

```text
MouseDown
MouseClick
Focus
ScrollView
Dropdown
InputBlock
```

会和 Designer 的选择/拖动行为冲突。

使用 RenderTexture 可以彻底隔离两套输入。

---

# 12. Preview Widget 创建

Designer 打开时：

```text
WidgetAsset working copy
    -> UI::Widget
    -> BindOutput(preview_render_texture)
```

Preview RenderTexture 尺寸应跟：

```text
WidgetAsset._design_size
```

一致。

第一版尺寸变化时允许重新创建 RenderTexture。

后续可以根据显存和 Preview zoom 做优化。

---

# 13. Designer 坐标系统

成员：

```cpp
Vector2f _view_offset = Vector2f::kZero;
f32 _zoom = 1.0f;
Vector2f _canvas_origin = Vector2f::kZero;
```

转换：

```cpp
Vector2f WidgetDesignerView::ScreenToDesign(const Vector2f &screen_pos) const
{
    return (screen_pos - _canvas_origin - _view_offset) / _zoom;
}

Vector2f WidgetDesignerView::DesignToScreen(const Vector2f &design_pos) const
{
    return _canvas_origin + _view_offset + design_pos * _zoom;
}
```

建议：

```text
Zoom min = 0.1
Zoom max = 8.0
```

输入：

```text
Mouse Wheel -> Zoom
Middle Mouse -> Pan
F -> Fit
1 -> 100%
```

Zoom 时尽量保持鼠标下的 Design point 不移动。

---

# 14. Designer HitTest

Design 模式下不要把鼠标事件派发给 Preview Widget。

只调用：

```cpp
UI::UIElement *element = preview_widget->Root()->HitTest(design_pos);
```

然后：

```cpp
SetSelectedElement(element);
```

注意：

如果 Root Canvas 被命中，但鼠标实际上点中子元素，应该返回最深层可命中的 UIElement。

沿用当前 `UIElement::HitTest()` 行为。

---

# 15. Selection

建议单独建立轻量 selection state，但不需要大型 subsystem。

可以由 `WidgetEditor` 持有：

```cpp
Guid _selected_guid;
UI::UIElement *_selected_element = nullptr;
```

任何入口：

```text
Hierarchy click
Designer click
Add element
Undo / Redo
Delete
```

最终统一调用：

```cpp
WidgetEditor::SetSelectedElement();
```

然后同步：

```text
Hierarchy
Designer
Details
```

---

# 16. Designer Overlay

Designer Overlay 属于 Editor，不属于 Runtime UI。

包括：

```text
Selection rectangle
Resize handles
Anchor marker
Parent bounds
Grid
Snap guides
Alignment guides
Hover outline
```

禁止给 `UIElement` 增加：

```text
_editor_selected
_editor_hovered
_editor_handle
```

绘制放在：

```cpp
WidgetDesignerView::RenderImpl()
```

中完成。

---

# 17. Canvas Move / Resize

只允许 `CanvasSlot` 自由操作。

## 17.1 Handle

```cpp
enum class EWidgetHandle
{
    kNone,
    kMove,

    kLeft,
    kRight,
    kTop,
    kBottom,

    kTopLeft,
    kTopRight,
    kBottomLeft,
    kBottomRight
};
```

Selection：

```text
o---------o---------o
|                   |
o      Element      o
|                   |
o---------o---------o
```

---

## 17.2 Move

修改：

```cpp
CanvasSlot::_position
```

不要修改：

```cpp
UIElement::_transition
```

除非后续明确引入 Layout Transform / Render Transform 的概念。

---

## 17.3 Resize

修改：

```cpp
CanvasSlot::_position
CanvasSlot::_size
```

不同 handle 分别修改对应边。

保持：

```text
width >= minimum size
height >= minimum size
```

第一版 minimum 可以是：

```text
1 px
```

---

## 17.4 实时刷新

MouseMove：

```text
update CanvasSlot
    -> InvalidateLayout
    -> Preview rerender
```

MouseUp：

```text
push one Undo Command
```

一次 drag 必须只生成一个 Undo step。

---

# 18. Linear Layout 编辑

对于：

```text
VerticalBox
HorizontalBox
```

子节点不能在 Designer 中自由 Move。

Designer 可以：

```text
select
highlight
drag to reparent
```

Details 中编辑：

```text
Margin
Size
SizePolicy
FillRate
CrossAlignment
```

Hierarchy 中编辑：

```text
Sibling order
Parent
```

如果用户试图拖动 Linear child：

第一版可以：

```text
不响应自由位移
```

后续可显示一个提示：

```text
Position is controlled by parent layout
```

---

# 19. Anchor

Anchor 属于 Phase 2 / Phase 3，不阻塞 MVP。

当前 `CanvasSlot` 已有：

```cpp
Vector2f _anchor;
```

显示：

```text
Parent Canvas
+----------------------------+
|             x              |
|                            |
|        Selected UI         |
|                            |
+----------------------------+
```

拖 Anchor：

```cpp
slot->_anchor = normalized_anchor;
```

范围：

```text
0..1
```

如果当前 Canvas layout 对 Anchor 的行为尚不完全符合 UE，需要先修 Runtime Canvas layout，而不是在 Editor 做特殊补偿。

---

# 20. WidgetHierarchyPanel

新增：

```text
Editor/Inc/Editors/Widget/WidgetHierarchyPanel.h
Editor/Src/Editors/Widget/WidgetHierarchyPanel.cpp
```

复用：

```cpp
UI::TreeView
UI::ITreeViewDataSource
```

建议：

```cpp
class WidgetTreeDataSource : public UI::ITreeViewDataSource
```

TreeItemId 不直接依赖裸指针地址。

可以：

```text
Guid hash -> TreeItemId
```

或者 Editor Session 中维护：

```text
TreeItemId <-> Guid
```

---

# 21. Hierarchy 功能

MVP：

```text
Select
Expand / Collapse
Delete
Rename
Reparent
Reorder
Context Menu
```

快捷键：

```text
Delete      -> Delete
F2          -> Rename
Ctrl+D      -> Duplicate
Ctrl+C/V    -> Phase 2
```

---

# 22. Reparent

执行：

```text
source
    -> remove from old parent
    -> AddChild(new parent)
```

需要注意当前 `UIElement::AddChild()` 会根据新 Parent 创建新的 Slot。

这正是需要的行为。

但需要保存可恢复信息：

```text
old parent guid
old sibling index
old slot snapshot
new parent guid
new sibling index
new slot snapshot
```

用于 Undo。

---

# 23. Reparent Slot 转换

例如：

```text
Canvas -> VerticalBox
```

Slot：

```text
CanvasSlot -> LinearSlot
```

旧位置数据不能完全保留。

建议策略：

通用字段保留：

```text
margin
size
```

其余使用新 Slot 默认值。

当前 `AddChild()` 已经有类似保留通用字段的行为，应优先复用。

---

# 24. Sibling Reorder

需要补充明确 API。

当前如果缺乏安全 reorder API，建议为 `UIElement` 增加：

```cpp
bool MoveChild(UI::UIElement *child, u32 new_index);
```

或：

```cpp
bool SetChildIndex(UI::UIElement *child, u32 new_index);
```

不要让 Editor 直接访问 `_children` 修改 Vector。

必须正确触发：

```text
Hierarchy invalidation
Layout invalidation
```

---

# 25. WidgetPalettePanel

新增：

```text
Editor/Inc/Editors/Widget/WidgetPalettePanel.h
Editor/Src/Editors/Widget/WidgetPalettePanel.cpp
```

不需要复杂 Registry。

数据：

```cpp
struct WidgetPaletteEntry
{
    String _name;
    String _category;
    const Type *_type = nullptr;
};
```

---

# 26. Palette 类型来源

优先方案：

通过 Reflection 枚举可实例化的 `UIElement` 派生类。

过滤：

```text
abstract type
editor-only type
internal helper UIElement
没有公开构造能力的 type
```

如当前 Reflection 不方便遍历全部派生类，则 MVP 可以显式注册：

```cpp
RegisterPaletteType<UI::Canvas>("Layout");
RegisterPaletteType<UI::VerticalBox>("Layout");
RegisterPaletteType<UI::HorizontalBox>("Layout");
RegisterPaletteType<UI::Text>("Basic");
RegisterPaletteType<UI::Image>("Basic");
RegisterPaletteType<UI::Button>("Basic");
```

但注册表只保存：

```text
Type + Category
```

不要保存另一套属性 schema。

---

# 27. Palette 分类

建议：

```text
Layout
    Canvas
    VerticalBox
    HorizontalBox
    ScrollView

Basic
    Text
    Image
    Button
    CheckBox
    InputBlock

Advanced
    ListView
    Dropdown
    ...
```

根据当前实际 UI 类逐步补充。

---

# 28. Palette Drag & Drop

拖入 Canvas：

```text
Create UIElement
    -> canvas.AddChild()
    -> CanvasSlot.Position(drop_position)
    -> select newly created element
    -> push Add Command
```

拖入 LinearBox：

```text
Create UIElement
    -> linear_box.AddChild()
    -> select newly created element
    -> push Add Command
```

Designer Drop Target：

优先选择鼠标位置下最深的、能够接收 child 的 Container。

MVP 可以先只允许：

```text
Canvas
VerticalBox
HorizontalBox
```

---

# 29. WidgetDetailsPanel

新增：

```text
Editor/Inc/Editors/Widget/WidgetDetailsPanel.h
Editor/Src/Editors/Widget/WidgetDetailsPanel.cpp
```

布局：

```text
Details
├── Slot
│   └── ReflectedPropertyPanel
└── Element
    └── ReflectedPropertyPanel
```

---

# 30. Details Build

概念：

```cpp
void WidgetDetailsPanel::SetElement(UI::UIElement *element)
{
    _element = element;

    if (_element == nullptr)
    {
        Clear();
        return;
    }

    auto &slot = _element->GetSlot();

    if (slot != nullptr)
    {
        _slot_panel.Build({
            ._type = slot->GetType(),
            ._instance = slot.get(),
            ._parent = _slot_root,
            ._on_property_changed = [this](const PropertyInfo &property) {
                OnPropertyChanged(property);
            }
        });
    }

    _element_panel.Build({
        ._type = _element->GetType(),
        ._instance = _element,
        ._parent = _element_root,
        ._on_property_changed = [this](const PropertyInfo &property) {
            OnPropertyChanged(property);
        }
    });
}
```

实际代码按当前 C++ 标准和 `BuildArgs` 初始化规则调整。

---

# 31. Property Change 与 Undo

现有 `ReflectedPropertyPanel` 的 `_on_property_changed` 回调目前只告诉调用者：

```cpp
const PropertyInfo &
```

要实现通用 Property Undo，可能需要知道：

```text
old value
new value
instance
property
```

推荐增加 Editor 层属性编辑事务，而不是把 Undo 逻辑写进每个控件。

可选方案：

### 方案 A：修改 Composite Property Editor

提供：

```text
OnValueBegin
OnValueChanged
OnValueEnd
```

一次 slider/input drag 最后生成一个 command。

### 方案 B：WidgetDetailsPanel 维护 serialized snapshot

Property edit begin：

```text
serialize target property / target object
```

Property edit end：

```text
compare
push command
```

MVP 优先选择改动最小的可行方案。

---

# 32. Undo / Redo

复用当前：

```text
ICommand
CommandMgr
```

至少实现：

```cpp
WidgetAddElementCommand
WidgetRemoveElementCommand
WidgetReparentCommand
WidgetReorderCommand
WidgetSlotEditCommand
WidgetPropertyEditCommand
```

---

# 33. Command 原则

Command 不保存长期裸指针。

保存：

```text
WidgetEditor weak/reference
element guid
parent guid
serialized snapshot
index
```

执行时：

```text
guid -> resolve UIElement
```

防止树变化后悬空。

---

# 34. Move / Resize Command

MouseDown：

```text
capture old CanvasSlot
```

MouseMove：

```text
live modify
```

MouseUp：

```text
capture new CanvasSlot
push one command
```

如果 old == new：

```text
do not push
```

---

# 35. Remove Command

删除需要能够完整恢复子树。

保存：

```text
parent guid
sibling index
serialized subtree
slot snapshot
```

Undo：

```text
deserialize subtree
insert at old index
restore slot
select restored root
```

---

# 36. Duplicate

MVP 可支持：

```text
Ctrl+D
```

通过：

```text
CloneUIElementTree(selected)
```

然后：

```text
same parent
next sibling position
```

Canvas child 可以：

```text
position += {10, 10}
```

并重新生成 **Asset Element Guid**。

注意：

> Duplicate 创建的是新模板对象，所以必须重新生成整棵 duplicate subtree 的 Guid。

Runtime Clone 则应该保留 Guid。

需要明确区分：

```text
CloneForRuntime()
DuplicateForAsset()
```

---

# 37. Design / Preview 模式

Toolbar：

```text
[Design] [Preview]
```

---

## 37.1 Design

Preview Widget 不接收真实输入。

Designer 使用输入进行：

```text
selection
move
resize
pan
zoom
drag/drop
```

可以直接调用 Preview tree 的 `HitTest()`，但不要调用：

```text
Widget::OnEvent()
```

---

## 37.2 Preview

Designer 不执行编辑交互。

将：

```text
mouse
keyboard
focus
scroll
```

转换到 Preview Widget 的设计坐标并交给：

```cpp
_preview_widget->OnEvent(event);
```

可直接测试：

```text
Button Hover
Pressed
CheckBox
ScrollView
Dropdown
InputBlock
```

ESC 返回 Design。

---

# 38. Input Routing

必须明确：

```text
Design mode:
    Editor owns input

Preview mode:
    Preview Widget owns input when mouse is inside preview
```

Pan / Zoom 可以约定 Preview 模式下：

```text
Alt + Mouse Wheel
Alt + Middle Mouse
```

或者直接禁用。

MVP 选择简单一致的方案即可。

---

# 39. Grid 与 Snap

MVP：

```cpp
bool _show_grid = true;
bool _snap_enabled = true;
f32 _grid_size = 8.0f;
```

Move：

```text
position = round(position / grid_size) * grid_size
```

Resize 同理。

建议：

```text
Ctrl held -> temporarily disable snap
```

---

# 40. Alignment Guides

Phase 2。

检测与：

```text
parent edge
parent center
sibling edge
sibling center
```

的距离。

阈值：

```text
4~6 screen pixels
```

注意阈值应使用 Screen Space，不应被 Zoom 改变视觉体验。

---

# 41. Toolbar

建议：

```text
Save
Undo
Redo
|
Design / Preview
|
Resolution
Zoom
Fit
1:1
|
Grid
Snap
```

快捷键：

```text
Ctrl+S      Save
Ctrl+Z      Undo
Ctrl+Y      Redo
Delete      Delete
Ctrl+D      Duplicate
F           Fit
1           100%
Escape      Cancel current drag / return Design
```

---

# 42. Design Resolution

Widget Asset：

```cpp
Vector2f _design_size;
```

Toolbar 可以选择：

```text
1920 x 1080
2560 x 1440
1280 x 720
iPhone / mobile presets -- Phase 2
Custom
```

MVP 只需要：

```text
Width
Height
```

两项输入。

Design size 改变：

```text
Widget.SetSize()
RenderTexture resize
Root Canvas Arrange()
```

---

# 43. Dirty State

WidgetEditor 需要明确 Dirty。

推荐维护 revision：

```cpp
u64 _edit_revision = 0;
u64 _saved_revision = 0;
```

每个成功 Command：

```text
_edit_revision++
```

Save：

```text
_saved_revision = _edit_revision
```

Undo/Redo 若当前 CommandManager 不支持方便判断 dirty，可以保存一个 editor-local state id。

MVP 也可以采用：

```text
serialized working copy != serialized saved copy
```

但 revision 更轻。

---

# 44. Save

保存流程：

```text
Preview working tree
    -> clone to WidgetAsset
    -> build WidgetAssetDocument
    -> AssetHandler Save
    -> update dirty state
```

不要直接序列化 RenderTexture / Widget runtime state。

---

# 45. WidgetAssetHandler

新增：

```text
WidgetAssetHandler
```

遵循现有：

```text
IAssetHandler
Load()
Save()
AssetType()
```

并在 AssetHandler 注册系统中注册。

需要支持：

```text
.alasset
```

现有 Asset Browser 应识别 WidgetAsset。

---

# 46. Asset Dependencies

Widget Asset 的 dependency 应从 UI tree 中收集。

例如：

```text
Image -> Sprite
Style -> Theme
Font -> Font Asset
```

MVP 如果现有 UIElement 还没有统一 asset dependency 枚举接口，可以先保存资源引用本身，后续再统一 dependency collection。

不要为了 Widget Editor 单独设计另一套依赖系统。

---

# 47. Suggested Public API

如现有接口不足，优先新增以下少量 Runtime API。

## UIElement

```cpp
const Guid &GuidValue() const;
bool SetChildIndex(UIElement *child, u32 index);
UIElement *FindChildByGuid(const Guid &guid, bool recursive = true);
```

可根据当前命名习惯调整。

---

## WidgetAsset

```cpp
Ref<UI::Widget> CreateInstance() const;
const Vector2f &DesignSize() const;
UI::UIElement *Root() const;
```

Editor 写入可使用 friend / Editor-only API / setter，按现有 Asset 风格决定。

---

# 48. 不建议新增的 Runtime API

不要因为 Editor 需要就污染 Runtime：

```text
UIElement::SetEditorSelected()
UIElement::DrawEditorOutline()
UIElement::IsInWidgetEditor()
UIElement::EditorResize()
UIElement::EditorDrag()
```

全部放 Editor。

---

# 49. 文件结构

建议最终新增：

```text
Engine/
├── Inc/
│   ├── Assets/
│   │   └── WidgetAsset.h
│   └── UI/
│       └── UIElement.h                     [small change: Guid / helper]
│
├── Src/
│   ├── Assets/
│   │   └── WidgetAsset.cpp
│   └── UI/
│       └── UIElement.cpp                   [small change]
│
└── Inc/Assets/
    └── AssetDocument.h                     [WidgetAssetDocument]

Editor/
├── Inc/Editors/Widget/
│   ├── WidgetEditor.h
│   ├── WidgetDesignerView.h
│   ├── WidgetHierarchyPanel.h
│   ├── WidgetPalettePanel.h
│   ├── WidgetDetailsPanel.h
│   └── WidgetEditorCommands.h
│
└── Src/Editors/Widget/
    ├── WidgetEditor.cpp
    ├── WidgetDesignerView.cpp
    ├── WidgetHierarchyPanel.cpp
    ├── WidgetPalettePanel.cpp
    ├── WidgetDetailsPanel.cpp
    └── WidgetEditorCommands.cpp
```

以及修改：

```text
AssetHandlers
Asset handler registration
AssetBrowser create menu
AssetEditorRegistry registration
CMakeLists
generated reflection files -- 由现有生成流程处理
```

如项目现有 Editor 目录约定不同，以当前约定为准，不强行新建层级。

---

# 50. 类关系

```text
DockWindow
└── WidgetEditor
    ├── WidgetPalettePanel
    ├── WidgetHierarchyPanel
    │   └── TreeView
    ├── WidgetDesignerView
    └── WidgetDetailsPanel
        ├── ReflectedPropertyPanel : Slot
        └── ReflectedPropertyPanel : Element
```

Runtime：

```text
Asset
└── WidgetAsset
    └── UIElement template root

UI::Widget
└── UIElement runtime root
```

---

# 51. 实现阶段

---

## Phase 0 - 架构准备

目标：

确认现有系统能够完整支持 UIElement tree round-trip。

任务：

- [ ] 阅读 `UIElement::Serialize/Deserialize`。
- [ ] 阅读 `SerializerWrapper<Ref<T>>` 多态反序列化。
- [ ] 写测试：Canvas + VBox + Text + Button 序列化后重新加载。
- [ ] 验证 Slot concrete type 正确恢复。
- [ ] 验证 parent / owning widget / hierarchy depth 正确恢复。
- [ ] 验证 UI 可以正常 layout/render。
- [ ] 如果发现序列化缺陷，优先修复通用 SerializeObject，不在 WidgetAsset 做 workaround。

验收：

```text
UIElement tree -> JSON/Archive -> UIElement tree
```

结构和关键属性一致。

---

## Phase 1 - WidgetAsset

任务：

- [ ] 新建 `WidgetAsset`。
- [ ] 新建 `WidgetAssetDocument`。
- [ ] 新建 `WidgetAssetHandler`。
- [ ] 注册 Asset Handler。
- [ ] AssetBrowser 支持创建 WidgetAsset。
- [ ] 默认创建 Root Canvas。
- [ ] 实现 `CreateInstance()`。
- [ ] 实现 runtime clone。
- [ ] 添加 round-trip 单元测试。

验收：

```text
Create WidgetAsset
Save
Restart / Reload
Load
CreateInstance
Render
```

全部正常。

---

## Phase 2 - WidgetEditor Shell

任务：

- [ ] 新建 WidgetEditor。
- [ ] 注册 AssetEditorRegistry。
- [ ] 搭建 Toolbar。
- [ ] 左 Palette + Hierarchy。
- [ ] 中 Designer。
- [ ] 右 Details。
- [ ] 支持 Asset 打开和关闭。
- [ ] 支持 Save。
- [ ] 支持 Dirty 标记。

验收：

双击 `.alasset` Widget 可以打开独立 Dock Editor。

---

## Phase 3 - Hierarchy

任务：

- [ ] `WidgetTreeDataSource`。
- [ ] TreeView 显示完整 UI tree。
- [ ] Hierarchy Selection。
- [ ] 与 Editor Selection 双向同步。
- [ ] Delete。
- [ ] Rename。
- [ ] Reorder。
- [ ] Reparent。
- [ ] Context Menu。

验收：

Hierarchy 可以完成基本树编辑，Save/Reload 后一致。

---

## Phase 4 - Details

任务：

- [ ] Slot property panel。
- [ ] Element property panel。
- [ ] Selection 时 rebuild。
- [ ] Property change 后 Preview 即时更新。
- [ ] Property Undo / Redo。
- [ ] 过滤不应在 Editor 显示的内部属性。

建议默认隐藏：

```text
Object internal id/hash
runtime pointers
runtime state flags
dirty state
owning widget
parent
children raw container
```

具体依赖 Reflection metadata / PropertyFilter。

验收：

CanvasSlot Position / Size、Text、Image、Button 等主要属性可编辑。

---

## Phase 5 - Designer Preview

任务：

- [ ] Preview Widget。
- [ ] Preview RenderTexture。
- [ ] Designer RenderTexture drawing。
- [ ] Fit。
- [ ] Zoom。
- [ ] Pan。
- [ ] Grid。
- [ ] Design coordinates。
- [ ] HitTest selection。
- [ ] Hover / selection outline。

验收：

Hierarchy 与 Designer 点击能够稳定选择同一个元素。

---

## Phase 6 - Canvas Editing

任务：

- [ ] 8 resize handles。
- [ ] Move。
- [ ] Resize。
- [ ] Snap。
- [ ] Cancel drag。
- [ ] Move / Resize Undo。
- [ ] Zoom 下 handle screen size 保持一致。

验收：

体验应达到基础 UMG Designer 水平。

---

## Phase 7 - Palette

任务：

- [ ] Palette list。
- [ ] Category。
- [ ] Click-to-add 或 drag-to-add。
- [ ] Designer Drop。
- [ ] Hierarchy Drop。
- [ ] 自动创建正确 Slot。
- [ ] Add Undo。
- [ ] 新元素自动 Selection。

验收：

不需要写代码即可构建：

```text
Canvas
└── VerticalBox
    ├── Text
    ├── Button
    └── Button
```

---

## Phase 8 - Design / Preview

任务：

- [ ] Toolbar toggle。
- [ ] Design mode input interception。
- [ ] Preview mode event forwarding。
- [ ] Mouse coordinate conversion。
- [ ] Keyboard forwarding。
- [ ] Focus。
- [ ] ESC return Design。

验收：

Button / ScrollView / Dropdown 可以在编辑器内直接试玩。

---

## Phase 9 - Quality

任务：

- [ ] Ctrl+S。
- [ ] Ctrl+Z/Y。
- [ ] Delete。
- [ ] Ctrl+D。
- [ ] F / 1。
- [ ] Selection persistence。
- [ ] Reopen persistence。
- [ ] Invalid pointer / delete robustness。
- [ ] Empty widget robustness。
- [ ] Resize Editor robustness。
- [ ] Large hierarchy test。
- [ ] Reparent cycle prevention。

---

# 52. Reparent Cycle Prevention

必须阻止：

```text
A
└── B
    └── C
```

将：

```text
A -> C
```

因为会形成 cycle。

实现 helper：

```cpp
bool IsDescendantOf(const UI::UIElement *element, const UI::UIElement *potential_parent);
```

Drop validation 必须检查。

---

# 53. Delete Root

默认 Root Canvas 不允许直接 Delete。

Context menu：

```text
Root Canvas
    Delete disabled
```

后续如果允许 Root 替换，单独设计。

MVP 保持简单。

---

# 54. Container Drop Rules

建议接口：

```cpp
bool CanAcceptChild(const UI::UIElement *parent, const Type *child_type);
```

MVP：

```text
Canvas         -> true
VerticalBox    -> true
HorizontalBox  -> true
```

对于：

```text
Button
Image
Text
```

是否允许 children，应遵守 Runtime 实际语义。

不要让 Editor 允许 Runtime 不支持的结构。

---

# 55. Multi Selection

MVP 不做。

所有架构按单选设计：

```text
Guid _selected_guid
```

以后需要时升级成：

```text
Vector<Guid>
```

不要为未来 multi-select 提前增加大量复杂度。

---

# 56. Copy / Paste

Phase 2。

基本实现可复用序列化 subtree。

Clipboard：

```text
serialized UIElement subtree
```

Paste：

```text
deserialize
regenerate guid recursively
add to selected container / selected parent
```

---

# 57. Styles

当前 UI Runtime 已有 Theme / Style 系统。

Widget Editor 只编辑现有 style/reflected properties。

不要实现：

```text
WidgetEditorStyle
WidgetBlueprintStyle
Editor-only copied visual state
```

如果某些 style override 尚未 APROPERTY/Reflection 化，应修 Runtime Reflection 能力。

---

# 58. Performance

目标不是为 100000 UIElement 优化。

合理目标：

```text
100~1000 UIElement
```

基本编辑流畅。

优化原则：

Hierarchy：

```text
structure revision changed -> Refresh Tree
```

不要每帧 rebuild。

Details：

```text
selection changed -> rebuild
```

不要每帧 rebuild。

Preview：

依赖现有 Widget invalidation。

Designer overlay：

只绘制 Editor overlay。

---

# 59. Preview RenderTexture 生命周期

不要在 Designer resize / zoom 时不停创建 RenderTexture。

Preview RenderTexture 跟：

```text
design resolution
```

而不是跟：

```text
Editor panel pixel size
```

绑定。

因此：

```text
Zoom / Dock resize
    -> no RenderTexture recreate

Design resolution changed
    -> recreate / resize
```

这是重要性能约束。

---

# 60. Error Handling

Asset 加载失败：

```text
显示错误状态
不崩溃
```

UIElement Type 找不到：

```text
日志中打印 type name
asset load failure
```

不要静默丢节点然后保存覆盖原 Asset。

如果 Widget Asset 存在未知 UIElement type：

```text
Editor should enter read/error state
```

至少禁止无提示 Save。

---

# 61. Testing

至少增加以下测试。

## Serialization

```text
WidgetAssetSerializeTest
WidgetAssetPolymorphicElementTest
WidgetAssetSlotTypeTest
WidgetAssetGuidPersistenceTest
```

---

## Clone

```text
WidgetAssetRuntimeCloneTest
WidgetAssetDuplicateGuidTest
```

要求：

```text
Runtime clone:
    same element guid
    different C++ object address

Asset duplicate:
    different element guid
```

---

## Hierarchy

```text
WidgetReparentTest
WidgetReparentCycleTest
WidgetReorderTest
WidgetDeleteUndoTest
```

---

## Canvas

```text
WidgetCanvasMoveTest
WidgetCanvasResizeTest
WidgetCanvasSnapTest
```

---

# 62. 关键验收场景

实现结束后手工构建以下 UI：

```text
RootCanvas
├── Background : Image
├── Header : HorizontalBox
│   ├── Logo : Image
│   └── Title : Text
└── MainMenu : VerticalBox
    ├── PlayButton : Button
    ├── OptionsButton : Button
    └── ExitButton : Button
```

要求：

- [ ] Palette 可创建所有节点。
- [ ] Hierarchy 显示正确。
- [ ] Reorder 正确。
- [ ] Reparent 正确。
- [ ] Details 可编辑。
- [ ] Canvas child 可拖动。
- [ ] LinearBox child 不允许自由 Position。
- [ ] Undo / Redo 正确。
- [ ] Save / Reload 完全一致。
- [ ] Design mode 可选择。
- [ ] Preview mode Button Hover/Click 工作。
- [ ] Editor resize 不改变 Asset design resolution。
- [ ] Zoom 不重建 Preview RenderTexture。

---

# 63. 第一版完成定义

以下全部满足即可认为 Widget Editor MVP 完成：

```text
Widget Asset
Hierarchy
Palette
Designer Preview
Details
Canvas Move / Resize
Undo / Redo
Save / Load
Design / Preview
```

不要求：

```text
Animation
Multi Selection
Copy Paste
Responsive Presets
Advanced Snap Guides
Prefab
Blueprint
```

---

# 64. 实现约束

Claude 实现时必须遵守。

## 64.1 不重复造系统

禁止新增：

```text
Editor Widget Node Graph
Editor Property Schema
Editor Layout Engine
Editor-only Widget Serializer
Editor-only UI Renderer
```

必须复用：

```text
UIElement
UISlot
Widget
Reflection
SerializeObject
TreeView
ReflectedPropertyPanel
AssetEditorRegistry
CommandMgr
```

---

## 64.2 Runtime 与 Editor 解耦

Runtime 只允许少量通用能力增强，例如：

```text
stable Guid
safe child reorder
tree lookup helper
WidgetAsset
```

所有：

```text
Gizmo
Selection
Designer
Palette
Hierarchy
Preview controls
```

放 Editor。

---

## 64.3 不提前抽象

第一版避免：

```text
WidgetEditorSubsystem
WidgetEditorManager
WidgetBlueprintCompiler
WidgetDocumentModel
WidgetTemplateManager
WidgetPreviewSubsystem
```

除非实现过程中发现明确的两个以上独立使用者。

---

## 64.4 保持实现可删除

每个 Editor Panel 应职责单一。

WidgetEditor 负责协调：

```text
Asset
Selection
Panels
Preview
Save
```

Panel 不互相强依赖。

---

# 65. 推荐实现顺序给 Claude

严格按以下顺序实施，**每阶段编译和测试通过后再进入下一阶段**。

```text
Task 1
Inspect existing architecture and write no code.

Task 2
Implement WidgetAsset + serialization + tests.

Task 3
Implement runtime Clone / CreateInstance + tests.

Task 4
Implement WidgetEditor empty shell and AssetEditor registration.

Task 5
Implement Hierarchy using existing TreeView.

Task 6
Implement Details using existing ReflectedPropertyPanel.

Task 7
Implement Preview Widget + RenderTexture + pan/zoom/fit.

Task 8
Implement Designer HitTest + selection synchronization.

Task 9
Implement Canvas move / resize.

Task 10
Implement Undo / Redo commands.

Task 11
Implement Palette create / drag drop.

Task 12
Implement reparent / reorder.

Task 13
Implement Design / Preview mode.

Task 14
Polish shortcuts, error handling and tests.
```

---

# 66. Claude 每阶段提交要求

每个 Task 完成时输出：

```text
1. Changed files
2. Architecture decisions
3. Important implementation notes
4. Tests added
5. Build/test result
6. Remaining limitations
```

禁止一次性提交整个 Widget Editor。

原因：

```text
Asset
Serialization
Hierarchy
Designer
Undo
Input routing
```

任何一层错误都会让后续 debugging 复杂度指数增加。

---

# 67. 开始实现前必须重新阅读的源码

Claude 开始编码前必须实际打开当前仓库中的：

```text
Engine/Inc/UI/UIElement.h
Engine/Src/UI/UIElement.cpp

Engine/Inc/UI/UISlot.h
Engine/Src/UI/UISlot.cpp

Engine/Inc/UI/Widget.h
Engine/Src/UI/Widget.cpp

Engine/Inc/UI/Container.h
Engine/Src/UI/Container.cpp

Engine/Inc/UI/TreeView.h
Engine/Src/UI/TreeView.cpp

Engine/Inc/Objects/Serialize.h

Engine/Inc/Assets/AssetDocument.h
Engine/Inc/Assets/AssetHandlers.h
Engine/Src/Assets/AssetHandlers.cpp

Editor/Inc/Inspector/ReflectedPropertyPanel.h
Editor/Src/Inspector/ReflectedPropertyPanel.cpp

Editor/Inc/Widgets/AssetEditorRegistry.h
Editor/Src/Widgets/AssetBrowser.cpp

Editor/Inc/Editors/SpriteAssetEditor.h
Editor/Src/Editors/SpriteAssetEditor.cpp

Editor/Inc/Widgets/WorldOutline.h
Editor/Src/Widgets/WorldOutline.cpp

Editor/Inc/Dock/DockWindow.h
```

如果实际仓库已经发生变化，以当前源码为准。

---

# 68. 需要优先核实的现有行为

实现者不能假设以下行为，必须先用源码/测试确认：

```text
1. Ref<UIElement> 是否可以完整多态 Deserialize。
2. UIElement::Deserialize 是否会重复调用 PostDeserialize。
3. AddChild reparent 后 Slot 的通用字段保留行为。
4. UIElement HitTest 的 child traversal 顺序是否与绘制顺序一致。
5. UIElement 删除时 UIManager focus/capture 是否安全清理。
6. ReflectedPropertyPanel property callback 的触发时机。
7. CommandMgr 是否支持 Editor-specific command 生命周期。
8. AssetHandler 当前保存 AssetDocument 的标准流程。
9. AssetBrowser 创建新 Asset 的入口和类型注册方式。
10. UI Renderer 对外部 Widget RenderTexture 的正常使用方式。
```

如发现现有基础能力有 bug：

> 优先修复通用系统，而不是在 WidgetEditor 中添加 workaround。

---

# 69. 设计决策摘要

最终应保持以下设计：

```text
WidgetAsset
    = UI template

UI::Widget
    = runtime instance

UIElement
    = actual widget node

UISlot
    = actual layout data

WidgetEditor
    = orchestration

Hierarchy
    = TreeView over UIElement tree

Details
    = Reflection over UIElement + UISlot

Designer
    = RenderTexture preview + editor overlays

Undo
    = Guid-based commands
```

这是整个实现的核心。

---

# 70. 最终原则

AiluEngine Widget Editor 不应该成为第二套 UI Framework。

它应该只是：

```text
Existing UI Runtime
        +
Existing Reflection
        +
Existing Serialization
        +
Existing Editor Widgets
        +
Small Visual Editing Layer
```

只要始终保持这一点，整个系统可以控制在较小规模，同时具备接近 UE UMG Designer 的核心使用体验。

