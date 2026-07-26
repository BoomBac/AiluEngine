# AiluEngine UI Paint Cache 与增量绘制改造设计

## 1. 文档目的

本文档用于指导 AiluEngine UI 系统从当前的“Retained UI Tree + 每帧重建绘制数据”模式，
改造为“Retained UI Tree + Dirty Invalidation + Paint Cache + 持久 GPU Geometry”模式。

本文档的直接目标是让实现者可以按阶段完成改造，并确保：

- 静态 UI 不再每帧遍历完整 UI 树。
- 静态 UI 不再每帧重新生成顶点、索引和 DrawNode。
- 静态文本不再每帧重新执行 Glyph Layout。
- 静态 UI 不再每帧上传全部顶点和索引。
- UI 仍然保持正确的绘制顺序、裁剪、状态切换和批处理行为。
- 动态 UI 可以通过局部失效或 Volatile 路径更新，而不强制整个界面重建。

本次改造不要求立即实现复杂的全局 UI Geometry Arena、RenderTarget Retainer 或 DX12 Bundle。
第一阶段应优先完成 Widget 级 Paint Cache，并保证失效传播正确。

---

## 2. 当前代码范围

本设计基于以下主要文件：

```text
Engine/Inc/UI/UIElement.h
Engine/Src/UI/UIElement.cpp

Engine/Inc/UI/Widget.h
Engine/Src/UI/Widget.cpp

Engine/Inc/UI/UIRenderer.h
Engine/Src/UI/UIRenderer.cpp

Engine/Inc/UI/DrawerBlock.h
Engine/Src/UI/DrawerBlock.cpp

Engine/Inc/UI/TextRenderer.h
Engine/Src/UI/TextRenderer.cpp

Engine/Inc/UI/Basic.h
Engine/Src/UI/Basic.cpp

Engine/Inc/UI/Container.h
Engine/Src/UI/Container.cpp
```

当前 UI 系统已经具备：

- Retained Mode UI Tree。
- Measure / Arrange 布局流程。
- `_is_layout_dirty`。
- `_is_transf_dirty`。
- `_paint_dirty` 字段。
- Style Dirty 与 Theme Revision。
- Widget 级 DrawerBlock 隔离。
- 相邻 DrawNode 合并。
- Scissor Stack。
- 文本 Glyph Quad 生成。
- 多帧 DrawerBlock 容器。

当前缺失的是：

- `_paint_dirty` 没有真正控制 Paint 构建。
- Paint 构建结果在提交后被 `Flush()` 丢弃。
- `SubmitVertexData()` 每帧上传完整顶点和索引。
- 文本每帧重新执行 `LayoutText()`。
- UI 状态 Setter 没有完整触发 Paint Invalidation。
- Widget 没有缓存命中与缓存重建路径。
- Transform、Paint、Layout、Hierarchy 的失效类型没有完整区分。

---

## 3. 当前问题分析

### 3.1 布局已经是增量更新

`UIElement::Update()` 仅在 `_is_layout_dirty` 为 `true` 时执行 `MeasureAndArrange()`。

这部分方向正确，应保留当前机制，并在本次改造中补充：

- Layout Dirty 必须隐含 Paint Dirty。
- Layout Dirty 应传播到适当的 Layout Root。
- Layout 完成后应清除 Layout Dirty。
- 布局结果发生变化时，应使相关 Paint Cache 失效。

### 3.2 Paint 每帧重建

当前 `UIRenderer::Render()` 每帧执行：

```text
Widget::PreUpdate()
Widget::Update()
Widget::Render()
```

`Widget::Render()` 会调用 `_root->Render(renderer)`。

容器控件的 `RenderImpl()` 会继续递归调用所有子节点：

```text
Canvas::RenderImpl()
LinearBox::RenderImpl()
Button::RenderImpl()
...
```

即使 UI 连续多帧完全静止，仍然会：

- 遍历完整 UI 树。
- 调用所有可见元素的 `RenderImpl()`。
- 重建所有 Quad。
- 重建所有文字 Glyph。
- 重建 DrawNode。
- 上传所有顶点流和索引。

### 3.3 DrawerBlock 当前是临时帧数据

当前 `DrawerBlock::Flush()` 会清除：

```cpp
_cur_vert_num = 0u;
_cur_index_num = 0u;
_nodes.clear();
```

`UIRenderer::SubmitBlock()` 在提交后无条件调用 `Flush()`。

这意味着 `DrawerBlock` 虽然持有 GPU Buffer，但 CPU 侧绘制记录每帧都会丢失，
下一帧无法复用已有几何和 DrawNode。

### 3.4 顶点与索引每帧完整上传

当前 `DrawerBlock::SubmitVertexData()` 会上传：

- Position Stream。
- UV Stream。
- Color Stream。
- Rect Stream。
- Corner Radius Stream。
- Index Buffer。

对于静态 UI，这些上传都是重复数据。

### 3.5 文本每帧重新排版

当前 `TextRenderer::AppendText()` 每次都会调用 `LayoutText()`，随后逐 Glyph 生成 Quad。

静态文本的以下数据实际上可以缓存：

- Glyph 列表。
- Glyph Position。
- Glyph Size。
- Glyph UV。
- Font Page。
- 文本尺寸。
- Visual Bounds。

只要文字、字体、字号、换行宽度、DPI 等没有变化，就不应重新 Layout。

### 3.6 Paint Invalidation 未闭环

`UIElement` 已经存在 `_paint_dirty`，但当前：

- 没有统一的 `InvalidatePaint()`。
- 没有缓存根节点。
- 没有 Paint Dirty 向 Cache Root 的传播。
- Paint 构建完成后没有清理 `_paint_dirty`。
- Hover、Pressed、Focused 等状态改变后没有稳定触发 Paint 重建。

一旦引入缓存，如果这些失效通知不完整，就会出现画面不更新的问题。

---

## 4. 改造目标

### 4.1 功能目标

完成第一阶段后，静态 Widget 每帧应只执行：

```text
Submit cached DrawNode
```

不再执行：

```text
遍历 UIElement
执行 RenderImpl
生成 Glyph Layout
写 CPU 顶点
写 CPU 索引
重新生成 DrawNode
上传 GPU Buffer
```

当 Widget 中有视觉变化时：

```text
标记 Paint Cache Dirty
重新构建 Paint 数据
上传 GPU Buffer
提交 DrawNode
```

当 Widget 中有布局变化时：

```text
重新 Measure / Arrange
标记 Paint Cache Dirty
重新构建 Paint 数据
上传 GPU Buffer
提交 DrawNode
```

### 4.2 性能目标

在完全静态的 UI 场景中：

- `ui_render_impl_count == 0`。
- `ui_text_layout_count == 0`。
- `ui_generated_vertex_count == 0`。
- `ui_uploaded_bytes == 0`。
- Draw Call 数量保持与缓存前基本一致。
- CPU 只保留 Widget 遍历、缓存状态检查和 Draw Call 提交。

### 4.3 正确性目标

必须保证：

- 绘制顺序不变。
- Material / Texture 切换顺序不变。
- Scissor 行为不变。
- Text Font Page 切换不变。
- Widget Sort Order 不变。
- Hover / Pressed / Focused 状态正确更新。
- AddChild / RemoveChild 后界面正确刷新。
- Visibility 改变后界面正确刷新。
- Resize 后布局和缓存正确更新。
- 多帧资源使用符合 DX12 Frame In Flight 规则。

---

## 5. 非目标

第一阶段不实现以下内容：

- 全局 UI Vertex Arena。
- 每个 UIElement 独立 GPU Buffer。
- RenderTarget Retainer。
- Dirty Rect 局部屏幕重绘。
- DX12 Bundle。
- GPU Driven UI。
- 自动生成最优 Cache Root。
- 跨 Widget 合批。
- 多线程 Paint 构建。

这些能力可以在 Widget 级 Paint Cache 稳定后再评估。

---

## 6. 核心设计原则

### 6.1 RenderImpl 不再表示“每帧绘制”

改造后，`RenderImpl()` 的语义应变为：

> 将当前元素的 Paint 数据写入当前 Paint Builder。

它只在对应 Paint Cache 失效时执行，而不是每帧执行。

### 6.2 Paint Cache 与 Draw Submission 分离

当前流程：

```text
Build + Upload + Submit + Flush
```

目标流程：

```text
Build only when dirty
Upload only when dirty
Submit every visible frame
Reset only before rebuild
```

### 6.3 缓存粒度从 Widget 开始

第一阶段以 Widget 为唯一 Paint Cache Root。

原因：

- 当前已经按 Widget 分配 DrawerBlock。
- Widget 天然拥有输出 RenderTarget。
- Widget 有明确生命周期。
- Widget 有明确 Sort Order。
- 不需要处理跨缓存根的复杂层级顺序。
- 实现风险最低。

### 6.4 不为每个 UIElement 创建 GPU Buffer

每个元素独立 Buffer 会导致：

- Buffer 数量过多。
- Draw Call 增多。
- 合批能力下降。
- GPU 内存碎片。
- Descriptor 和状态切换增多。
- 缓存管理复杂度过高。

第一阶段应保持一个 Widget 对应一个或少量 DrawerBlock。

### 6.5 Layout Dirty 必须隐含 Paint Dirty

布局结果发生变化后，顶点位置、裁剪矩形或文本对齐通常都会变化，因此：

```text
Layout Dirty => Paint Dirty
```

### 6.6 Hierarchy Dirty 必须隐含 Layout 与 Paint Dirty

子节点增删、顺序变化或可见性结构变化会影响：

- Desired Size。
- Arrange 结果。
- Paint 顺序。
- DrawNode 合并结果。
- Scissor 嵌套。

因此：

```text
Hierarchy Dirty => Layout Dirty + Paint Dirty
```

---

## 7. 目标架构

### 7.1 总体流程

```text
UI property changed
        |
        v
InvalidatePaint / Layout / Transform / Hierarchy
        |
        v
Propagate to owning Widget Paint Cache
        |
        v
UIRenderer::Render()
        |
        +-- cache clean --> Submit cached GPU geometry
        |
        +-- cache dirty --> Update layout if needed
                           Reset build data
                           Traverse UI tree
                           Execute RenderImpl
                           Build DrawNode
                           Upload GPU data
                           Clear dirty flags
                           Submit
```

### 7.2 第一阶段对象关系

```text
Widget
├── UIElement Tree
├── UIPaintCache
│   ├── DrawerBlock
│   ├── CPU Build Data
│   ├── GPU Buffers
│   ├── DrawNode List
│   ├── Dirty Flags
│   └── Revision
└── Output RenderTarget
```

---

## 8. 失效类型设计

建议使用位域替代多个松散 `bool`。

```cpp
enum class EUIInvalidationReason : u32
{
    kNone = 0u,
    kPaint = 1u << 0u,
    kLayout = 1u << 1u,
    kTransform = 1u << 2u,
    kHierarchy = 1u << 3u,
    kClip = 1u << 4u,
    kVisibility = 1u << 5u,
    kTextLayout = 1u << 6u
};
```

需要为枚举补充位运算支持。

```cpp
inline EUIInvalidationReason operator|(EUIInvalidationReason lhs, EUIInvalidationReason rhs)
{
    return static_cast<EUIInvalidationReason>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}

inline EUIInvalidationReason &operator|=(EUIInvalidationReason &lhs, EUIInvalidationReason rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

inline bool HasInvalidation(EUIInvalidationReason value, EUIInvalidationReason flag)
{
    return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0u;
}
```

### 8.1 Paint Dirty

适用属性：

- Background。
- Border。
- Corner Radius。
- Color。
- Text Color。
- Texture。
- UV。
- Hover / Pressed / Focused / Disabled 状态。
- Brush。
- Opacity。

行为：

```text
不重新布局
重新构建对应 Paint Cache
```

### 8.2 Layout Dirty

适用属性：

- Padding。
- Margin。
- Size。
- Min Size。
- Font Size。
- Size Policy。
- Fill Rate。
- Alignment。
- 文本内容导致的尺寸变化。
- Widget Size。

行为：

```text
重新 Measure / Arrange
同时标记 Paint Dirty
```

### 8.3 Transform Dirty

第一阶段 Transform 仍然烘焙到顶点，因此：

```text
Transform Dirty => Paint Dirty
```

同时继续更新 `_matrix` 和 `_inv_matrix`。

后续如果 Cache Root Transform 从顶点中解耦，可以将 Cache Root 整体 Transform 改为只更新常量。

### 8.4 Hierarchy Dirty

适用操作：

- AddChild。
- RemoveChild。
- ClearChildren。
- Child Reorder。
- 子节点类型替换。
- Visibility 变化导致 Paint 顺序改变。

行为：

```text
Hierarchy Dirty => Layout Dirty + Paint Dirty
```

### 8.5 Clip Dirty

适用属性：

- Scissor Rect。
- Clip To Bounds。
- Scroll Offset。
- Clip Hierarchy 变化。

第一阶段可直接将 Clip Dirty 视为 Paint Dirty。

### 8.6 Text Layout Dirty

适用属性：

- Text。
- Font。
- Font Size。
- Wrapping Width。
- Letter Spacing。
- Line Spacing。
- DPI Scale。
- Text Direction。
- Shaping 参数。

行为：

```text
重建 TextLayoutCache
同时根据尺寸变化决定是否触发 Layout Dirty
必然触发 Paint Dirty
```

---

## 9. UIElement 改造

### 9.1 新增接口

建议在 `UIElement` 中新增：

```cpp
void InvalidatePaint();
void InvalidateLayout(bool propagate_down = false);
void InvalidateTransform();
void InvalidateHierarchy();
void InvalidateClip();
void InvalidateTextLayout();

void ClearPaintDirtyRecursive();
bool IsPaintDirty() const;
bool IsLayoutDirty() const;
bool IsTransformDirty() const;
```

现有 `InvalidateLayout()` 和 `InvalidateTransform()` 可以保留名称，但需要补充 Paint Cache 传播。

### 9.2 Owning Widget 指针

为了让任意 UIElement 能够快速使 Widget Cache 失效，建议增加：

```cpp
Widget *_owning_widget = nullptr;
```

在以下操作中维护：

- `Widget::AddToWidget()`。
- `UIElement::AddChild()`。
- `UIElement::RemoveChild()`。
- `UIElement::ClearChildren()`。
- Deserialize。
- Reparent。

新增：

```cpp
void SetOwningWidgetRecursive(Widget *widget);
Widget *GetOwningWidget() const;
```

示例：

```cpp
void UIElement::SetOwningWidgetRecursive(Widget *widget)
{
    _owning_widget = widget;

    for (auto &child : _children)
        child->SetOwningWidgetRecursive(widget);
}
```

### 9.3 Paint 失效传播

```cpp
void UIElement::InvalidatePaint()
{
    if (_paint_dirty)
        return;

    _paint_dirty = true;

    if (_owning_widget != nullptr)
        _owning_widget->InvalidatePaint(EUIInvalidationReason::kPaint);
}
```

注意：即使当前节点已经 Paint Dirty，也要确保 Widget Cache 已经 Dirty。
实现时可以让 Widget 的失效操作具备幂等性。

### 9.4 Layout 失效传播

建议保留当前向上传播逻辑，但补充 Paint Dirty。

```cpp
void UIElement::InvalidateLayout(bool propagate_down)
{
    _paint_dirty = true;

    if (_owning_widget != nullptr)
    {
        _owning_widget->InvalidatePaint(
            EUIInvalidationReason::kLayout |
            EUIInvalidationReason::kPaint);
    }

    if (_is_layout_dirty)
        return;

    _is_layout_dirty = true;

    if (propagate_down)
    {
        for (auto &child : _children)
            child->InvalidateLayout(true);
    }
    else if (_parent != nullptr)
    {
        _parent->InvalidateLayout(false);
    }
}
```

后续可以增加 Layout Boundary 停止传播，但第一阶段不要求。

### 9.5 Transform 失效传播

第一阶段 Transform 变化会改变最终顶点，因此：

```cpp
void UIElement::InvalidateTransform()
{
    _paint_dirty = true;

    if (_owning_widget != nullptr)
    {
        _owning_widget->InvalidatePaint(
            EUIInvalidationReason::kTransform |
            EUIInvalidationReason::kPaint);
    }

    if (_is_transf_dirty)
        return;

    _is_transf_dirty = true;

    for (auto &child : _children)
        child->InvalidateTransform();
}
```

### 9.6 状态 Setter 必须触发 Paint

以下 Setter 必须检查值是否真的变化，并触发 Paint：

```cpp
void SetHovered(bool hovered);
void SetPressed(bool pressed);
void SetFocused(bool focused);
void SetInteractiveEnabled(bool enabled);
void SetStateVisible(bool visible);
void SetWantsMouseEvents(bool enabled);
```

示例：

```cpp
void UIElement::SetHovered(bool hovered)
{
    if (IsHovered() == hovered)
        return;

    SetStateFlag(EUIElementState::kHovered, hovered);
    InvalidatePaint();
}
```

不建议继续让状态 Setter 只修改 `_state_flags`。

### 9.7 Visible 状态统一

当前同时存在：

```text
_visibility
_is_visible
EUIElementState::kVisible
```

这会增加缓存失效遗漏风险。

建议至少明确三者职责：

- `_visibility`：序列化属性。
- `_is_visible`：运行时最终可见状态。
- `EUIElementState::kVisible`：不要与 `_is_visible` 重复，或者明确只表示交互状态。

推荐最终只保留一个运行时可见状态来源。

`SetVisible()` 必须：

```text
更新可见状态
InvalidateHierarchy
InvalidateLayout
InvalidatePaint
```

如果隐藏元素不参与布局，应使用 Hierarchy + Layout。
如果隐藏元素只不绘制但仍占布局，应只使用 Paint。

建议后续将可见性扩展为：

```cpp
enum class EVisibility
{
    kVisible,
    kHidden,
    kCollapsed
};
```

含义：

- `kVisible`：参与布局并绘制。
- `kHidden`：参与布局但不绘制。
- `kCollapsed`：不参与布局也不绘制。

---

## 10. Widget Paint Cache

### 10.1 数据结构

建议新增：

```cpp
struct UIPaintCache
{
    DrawerBlock *_drawer_block = nullptr;

    EUIInvalidationReason _dirty_reasons = EUIInvalidationReason::kPaint;

    u64 _build_revision = 0u;
    u64 _upload_revision = 0u;

    bool _is_building = false;
    bool _is_valid = false;
};
```

如果继续使用多帧 DrawerBlock，则可以使用：

```cpp
struct UIPaintCache
{
    Array<DrawerBlock *, Render::RenderConstants::kFrameCount> _frame_blocks;
    EUIInvalidationReason _dirty_reasons = EUIInvalidationReason::kPaint;
    u64 _build_revision = 0u;
    bool _is_valid = false;
};
```

但需要注意：如果每帧 Buffer 独立，那么缓存构建结果必须同步复制到所有 Frame Block，
或者改为持久 GPU Buffer 并由底层 RHI 正确管理资源更新。

更推荐的第一版是：

- CPU Paint Cache 只保留一份。
- 每帧 GPU Buffer 需要时从 CPU Cache 上传。
- 所有 Frame Buffer 都上传完成后，静态帧停止上传。

可增加每帧上传 Revision：

```cpp
struct UIPaintCache
{
    Scope<DrawerBlock> _cpu_block;

    Array<DrawerBlock *, Render::RenderConstants::kFrameCount> _gpu_blocks;
    Array<u64, Render::RenderConstants::kFrameCount> _gpu_revisions;

    EUIInvalidationReason _dirty_reasons = EUIInvalidationReason::kPaint;
    u64 _build_revision = 1u;
    bool _is_valid = false;
};
```

这里需要根据 AiluEngine 当前 `VertexBuffer::SetData()` 的底层实现决定最终结构。

### 10.2 Widget 新增接口

```cpp
void InvalidatePaint(EUIInvalidationReason reason);
bool IsPaintCacheDirty() const;
void RebuildPaintCache(UIRenderer &renderer);
void SubmitPaintCache(UIRenderer &renderer, Render::CommandBuffer *cmd);
void ClearPaintInvalidation();
```

### 10.3 Widget::InvalidatePaint

```cpp
void Widget::InvalidatePaint(EUIInvalidationReason reason)
{
    _paint_cache._dirty_reasons |= reason;
    _paint_cache._is_valid = false;
}
```

### 10.4 Widget::AddToWidget

```cpp
void Widget::AddToWidget(Ref<UIElement> root)
{
    if (_root != nullptr)
        LOG_WARNING("Widget::AddToWidget replaces existing root");

    _root = std::move(root);

    if (_root != nullptr)
    {
        _root->SetOwningWidgetRecursive(this);
        _root->Translate(_position);
        _root->Arrange(0.0f, 0.0f, _size.x, _size.y);
    }

    InvalidatePaint(
        EUIInvalidationReason::kHierarchy |
        EUIInvalidationReason::kLayout |
        EUIInvalidationReason::kPaint);
}
```

### 10.5 Widget::SetSize

Widget Size 变化必须使布局和 Paint Cache 失效。

```cpp
void Widget::SetSize(Vector2f size)
{
    if (NearbyEqual(size, _size))
        return;

    _size = size;

    if (_root != nullptr)
        _root->Arrange(0.0f, 0.0f, _size.x, _size.y);

    InvalidatePaint(
        EUIInvalidationReason::kLayout |
        EUIInvalidationReason::kPaint);
}
```

### 10.6 Widget::SetPosition

第一阶段位置仍然烘焙到顶点，因此：

```cpp
void Widget::SetPosition(Vector2f position)
{
    if (NearbyEqual(position, _position))
        return;

    _position = position;

    if (_root != nullptr)
        _root->Translate(position);

    InvalidatePaint(
        EUIInvalidationReason::kTransform |
        EUIInvalidationReason::kPaint);
}
```

---

## 11. UIRenderer 改造

### 11.1 当前职责拆分

当前 `UIRenderer::Render()` 同时负责：

- UI 生命周期更新。
- UI Tree Paint。
- DrawerBlock 构建。
- GPU Upload。
- Draw Submission。
- Flush。

应拆分为：

```text
Update UI
Build dirty Paint Cache
Upload dirty GPU data
Submit cached DrawNode
```

### 11.2 建议新增接口

```cpp
void BeginPaintBuild(Widget *widget, DrawerBlock *block);
void EndPaintBuild(Widget *widget, DrawerBlock *block);

void UploadBlockIfDirty(DrawerBlock *block);
void SubmitBlock(
    DrawerBlock *block,
    Render::CommandBuffer *cmd,
    Render::RenderTexture *color,
    Render::RenderTexture *depth = nullptr);
```

`SubmitBlock()` 不再调用 `Flush()`。

### 11.3 Build Context

为了让现有 `DrawQuad()`、`DrawText()` 等接口继续使用，Renderer 应持有当前构建目标：

```cpp
Widget *_building_widget = nullptr;
DrawerBlock *_building_block = nullptr;
bool _is_building_paint_cache = false;
```

`GetAvailableBlock()` 第一阶段可以直接返回当前 Widget Cache Block。

如果一个 Widget 可能超过单个 DrawerBlock 容量，应让 Paint Cache 持有多个 Block：

```cpp
Vector<DrawerBlock *> _blocks;
```

不要再使用 `_cur_widget_index` 隐式映射，建议让 `Widget` 直接拥有自己的 Cache。

### 11.4 Render 主流程

建议修改为：

```cpp
void UIRenderer::Render(CommandBuffer *cmd)
{
    const f32 dt = TimeMgr::s_delta_time;
    auto &widgets = UIManager::Get()->_widgets;

    for (auto &widget : widgets)
    {
        if (widget->_visibility != EVisibility::kVisible)
            continue;

        widget->PreUpdate(dt);
    }

    for (auto &widget : widgets)
    {
        if (widget->_visibility != EVisibility::kVisible)
            continue;

        widget->Update(dt);
    }

    DragDropManager::Get().Update();

    for (auto &widget : widgets)
    {
        if (widget->_visibility != EVisibility::kVisible)
            continue;

        if (widget->IsPaintCacheDirty())
            widget->RebuildPaintCache(*this);

        auto [color, depth] = widget->GetOutput();
        widget->SubmitPaintCache(*this, cmd, color, depth);
    }

    SubmitGlobalUi(cmd);
    SubmitWindowUi(cmd);
}
```

### 11.5 Debug Highlight

当前 Debug Highlight 是全局动态绘制内容。

不要让它强制所有 Widget Cache 失效。

建议将 Debug Highlight 放入：

```text
Global Volatile Layer
```

每帧单独重建并提交。

同样适用于：

- Drag Preview。
- Mouse Cursor。
- Debug Rect。
- 临时辅助线。
- UI Reflector Highlight。

---

## 12. DrawerBlock 改造

### 12.1 拆分 Reset、Upload 和 Submit

当前 `Flush()` 语义过重。

建议替换为：

```cpp
void ResetBuildData();
void MarkGpuDirty();
void UploadIfDirty();
void ClearGpuDirty();

bool IsGpuDirty() const;
bool HasDrawNodes() const;
```

示例：

```cpp
void DrawerBlock::ResetBuildData()
{
    _cur_vert_num = 0u;
    _cur_index_num = 0u;
    _nodes.clear();
    _gpu_dirty = true;
}
```

```cpp
void DrawerBlock::UploadIfDirty()
{
    if (!_gpu_dirty)
        return;

    SubmitVertexData();
    _gpu_dirty = false;
}
```

### 12.2 保留构建结果

构建结束后不应清空：

```text
_pos_buf
_uv_buf
_color_buf
_rect_buf
_corner_radius_buf
_index_buf
_nodes
_cur_vert_num
_cur_index_num
```

只有下一次缓存重建之前才调用 `ResetBuildData()`。

### 12.3 GPU Dirty 字段

新增：

```cpp
bool _gpu_dirty = true;
u64 _build_revision = 0u;
u64 _upload_revision = 0u;
```

### 12.4 容量增长

当前超出容量会 Assert。

缓存系统下，Widget 内容可能发生结构增长，建议支持扩容。

```cpp
bool EnsureCapacity(u32 required_vert_num, u32 required_index_num);
```

扩容策略：

```text
new_capacity = max(required_capacity, old_capacity * 2)
```

注意当前 `_max_vert_num` 同时被用于顶点和索引容量判断，这不够严谨。

建议拆分：

```cpp
u32 _max_vert_num = 0u;
u32 _max_index_num = 0u;
```

`CanAppend()` 改为：

```cpp
bool DrawerBlock::CanAppend(u32 vert_num, u32 index_num) const
{
    return _cur_vert_num + vert_num <= _max_vert_num &&
           _cur_index_num + index_num <= _max_index_num;
}
```

当前代码使用 `<`，会浪费最后一个合法位置，应改为 `<=`。

### 12.5 Index Buffer 初始容量

Quad 的顶点与索引比例通常是：

```text
4 vertices : 6 indices
```

Index Buffer 不应只按 `vert_num` 创建。

构造函数应明确传入：

```cpp
DrawerBlock(
    Ref<Render::Material> material,
    u32 max_vert_num,
    u32 max_index_num);
```

---

## 13. 多帧 GPU 资源处理

AiluEngine 当前使用：

```cpp
Array<Vector<DrawerBlock *>, Render::RenderConstants::kFrameCount>
```

说明底层可能按 Frame In Flight 分离动态 Buffer。

引入缓存后必须明确 GPU Buffer 所属策略。

### 13.1 推荐方案 A：每帧 GPU Buffer + Revision

CPU Paint Cache 构建一次。

每个 Frame Index 对应一个 GPU DrawerBlock：

```text
frame 0 buffer revision
frame 1 buffer revision
frame 2 buffer revision
```

Paint Cache 重建后：

```text
build_revision++
```

提交某个 Frame Index 时：

```cpp
if (_gpu_revisions[frame_index] != _build_revision)
{
    UploadCpuCacheToFrameBlock(frame_index);
    _gpu_revisions[frame_index] = _build_revision;
}
```

这意味着缓存变化后，接下来的 `kFrameCount` 帧可能各上传一次。
所有帧资源同步完成后，后续静态帧不再上传。

该方案与现有多帧结构兼容，风险较低。

### 13.2 方案 B：真正持久 Default Heap Buffer

如果 RHI 支持：

- Default Heap Vertex Buffer。
- Copy Queue 或 Upload Buffer 更新。
- 正确的 Fence 生命周期。
- Buffer 内容跨帧持久。

则每个 Widget 可以只保留一份 GPU Buffer。

该方案长期更合理，但第一阶段是否采用取决于当前 RHI 能力。

### 13.3 不允许的实现

不要让缓存命中帧仍然执行：

```cpp
_vbuf->SetData(...);
_ibuf->SetData(...);
```

否则只能节省 CPU Paint 构建，不能节省上传。

---

## 14. TextLayoutCache

### 14.1 数据结构

建议在 `Text` 中增加：

```cpp
struct TextLayoutCache
{
    Vector<GlyphLayout> _glyphs;

    Vector2f _size = Vector2f::kZero;
    Vector4f _visual_bounds = Vector4f::kZero;

    Render::Font *_font = nullptr;
    f32 _font_size = 0.0f;
    Vector2f _scale = Vector2f::kOne;

    u64 _revision = 0u;
    bool _dirty = true;
};
```

如果 `GlyphLayout` 当前位于 Text Layout 模块内部，应将可复用结果类型公开或增加缓存专用结构。

### 14.2 Text::UpdateTextLayout

当前 `UpdateTextLayout()` 会分别调用：

```text
CalculateTextSize()
CalculateTextVisualBounds()
```

这可能导致两次 `LayoutText()`。

建议改为一次：

```cpp
void Text::UpdateTextLayout()
{
    const TextLayoutResult layout = TextRenderer::BuildLayout(
        _text,
        Vector2f::kZero,
        _font_size,
        Vector2f::kOne,
        Vector2f::kZero,
        _font);

    _text_layout_cache._glyphs = layout._glyphs;
    _text_layout_cache._size = layout._size;
    _text_layout_cache._visual_bounds = CalculateVisualBounds(layout);
    _text_layout_cache._dirty = false;

    const Vector2f new_size = layout._size + Vector2f(
        _padding._l + _padding._r,
        _padding._t + _padding._b);

    const Vector2f delta = Abs(new_size - _text_size);
    const f32 tolerance = std::max(1.0f, _font_size * 0.1f);

    if (delta.x > tolerance || delta.y > tolerance)
    {
        _text_size = new_size;
        InvalidateLayout();
    }
    else
    {
        InvalidatePaint();
    }
}
```

### 14.3 TextRenderer 新接口

```cpp
TextLayoutResult BuildLayout(
    const String &text,
    Vector2f position,
    f32 font_size,
    Vector2f scale,
    Vector2f padding,
    Render::Font *font);

void AppendTextLayout(
    const TextLayoutResult &layout,
    Matrix4x4f matrix,
    Color color,
    Render::Font *font,
    DrawerBlock *block);
```

这样 Paint 重建时可以直接使用缓存 Glyph，而不重新 Shaping/Layout。

### 14.4 仅颜色变化

文本颜色变化时：

```text
TextLayoutCache 保留
Paint Cache 失效
```

不能重新执行 Text Layout。

### 14.5 Font Atlas Revision

如果字体图集支持动态扩容或运行时加入 Glyph，需要在缓存中记录：

```cpp
u64 _font_atlas_revision = 0u;
```

Atlas Revision 变化时，Glyph UV 可能失效，必须重建 TextLayoutCache 或至少更新 UV。

---

## 15. Button Visual State

当前 Button 背景通过 `GetCurrentVisual()` 动态选择，但 Text Color 在 `ResolveStyle()` 中写入子 Text。

这会产生问题：

- Hover 状态变化后背景可以切换。
- Text Color 可能仍然停留在旧状态。
- Style Resolution 不应该因为 Hover 每帧重新执行。

建议将 Theme Style Resolution 与 Visual State Resolution 分离。

### 15.1 Style Resolution

只在以下变化时执行：

- Theme Revision。
- Style Id。
- Local Style Override。
- Theme Asset 变化。

生成：

```text
_resolved_style
```

### 15.2 Visual State Resolution

每次 Paint 构建时根据当前状态选择：

```cpp
const UIControlVisual *visual = GetVisual(GetVisualState());
```

Button 子 Text 的颜色有两种实现方式。

#### 方案 A：Paint Context 传递 Content Color

推荐。

Button 在绘制子 Text 前设置临时 Paint Context：

```cpp
UIPaintContext context;
context._content_color = visual->_content_color;
```

Text 优先使用继承的 Content Color。

#### 方案 B：状态变化时设置 Text Color

实现简单，但会增加父子耦合。

```cpp
void Button::OnVisualStateChanged()
{
    if (_text != nullptr)
        _text->SetColor(GetCurrentVisual()->_content_color);

    InvalidatePaint();
}
```

第一阶段可以使用方案 B，后续再增加 Paint Context。

---

## 16. Scissor 与裁剪缓存

当前 `PushScissor()` / `PopScissor()` 会把 Scissor 写入 DrawNode。

这部分可以直接缓存，但必须保证：

- Paint 重建时 Scissor Stack 为空。
- Paint 构建结束时 Scissor Stack 为空。
- 缓存的 DrawNode 保留 Scissor。
- Submit 时只读取缓存的 DrawNode。
- Cache Root 不能在不重建 DrawNode 的情况下改变内部 Scissor。

建议增加断言：

```cpp
void UIRenderer::BeginPaintBuild(...)
{
    AL_ASSERT(_scissor_stack.empty());
}
```

```cpp
void UIRenderer::EndPaintBuild(...)
{
    AL_ASSERT(_scissor_stack.empty());
}
```

如果 Widget Resize、Scroll Offset 或 Clip Rect 变化，应触发 Clip Dirty。

---

## 17. DrawNode 合批规则

当前相邻 DrawNode 在以下条件一致时合并：

- Material。
- Texture。
- Custom Scissor 状态。
- Scissor Rect。
- MSDF Px Range。

缓存后必须保持完全相同的合并规则。

不要在第一阶段重新排序 DrawNode 以追求更强合批，因为 UI 通常依赖严格绘制顺序。

只有相邻且状态完全一致的 DrawNode 可以合并。

后续 Bindless UI 可以减少 Texture 切换，但不属于本次范围。

---

## 18. Volatile 元素

对于每帧变化的 UI，不应让静态 Widget Cache 每帧全部重建。

第二阶段建议增加：

```cpp
enum class EUICachePolicy
{
    kAuto,
    kCached,
    kVolatile
};
```

`UIElement` 新增：

```cpp
EUICachePolicy _cache_policy = EUICachePolicy::kAuto;
```

### 18.1 Volatile 典型对象

- 鼠标光标。
- 输入框光标。
- 拖拽预览。
- 高频动画。
- 实时波形。
- 实时曲线。
- 高频滚动层。
- Debug Overlay。

### 18.2 第一阶段处理

第一阶段不实现子树级缓存时，可以：

- 将高频动态内容放到独立 Widget。
- 将 Debug 和 DragDrop 放到 Global Volatile Block。
- 由调用层主动拆分静态 Widget 与动态 Widget。

### 18.3 第二阶段处理

增加 `UIPaintCacheRoot`：

```cpp
class UIPaintCacheRoot : public UIElement
{
public:
    void InvalidateCache(EUIInvalidationReason reason);

protected:
    UIPaintCache _paint_cache;
};
```

失效向最近的 Cache Root 传播，而不是始终传播到 Widget。

---

## 19. Transform 与几何解耦

当前 Quad 与 Text 都在 CPU 上将矩阵乘到每个顶点。

这意味着：

- 元素平移会重建几何。
- Widget 整体移动会重建所有子节点。
- Cache Root 整体缩放会重建所有顶点。

### 19.1 第一阶段

保持当前行为：

```text
Transform Dirty => Paint Dirty
```

优先保证正确性。

### 19.2 后续优化

缓存局部空间顶点，并给 Cache Root 提交一个整体 Transform：

```cpp
struct UIDrawCommand
{
    u32 _index_offset = 0u;
    u32 _index_count = 0u;

    Render::Material *_material = nullptr;
    Render::Texture *_texture = nullptr;

    Rect _scissor;
    Matrix4x4f _root_transform = Matrix4x4f::Identity();

    f32 _msdf_px_range = 0.0f;
};
```

不要给每个 UIElement 单独 ConstantBuffer，否则会破坏合批。

合理粒度是：

```text
一个 Paint Cache Root 一个整体 Transform
```

---

## 20. 属性 Setter 失效审计

实现缓存前必须审计所有会影响 UI 的 Setter。

### 20.1 UIElement

| 属性或操作 | 失效类型 |
|---|---|
| SetVisible | Visibility + Hierarchy 或 Paint |
| SetHovered | Paint |
| SetPressed | Paint |
| SetFocused | Paint |
| SetInteractiveEnabled | Paint |
| Translate | Transform + Paint |
| Rotate | Transform + Paint |
| Scale | Transform + Paint |
| SetSlot | Layout + Paint |
| SlotPadding | Layout + Paint |
| AddChild | Hierarchy + Layout + Paint |
| RemoveChild | Hierarchy + Layout + Paint |
| ClearChildren | Hierarchy + Layout + Paint |

### 20.2 Text

| 属性或操作 | 失效类型 |
|---|---|
| SetText | TextLayout + Paint，必要时 Layout |
| FontSize | TextLayout + Layout + Paint |
| SetFont | TextLayout + Layout + Paint |
| SetColor | Paint |
| Horizontal Align | Paint，尺寸策略改变时 Layout |
| Vertical Align | Paint |
| Wrapping Width | TextLayout + Layout + Paint |
| Padding | TextLayout 或 Layout + Paint |

### 20.3 Image

| 属性或操作 | 失效类型 |
|---|---|
| SetTexture | Paint |
| SetTint | Paint |
| SetUvRect | Paint |
| SetBrush | Paint |
| Size To Content + Texture | Layout + Paint |

### 20.4 Button

| 属性或操作 | 失效类型 |
|---|---|
| SetStyleId | Style + Layout + Paint |
| Style Override Visual | Paint |
| Style Override Padding | Layout + Paint |
| SetText | Child Text 负责失效 |
| SetTexture | Child Image 负责失效 |
| Hover / Pressed / Focused | Paint |

### 20.5 Container

| 属性或操作 | 失效类型 |
|---|---|
| Orientation | Layout + Paint |
| Padding | Layout + Paint |
| Child Slot Margin | Layout + Paint |
| Size Policy | Layout + Paint |
| Fill Rate | Layout + Paint |
| Cross Align | Layout + Paint |
| Child Reorder | Hierarchy + Layout + Paint |
| Scroll Offset | Clip + Paint |

---

## 21. 修改文件清单

### 21.1 `Engine/Inc/UI/UIElement.h`

需要：

- 增加 `EUIInvalidationReason`。
- 增加 `_owning_widget`。
- 增加 `InvalidatePaint()`。
- 增加 `InvalidateHierarchy()`。
- 增加 `InvalidateClip()`。
- 增加 `SetOwningWidgetRecursive()`。
- 增加 Paint Dirty 查询和清理接口。
- 统一状态 Setter 的失效行为。
- 明确 Visibility 字段职责。

### 21.2 `Engine/Src/UI/UIElement.cpp`

需要：

- 实现 Owning Widget 传播。
- 在 AddChild / RemoveChild / ClearChildren 中维护 Owning Widget。
- 补全 Paint / Layout / Transform / Hierarchy 失效传播。
- 状态变化时触发 Paint。
- Render 构建完成后由 Cache Root 统一清理 Paint Dirty。
- 修复缓存启用后暴露的 Setter 漏失效问题。

### 21.3 `Engine/Inc/UI/Widget.h`

需要：

- 增加 `UIPaintCache`。
- 增加 `InvalidatePaint()`。
- 增加 `IsPaintCacheDirty()`。
- 增加 `RebuildPaintCache()`。
- 增加 `SubmitPaintCache()`。
- Widget 生命周期负责创建和销毁 Cache Block。

### 21.4 `Engine/Src/UI/Widget.cpp`

需要：

- `AddToWidget()` 设置 Owning Widget。
- `SetSize()` 触发 Layout + Paint。
- `SetPosition()` 触发 Transform + Paint。
- `Render()` 改为 Dirty Build + Cached Submit。
- Deserialize 后恢复 Owning Widget。
- Root 替换时释放旧 Cache 关联。

### 21.5 `Engine/Inc/UI/UIRenderer.h`

需要：

- 增加 Paint Build Context。
- 增加 Begin / End Paint Build。
- 拆分 Upload 与 Submit。
- 移除依赖 `_cur_widget_index` 的隐式缓存映射。
- 增加 Global Volatile Block。
- 增加统计接口。

### 21.6 `Engine/Src/UI/UIRenderer.cpp`

需要：

- `Render()` 使用 Widget Paint Cache。
- `SubmitBlock()` 不再 Flush。
- Debug Highlight 移到 Volatile Block。
- Paint Build 前后检查 Scissor Stack。
- Cache Dirty 时才执行 Render Tree。
- GPU Dirty 时才上传。
- Cache Clean 时只提交 DrawNode。

### 21.7 `Engine/Inc/UI/DrawerBlock.h`

需要：

- `Flush()` 拆为 `ResetBuildData()`。
- 增加 `_gpu_dirty`。
- 增加 Revision。
- 增加独立顶点和索引容量。
- 增加扩容接口。
- 增加只读 DrawNode 提交数据访问。

### 21.8 `Engine/Src/UI/DrawerBlock.cpp`

需要：

- 实现 Dirty Upload。
- 修复 Buffer 容量。
- 支持 Resize 或重建 GPU Buffer。
- `SubmitVertexData()` 只在 GPU Dirty 时调用。
- 保证资源更新满足多帧 RHI 规则。

### 21.9 `Engine/Inc/UI/TextRenderer.h`

需要：

- 暴露 `TextLayoutResult`。
- 增加 `BuildLayout()`。
- 增加 `AppendTextLayout()`。
- 增加 Text Layout 统计。

### 21.10 `Engine/Src/UI/TextRenderer.cpp`

需要：

- 避免一次文字更新重复 Layout。
- Paint 时使用缓存 Glyph。
- Font Atlas Revision 变化时正确失效。
- 保持 Bitmap Font 与 MSDF Font DrawNode 行为一致。

### 21.11 `Engine/Src/UI/Basic.cpp`

需要：

- Text 引入 `TextLayoutCache`。
- Button 状态变化时正确更新 Content Color。
- Image Setter 补全 Paint Invalidation。
- Slider 等动态控件只在值变化时失效。
- 所有直接写成员变量的位置改为 Setter 或显式 Invalidate。

---

## 22. 第一阶段实施步骤

### 阶段 0：加入统计，不改变行为

新增：

```text
ui_element_visit_count
ui_render_impl_count
ui_layout_count
ui_text_layout_count
ui_generated_vertex_count
ui_generated_index_count
ui_uploaded_bytes
ui_draw_node_count
ui_draw_call_count
ui_cache_hit_count
ui_cache_miss_count
ui_paint_build_time
ui_gpu_upload_time
ui_submit_time
```

验收：

- 能在 UI Debug Panel 中查看。
- 能确认当前静态帧仍在持续生成和上传。

### 阶段 1：DrawerBlock 持久化

改造：

- `Flush()` 改为 `ResetBuildData()`。
- `SubmitBlock()` 不再清空。
- 加入 GPU Dirty。
- Cache Clean 时跳过 `SubmitVertexData()`。

暂时仍可每帧 Build，先验证持久数据不会破坏绘制。

验收：

- 不 Reset 时能够连续多帧正确提交同一批数据。
- Scissor、Texture 和 Text 均正确。

### 阶段 2：Widget 级 Paint Dirty

改造：

- Widget 拥有 Paint Cache。
- UIElement 失效传播到 Widget。
- Widget Dirty 时才遍历 UI Tree。
- Widget Clean 时直接提交缓存。

验收：

- 静态 Widget 的 `ui_render_impl_count` 归零。
- Hover 一个 Button 时只重建其所在 Widget。
- AddChild 后正确重建。
- Resize 后正确重建。

### 阶段 3：TextLayoutCache

改造：

- Text 内容变化时构建 Layout Cache。
- Paint Build 使用缓存 Glyph。
- 颜色变化不重新 Layout。

验收：

- 静态文本 `ui_text_layout_count == 0`。
- Text Color 动画只重建 Paint，不重新 Layout。
- Text 改变后尺寸和视觉正确。

### 阶段 4：多帧 GPU Revision

改造：

- 每个 Frame Buffer 记录上传 Revision。
- 缓存变化后只在必要帧上传。
- 所有 Frame Buffer 同步后停止上传。

验收：

- 静态 UI 的 `ui_uploaded_bytes == 0`。
- 更新后最多在 `kFrameCount` 个帧资源上发生必要上传。
- 不出现 DX12 资源仍在使用时修改的问题。

### 阶段 5：Volatile Layer

改造：

- Debug Highlight。
- DragDrop Preview。
- Cursor。
- 高频动画控件。

从静态 Widget Cache 中分离。

验收：

- 动态 Overlay 每帧更新。
- 静态 Widget Cache 仍保持命中。

---

## 23. 第二阶段：子树 Paint Cache Root

Widget 级缓存稳定后，再实现子树缓存。

### 23.1 新增控件

```cpp
class UIPaintCacheRoot : public UIElement
{
public:
    void SetCachePolicy(EUICachePolicy policy);
    void InvalidateCache(EUIInvalidationReason reason);

protected:
    UIPaintCache _paint_cache;
    EUICachePolicy _cache_policy = EUICachePolicy::kAuto;
};
```

### 23.2 失效传播

UIElement 失效时向上查找最近的 Cache Root：

```text
UIElement
   |
   v
nearest UIPaintCacheRoot
   |
   v
Widget fallback
```

### 23.3 Cache Root 边界约束

Cache Root 必须保证：

- 内部绘制顺序保持。
- 外部父节点不能插入到 Cache Root 内部顺序中。
- Scissor 边界明确。
- Transform 边界明确。
- Cache Root 不能破坏透明混合顺序。

---

## 24. 风险与规避

### 24.1 Setter 漏失效

风险最高。

规避方式：

- 禁止外部直接修改影响 UI 的 Public Member。
- 逐步改为 Setter。
- Reflection 修改后统一进入 `OnPropertyChanged()`。
- 增加 Debug 模式强制每 N 帧重建并对比结果。
- 增加 Cache On / Off 开关。

### 24.2 多帧 Buffer 被 GPU 使用时修改

规避方式：

- 使用每帧 GPU Buffer。
- 记录 Frame Revision。
- 遵守 Fence。
- 不直接覆写仍在 Flight 的 Buffer。

### 24.3 DrawNode 中保存裸指针失效

当前 DrawNode 保存：

```text
Material *
Texture *
```

缓存生命周期变长后，需要保证资源在 Cache 存活期间有效。

建议：

- Cache 持有 `Ref<Material>` 与 `Ref<Texture>`。
- 或 DrawNode 保存稳定 Resource Handle。
- Resource Reload 时使相关 Paint Cache 失效。

### 24.4 Font Atlas 更新导致 UV 失效

规避方式：

- Font Atlas 增加 Revision。
- TextLayoutCache 保存 Atlas Revision。
- Revision 不一致时重新 Layout 或更新 UV。

### 24.5 Widget Sort Order 变化

Widget Cache 内容本身无需重建，但全局提交顺序必须更新。

因此：

```text
Sort Order Dirty != Paint Dirty
```

UIManager 重新排序 Widget 即可。

### 24.6 RenderTarget Resize

输出尺寸变化可能影响：

- Projection。
- Root Arrange。
- Clip Rect。
- Pixel Alignment。

必须触发：

```text
Layout + Clip + Paint
```

### 24.7 透明混合顺序

不要跨 Cache Root 或跨 Widget 自动排序 Material / Texture。
UI 绘制顺序优先于合批收益。

---

## 25. 调试能力

建议在 UI Debug Panel 增加：

```text
Widget Name
Cache Valid
Dirty Reasons
Build Revision
GPU Revision Per Frame
Vertex Count
Index Count
DrawNode Count
Upload Bytes
Last Build Time
Cache Hit Count
Cache Miss Count
```

增加调试选项：

```cpp
bool s_force_rebuild_ui_cache = false;
bool s_disable_ui_paint_cache = false;
bool s_draw_ui_cache_bounds = false;
bool s_log_ui_invalidation = false;
```

常量命名应遵循项目代码风格，例如：

```cpp
inline static constexpr bool kEnableUiCacheValidation = true;
```

---

## 26. 测试计划

### 26.1 静态界面

场景：

- 多个 Panel。
- 多个 Button。
- 多段静态文本。
- 多张纹理。
- 多层 Scissor。

验证：

- 第一帧构建。
- 后续帧无 Build。
- 后续帧无 Upload。
- 绘制结果不变。

### 26.2 Hover / Pressed

验证：

- Hover 后背景和文字颜色正确。
- Pressed 后视觉正确。
- Mouse Exit 后恢复。
- 每次状态变化仅触发一次 Cache Rebuild。

### 26.3 文本更新

验证：

- 修改文本后 Glyph 更新。
- Desired Size 正确变化。
- 父布局重新 Arrange。
- 颜色变化不执行 Text Layout。
- Font Size 变化执行 Text Layout 和 Layout。

### 26.4 Hierarchy

验证：

- AddChild。
- RemoveChild。
- ClearChildren。
- Reparent。
- Deserialize。
- Child Reorder。

### 26.5 Transform

验证：

- Translate。
- Rotate。
- Scale。
- 父 Transform 变化。
- 子 Transform 变化。
- HitTest 矩阵同步正确。

### 26.6 Visibility

验证：

- Visible。
- Hidden。
- Collapsed，如果已实现。
- 父隐藏。
- 子隐藏。
- 隐藏后 HitTest 行为正确。

### 26.7 Resize

验证：

- Widget Resize。
- Window Resize。
- RenderTarget Resize。
- DPI Scale 改变。
- Scissor 正确更新。

### 26.8 多帧资源

验证：

- `kFrameCount` 个 Frame Buffer Revision 正确。
- 不出现 DX12 Debug Layer 报错。
- 不出现资源仍在 GPU 使用时修改。
- 连续快速修改 UI 不出现旧帧数据。

### 26.9 容量扩展

验证：

- 运行时增加大量元素。
- 顶点超过初始容量。
- 索引超过初始容量。
- Buffer 扩容后数据正确。
- 无越界和悬挂资源。

---

## 27. 验收标准

第一阶段完成后必须满足：

1. 静态 Widget 只在首次显示时构建 Paint Cache。
2. 静态 Widget 后续帧不遍历 UI Tree。
3. 静态文本后续帧不执行 `LayoutText()`。
4. 静态 Widget 后续帧不调用 `VertexBuffer::SetData()`。
5. 静态 Widget 后续帧不调用 `IndexBuffer::SetData()`。
6. Hover、Pressed、Focused 状态变化后视觉正确。
7. Text、Texture、Color、Brush 变化后视觉正确。
8. AddChild、RemoveChild、Resize 后布局与绘制正确。
9. Scissor 和绘制顺序与改造前一致。
10. 不增加无意义的 Draw Call。
11. DX12 Debug Layer 无新增错误。
12. 可以通过配置关闭 Paint Cache，回退到原始全量构建路径。
13. UI Debug Panel 能显示缓存命中、重建和上传统计。
14. 多帧 Buffer 在缓存变化后正确同步。
15. Widget 销毁后不遗留 GPU Buffer、资源引用或事件回调。

---

## 28. 推荐实现顺序

Claude 实现时严格按以下顺序进行：

```text
1. 添加统计
2. 拆分 DrawerBlock Reset / Upload / Submit
3. 让同一 DrawerBlock 可以跨帧重复 Submit
4. 增加 Widget Paint Cache
5. 增加 UIElement -> Widget 失效传播
6. 接通 Paint Dirty
7. 审计所有状态 Setter
8. 审计所有视觉属性 Setter
9. 审计 Hierarchy 与 Visibility
10. 增加 TextLayoutCache
11. 处理多帧 GPU Revision
12. 分离 Global Volatile Layer
13. 增加缓存开关和 Debug Panel
14. 完成测试
15. 再考虑 UIPaintCacheRoot
```

不要同时实现以下高级功能：

```text
全局 Geometry Arena
RenderTarget Retainer
DX12 Bundle
多线程 Paint
自动 Cache Root
跨 Cache Root 重排序
```

先保证 Widget 级缓存正确、稳定、可调试。

---

## 29. 最小可行改造伪代码

### 29.1 Widget

```cpp
void Widget::Render(UIRenderer &renderer, Render::CommandBuffer *cmd)
{
    EnsureRenderTarget();

    if (IsPaintCacheDirty())
    {
        RebuildPaintCache(renderer);
        ++_paint_cache_miss_count;
    }
    else
    {
        ++_paint_cache_hit_count;
    }

    auto [color, depth] = GetOutput();
    renderer.SubmitCachedBlocks(_paint_cache, cmd, color, depth);
}
```

### 29.2 RebuildPaintCache

```cpp
void Widget::RebuildPaintCache(UIRenderer &renderer)
{
    _paint_cache.ResetBuildData();

    renderer.BeginPaintBuild(this, &_paint_cache);

    if (_root != nullptr)
        _root->Render(renderer);

    renderer.EndPaintBuild(this, &_paint_cache);

    ++_paint_cache._build_revision;
    _paint_cache._is_valid = true;
    _paint_cache._dirty_reasons = EUIInvalidationReason::kNone;

    if (_root != nullptr)
        _root->ClearPaintDirtyRecursive();
}
```

### 29.3 Submit

```cpp
void UIRenderer::SubmitCachedBlocks(
    UIPaintCache &cache,
    CommandBuffer *cmd,
    RenderTexture *color,
    RenderTexture *depth)
{
    const u16 frame_index = GetCurrentFrameIndex();

    for (DrawerBlock *block : cache.GetBlocks())
    {
        if (block == nullptr || !block->HasDrawNodes())
            continue;

        block->UploadFrameIfRevisionChanged(frame_index, cache._build_revision);
        SubmitBlock(block, frame_index, cmd, color, depth);
    }
}
```

### 29.4 Paint Invalidation

```cpp
void UIElement::InvalidatePaint()
{
    _paint_dirty = true;

    if (_owning_widget != nullptr)
        _owning_widget->InvalidatePaint(EUIInvalidationReason::kPaint);
}
```

---

## 30. 最终架构方向

最终推荐架构：

```text
UIElement Retained Tree
        |
        +-- Layout Cache
        |
        +-- Style Cache
        |
        +-- Text Layout Cache
        |
        +-- Dirty Invalidation
                 |
                 v
        Nearest Paint Cache Root
                 |
                 +-- CPU Geometry Cache
                 +-- DrawCommand Cache
                 +-- Persistent GPU Geometry
                 +-- Frame Revision
                 |
                 v
          Per-frame Submission
```

第一阶段只需要做到：

```text
Widget == Paint Cache Root
```

后续再扩展为：

```text
Widget
├── Static Paint Cache Root
├── Dynamic Paint Cache Root
└── Volatile Layer
```

这条路径可以最大限度复用 AiluEngine 当前 UI Tree、Measure / Arrange、DrawerBlock 和 UIRenderer，
不需要推翻现有系统，同时能消除静态 UI 每帧重复构建和上传的主要浪费。
