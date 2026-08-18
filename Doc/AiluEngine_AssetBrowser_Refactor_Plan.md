# AiluEngine AssetBrowser 职能拆分开发任务包

## 目标

对当前 `AssetBrowser` 做一次**轻量职责拆分**，解决单文件过度膨胀、UI 与资源操作耦合、创建逻辑重复的问题。

原则：

- 优先复用现有系统，不引入新的重型框架。
- 不为了拆文件而拆文件。
- `AssetBrowser` 保留 UI、交互状态和刷新协调职责。
- 文件系统、资源 CRUD、导入流程、资产创建等逻辑从 `AssetBrowser` 中移出。
- 优先减少重复代码和隐式状态依赖。
- 保持现有功能、交互和数据格式不变。
- 分阶段实施，每一步都应可单独编译验证。

---

# 一、当前主要问题

当前 `AssetBrowser.cpp` 已同时承担：

1. Asset Browser UI 构建。
2. Directory Tree 数据构建。
3. 当前目录导航与路径转换。
4. Asset / Folder 查询和搜索。
5. Icon / List 两种布局。
6. Asset / Folder 选择状态。
7. Context Menu。
8. 文件导入及导入弹窗队列。
9. Asset / Folder Rename。
10. Asset / Folder Delete。
11. 各类 Asset 创建。
12. Prefab 拖拽创建。
13. Asset 打开及 Editor 路由。
14. Audio Preview。
15. Asset Drag & Drop。
16. Dock Layout 状态保存。

真正的问题不是文件行数，而是：

> UI、资源数据、文件系统操作和具体 Asset 类型知识集中在同一个 Window 中。

---

# 二、最终职责边界

建议最终只保留以下 4 个核心模块：

```text
AssetBrowser
AssetBrowserContent
AssetBrowserOperations
AssetImportController
```

并继续复用：

```text
AssetTypeRegistry
AssetEditorRegistry
ResourceMgr
EditorPopup
DragDropManager
```

Asset 创建行为建议扩展现有 Registry，优先避免引入新的大型系统。

---

# 三、AssetBrowser

## 职责

`AssetBrowser` 只负责：

- 构建 Browser UI。
- 当前路径。
- 搜索文本。
- 选择状态。
- Directory Tree / Content / Layout 刷新。
- 把用户操作转发给对应模块。
- 根据操作结果设置 dirty flag。

不要负责：

- `fs::rename`
- `fs::remove`
- `fs::remove_all`
- `ResourceMgr::CreateAsset`
- Lua 脚本模板生成
- Graph 初始化
- Texture / Mesh Import 参数处理
- 各具体 Asset Editor 的创建逻辑

建议整理为：

```cpp
class AssetBrowser final : public DockWindow
{
public:
    AssetBrowser();
    ~AssetBrowser() override;

    void Update(f32 dt) override;

private:
    void BuildUI();

    void NavigateToPath(const fs::path &path);

    void RefreshContent();
    void RefreshContentLayout();
    void RefreshDirectoryTree();
    void UpdatePathButtons();

    void CreateFolderWidget(const AssetBrowserEntry &entry);
    void CreateAssetWidget(const AssetBrowserEntry &entry);

    void SelectFolder(const fs::path &path, UI::UIElement *root, UI::Text *text);
    void SelectAsset(Asset *asset, UI::UIElement *root, UI::Text *text);

private:
    fs::path _current_path;
    String _search_text;

    Asset *_selected_asset = nullptr;
    fs::path _selected_folder_path;

    bool _content_dirty = true;
    bool _layout_dirty = true;
    bool _directory_tree_dirty = true;

    AssetBrowserContent _content;
    AssetBrowserOperations _operations;
    AssetImportController _import_controller;
};
```

不要额外引入 Presenter / ViewModel / Controller 层。

---

# 四、拆分 Update()

当前 `Update()` 同时负责：

- Keyboard Shortcut。
- Directory Tree 刷新。
- Asset 查询。
- Folder 查询。
- 搜索。
- UI Item 创建。
- Mouse Event 绑定。
- Drag Event 绑定。
- Grid / List 布局。

这是最明显的代码热点。

改成：

```cpp
void AssetBrowser::Update(f32 dt)
{
    DockWindow::Update(dt);

    HandleShortcuts();

    if (_directory_tree_dirty)
        RefreshDirectoryTree();

    const Vector2f content_size = _icon_area->GetContentRect().zw;
    if (!NearbyEqual(content_size, _last_icon_area_size))
    {
        _last_icon_area_size = content_size;
        _layout_dirty = true;
    }

    if (_content_dirty)
        RefreshContent();

    if (_layout_dirty)
        RefreshContentLayout();
}
```

其中：

```text
RefreshContent
    -> 查询当前目录数据
    -> ClearChildren
    -> CreateFolderWidget
    -> CreateAssetWidget

RefreshContentLayout
    -> List / Icon Cell 尺寸
    -> Item Position
    -> Text Ellipsis
```

第一阶段不需要额外创建 `AssetBrowserView` 类。

---

# 五、AssetBrowserContent

## 职责

统一管理 Asset Browser 的“数据查询和路径模型”。

把以下现有逻辑迁移进去：

```text
GetAssetBrowserRoots
FindRootForPath
GetDomainForPath
GetRelativeAssetDirectory

NormalizeSysPath
IsSameOrChildPath

FormatLogicalAssetPath
NormalizeLogicalPathWithoutTrailingSlash
NormalizeLogicalDirectoryPath
ExtractLogicalAssetDirectory
StripLogicalAssetPathScheme

IsAssetInDirectory
CollectAssetsUnderDirectory
```

同时负责：

- 当前目录 Folder 枚举。
- 当前目录 Asset 枚举。
- Search Filter。
- 排序。

推荐数据：

```cpp
struct AssetBrowserEntry
{
    enum class EType : u8
    {
        kFolder,
        kAsset
    };

    EType _type = EType::kAsset;

    String _display_name;
    fs::path _sys_path;
    Asset *_asset = nullptr;
};
```

接口：

```cpp
class AssetBrowserContent
{
public:
    Vector<AssetBrowserEntry> Query(const fs::path &directory, const String &search_text) const;

    Vector<AssetBrowserRootDesc> GetRoots() const;

    bool IsInsideRoot(const fs::path &path) const;

    EAssetDomain GetDomain(const fs::path &path) const;

    WString GetAssetDirectory(const fs::path &path) const;

    Vector<Asset *> CollectAssetsUnderDirectory(const WString &directory_asset_path) const;
};
```

## Query()

将当前 `Update()` 中的：

```text
ResourceMgr 遍历
Asset Domain Filter
Asset Directory Filter
Search Filter
Folder directory_iterator
Asset Sort
Folder Sort
```

统一移动到：

```cpp
Vector<AssetBrowserEntry> AssetBrowserContent::Query(
    const fs::path &directory,
    const String &search_text) const;
```

Browser 不再直接遍历整个 `ResourceMgr`。

---

# 六、DirectoryTreeDataSource

当前已有：

```cpp
AssetBrowser::DirectoryTreeDataSource
```

这部分设计本身可以保留。

只做两件事：

1. 将 Root / Path 辅助函数改为调用 `AssetBrowserContent`。
2. 从 `AssetBrowser.cpp` 移到：

```text
AssetBrowserContent.h/.cpp
```

或者单独保留：

```text
AssetBrowserDirectoryTree.cpp
```

如果不需要外部引用，优先放到 `AssetBrowserContent.cpp`，避免继续增加文件数量。

---

# 七、AssetBrowserOperations

这是本次最重要的拆分。

## 职责

统一处理 Asset / Folder 的文件系统与 ResourceMgr 修改。

迁移：

```text
RenameAssetEntry
RenameFolderEntry
DeleteAssetEntry
DeleteFolderEntry
CreateFolderEntry
MakeUniqueEntryName
RewriteAssetHeaderName
```

以及 Asset Creation 的公共部分。

推荐接口：

```cpp
class AssetBrowserOperations
{
public:
    bool RenameAsset(Asset *asset, const String &new_name);

    bool RenameFolder(
        const fs::path &folder,
        const String &new_name);

    bool DeleteAsset(Asset *asset);

    bool DeleteFolder(const fs::path &folder);

    bool CreateFolder(
        const fs::path &directory,
        const String &name);

    String MakeUniqueEntryName(
        const fs::path &directory,
        const String &base_name,
        const WString &extension,
        bool is_directory) const;
};
```

---

# 八、取消 `_current_path` 隐式依赖

目前大量 Create 操作隐式使用：

```cpp
_current_path
```

Folder Context Menu 为了在目标 Folder 中创建 Asset，会出现：

```cpp
const fs::path previous_path = _current_path;
_current_path = folder_sys_path;

const bool created = CreateXXXEntry(name);

_current_path = previous_path;
```

这一模式必须删除。

所有操作改成显式：

```cpp
CreateSpriteEntry(target_directory, name);
CreateScriptEntry(target_directory, name);
CreateFlowGraphEntry(target_directory, name);
```

或者最终统一成 Registry Creator：

```cpp
creator._create(target_directory, name);
```

原则：

> 操作目标通过参数传入，不通过临时修改 Browser 状态传递。

这既简化代码，也避免 Popup 异步期间路径变化导致目标目录错误。

---

# 九、Asset Creation 注册化

当前存在：

```text
CreateSceneEntry
CreateSpriteEntry
CreateMaterialEntry
CreateInputActionAssetEntry
CreateWidgetAssetEntry
CreateFlowGraphEntry
CreateScriptEntry
```

并且同一批创建入口同时重复出现在：

```text
Blank Area Context Menu
Folder Context Menu
```

应该改成数据驱动。

优先考虑扩展现有：

```text
AssetTypeRegistry
```

如果现有 `AssetTypeRegistry` 语义不适合，再增加一个很小的：

```text
AssetCreationRegistry
```

不要引入复杂 Factory 系统。

---

# 十、Asset Creator 描述

推荐：

```cpp
struct AssetCreatorDesc
{
    String _menu_name;
    String _default_name;
    WString _extension;

    Function<bool(const fs::path &, const String &)> _create;
};
```

注册：

```cpp
registry.Register({
    "Sprite",
    "NewSprite",
    L".alasset",
    [](const fs::path &directory, const String &name)
    {
        return CreateSpriteAsset(directory, name);
    }
});
```

Context Menu：

```cpp
for (const auto &creator : registry.Creators())
    AddCreateAssetAction(actions, creator, target_directory);
```

这样：

```text
Blank Area
Folder
```

复用完全相同的创建菜单构建逻辑。

---

# 十一、特殊 Asset Creator

不要强行把所有 Asset 统一成一个模板。

例如 Flow Graph 创建时仍然需要：

```cpp
auto graph = MakeRef<GraphAsset>(name);
graph->SchemaType("FlowGraphSchema");

GraphDocument document;
document.Open(graph.get());
document.AddNode("Flow.Entry", {80.0f, 120.0f});
document.Apply();
```

这段逻辑放在 Flow Graph Creator 中。

类似：

```text
Scene
Script
Material
Prefab
```

都允许有自己的特殊 Creator。

Registry 只解决：

```text
AssetBrowser 不需要知道如何创建具体 Asset
```

而不是强制所有资源使用相同实现。

---

# 十二、Script Creation

当前 Lua Script 创建包含：

1. Lua 文件名。
2. `.alasset` 文件名。
3. Lua 模板字符串。
4. `FileManager::WriteFile`。
5. `ScriptAsset` 创建。
6. `ResourceMgr::CreateAsset`。

这些全部迁出 Browser。

推荐：

```cpp
bool CreateScriptAsset(
    const fs::path &directory,
    const String &name);
```

Lua 模板可以继续保持当前实现。

本次不额外增加：

```text
ScriptTemplateService
ScriptAssetFactory
LuaTemplateGenerator
```

避免过度设计。

---

# 十三、Prefab Drag Create

当前 World Outline Entity 拖到 Browser：

```text
Entity
    -> PrefabSystem::CreatePrefabDocument
    -> CreateAsset
```

迁出：

```cpp
bool CreatePrefabAsset(
    ECS::Entity entity,
    const fs::path &target_directory);
```

Browser 仍负责：

```text
检测 DragPayload
确认 payload 是 WorldOutlineTree Entity
调用 CreatePrefabAsset
```

但不要负责 Prefab Document 的实际构造。

---

# 十四、AssetImportController

完整迁移：

```text
QueueImportFiles
ShowNextImportPopup
ShowImportPopupForFile
AdvanceImportQueue
GetImportPopupType
```

文件 Drop Event 本身可以留在 Browser：

```cpp
void AssetBrowser::HandleFileDrop(UI::UIEvent &event)
{
    _import_controller.QueueFiles(
        event._drop_files,
        _current_path,
        event._mouse_position);
}
```

---

# 十五、Import Queue 必须记录目标目录

不要让 Import Controller 在执行时读取：

```cpp
AssetBrowser::_current_path
```

推荐：

```cpp
struct PendingImport
{
    WString _sys_path;
    fs::path _target_directory;
};
```

接口：

```cpp
class AssetImportController
{
public:
    void QueueFiles(
        const Vector<WString> &files,
        const fs::path &target_directory,
        Vector2f popup_pos);

private:
    void ProcessNext();
    void ShowImportDialog(const PendingImport &item);

private:
    Vector<PendingImport> _queue;
    Vector2f _popup_pos;
};
```

原因：

```text
用户 Drop 文件
    ↓
弹出 Texture Import Popup
    ↓
用户切换 Browser 目录
    ↓
点击 Import
```

最终仍应导入到 Drop 时的目录。

---

# 十六、Texture / Mesh Import

保持现有：

```text
TextureImportSetting
MeshImportSetting
EditorPopup
ResourceMgr::ImportResource
```

不重构 Import Setting 系统。

只把当前 UI 和 Queue 状态迁移到 `AssetImportController`。

---

# 十七、AssetEditorRegistry

当前已经存在：

```text
AssetEditorRegistry
```

优先继续扩展它，不新增另一个 OpenAsset 系统。

目前 `OpenAsset()` 已先尝试：

```cpp
AssetEditorRegistry::Get().CreateEditor(asset);
```

但随后又对：

```text
Scene
Prefab
Mesh
Sprite
InputActionAsset
AudioClip
```

做显式分派。

目标是让：

```cpp
void AssetBrowser::OpenAsset(Asset *asset)
{
    AssetEditorRegistry::Get().Open(asset);
}
```

如果 Registry 当前只支持创建 `DockWindow`，可以增加：

```cpp
using AssetOpenHandler = Function<void(Asset *)>;
```

或：

```cpp
void RegisterOpenHandler(Type *type, AssetOpenHandler handler);
bool Open(Asset *asset);
```

---

# 十八、特殊 Open Handler

允许非 DockWindow 行为。

例如 Scene：

```cpp
RegisterOpenHandler(StaticClass<Scene>(), [](Asset *asset)
{
    Selection::RemoveSlection();
    SceneMgr::Get().OpenScene(asset->_asset_path);
});
```

Prefab：

```text
Load Prefab
Create Temporary Scene
Instantiate Prefab
OpenTemporaryScene
Set Selection
```

全部放在对应 Handler。

Mesh：

```text
Load Asset
```

也可以注册简单 Open Handler。

Browser 不需要 include：

```text
SpriteAssetEditor
InputActionAssetEditor
AudioClipEditor
WidgetEditor
GraphEditorWindow
PrefabSystem
```

---

# 十九、Audio Preview

Audio Context Menu 目前有：

```text
Play Audio
Stop Audio
```

短期可以保留 helper：

```cpp
PlayAudioClipAsset
StopAudioPreview
```

但不要让它阻塞本次重构。

如果 `AssetEditorRegistry` 或 Asset Action Registry 已存在适合扩展的位置，可以把 Audio Action 注册进去。

否则暂时保持现状。

原则：

> 本次重点解决核心耦合，不为了追求绝对纯净继续扩展范围。

---

# 二十、Context Menu 去重

建议增加 Browser 内部公共函数：

```cpp
void AssetBrowser::BuildCreateAssetActions(
    Vector<PopupMenuAction> &actions,
    const fs::path &target_directory,
    Vector2f popup_pos);
```

然后：

```text
ShowBlankAreaContextMenu
ShowFolderContextMenu
```

共用。

Folder Menu 只额外增加：

```text
Open
Rename
Delete
```

Asset Menu 保持：

```text
Open
Asset-specific Actions
Rename
Delete
```

这样可以先解决大部分重复，而不必立即设计复杂 Action Registry。

---

# 二十一、Rename UI 与 Rename Operation 分开

保留 Browser：

```text
BeginFolderRename
BeginAssetRename
```

因为：

```text
EditorPopup::BeginInlineTextInput
```

属于 UI。

但回调内部只调用：

```cpp
_operations.RenameFolder(...);
_operations.RenameAsset(...);
```

Browser 不再知道 rename 的文件系统实现。

---

# 二十二、Delete UI 与 Delete Operation 分开

Browser 保留：

```text
EditorPopup::ShowConfirmAt
```

实际 Delete：

```cpp
_operations.DeleteAsset(asset);
_operations.DeleteFolder(folder);
```

完成后：

```cpp
_content_dirty = true;
_directory_tree_dirty = true;
```

---

# 二十三、Dirty Flag 统一

建议统一命名：

```cpp
_content_dirty
_layout_dirty
_directory_tree_dirty
```

替换语义较模糊的：

```cpp
_is_dirty
_is_icon_layout_dirty
_is_directory_tree_dirty
```

规则：

### Content Dirty

以下操作设置：

```text
Navigate
Search
Create
Delete
Rename
Import Complete
Asset Load State 变化
```

### Layout Dirty

以下操作设置：

```text
Icon Size
List / Icon Mode
Content Area Size
Content Rebuild
```

### Directory Tree Dirty

以下操作设置：

```text
Create Folder
Rename Folder
Delete Folder
```

Asset Rename / Delete 原则上不需要刷新 Tree，除非当前实现依赖它。

---

# 二十四、推荐目录结构

不要过度拆文件。

建议：

```text
Editor/
└── AssetBrowser/
    ├── AssetBrowser.h
    ├── AssetBrowser.cpp
    ├── AssetBrowserContent.h
    ├── AssetBrowserContent.cpp
    ├── AssetBrowserOperations.h
    ├── AssetBrowserOperations.cpp
    ├── AssetImportController.h
    └── AssetImportController.cpp
```

继续复用现有：

```text
Assets/
    AssetTypeRegistry
    AssetEditorRegistry
```

只有在 `AssetTypeRegistry` 明显无法承载 Creation Metadata 时才增加：

```text
AssetCreationRegistry
```

---

# 二十五、不建议创建的类

本次不要增加：

```text
AssetBrowserViewModel
AssetBrowserPresenter
AssetBrowserController
AssetBrowserSelectionManager
AssetBrowserLayoutManager
AssetBrowserContextMenuManager
AssetBrowserSearchService
AssetBrowserPathUtils
AssetBrowserDragDropService
AssetBrowserAudioService
```

这些职责目前体量不足以独立存在。

优先使用：

```text
函数
匿名 namespace helper
现有 Registry
现有 Manager
```

---

# 二十六、实施顺序

## Step 1：拆 AssetBrowserOperations

迁移：

```text
Rename Asset
Rename Folder
Delete Asset
Delete Folder
Create Folder
Name Validation / Unique Name
Asset Header Rename
```

所有操作显式传 Target Directory。

要求：

- 功能不变。
- UI 不变。
- 不改 Asset 格式。

---

## Step 2：取消 `_current_path` 临时切换

删除所有：

```cpp
const fs::path previous_path = _current_path;
_current_path = target;
...
_current_path = previous_path;
```

改成显式参数。

这是优先级非常高的一步。

---

## Step 3：拆 Update

提取：

```text
RefreshContent
CreateFolderWidget
CreateAssetWidget
RefreshContentLayout
```

行为保持一致。

---

## Step 4：拆 AssetBrowserContent

迁移：

```text
Root
Path
Domain
Directory Query
Search
Sort
CollectAssetsUnderDirectory
```

Directory Tree 同时改用它。

---

## Step 5：拆 AssetImportController

迁移完整 Import Queue 与 Popup 状态。

`PendingImport` 保存 target directory。

---

## Step 6：Context Menu 去重

新增：

```text
BuildCreateAssetActions
```

Blank / Folder 共用。

---

## Step 7：Asset Creation 注册化

优先扩展 `AssetTypeRegistry`。

使新增 Asset 类型不再需要修改：

```text
ShowBlankAreaContextMenu
ShowFolderContextMenu
AssetBrowser::CreateXXXEntry
```

---

## Step 8：OpenAsset 收敛到 AssetEditorRegistry

扩展 Registry 支持普通 Open Handler。

删除 Browser 中的具体 Asset Type 分派。

---

# 二十七、验收标准

完成后：

### AssetBrowser

不应直接出现：

```cpp
fs::rename
fs::remove
fs::remove_all

ResourceMgr::CreateAsset
ResourceMgr::DeleteAsset
ResourceMgr::RenameAsset
ResourceMgr::MoveAsset
```

允许：

```text
ResourceMgr 获取 Icon / 简单读取
```

如果 `AssetBrowserContent` 已接管查询，则 Browser 也不应再遍历：

```cpp
ResourceMgr::Get().Begin()
ResourceMgr::Get().End()
```

---

### Update

`AssetBrowser::Update()` 控制在约：

```text
30 ~ 60 行
```

主要表达刷新流程，不承载 Item 构建细节。

---

### Context Menu

Blank Area / Folder 的：

```text
New Folder
New Scene
New Sprite
New Material
New Input Action
New Widget
New Flow Graph
New Script
```

不允许继续维护两份基本相同的代码。

---

### Asset Creation

新增一种 Asset 类型时，原则上不需要修改：

```text
AssetBrowser.cpp
```

只注册新的 Creator。

---

### Open Asset

新增 Editor 时，原则上只需要：

```text
AssetEditorRegistry::Register...
```

不修改 `AssetBrowser::OpenAsset()`。

---

### Import

用户 Drop 文件后，即使 Browser 在 Import Popup 期间切换目录，也必须导入到 Drop 时记录的目录。

---

# 二十八、保持不变的部分

本次不要修改：

```text
ResourceMgr Asset 数据结构
Asset 文件格式
Guid 机制
Prefab 数据格式
EditorPopup 基础设施
TreeView 基础设施
DragDropManager 基础设施
SceneMgr 架构
Dock 系统
Lua Runtime
Graph 系统
```

只重构 Asset Browser 对这些系统的调用边界。

---

# 二十九、最终目标代码形态

理想状态：

```cpp
void AssetBrowser::RefreshContent()
{
    const auto entries = _content.Query(_current_path, _search_text);

    _icon_content->ClearChildren();
    ClearSelection();

    for (const auto &entry : entries)
    {
        if (entry._type == AssetBrowserEntry::EType::kFolder)
            CreateFolderWidget(entry);
        else
            CreateAssetWidget(entry);
    }

    UpdatePathButtons();

    _content_dirty = false;
    _layout_dirty = true;
}
```

创建：

```cpp
_operations.CreateFolder(target_directory, name);

creator._create(target_directory, name);
```

重命名：

```cpp
_operations.RenameAsset(asset, name);
_operations.RenameFolder(path, name);
```

导入：

```cpp
_import_controller.QueueFiles(files, target_directory, popup_pos);
```

打开：

```cpp
AssetEditorRegistry::Get().Open(asset);
```

最终 `AssetBrowser` 只描述：

> 当前展示什么、用户点击了什么、调用哪个已有模块处理、哪些 UI 需要刷新。

---

# 三十、实现原则总结

实现时优先遵守：

1. **复用已有 Registry / Manager。**
2. **不增加无必要的抽象层。**
3. **操作目标显式传参。**
4. **UI 和资源修改逻辑分离。**
5. **减少重复优先于单纯减少文件行数。**
6. **每一步都保持可编译、可运行。**
7. **避免一次性重写 AssetBrowser。**

推荐最终控制在：

```text
AssetBrowser.cpp             约 600 ~ 900 行
AssetBrowserContent.cpp      约 250 ~ 400 行
AssetBrowserOperations.cpp   约 300 ~ 500 行
AssetImportController.cpp    约 150 ~ 250 行
```

行数不是硬指标。

真正指标是：

> `AssetBrowser` 不再知道具体 Asset 如何创建、修改、删除、导入和打开。
