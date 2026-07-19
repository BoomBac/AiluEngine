#ifndef __ASSETBROWSER_H__
#define __ASSETBROWSER_H__
#include "Dock/DockWindow.h"
#include "generated/AssetBrowser.gen.h"

namespace Ailu
{
    class Asset;
    namespace Render
    {
        class Mesh;
        class RenderTexture;
    }
    namespace UI
    {
        class Image;
        class SplitView;
        class Canvas;
        class VerticalBox;
        class Slider;
        class Text;
        class ScrollView;
    }// namespace UI
    namespace Editor
    {
        ACLASS()
        class AssetBrowser : public DockWindow
        {
            GENERATED_BODY()
        public:
            AssetBrowser();
            void Update(f32 dt) final;

        private:
            void QueueImportFiles(const Vector<WString> &files, Vector2f popup_pos);
            void ShowNextImportPopup();
            void ShowImportPopupForFile(const WString &sys_path);
            void AdvanceImportQueue();
            void OpenAsset(Asset *asset);
            void ShowBlankAreaContextMenu(Vector2f popup_pos);
            void ShowFolderContextMenu(const WString &folder_sys_path, Vector2f popup_pos);
            void ShowAssetContextMenu(Asset *asset, Vector2f popup_pos);
            bool RenameAssetEntry(Asset *asset, const String &new_name);
            bool RenameFolderEntry(const WString &folder_sys_path, const String &new_name);
            void DeleteAssetEntry(Asset *asset);
            void DeleteFolderEntry(const WString &folder_sys_path);
            bool CreateFolderEntry(const String &name);
            bool CreateSceneEntry(const String &name);
            bool CreateMaterialEntry(const String &name);
            WString CurrentAssetDirectoryPath() const;
            WString BuildCurrentAssetPath(const WString &file_name) const;
            Vector<Asset *> CollectAssetsUnderDirectory(const WString &directory_asset_path) const;
            String MakeUniqueEntryName(const WString &directory_sys_path, const String &base_name, const WString &extension, bool is_directory) const;
            void SaveDockLayoutState(JsonArchive &ar) override;
            void LoadDockLayoutState(JsonArchive &ar) override;
            void OnDockLayoutLoaded() override;

            inline static const f32 kDragThreshold = 5.0f;
            APROPERTY(Category = "DockLayout")
            f32 _split_ratio = 0.5f;
            UI::SplitView *_sv = nullptr;
            UI::VerticalBox *_right = nullptr;
            UI::ScrollView *_icon_area = nullptr;
            UI::Canvas * _icon_content = nullptr;
            UI::Text *_path_title;

            bool _is_dirty = true;
            bool _is_icon_layout_dirty = true;
            f32 _icon_size = 64.0f;
            UI::UIElement *_hover_item = nullptr;
            fs::path _current_path;
            Vector2f _last_icon_area_size = Vector2f::kZero;
            Vector<Asset *> _cur_dir_assets;
            Vector<WString> _pending_import_files;
            Vector2f _import_popup_pos = Vector2f::kZero;
            HashMap<Render::Mesh *, Ref<Render::RenderTexture>> _mesh_preview_icons;
            bool _is_dragging = false;
            Vector2f _drag_start_pos;
        };
    }// namespace Editor
}// namespace Ailu
#endif// !__ASSETBROWSER_H__
