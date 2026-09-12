#ifndef __ASSETBROWSER_H__
#define __ASSETBROWSER_H__
#include "Dock/DockWindow.h"
#include "Scene/Entity.h"
#include "Widgets/AssetBrowserContent.h"
#include "Widgets/AssetBrowserOperations.h"
#include "Widgets/AssetImportController.h"
#include "Common/EditorPopup.h"
#include "UI/DragDrop.h"
#include <filesystem>
#include <tuple>
#include "generated/AssetBrowser.gen.h"

namespace Ailu
{
    class Asset;
    namespace UI
    {
        class Image;
        class UIElement;
        class SplitView;
        class Canvas;
        class HorizontalBox;
        class InputBlock;
        class VerticalBox;
        class Slider;
        class Text;
        class ScrollView;
        class TreeView;
    }// namespace UI
    namespace Editor
    {
        class DirectoryTreeDataSource;

        ACLASS()
        class AssetBrowser : public DockWindow
        {
            GENERATED_BODY()
        public:
            AssetBrowser();
            ~AssetBrowser() override;
            void Update(f32 dt) final;

        private:
            struct SelectedEntry
            {
                fs::path _path;
                Asset *_asset = nullptr;
                Guid _sub_asset_guid = Guid::EmptyGuid();
                UI::UIElement *_root = nullptr;
                UI::Text *_text = nullptr;
            };

            struct AssetDragData
            {
                Vector<Asset *> _assets;
            };

            void HandleShortcuts();
            void HandleFileDrop(UI::UIEvent &e);

            void NavigateToPath(const std::filesystem::path &path);
            void OpenInFileExplorer(const std::filesystem::path &path, bool select_path = false);
            void RefreshDirectoryTree();
            void RefreshContent();
            void RefreshContentLayout();
            void UpdatePathButtons();
            void UpdateSelectedPathDisplay();

            void OpenAsset(Asset *asset);
            void ShowDeleteSelectionConfirm(Vector2f popup_pos, Asset *fallback_asset = nullptr);
            void ShowBlankAreaContextMenu(Vector2f popup_pos);
            void ShowFolderContextMenu(const WString &folder_sys_path, Vector2f popup_pos, UI::UIElement *item_root, UI::Text *item_text);
            void ShowAssetContextMenu(Asset *asset, const std::filesystem::path &asset_sys_path, Vector2f popup_pos,
                                      UI::UIElement *item_root, UI::Text *item_text);
            void BuildCreateAssetActions(Vector<PopupMenuAction> &actions, const std::filesystem::path &target_directory, Vector2f popup_pos);
            void BeginFolderRename(const WString &folder_sys_path, UI::UIElement *item_root, UI::Text *item_text);
            void BeginAssetRename(Asset *asset, UI::UIElement *item_root, UI::Text *item_text);

            void CreateFolderWidget(const AssetBrowserEntry &entry);
            void CreateAssetWidget(const AssetBrowserEntry &entry);
            void CreateSubAssetWidget(const AssetBrowserEntry &entry);

            std::tuple<Ref<UI::UIElement>, UI::Image *, UI::Text *> CreateEntryWidgetRoot(const String &display_name,
                                                                                           bool is_sub_asset = false);

            void SelectFolder(const fs::path &path, u32 index, UI::UIElement *root, UI::Text *text, bool preserve_modifiers);
            void SelectAsset(Asset *asset, u32 index, UI::UIElement *root, UI::Text *text, bool preserve_modifiers);
            void SelectSubAsset(const AssetBrowserEntry &entry, u32 index, UI::UIElement *root, UI::Text *text,
                                bool preserve_modifiers);
            void SelectEntry(const fs::path &path, Asset *asset, u32 index, UI::UIElement *root, UI::Text *text,
                             const Guid &sub_asset_guid, bool preserve_modifiers);
            void ClearSelection();
            bool IsSelected(const fs::path &path, const Guid &sub_asset_guid = Guid::EmptyGuid()) const;
            bool IsAssetExpanded(const Asset *asset) const;
            void ToggleAssetExpanded(Asset *asset);
            void UpdateEntryVisual(UI::UIElement *root, bool is_hovered);
            void UpdateSelectionVisuals();
            void SyncPrimarySelection();
            Vector<Asset *> GetSelectedAssets() const;
            Vector<Asset *> GetDraggedAssets(const UI::DragPayload &payload) const;
            bool CanAcceptAssetDrag(const UI::DragPayload &payload) const;
            void ShowAssetTransferDialog(const UI::DragPayload &payload, const fs::path &target_directory, Vector2f popup_pos);
            void BeginAssetDrag(Asset *asset, const String &display_name);
            void CopySelectionToClipboard();
            void PasteClipboard();

            void SaveDockLayoutState(JsonArchive &ar) override;
            void LoadDockLayoutState(JsonArchive &ar) override;
            void OnDockLayoutLoaded() override;

            inline static const f32 kDragThreshold = 5.0f;
            APROPERTY(Category = "DockLayout")
            f32 _split_ratio = 0.5f;
            UI::SplitView *_sv = nullptr;
            UI::VerticalBox *_right = nullptr;
            UI::TreeView *_directory_tree = nullptr;
            UI::HorizontalBox *_path_bar = nullptr;
            UI::InputBlock *_search_input = nullptr;
            UI::ScrollView *_icon_area = nullptr;
            UI::Canvas * _icon_content = nullptr;
            UI::Text *_path_title = nullptr;
            UI::Text *_selected_path_title = nullptr;

            bool _content_dirty = true;
            bool _layout_dirty = true;
            bool _directory_tree_dirty = true;
            // AssetTypeRegistry::PreviewRevision 的快照：预览从"占位图标"变成真预览后要重建一次
            u32 _preview_revision = 0u;
            f32 _icon_size = 64.0f;
            UI::UIElement *_hover_item = nullptr;
            std::filesystem::path _current_path;
            String _search_text;
            DirectoryTreeDataSource *_directory_tree_data_source = nullptr;
            Vector2f _last_icon_area_size = Vector2f::kZero;
            bool _is_list_view = false;
            Vector<AssetBrowserEntry> _visible_entries;
            Vector<Guid> _expanded_asset_guids;
            Vector<SelectedEntry> _selected_entries;
            i32 _selection_anchor = -1;
            UI::UIElement *_selected_item_root = nullptr;
            UI::Text *_selected_item_text = nullptr;
            Asset *_selected_asset = nullptr;
            WString _selected_folder_path;
            Asset *_drag_source_asset = nullptr;
            bool _is_dragging = false;
            Vector2f _drag_start_pos;
            AssetDragData _asset_drag_data;
            Vector<Asset *> _clipboard_assets;

            AssetBrowserContent _content;
            AssetBrowserOperations _operations;
            AssetImportController _import_controller;
        };
    }// namespace Editor
}// namespace Ailu
#endif// !__ASSETBROWSER_H__
