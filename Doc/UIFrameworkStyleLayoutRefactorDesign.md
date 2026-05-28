# Ailu UI Framework Style/Layout Refactor Design

## 目标

当前 UI 框架已经具备：

* retained mode UI tree
* Measure / Arrange layout pipeline
* event bubbling
* dirty propagation
* transform hierarchy
* content rect
* layout container

但当前结构存在以下问题：

1. Layout / Style / Visual state 耦合
2. Slot 类型设计不可扩展
3. Visual 属性散落在各 Widget
4. UIElement 基类职责过重
5. 后续 Theme / Animation / StyleSheet 会难以扩展

本次重构目标：

* 引入统一 Style 系统
* 将 Slot 从“枚举类型”重构为“父布局协议”
* 清理 UIElement 职责
* 为后续 Theme / Animation / Editor / StyleSheet 做准备
* 保持现有 retained-mode/layout pipeline 不变

---

# 一、核心设计原则

## 1. UIElement 不拥有视觉风格

UIElement 只负责：

* hierarchy
* lifecycle
* transform
* dirty state
* event
* layout cache

不要在 UIElement 中保存：

* background color
* border
* font
* padding
* text color
* corner radius

这些应该属于 Widget Style。

---

## 2. Slot 是 Parent Layout Protocol

当前：

```cpp
struct Slot
{
    ESlotType _type;
}
```

设计错误。

原因：

slot 描述的是：

> parent 如何布局 child

而不是：

> child 是什么

因此：

* HorizontalLayout
* VerticalLayout
* Canvas

应该拥有不同 Slot 类型。

---

## 3. Style 是 Widget 外观描述

Style 应包含：

* visual
* content box
* typography

但：

layout policy 不一定属于 style。

margin 可以属于 slot。

padding 应属于 visual/content box。

---

# 二、目标架构

# UIElement

```cpp
class UIElement : public SerializeObject
{
protected:
    UIElement* _parent;
    Vector<Ref<UIElement>> _children;

    LayoutCache _layout_cache;

    Math::Transform2D _transform;

    DirtyFlags _dirty_flags;

    State _state;

    Ref<UISlot> _slot;
};
```

UIElement 不再拥有：

```cpp
Padding _padding;
Slot _slot;
```

---

# 三、Slot 系统重构

## 基础 Slot

```cpp
class UISlot : public SerializeObject
{
public:
    Padding _margin;
};
```

---

## CanvasSlot

```cpp
class CanvasSlot : public UISlot
{
public:
    Vector2f _anchor;
    Vector2f _position;

    Vector2f _size;

    bool _size_to_content = false;

    EAlignment _alignment_h;
    EAlignment _alignment_v;
};
```

---

## LinearSlot

```cpp
class LinearSlot : public UISlot
{
public:
    ESizePolicy _size_policy_h;
    ESizePolicy _size_policy_v;

    f32 _fill_rate = 1.0f;

    EAlignment _cross_align;
};
```

---

## 未来扩展

后续允许：

```cpp
GridSlot
DockSlot
OverlaySlot
ScrollSlot
```

避免单 Slot 巨型垃圾桶结构。

---

# 四、Style 系统设计

# 基础 Style

```cpp
class WidgetStyle : public SerializeObject
{
};
```

---

# BoxStyle

```cpp
struct BoxStyle
{
    Padding _padding;

    Color _background_color;

    Color _border_color;

    Vector4f _border_thickness;

    f32 _corner_radius = 0.0f;
};
```

用于：

* Border
* Button
* Panel
* InputBlock

---

# TextStyle

```cpp
struct TextStyle
{
    Color _color;

    u16 _font_size = 16;

    EAlignment _horizontal_align;

    EAlignment _vertical_align;
};
```

---

# ImageStyle

```cpp
struct ImageStyle
{
    Color _tint = Colors::kWhite;

    Vector2f _size_override;
};
```

---

# ButtonStyle

```cpp
struct ButtonStyle
{
    BoxStyle _normal;

    BoxStyle _hovered;

    BoxStyle _pressed;

    BoxStyle _disabled;
};
```

---

# 五、State 与 Style 解耦

当前代码存在：

```cpp
if (_state._is_hovered)
    draw_red();
else
    draw_green();
```

问题：

状态逻辑散落在 RenderImpl。

目标：

```cpp
const BoxStyle& style = ResolveCurrentStyle();
```

由：

```cpp
_state._is_hovered
_state._is_pressed
_state._is_enabled
```

决定最终 style。

RenderImpl 不直接判断状态。

---

# 六、Padding 重构

当前：

```cpp
UIElement::_padding
```

需要移除。

原因：

padding 不是所有 Widget 都天然拥有。

目标：

```cpp
Border::_style._padding
Button::_style._padding
```

只有具备 content box 的控件拥有 padding。

---

# 七、Layout Cache

建议新增：

```cpp
struct LayoutCache
{
    Vector4f _desired_rect;

    Vector4f _content_rect;

    Vector4f _arrange_rect;

    Vector4f _absolute_rect;
};
```

UIElement 内：

```cpp
LayoutCache _layout_cache;
```

避免 rect 数据散落。

---

# 八、Dirty Flag 重构

当前：

```cpp
bool _is_layout_dirty;
bool _paint_dirty;
bool _is_transf_dirty;
```

改为：

```cpp
enum class EDirtyFlags : u8
{
    kNone       = 0,
    kLayout     = 1 << 0,
    kPaint      = 1 << 1,
    kTransform  = 1 << 2,
    kStyle      = 1 << 3,
};
```

使用 bitmask。

---

# 九、推荐目录结构

```text
UI/

    Core/
        UIElement.h
        UISlot.h
        UIStyle.h
        UILayout.h

    Layout/
        CanvasLayout.h
        HorizontalLayout.h
        VerticalLayout.h
        GridLayout.h

    Slot/
        CanvasSlot.h
        LinearSlot.h
        GridSlot.h

    Style/
        BoxStyle.h
        TextStyle.h
        ButtonStyle.h
        ImageStyle.h

    Widgets/
        Button.h
        Border.h
        Text.h
        Slider.h
        Image.h
```

---

# 十、Widget 重构规范

## Button

Button 不再保存：

```cpp
Color _bg_color;
```

改为：

```cpp
ButtonStyle _style;
```

---

## Border

Border 使用：

```cpp
BoxStyle _style;
```

padding 来源：

```cpp
_style._padding
```

---

## Text

Text 使用：

```cpp
TextStyle _style;
```

移除：

```cpp
_horizontal_align
_vertical_align
_color
_font_size
```

---

# 十一、Layout Pipeline 保持不变

保留：

```cpp
MeasureDesiredSize()
Arrange()
MeasureAndArrange()
```

这是正确设计。

无需改 Immediate 模式。

---

# 十二、未来扩展目标

本次重构完成后，应支持未来扩展：

## Theme

```cpp
Theme->GetStyle<ButtonStyle>("PrimaryButton")
```

---

## StyleSheet

```css
Button:hover
{
    background: red;
}
```

---

## Animation

```cpp
Animate(background_color)
```

---

## Editor

允许 Editor 修改：

* Slot
* Style
* State

并正确 dirty propagation。

---

# 十三、迁移步骤（重要）

必须按顺序进行。

---

## Phase 1

引入：

```cpp
UISlot hierarchy
```

但保留旧 Slot 接口兼容层。

---

## Phase 2

引入：

```cpp
WidgetStyle hierarchy
```

但先不做 Theme。

---

## Phase 3

移除：

```cpp
UIElement::_padding
```

---

## Phase 4

将 Widget visual property 全部迁移到 Style。

---

## Phase 5

统一 dirty flags。

---

## Phase 6

清理旧接口：

```cpp
SlotType
_slot
```

---

# 十四、明确禁止的设计

禁止：

```cpp
struct MegaStyle
{
    // 所有 widget 共用
}
```

会导致：

* 巨型无意义字段
* editor 爆炸
* serialization 冗余
* style inheritance 混乱

---

禁止：

```cpp
ESlotType
```

slot 必须多态化。

---

禁止：

```cpp
UIElement 拥有视觉属性
```

---

# 十五、最终目标

最终框架结构：

```text
UIElement
    -> hierarchy/event/layout

Slot
    -> parent layout protocol

Style
    -> visual appearance

Layout
    -> arrange algorithm

Widget
    -> behavior + style composition
```

这是长期可维护结构。
