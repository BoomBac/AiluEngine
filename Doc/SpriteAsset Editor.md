# Sprite Asset Editor 设计文档

## 1. 目标

实现一个独立的 `SpriteAsset Editor`，基于已有的DockWindow，用于可视化编辑 `SpriteAsset` 资源中的以下属性：

```cpp
ACLASS()
class AILU_API SpriteAsset : public Object
{
    GENERATED_BODY()

public:
    APROPERTY()
    Guid _texture = Guid::EmptyGuid();

    APROPERTY()
    Vector4f _uv_rect = {0.0f, 0.0f, 1.0f, 1.0f};

    APROPERTY()
    Vector2f _pivot = {0.5f, 0.5f};

    APROPERTY()
    Vector2f _size = Vector2f::kOne;

    // 九宫格切片，RLBT，靠近边界的像素不会被拉伸。
    APROPERTY()
    Vector4f _border = Vector4f::kZero;
};
```

编辑器主要解决以下问题：

1. 选择或替换 Sprite 使用的纹理。
2. 在纹理预览中直接编辑 Sprite 的 UV 区域。
3. 编辑 Sprite 的 Pivot。
4. 编辑 Sprite 的逻辑尺寸。
5. 编辑九宫格 Border。
6. 实时预览 Sprite 裁剪、Pivot 和九宫格区域。
7. 支持撤销、重做、应用和还原。

---

# 2. 数据语义

## 2.1 `_texture`

```cpp
Guid _texture;
```

表示 Sprite 引用的纹理资源 GUID。

要求：

* 仅允许引用有效的二维纹理资源。
* 纹理为空时，中央预览区域显示空状态。
* 纹理发生变化后，需要重新验证 `_uv_rect` 和 `_border`。
* 修改纹理时不自动清空其他属性，除非原数据已超出新纹理范围。

---

## 2.2 `_uv_rect`

```cpp
Vector4f _uv_rect;
```

统一定义为：

```text
x = UV 左下角 X
y = UV 左下角 Y
z = UV 宽度
w = UV 高度
```

即：

```text
_uv_rect = {x, y, width, height}
```

所有值均使用归一化纹理坐标，合法范围为：

```text
0 <= x <= 1
0 <= y <= 1
0 <= width <= 1
0 <= height <= 1

x + width <= 1
y + height <= 1
```

编辑器界面中建议显示为：

```text
X
Y
W
H
```

需要明确渲染系统中的 UV 原点方向。

建议资源层始终使用左下角作为 UV 原点，编辑器预览内部根据图像显示坐标进行 Y 轴转换。

---

## 2.3 `_pivot`

```cpp
Vector2f _pivot;
```

表示 Pivot 在 Sprite 局部矩形中的归一化位置：

```text
(0, 0) = 左下角
(1, 1) = 右上角
(0.5, 0.5) = 中心
```

合法范围：

```text
0 <= pivot.x <= 1
0 <= pivot.y <= 1
```

默认值：

```text
{0.5, 0.5}
```

---

## 2.4 `_size`

```cpp
Vector2f _size;
```

表示 Sprite 在逻辑空间中的宽度和高度。

该字段不等同于 UV 区域对应的纹理像素尺寸。

建议语义：

```text
_size.x = Sprite 逻辑宽度
_size.y = Sprite 逻辑高度
```

例如渲染单位为像素时，可令其等于裁剪区域像素尺寸；若引擎使用世界单位，也可以设置为经过 Pixels Per Unit 转换后的尺寸。

编辑器不应强制 `_size` 与裁剪像素尺寸一致，但提供快捷按钮：

```text
Set From UV Pixels
```

点击后：

```text
_size.x = uv_pixel_width
_size.y = uv_pixel_height
```

---

## 2.5 `_border`

```cpp
Vector4f _border;
```

当前注释定义为：

```text
RLBT
```

因此成员顺序必须明确为：

```text
x = Right
y = Left
z = Bottom
w = Top
```

建议在后续重构中改为显式结构体，避免顺序歧义。

当前编辑器必须严格按以下方式读写：

```cpp
f32 right = sprite_asset->_border.x;
f32 left = sprite_asset->_border.y;
f32 bottom = sprite_asset->_border.z;
f32 top = sprite_asset->_border.w;
```

界面中显示顺序使用更符合视觉习惯的：

```text
Left
Right
Top
Bottom
```

但保存时仍需转换回 RLBT。

Border 单位定义为纹理像素，而不是归一化坐标。

合法范围：

```text
left >= 0
right >= 0
top >= 0
bottom >= 0

left + right <= sprite_pixel_width
top + bottom <= sprite_pixel_height
```

---

# 3. 整体界面布局

编辑器采用三栏布局：

```text
┌──────────────────────────────────────────────────────────────────────┐
│ Toolbar                                                              │
├───────────────┬─────────────────────────────────┬────────────────────┤
│ Asset Info    │ Texture Preview                 │ Sprite Properties  │
│               │                                 │                    │
│ Preview       │ UV Rect                         │ Texture            │
│ Settings      │ Pivot                           │ UV Rect            │
│               │ Border Guides                   │ Pivot              │
│               │                                 │ Size               │
│               │                                 │ Border             │
├───────────────┴─────────────────────────────────┴────────────────────┤
│ Status Bar                                                           │
└──────────────────────────────────────────────────────────────────────┘
```

建议宽度：

```text
左栏：220～260 px
中栏：自适应
右栏：320～380 px
```

最小窗口尺寸：

```text
1000 × 650
```

---

# 4. 顶部工具栏

工具栏内容从左到右：

```text
Apply
Revert
Undo
Redo
Show Grid
Grid Size
Show Pivot
Show Border
Zoom
Fit
1:1
```

## 4.1 Apply

将编辑中的临时数据写回 `SpriteAsset`，并保存资源。

启用条件：

```text
当前编辑数据与资源原始数据不同
```

应用流程：

1. 校验数据。
2. 写回 `SpriteAsset`。
3. 标记资源 Dirty。
4. 触发资源序列化。
5. 刷新依赖该 Sprite 的对象。
6. 更新编辑器原始快照。

---

## 4.2 Revert

放弃尚未 Apply 的修改，恢复为资源当前保存状态。

若存在未保存修改，应弹出确认提示：

```text
Discard unsaved Sprite Asset changes?
```

---

## 4.3 Undo / Redo

所有编辑行为必须接入编辑器 Undo 系统，包括：

* 修改纹理。
* 修改 UV Rect。
* 拖动 UV 边界。
* 修改 Pivot。
* 拖动 Pivot。
* 修改 Size。
* 修改 Border。
* 拖动 Border 边界。
* 使用预设 Pivot。
* 使用 Set From UV Pixels。

连续拖动操作应合并为一个 Undo Command：

```text
鼠标按下：BeginTransaction
鼠标移动：更新临时值
鼠标释放：CommitTransaction
```

不能每帧生成一个 Undo 记录。

---

# 5. 左侧区域

## 5.1 Asset Info

显示只读信息：

```text
Name
Path
GUID
Type
Texture
Texture Resolution
Texture Format
Sprite Pixel Size
```

其中：

```text
Sprite Pixel Size =
{
    round(_uv_rect.z * texture_width),
    round(_uv_rect.w * texture_height)
}
```

纹理缩略图可点击，在 Project 面板中定位对应纹理资源。

---

## 5.2 Preview Settings

包含：

```text
Background
Zoom
Filtering
Show Alpha
```

### Background

可选：

```text
Checkerboard
Dark
Light
Black
White
Custom
```

默认：

```text
Checkerboard
```

### Zoom

支持：

```text
25%
50%
100%
200%
400%
800%
Fit
```

同时支持鼠标滚轮缩放。

缩放中心以鼠标所在位置为中心。

### Filtering

可选：

```text
Nearest
Linear
```

像素风 Sprite 默认使用 `Nearest`。

该选项仅影响编辑器预览，不修改纹理资源采样设置。

### Show Alpha

启用时正常显示透明度。

禁用时忽略 Alpha，以 RGB 方式显示纹理。

---

## 5.3 关于 Sprite List

当前 `SpriteAsset` 结构只表示一个 Sprite，并不包含同纹理下的 Sprite 集合关系。

因此第一版不实现左侧 `Sprite List`。

若后续引入以下资源：

```cpp
class SpriteAtlasAsset;
```

或纹理导入设置中包含多个 Sprite Rect，再增加列表：

```text
Sprites In Texture
```

第一版避免通过全资源扫描动态查找引用同一纹理的所有 SpriteAsset，以免产生高成本和不稳定排序。

---

# 6. 中央纹理预览区

中央区域是编辑器的核心交互区域。

## 6.1 基础显示

显示完整纹理，而不是只显示裁剪后的 Sprite。

叠加元素包括：

1. 纹理网格。
2. 当前 UV Rect。
3. UV Rect 外部遮罩。
4. Pivot 标记。
5. 九宫格 Border 线。
6. UV Rect 控制点。
7. Border 控制点。
8. 鼠标坐标信息。

---

## 6.2 坐标转换

需要定义以下坐标空间：

```text
Screen Space
Preview Local Space
Texture Pixel Space
Texture UV Space
Sprite Local Pixel Space
```

建议提供统一转换函数：

```cpp
Vector2f ScreenToTexturePixel(const Vector2f& screen_position) const;
Vector2f TexturePixelToScreen(const Vector2f& texture_pixel) const;
Vector2f TexturePixelToUv(const Vector2f& texture_pixel) const;
Vector2f UvToTexturePixel(const Vector2f& uv) const;
Vector2f ScreenToSpritePixel(const Vector2f& screen_position) const;
Vector2f SpritePixelToScreen(const Vector2f& sprite_pixel) const;
```

注意：

图像控件通常以左上角为原点，而 UV 通常以左下角为原点，因此 Y 坐标转换应集中处理，不能散布在各个交互逻辑中。

---

## 6.3 UV Rect 显示

当前 Sprite 区域使用高亮边框显示。

建议颜色：

```text
UV Rect：蓝色或青色
Pivot：绿色
Border：橙色
```

UV Rect 外部区域覆盖半透明黑色遮罩，使当前 Sprite 区域更突出。

UV Rect 四角和四边显示拖动控制点：

```text
Top Left
Top
Top Right
Right
Bottom Right
Bottom
Bottom Left
Left
```

---

## 6.4 UV Rect 拖动规则

### 拖动整体

鼠标在 UV Rect 内部且未命中 Pivot 或 Border 时，可拖动整个 UV Rect。

拖动时保持宽高不变，并限制在纹理范围内。

### 拖动边缘

拖动单边时，仅改变对应边界。

### 拖动角点

同时改变两个方向的边界。

### 最小尺寸

UV Rect 最小尺寸建议为：

```text
1 × 1 texture pixel
```

不能允许宽度或高度变为零。

### 像素吸附

默认启用像素吸附：

```text
UV 边界最终落在整数纹理像素坐标
```

修改流程：

```cpp
pixel_x = round(uv_x * texture_width);
uv_x = pixel_x / texture_width;
```

提供工具栏选项：

```text
Snap To Pixels
```

默认开启。

按住 `Alt` 可临时关闭吸附。

---

## 6.5 创建 UV Rect

当纹理已设置时，允许用户在 UV Rect 外部拖出新的矩形。

建议交互：

```text
按住 Shift + 鼠标左键拖动
```

创建完成后替换当前 UV Rect。

不建议普通鼠标拖动直接创建，避免与平移预览冲突。

---

## 6.6 Pivot 显示

Pivot 显示为：

```text
圆形中心点
十字线
水平和垂直辅助线
```

Pivot 的实际位置：

```cpp
pivot_pixel.x = sprite_left + _pivot.x * sprite_pixel_width;
pivot_pixel.y = sprite_bottom + _pivot.y * sprite_pixel_height;
```

---

## 6.7 Pivot 拖动

拖动 Pivot 时：

1. 鼠标坐标转换到 Sprite 局部像素空间。
2. 除以 Sprite 像素尺寸。
3. 得到归一化 Pivot。
4. 限制到 `[0, 1]`。

计算方式：

```cpp
pivot_x = local_pixel_x / sprite_pixel_width;
pivot_y = local_pixel_y / sprite_pixel_height;
```

默认不吸附。

按住 `Shift` 时吸附到常用位置：

```text
0.0
0.5
1.0
```

---

## 6.8 Border 显示

Border 使用四条橙色线显示：

```text
Left Border
Right Border
Top Border
Bottom Border
```

Border 线仅存在于当前 UV Rect 内。

计算位置：

```cpp
left_x = sprite_left + left;
right_x = sprite_right - right;
bottom_y = sprite_bottom + bottom;
top_y = sprite_top - top;
```

Border 区域可使用轻微半透明填充进行提示。

---

## 6.9 Border 拖动

拖动 Border 线时修改对应像素值。

必须满足：

```text
left + right <= sprite_pixel_width
top + bottom <= sprite_pixel_height
```

当拖动导致冲突时，当前拖动边界应被限制，而不是自动修改另一侧 Border。

例如：

```text
left 最大值 = sprite_pixel_width - right
```

Border 默认吸附到整数像素。

---

## 6.10 预览区域平移和缩放

建议交互：

```text
鼠标滚轮：缩放
鼠标中键拖动：平移
Alt + 鼠标左键：平移
F：Fit
1：100%
```

缩放范围：

```text
10% ～ 3200%
```

缩放步长应使用指数变化，而不是固定加减。

例如：

```cpp
zoom *= 1.1f;
zoom /= 1.1f;
```

---

# 7. 右侧属性面板

右侧属性面板按以下顺序组织：

```text
Texture
UV Rect
Pivot
Size
Border / 9-Slice
Advanced
```

---

## 7.1 Texture

显示：

```text
Texture Object Field
Texture Preview
Resolution
Format
```

支持：

* 从资源浏览器拖入纹理。
* 点击选择按钮打开资源选择器。
* 点击清除按钮解除纹理引用。
* 双击定位纹理资源。

纹理发生变化后：

1. 保留 `_uv_rect`。
2. 对 `_uv_rect` 做范围裁剪。
3. 对 `_border` 做合法性裁剪。
4. 刷新预览。

---

## 7.2 UV Rect

同时提供两组输入。

### Normalized

```text
X
Y
W
H
```

直接对应 `_uv_rect`。

### Pixels

```text
X
Y
W
H
```

以纹理像素为单位。

两组数据双向同步。

计算方式：

```cpp
pixel_x = round(uv_x * texture_width);
pixel_y = round(uv_y * texture_height);
pixel_w = round(uv_width * texture_width);
pixel_h = round(uv_height * texture_height);
```

建议默认展开 `Pixels`，因为美术编辑 Sprite 时像素值更直观。

提供快捷按钮：

```text
Full Texture
Trim Transparent
```

### Full Texture

设置：

```cpp
_uv_rect = {0.0f, 0.0f, 1.0f, 1.0f};
```

### Trim Transparent

根据纹理 Alpha 自动计算非透明像素包围盒。

第一版可暂不实现，但应预留按钮和接口。

接口建议：

```cpp
bool CalculateOpaqueBounds(Texture* texture, RectI& out_rect);
```

---

## 7.3 Pivot

显示：

```text
X
Y
```

以及九宫格预设按钮：

```text
Top Left
Top
Top Right
Left
Center
Right
Bottom Left
Bottom
Bottom Right
```

对应值：

```text
Top Left     = {0.0, 1.0}
Top          = {0.5, 1.0}
Top Right    = {1.0, 1.0}
Left         = {0.0, 0.5}
Center       = {0.5, 0.5}
Right        = {1.0, 0.5}
Bottom Left  = {0.0, 0.0}
Bottom       = {0.5, 0.0}
Bottom Right = {1.0, 0.0}
```

---

## 7.4 Size

显示：

```text
Width
Height
```

支持锁定宽高比例。

按钮：

```text
Set From UV Pixels
Reset
```

### Set From UV Pixels

```cpp
_size = {
    static_cast<f32>(sprite_pixel_width),
    static_cast<f32>(sprite_pixel_height)
};
```

### Reset

```cpp
_size = Vector2f::kOne;
```

当宽高比例锁定时，修改一个分量应按原比例修改另一个分量。

---

## 7.5 Border / 9-Slice

显示：

```text
Left
Right
Top
Bottom
```

输入单位为像素。

提供快捷按钮：

```text
Reset
Equal
Preview 9-Slice
```

### Reset

```cpp
_border = Vector4f::kZero;
```

### Equal

将四边统一为最小边值或用户最近编辑值。

建议点击后取：

```cpp
f32 border = Min(sprite_pixel_width, sprite_pixel_height) * 0.1f;
```

并取整。

### Preview 9-Slice

启用一个额外预览区域，用于展示 Sprite 被拉伸后的九宫格效果。

建议提供目标尺寸输入：

```text
Preview Width
Preview Height
```

该功能可放入第二阶段。

---

# 8. 状态栏

底部状态栏显示：

```text
Texture: hero_atlas.png
Texture Size: 2048 × 2048
Sprite Rect: 256 × 256
UV: 0.125, 0.250, 0.125, 0.125
Zoom: 200%
Cursor: 128, 96 px
```

鼠标移动时实时更新 Cursor 信息。

---

# 9. 编辑状态模型

不能直接在每次 UI 修改时写入资源对象。

建议维护编辑快照：

```cpp
struct SpriteAssetEditorData
{
    Guid _texture = Guid::EmptyGuid();
    Vector4f _uv_rect = {0.0f, 0.0f, 1.0f, 1.0f};
    Vector2f _pivot = {0.5f, 0.5f};
    Vector2f _size = Vector2f::kOne;
    Vector4f _border = Vector4f::kZero;
};
```

编辑器内部维护：

```cpp
SpriteAssetEditorData _original_data;
SpriteAssetEditorData _editing_data;
```

状态判断：

```cpp
bool IsDirty() const;
```

打开资源时：

```cpp
_original_data = ReadFromAsset(sprite_asset);
_editing_data = _original_data;
```

Apply 时：

```cpp
WriteToAsset(_editing_data, sprite_asset);
_original_data = _editing_data;
```

Revert 时：

```cpp
_editing_data = _original_data;
```

---

# 10. 数据校验

建议统一实现：

```cpp
void ValidateEditingData();
```

校验内容：

## 10.1 UV Rect

```text
宽高至少为 1 像素
UV Rect 不得超出纹理
所有值必须为有限浮点数
```

## 10.2 Pivot

```text
限制到 [0, 1]
```

## 10.3 Size

```text
宽高必须大于 0
所有值必须为有限浮点数
```

建议最小值：

```text
0.0001
```

## 10.4 Border

```text
所有值必须大于等于 0
左右和不得超过 Sprite 像素宽度
上下和不得超过 Sprite 像素高度
```

---

# 11. 类设计建议

```cpp
class SpriteAssetEditor : public AssetEditor
{
public:
    SpriteAssetEditor();
    ~SpriteAssetEditor() override;

    void Open(Object* asset) override;
    void Close() override;
    void Tick(f32 delta_time) override;
    void Render() override;

private:
    void DrawToolbar();
    void DrawAssetInfoPanel();
    void DrawPreviewPanel();
    void DrawPropertiesPanel();
    void DrawStatusBar();

    void DrawTextureField();
    void DrawUvRectProperties();
    void DrawPivotProperties();
    void DrawSizeProperties();
    void DrawBorderProperties();

    void DrawTexturePreview();
    void DrawUvRectOverlay();
    void DrawPivotOverlay();
    void DrawBorderOverlay();

    void HandlePreviewInput();
    void HandleUvRectInput();
    void HandlePivotInput();
    void HandleBorderInput();
    void HandleViewNavigation();

    void Apply();
    void Revert();
    void ValidateEditingData();

    bool IsDirty() const;

    Vector2f ScreenToTexturePixel(const Vector2f& screen_position) const;
    Vector2f TexturePixelToScreen(const Vector2f& texture_pixel) const;
    RectF GetUvRectInTexturePixels() const;
    RectF GetUvRectInScreenSpace() const;

private:
    SpriteAsset* _sprite_asset = nullptr;
    Texture* _texture = nullptr;

    SpriteAssetEditorData _original_data;
    SpriteAssetEditorData _editing_data;

    Vector2f _preview_pan = Vector2f::kZero;
    f32 _preview_zoom = 1.0f;

    bool _show_grid = true;
    bool _show_pivot = true;
    bool _show_border = true;
    bool _snap_to_pixel = true;
    bool _show_alpha = true;

    SpriteEditorDragMode _drag_mode = SpriteEditorDragMode::kNone;
    Vector2f _drag_start_mouse = Vector2f::kZero;
    SpriteAssetEditorData _drag_start_data;
};
```

拖动类型：

```cpp
enum class SpriteEditorDragMode
{
    kNone,

    kPanView,

    kMoveUvRect,
    kResizeUvLeft,
    kResizeUvRight,
    kResizeUvTop,
    kResizeUvBottom,
    kResizeUvTopLeft,
    kResizeUvTopRight,
    kResizeUvBottomLeft,
    kResizeUvBottomRight,

    kMovePivot,

    kMoveBorderLeft,
    kMoveBorderRight,
    kMoveBorderTop,
    kMoveBorderBottom
};
```

---

# 12. 交互命中优先级

鼠标命中测试必须有明确优先级，避免多个元素同时响应。

从高到低：

```text
UV Rect 角点
UV Rect 边
Pivot
Border 控制线
UV Rect 内部
空白区域
```

建议实际优先级调整为：

```text
当前选中的工具控制点
Pivot
UV Rect 角点
UV Rect 边
Border
UV Rect 内部
预览平移
```

控制点屏幕命中范围应使用固定像素尺寸，而不是随 Zoom 缩放：

```text
6～8 px
```

---

# 13. 快捷键

建议支持：

```text
Ctrl + S       Apply
Ctrl + Z       Undo
Ctrl + Y       Redo
F              Fit Texture
1              100% Zoom
G              Toggle Grid
P              Toggle Pivot
B              Toggle Border
Delete         Clear Texture，仅当纹理字段获得焦点
Escape         取消当前拖动
```

---

# 14. 资源变更通知

Apply 后需要通知以下系统：

```text
Asset Database
Resource Manager
Sprite Renderer
Inspector
Scene View
Thumbnail Cache
```

建议发送资源变更事件：

```cpp
AssetChangedEvent event;
event._asset_guid = sprite_asset->GetGuid();
event._change_type = AssetChangeType::kContent;
event._source = AssetChangeSource::kEditor;

EventBus::Publish(event);
```

依赖该 Sprite 的渲染对象应在下一帧重新获取资源数据或刷新 GPU 数据。

---

# 15. 缩略图生成

SpriteAsset 的缩略图应只显示 `_uv_rect` 对应区域，而不是整张纹理。

缩略图需要：

1. 裁剪 Sprite UV。
2. 使用棋盘格背景。
3. 保持宽高比。
4. 默认使用透明背景。
5. 在 SpriteAsset Apply 后重新生成。

建议最大缩略图尺寸：

```text
128 × 128
```

---

# 16. 第一阶段实现范围

第一阶段必须实现：

```text
纹理选择
完整纹理预览
UV Rect 属性编辑
UV Rect 可视化拖动
Pivot 属性编辑
Pivot 可视化拖动
Size 属性编辑
Border 属性编辑
Border 可视化拖动
缩放和平移
像素吸附
Apply / Revert
Undo / Redo
数据校验
```

第一阶段不实现：

```text
自动透明裁剪
Sprite Atlas 列表
多 Sprite 批量编辑
自动轮廓提取
Polygon Sprite
九宫格最终拉伸预览
纹理导入参数编辑
动画帧管理
```

---

# 17. 第二阶段扩展

后续可扩展：

## 17.1 自动裁剪

根据 Alpha 阈值自动生成 UV Rect。

```text
Alpha Threshold
Padding
Trim
```

## 17.2 Sprite Atlas

增加 `SpriteAtlasAsset`，统一管理同一纹理中的多个 Sprite。

## 17.3 多选批量编辑

允许同时选中多个 Sprite，批量修改：

```text
Pivot
Size
Border
```

## 17.4 轮廓编辑

支持 Tight Mesh，用于减少透明区域 Overdraw。

## 17.5 动画帧创建

从规则网格或多个 SpriteAsset 快速创建 Sprite Frame Animation Asset。

---

# 18. 需要优先确认的问题

在实现前应确认以下引擎约定：

1. `_uv_rect` 的 `Vector4f` 顺序是否确定为 `x, y, width, height`。
2. UV 原点是否为左下角。
3. `_border` 的 `RLBT` 是否确实对应 `Right, Left, Bottom, Top`。
4. `_size` 的单位是像素、世界单位，还是逻辑 UI 单位。
5. Sprite 是否允许 Pivot 超出 `[0, 1]`。
6. SpriteAsset 是否直接引用 Texture GUID，还是应使用资源句柄。
7. 编辑器 Undo 系统和资源 Dirty 系统的现有接口。
8. 纹理 CPU 数据是否可读取，以支持后续自动透明裁剪。

第一版建议严格限制 Pivot 到 `[0, 1]`，Border 使用像素单位，UV 使用归一化坐标，Size 保持独立逻辑尺寸。
