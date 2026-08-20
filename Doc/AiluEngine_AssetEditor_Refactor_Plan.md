# AiluEngine AssetEditor 与 Dirty 机制重构任务包

## 目标

统一现有资产编辑器的公共框架与 Dirty 机制，减少 `SpriteAssetEditor`、`AudioClipEditor`、`InputActionAssetEditor`、`AnimationClipEditor`、`AnimationControllerEditor`、`WidgetEditor`、`GraphEditorWindow` 之间重复的生命周期、保存、快捷键、布局和 Dirty 代码。

核心原则：

- `Asset` 是 Dirty 状态的唯一权威来源。
- `AssetEditor` 只负责资产编辑器公共生命周期和保存行为，不绑定具体布局。
- 具体编辑器直接编辑 Asset/runtime object，不再维护通用 `_editing/_original/_is_dirty` 双状态。
- Undo/Redo 必须能正确恢复 revision，使“Undo 回保存点后自动 Clean”。

---

## 一、统一 Dirty / Revision

当前 `Asset` 已有 `_revision / _saved_revision`，保留该方向并完善为可恢复的状态 ID。

建议：

```cpp
class Asset : public Object
{
public:
    using Revision = u64;

    Revision MarkModified();
    void RestoreRevision(Revision revision);

    bool IsDirty() const { return _revision != _saved_revision; }
    Revision GetRevision() const { return _revision; }
    void MarkSaved(Revision revision) { _saved_revision = revision; }

private:
    Revision _revision = 0;
    Revision _saved_revision = 0;
    Revision _next_revision = 0;
};
```

`MarkModified()` 使用：

```cpp
_revision = ++_next_revision;
```

不要简单 `++_revision`，否则 Undo 后再编辑可能复用旧 revision，导致错误地判断为 Clean。

删除/淘汰各编辑器自己的：

```cpp
bool _is_dirty;
_original;
_editing;
```

作为通用 Dirty 来源。

Dirty 的唯一定义：

```cpp
asset->IsDirty();
```

---

## 二、Undo / Redo 与 Revision 协作

资产编辑命令需要记录前后 revision：

```cpp
class AssetEditCommand : public ICommand
{
protected:
    Asset *_asset = nullptr;
    Asset::Revision _before_revision = 0;
    Asset::Revision _after_revision = 0;
};
```

行为：

```text
Execute:
    保存 before_revision
    应用修改
    after_revision = asset->MarkModified()

Undo:
    恢复旧值
    asset->RestoreRevision(before_revision)

Redo:
    恢复新值
    asset->RestoreRevision(after_revision)
```

Redo 不应创建新 revision。

要求：

- Undo 回到 `_saved_revision` 时 `IsDirty()` 自动变 false。
- Undo 后新编辑必须生成新的、不会碰撞的 revision。

---

## 三、AssetEditor 基类

新增：

```text
Editor/Inc/Editors/AssetEditor.h
Editor/Src/Editors/AssetEditor.cpp
```

建议接口：

```cpp
class AssetEditor : public DockWindow
{
public:
    AssetEditor(const String &title, Vector2f size);
    ~AssetEditor() override = default;

    bool Open(Asset *asset);
    void Close();

    void Update(f32 dt) override;
    void RequestClose() override;

    bool IsDirty() const;
    bool Save();
    bool DiscardChanges();

    Asset *GetAsset() const { return _asset; }

    template<typename T>
    T *GetAssetObject() const
    {
        return _asset != nullptr ? _asset->As<T>() : nullptr;
    }

protected:
    virtual bool OnOpen() { return true; }
    virtual void OnClose() {}
    virtual void OnUpdate(f32 dt) {}
    virtual void OnAssetReloaded() {}
    virtual void RefreshEditor() {}

private:
    void RefreshTitle();
    void ShowClosePrompt();

private:
    Asset *_asset = nullptr;
};
```

### AssetEditor 负责

- 保存 `Asset*`。
- `Ctrl + S`。
- `Save()`。
- Dirty 标题，例如 `Sprite Editor - foo *`。
- 关闭时 `Save / Discard / Cancel` 提示。
- 通用 Open / Close 生命周期。
- 保存后刷新公共状态。

### AssetEditor 不负责

- Sprite/Graph/Animation 的具体编辑数据。
- Timeline、GraphCanvas、Preview 等业务 UI。
- 三栏/两栏布局。
- 属性输入控件构造。

---

## 四、统一为 Live Edit

具体编辑器直接修改 runtime asset，不再先写 `_editing` 再 Apply。

旧模式：

```text
UI -> _editing -> Apply -> Runtime Asset -> Save
```

新模式：

```text
UI -> Command -> Runtime Asset -> Asset::MarkModified() -> Save
```

例如 Sprite Pivot 修改通过 Undo Command 完成，Preview 直接读取 `Render::Sprite`。

通用层取消 `Apply / Revert` 语义，只保留：

```text
Edit
Undo
Redo
Save
Discard
```

如果某种资产确实存在“Apply Shader / Compile / Reimport”等行为，作为该编辑器自己的业务操作保留，不参与 Dirty 通用机制。

---

## 五、ResourceMgr 增加 Discard / Reload 能力

新增类似：

```cpp
bool ResourceMgr::ReloadAsset(Asset *asset);
```

要求尽量采用 in-place reload：

- 从磁盘重新反序列化到现有 runtime object。
- 不替换已有 Object 指针，避免 `Ref<T>` / 裸指针失效。
- Reload 成功后 revision 回到 Clean 状态。

用途：

- AssetEditor `Discard Changes`。
- 外部文件修改热重载。
- Git checkout 后刷新。

`SaveAsset()` 当前“捕获保存开始时的 revision，成功后 `MarkSaved(revision)`”的方式保留，保证未来异步保存期间再次修改时 Dirty 状态正确。

---

## 六、GraphDocument 简化

当前 GraphDocument 自己维护：

```cpp
_original_nodes
_original_links
_original_comments
_editing_nodes
_editing_links
_editing_comments
_is_dirty
```

目标：逐步移除 GraphDocument 自己的 Dirty 状态。

GraphDocument 主要保留：

```text
GraphAsset*
GraphCommandStack
indices
schema
validation
```

Graph 修改最终直接作用到 `GraphAsset`，并由 Asset revision 判断 Dirty。

如果一次重构直接去掉 editing/original 风险较大，可分两步：

1. 先删除 `_is_dirty`，Dirty 改为 Asset revision。
2. 再逐步将 `_editing_* / _original_*` 收敛到直接编辑 GraphAsset。

---

## 七、布局复用不要塞进 AssetEditor

新增轻量布局辅助，例如：

```text
AssetEditorLayout.h/.cpp
```

提供：

```cpp
BuildThreePanelAssetEditorLayout(...);
BuildTwoPanelAssetEditorLayout(...);
AddAssetEditorToolbar(...);
AddAssetEditorStatusBar(...);
```

三栏布局可返回：

```cpp
struct AssetEditorThreePanel
{
    UI::HorizontalBox *_toolbar = nullptr;
    UI::UIElement *_left = nullptr;
    UI::UIElement *_center = nullptr;
    UI::UIElement *_right = nullptr;
    UI::HorizontalBox *_status_bar = nullptr;
    UI::SplitView *_main_split = nullptr;
    UI::SplitView *_right_split = nullptr;
};
```

`SpriteAssetEditor` / `InputActionAssetEditor` 使用三栏辅助；`AudioClipEditor` 使用两栏；Widget/Graph 可自由组合。

---

## 八、统一 Editor UI Helper

目前多个 Editor 重复实现：

```text
AddSectionTitle
AddPropertyRow
AddTextInput
AddFloatInput
AddDropdown
AddCheckBox
```

项目已有 `ComponentEditorHelpers.h`，建议提升/改名为通用：

```text
EditorUIHelpers.h
```

让 Inspector 与 AssetEditor 共用，避免 AssetEditor 基类继续膨胀。

---

## 九、AssetEditorRegistry 简化

改为 AssetEditor 统一接收 `Asset*` 后，可增加模板注册：

```cpp
template<typename TAsset, typename TEditor>
void RegisterEditor();
```

最终注册尽量收敛为：

```cpp
RegisterEditor<GraphAsset, GraphEditorWindow>();
RegisterEditor<WidgetAsset, WidgetEditor>();
RegisterEditor<Render::Sprite, SpriteAssetEditor>();
RegisterEditor<AnimationClip, AnimationClipEditor>();
RegisterEditor<AnimationControllerAsset, AnimationControllerEditor>();
RegisterEditor<InputActionAsset, InputActionAssetEditor>();
RegisterEditor<AudioClip, AudioClipEditor>();
```

模板内部统一处理：

```text
asset 判空
Load<TAsset>
创建 TEditor
editor->Open(asset)
```

---

## 十、推荐迁移顺序

### Phase 1：基础设施

1. 完善 `Asset` revision。
2. Undo/Redo 支持 revision 恢复。
3. 实现 `AssetEditor`。
4. 实现 `ResourceMgr::ReloadAsset()`。
5. 实现通用关闭 Dirty 提示。

### Phase 2：简单 Editor 验证

优先迁移：

1. `SpriteAssetEditor`
2. `AudioClipEditor`

验证：

- Live Edit。
- Undo/Redo。
- Undo 回保存点自动 Clean。
- Save。
- Discard。
- 关闭确认。

### Phase 3：其余 Editor

迁移：

```text
InputActionAssetEditor
AnimationClipEditor
AnimationControllerEditor
WidgetEditor
GraphEditorWindow
```

Graph 最后处理，避免同时改 GraphDocument 和 AssetEditor 带来过大风险。

---

## 验收标准

- 所有 AssetEditor 的 Dirty 只来自 `Asset::IsDirty()`。
- 不再存在 Editor 自己维护的通用 `_is_dirty`。
- Undo 回到最后保存 revision 后窗口自动恢复 Clean。
- Undo 后新编辑不会与旧 revision 冲突。
- `Ctrl+S` 由 AssetEditor 统一处理。
- Dirty 窗口关闭统一出现 Save / Discard / Cancel。
- Save 期间继续编辑不会错误清除 Dirty。
- Sprite / Audio 等编辑器不再维护通用 `_editing/_original` 数据副本。
- GraphDocument 最终不再拥有独立 Dirty 状态。
- AssetEditor 不绑定三栏布局，特殊编辑器仍可自由构建 UI。
- 尽量复用现有 Undo、ResourceMgr、DockWindow、UI Helper，不新增重复系统。

