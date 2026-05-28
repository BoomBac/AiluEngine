# World Outline 与通用 TreeView 实施方案

## 1. 目标

首版同时完成通用 `TreeView` 和基于它实现的 `WorldOutline`。

首版功能范围：

- TreeView 通用数据源、展开/折叠、单选、滚动、双击、右键和拖放。
- 显示当前场景实体父子树。
- WorldOutline 与 `Selection`、场景视图、`ObjectDetail` 双向同步。
- 创建空实体、重命名、复制、递归删除。
- 拖到实体节点下进行 Reparent。
- 拖到 TreeView 空白区域进行 Detach。
- 双击实体聚焦场景相机。
- Dock 默认布局及持久化。
- 场景切换、Play Mode 切换和实体销毁后的安全刷新。

首版暂不包含：

- TreeView 多选和 Shift 范围选择。
- 同级节点精确排序及插入指示线。
- 节点虚拟化、异步加载、内置搜索。
- 行内重命名。
- 递归复制子树。
- 完整 Undo；首版完成后单独实施。

约定：

- 删除父实体时递归删除整棵子树。
- Reparent 和 Detach 默认保持世界变换不变。
- Duplicate 只复制当前实体，并挂在原父节点下。
- TreeView 不依赖 Scene、Entity、Selection 或 Editor。
- WorldOutline 不手工绘制树行，只负责适配场景数据和处理业务操作。

## 2. 总体架构

```text
Engine/UI
└─ TreeView
   ├─ 通用数据源接口
   ├─ 可见节点扁平化
   ├─ 展开状态
   ├─ 单选状态
   ├─ TreeViewRow 生成和样式
   ├─ 双击与右键事件
   └─ 通用 TreeItem 拖放

Engine/Scene
├─ 实体有效性
├─ 安全 Reparent/Detach
├─ 子树删除
├─ 安全 Duplicate
└─ StructureRevision

Editor
├─ Selection Revision
└─ WorldOutline
   ├─ SceneTreeDataSource
   ├─ TreeView 事件绑定
   ├─ 实体菜单操作
   ├─ Selection 同步
   └─ 相机聚焦
```

## 6. 通用 TreeView

新增文件：

- `Engine/Inc/UI/TreeView.h`
- `Engine/Src/UI/TreeView.cpp`

反射生成文件由现有代码生成器产生，不手工修改 `generated` 目录。

### 6.1 通用节点 ID

```cpp
namespace Ailu::UI
{
    using TreeItemId = u64;
    inline constexpr TreeItemId kInvalidTreeItemId = 0u;
}
```

数据源负责保证同一棵树内 ID 唯一。

### 6.2 节点显示信息

```cpp
struct AILU_API TreeItemPresentation
{
    String _label;
    Render::Texture* _icon = nullptr;
    Color _text_color = Colors::kWhite;
    bool _selectable = true;
    bool _draggable = false;
    bool _drop_target = false;
};
```

不要保存 `void*`、Entity/Component 指针或 `std::any`。业务对象只通过 ID 回查数据源。

### 6.3 数据源接口

```cpp
class AILU_API ITreeViewDataSource
{
public:
    virtual ~ITreeViewDataSource() = default;

    virtual Vector<TreeItemId>
    GetRootItems() const = 0;

    virtual Vector<TreeItemId>
    GetChildren(TreeItemId parent) const = 0;

    virtual TreeItemId
    GetParent(TreeItemId item) const = 0;

    virtual TreeItemPresentation
    GetPresentation(TreeItemId item) const = 0;

    virtual bool
    IsValid(TreeItemId item) const = 0;
};
```

TreeView 不拥有数据源：

```cpp
ITreeViewDataSource* _data_source = nullptr;
```

调用方销毁数据源前必须调用 `SetDataSource(nullptr)`。

### 6.4 TreeView 公共接口

```cpp
ACLASS()
class AILU_API TreeView : public ScrollView
{
    GENERATED_BODY()

    DECLARE_DELEGATE(on_selection_changed, TreeItemId);
    DECLARE_DELEGATE(on_item_double_clicked, TreeItemId);
    DECLARE_DELEGATE(on_item_context_menu, TreeItemId, Vector2f);
    DECLARE_DELEGATE(on_expansion_changed, TreeItemId, bool);

public:
    TreeView();
    ~TreeView() override = default;

    void SetDataSource(ITreeViewDataSource* data_source);
    ITreeViewDataSource* GetDataSource() const;

    void Refresh();

    void SetSelectedItem(
        TreeItemId item,
        bool notify = true);

    TreeItemId GetSelectedItem() const;
    void ClearSelection(bool notify = true);

    void SetExpanded(TreeItemId item, bool expanded);
    bool IsExpanded(TreeItemId item) const;
    void ToggleExpanded(TreeItemId item);
    void ExpandAll();
    void CollapseAll();
    void ClearExpansionState();

    void ExpandParents(TreeItemId item);
    void ScrollItemIntoView(TreeItemId item);
};
```

`SetSelectedItem()` 必须检查：

- 数据源存在。
- item 有效。
- presentation 允许选择。
- item 与当前选择不同。

外部同步使用 `notify = false`，避免 Selection 和 TreeView 互相递归通知。

### 6.5 可见节点扁平化

TreeView 不创建嵌套行树，先生成可见节点列表：

```cpp
struct VisibleTreeItem
{
    TreeItemId _id = kInvalidTreeItemId;
    u32 _depth = 0;
    bool _has_children = false;
};
```

成员建议：

```cpp
Vector<VisibleTreeItem> _visible_items;
HashSet<TreeItemId> _expanded_items;
HashMap<TreeItemId, UIElement*> _item_rows;
TreeItemId _selected_item = kInvalidTreeItemId;
VerticalBox* _content_box = nullptr;
```

扁平化算法要求：

1. 验证 item。
2. 使用 visited 防止重复 ID 和循环。
3. 获取 children。
4. 将当前节点加入 visible items。
5. 仅在展开时递归 children。
6. 设置最大深度保护，例如 256。
7. 设置最大节点数量保护。
8. 遇到损坏数据记录警告并跳过该分支。

### 6.6 TreeViewRow

实现内部行控件，推荐继承 `Border`：

```text
TreeViewRow / Border
└─ HorizontalBox
   ├─ 基于 margin 的缩进
   ├─ Expand Button
   ├─ Icon Image
   └─ Label Text
```

默认样式建议：

```cpp
f32 _row_height = 22.0f;
f32 _indent_width = 16.0f;
f32 _expand_button_width = 16.0f;
Color _normal_color;
Color _hover_color;
Color _selected_color;
Color _selected_unfocused_color;
```

缩进直接使用左 margin：

```cpp
left_margin = depth * _indent_width;
```

不要为每级缩进创建空控件。

### 6.7 行交互

- 左键行：`SetSelectedItem(item)`。
- Expand Button：`ToggleExpanded(item)`。
- Expand Button 事件不能重复冒泡为行点击。
- 双击行：触发 `on_item_double_clicked`。
- 右键行：如果不是当前选择，先选中，再触发 `on_item_context_menu`。

### 6.8 Refresh 规则

`Refresh()`：

- 保留仍有效节点的展开状态。
- 删除失效 ID 的展开状态。
- 当前选择失效时清空选择。
- 重新生成 visible items 和行。

`SetDataSource()`：

- 清空选择。
- 清空展开状态。
- 清空旧行。
- 设置新数据源。
- 非空数据源立即 Refresh。

`ExpandParents()`：

- 通过 `GetParent()` 沿父链向上遍历。
- 使用 visited 防环。
- 将祖先加入 expanded items。
- Refresh 后由 `ScrollItemIntoView()` 定位。

## 7. TreeView 通用拖放

### 7.1 拖放类型和 Payload

在 `EDragType` 中增加：

```cpp
kTreeItem
```

Payload：

```cpp
struct TreeViewDragPayload
{
    TreeView* _source_tree = nullptr;
    TreeItemId _item = kInvalidTreeItemId;
};
```

Payload 存放为 TreeView 成员，不能传局部变量地址：

```cpp
TreeViewDragPayload _drag_payload;
```

### 7.2 通用回调

```cpp
using TreeCanDragCallback =
    std::function<bool(TreeItemId)>;

using TreeCanDropCallback =
    std::function<bool(
        TreeView* source_tree,
        TreeItemId source_item,
        TreeItemId target_item)>;

using TreeDropCallback =
    std::function<void(
        TreeView* source_tree,
        TreeItemId source_item,
        TreeItemId target_item)>;

void SetCanDragCallback(TreeCanDragCallback callback);
void SetCanDropCallback(TreeCanDropCallback callback);
void SetDropCallback(TreeDropCallback callback);
```

约定：

- target item 有效：拖到该节点下。
- target item 为 Invalid：拖到树根/空白区域。
- TreeView 只发出业务请求，不直接修改数据源。

### 7.3 DropHandler

每个允许 drop 的行设置 DropHandler。

TreeView 内容空白区域也设置 DropHandler，target 为 Invalid。

HitTest 必须保证：

- 悬停在行上时优先命中行。
- 空白区域处理器不能同时触发行 drop。

### 7.4 修复现有 DragDrop bug

在 `Engine/Src/UI/DragDrop.cpp` 中修改：

```cpp
_display_name = std::move(_display_name);
```

为：

```cpp
_display_name = std::move(display_name);
```

## 8. WorldOutline

新增：

- `Editor/Inc/Widgets/WorldOutline.h`
- `Editor/Src/Widgets/WorldOutline.cpp`

### 8.1 SceneTreeDataSource

放在 `WorldOutline.cpp` 或独立私有文件中：

```cpp
class SceneTreeDataSource final
    : public UI::ITreeViewDataSource
{
public:
    void SetScene(SceneManagement::Scene* scene);

    Vector<UI::TreeItemId>
    GetRootItems() const override;

    Vector<UI::TreeItemId>
    GetChildren(UI::TreeItemId parent) const override;

    UI::TreeItemId
    GetParent(UI::TreeItemId item) const override;

    UI::TreeItemPresentation
    GetPresentation(UI::TreeItemId item) const override;

    bool IsValid(UI::TreeItemId item) const override;

private:
    SceneManagement::Scene* _scene = nullptr;
};
```

显式转换：

```cpp
static UI::TreeItemId ToTreeItem(ECS::Entity entity);
static ECS::Entity ToEntity(UI::TreeItemId item);
```

不要使用指针 reinterpret cast。

### 8.2 数据查询

`GetRootItems()` 返回：

- Entity alive。
- 拥有 `CHierarchy`。
- parent 为 Invalid。

`GetChildren()`：

- 从 first child 沿 next sibling 遍历。
- 校验 alive 和 CHierarchy。
- 使用 visited 和最大数量保护。
- 读取组件后先缓存 next sibling。

`GetParent()`：

- 无效 item 返回 Invalid。
- parent 不存活时返回 Invalid 并记录警告。

`GetPresentation()`：

```cpp
TreeItemPresentation result;
result._label = tag ? tag->_name : "<Unnamed>";
result._selectable = true;
result._draggable = true;
result._drop_target = true;
```

图标首版可以统一；若添加类型图标，建议优先级为 Camera、Light、StaticMesh、普通 Entity。

### 8.3 WorldOutline 类

```cpp
ACLASS()
class WorldOutline : public DockWindow
{
    GENERATED_BODY()

public:
    WorldOutline();
    ~WorldOutline() override;
    void Update(f32 dt) override;

private:
    void BindTreeEvents();
    void SyncSelection();
    void ShowEntityContextMenu(
        ECS::Entity entity,
        Vector2f position);
    void FocusEntity(ECS::Entity entity);

private:
    UI::TreeView* _tree_view = nullptr;
    UI::Text* _scene_title = nullptr;
    UI::Button* _add_button = nullptr;

    SceneTreeDataSource _data_source;
    SceneManagement::Scene* _observed_scene = nullptr;
    u64 _observed_structure_revision = 0;
    u64 _observed_selection_revision = 0;
};
```

析构时先执行：

```cpp
_tree_view->SetDataSource(nullptr);
```

### 8.4 UI 结构

```text
DockWindow Content
└─ VerticalBox
   ├─ Toolbar
   │  └─ Add Empty
   ├─ Scene Name Text
   └─ TreeView (Fill)
```

### 8.5 Update

每帧只比较 Scene 指针和 revision：

```cpp
auto* scene = SceneMgr::Get().ActiveScene();

if (scene != _observed_scene)
{
    _observed_scene = scene;
    _data_source.SetScene(scene);
    _tree_view->SetDataSource(
        scene ? &_data_source : nullptr);

    _observed_structure_revision =
        scene ? scene->StructureRevision() : 0;

    _scene_title->SetText(
        scene ? scene->Name() : "No Scene");

    SyncSelection();
}
else if (scene &&
         scene->StructureRevision() !=
             _observed_structure_revision)
{
    _observed_structure_revision =
        scene->StructureRevision();
    _tree_view->Refresh();
}

if (Selection::Revision() !=
    _observed_selection_revision)
{
    _observed_selection_revision =
        Selection::Revision();
    SyncSelection();
}
```

### 8.6 Selection 同步

用户选择 TreeView：

```cpp
_tree_view->_on_selection_changed +=
    [this](UI::TreeItemId item)
{
    auto entity = ToEntity(item);
    if (_observed_scene &&
        _observed_scene->IsValidEntity(entity))
    {
        Selection::SetSelection(entity);
    }
};
```

外部 Selection 同步：

```cpp
if (!scene || !scene->IsValidEntity(entity))
{
    _tree_view->ClearSelection(false);
}
else
{
    auto item = ToTreeItem(entity);
    _tree_view->ExpandParents(item);
    _tree_view->SetSelectedItem(item, false);
    _tree_view->ScrollItemIntoView(item);
}
```

### 8.7 右键菜单

复用：

- `EditorPopup::ShowActionMenuAt()`
- `EditorPopup::ShowTextInputAt()`
- `EditorPopup::ShowConfirmAt()`

菜单：

- Rename
- Duplicate
- Create Empty Child
- Detach（存在 parent 时）
- Delete

所有操作通过 Scene 公共接口执行，禁止直接写 `CHierarchy`。

Rename：

- trim 后不能为空。
- 设置合理长度上限。
- 调用 `Scene::RenameEntity()`。

Create Empty Child：

```cpp
auto child = scene->AddObject("GameObject");
scene->Reparent(child, entity);
Selection::SetSelection(child);
_tree_view->SetExpanded(ToTreeItem(entity), true);
```

Delete：

- 弹确认框。
- 清理 Selection 中属于待删子树的实体。
- 调用 `Scene::RemoveObject(entity)`。

### 8.8 WorldOutline 拖放

CanDrag：

- 当前 Scene 存在。
- Entity alive。

CanDrop：

- source tree 必须是当前 TreeView。
- source Entity alive。
- target 为 Invalid，或者 target Entity alive。
- source 不等于 target。
- target 不在 source 子树中。

Drop：

```cpp
if (target == UI::kInvalidTreeItemId)
    scene->Detach(source_entity, true);
else
    scene->Reparent(source_entity, target_entity, true);
```

成功后：

- 目标有效时展开目标节点。
- 更新 observed structure revision。
- Refresh TreeView。
- 保持 source 为选中项。

### 8.9 双击聚焦

使用实体世界 Transform，而不是 local position。

必须检查：

- Scene 仍是当前 Scene。
- Entity alive。
- TransformComponent 存在。
- StaticMesh AABB 在访问前非空。

无有效包围盒时使用默认相机距离。

## 9. Dock 接入

WorldOutline 必须：

- 继承 `DockWindow`。
- 使用 `ACLASS()` 和 `GENERATED_BODY()`。
- 提供无参构造。

反射类型名应为：

```text
Ailu::Editor::WorldOutline
```

修改：

- `Editor/dock_layout.json`

推荐布局：

```text
Root 横向分割
├─ WorldOutline（20%~24%）
└─ 原布局
   ├─ SceneView + AssetBrowser
   └─ ObjectDetail
```

Dock 树是二叉结构，因此新建 root split：

1. 新 root 的左 child 是 WorldOutline。
2. 右 child 是原 root。
3. 将原 root 的 parent ID 改为新 root ID。
4. 所有 node ID 保持唯一。
5. WorldOutline leaf type 参考 SceneView/ObjectDetail。

启动并退出 Editor 后，再验证布局序列化和恢复。

## 10. Debug 层级校验器

建议增加：

```cpp
bool Scene::ValidateHierarchy(
    bool log_errors = true) const;
```

检查：

- 每个 alive 且有 CHierarchy 的实体只出现一次。
- parent alive 或为 Invalid。
- first child 与 children count 一致。
- child parent 指向当前 parent。
- `prev.next == current`。
- `next.prev == current`。
- 无循环。
- 根节点 parent 为 Invalid。
- Transform `_p_parent` 与层级 parent Transform 一致。

Debug 下在以下操作后调用：

- Deserialize
- Reparent
- DeletePendingEntities
- DuplicateEntity

Release 不每帧运行。

## 11. 第二阶段 Undo

首版稳定后新增：

- `RenameEntityCommand`
- `ReparentEntityCommand`
- `CreateEntityCommand`
- `DeleteEntityCommand`

命令保存 Scene、Entity ID 和值快照，不长期保存 Component 裸指针，因为 sparse-set 移动会使指针失效。

Delete Undo 需要完整子树和组件快照；如果当前序列化系统不能可靠恢复子树，先不让删除进入 Undo。

## 12. 实施顺序

1. 修复 Register alive、EntityNum 和 ID 边界。
2. 修复 Scene Reparent、Detach、Delete 和 Duplicate。
3. 增加 Scene StructureRevision。
4. 增加 Selection Revision 和 SetSelection。
5. 新增 ITreeViewDataSource 和 TreeView 基础结构。
6. 实现可见节点扁平化、展开、选择和 Refresh。
7. 实现 TreeView 双击、右键、ExpandParents 和 ScrollItemIntoView。
8. 实现通用 TreeItem 拖放。
9. 实现 SceneTreeDataSource。
10. 实现 WorldOutline 和 Selection 同步。
11. 接入菜单、拖放和相机聚焦。
12. 修改默认 Dock 布局。
13. 添加层级校验和回归测试。
14. 第二阶段实现 Undo。

建议在第 2、6、10、12 步后分别完整编译一次。

## 13. 验收清单

### 13.1 Scene 层级

- 创建 A、B、C，三者显示为根。
- B、C 依次挂到 A 后顺序正确。
- Detach 首、中、末子节点均不崩溃。
- 已有 parent 的实体可以正确 Reparent。
- 拒绝实体挂到自己。
- 拒绝父实体挂到自己的后代。
- Reparent/Detach 前后世界 SRT 基本不变。
- 删除父实体会删除完整子树。
- Duplicate 不复制 sibling/child 链。
- 连续重挂后 `ValidateHierarchy()` 通过。

### 13.2 通用 TreeView

- 空数据源正常显示。
- 单根、多根和多层树正确。
- 展开只影响对应分支。
- Refresh 保留有效节点的展开状态。
- 失效选择自动清空。
- 重复 ID 或循环数据不会无限递归。
- 右键先更新选择再触发菜单。
- Expand 按钮不会造成重复点击。
- `SetSelectedItem(..., false)` 不触发通知。
- ExpandParents 能展开深层选中节点祖先。
- ScrollItemIntoView 能滚动到目标。
- 行 drop 产生有效 target。
- 空白 drop 产生 Invalid target。
- 不同 TreeView 默认拒绝互相拖放。
- 200 个节点频繁 Refresh 无明显卡顿。

### 13.3 WorldOutline

- 当前场景所有 root 正确显示。
- 点击实体后 ObjectDetail 更新。
- 场景视图选择后大纲展开、滚动并高亮对应实体。
- Rename 后大纲和 ObjectDetail 同步。
- Duplicate 后层级正确。
- Delete 后没有悬空行或悬空 Selection。
- 拖到实体行成为其子节点。
- 拖到空白区域解除 parent。
- 非法拖拽不修改层级。
- 双击可以聚焦实体。
- 切换场景、进入和退出 Play Mode 不访问旧 Scene。
- Editor 重启后 Dock 布局恢复。

### 13.4 稳定性

- 空场景正常。
- 实体达到上限时可以滚动。
- 删除当前选中实体不会使 ObjectDetail 访问已销毁组件。
- 快速创建、删除、复制、拖拽不崩溃。
- Debug 下所有层级校验通过。

