#pragma once
#ifndef __MESH_ASSET_EDITOR_H__
#define __MESH_ASSET_EDITOR_H__

#include "Animation/AssetPreviewViewport3D.h"
#include "Editors/AssetEditor.h"
#include "Framework/Core/SmartPtr.h"

namespace Ailu
{
    namespace Render
    {
        class Image;
        class Mesh;
    }

    struct MeshImportSetting;

    namespace UI
    {
        class Button;
        class CheckBox;
        class HorizontalBox;
        class Image;
        class ScrollView;
        class Text;
        class VerticalBox;
    }

    namespace Editor
    {
        class ReflectedPropertyPanel;

        class MeshAssetEditor final : public AssetEditor
        {
        public:
            MeshAssetEditor();
            ~MeshAssetEditor() override;

            void Update(f32 dt) override;

        protected:
            bool OnOpen() override;
            void OnBeforeSave() override;
            void OnAssetSaved() override;
            void OnAssetReloaded() override;

        private:
            void BuildToolbar(UI::HorizontalBox *toolbar);
            void BuildInfoPanel(UI::VerticalBox *panel);
            void BuildImportPanel(UI::VerticalBox *panel);
            void RefreshAllUI();
            void RefreshAssetInfo();
            void RefreshImportSettings();
            void RefreshPreview();
            void Reimport();
            void FocusPreview();
            void ToggleGrid();
            void ToggleWireframe();
            void MarkDirty();

            Render::Mesh *_mesh = nullptr;
            MeshImportSetting *_import_setting = nullptr;
            AssetPreviewViewport3D _preview;
            Scope<ReflectedPropertyPanel> _import_panel;

            UI::Image *_preview_image = nullptr;
            UI::VerticalBox *_info_root = nullptr;
            UI::VerticalBox *_import_root = nullptr;
            UI::Text *_txt_asset_name = nullptr;
            UI::Text *_txt_asset_path = nullptr;
            UI::Text *_txt_source_path = nullptr;
            UI::Text *_txt_asset_guid = nullptr;
            UI::Text *_txt_vertex_count = nullptr;
            UI::Text *_txt_triangle_count = nullptr;
            UI::Text *_txt_submesh_count = nullptr;
            UI::Text *_txt_uv_channels = nullptr;
            UI::Text *_txt_attributes = nullptr;
            UI::Text *_txt_bounds = nullptr;
            UI::Text *_txt_derived_data = nullptr;
            UI::Button *_btn_reimport = nullptr;
            UI::Button *_btn_grid = nullptr;
            UI::Button *_btn_wireframe = nullptr;
            UI::CheckBox *_chk_grid = nullptr;
            UI::CheckBox *_chk_wireframe = nullptr;
            bool _show_grid = true;
            bool _wireframe = false;
        };
    }
}

#endif // __MESH_ASSET_EDITOR_H__
