#pragma once
#ifndef __MATERIAL_ASSET_EDITOR_H__
#define __MATERIAL_ASSET_EDITOR_H__

#include "Animation/AssetPreviewViewport3D.h"
#include "Editors/AssetEditor.h"

namespace Ailu
{
    namespace Render
    {
        class Material;
        class Mesh;
        class Shader;
        struct ShaderPropertyInfo;
    }

    namespace UI
    {
        class Button;
        class Dropdown;
        class HorizontalBox;
        class Image;
        class InputBlock;
        class ObjectAssetDropdown;
        class ScrollView;
        class Text;
        class VerticalBox;
    }

    namespace Editor
    {
        class MaterialAssetEditor final : public AssetEditor
        {
        public:
            MaterialAssetEditor();
            ~MaterialAssetEditor() override;

            void Update(f32 dt) override;

        protected:
            bool OnOpen() override;
            void OnClose() override;
            void OnBeforeSave() override;
            void OnAssetSaved() override;
            void OnAssetReloaded() override;

        private:
            void BuildToolbar(UI::HorizontalBox *toolbar);
            void BuildPropertyPanel(UI::VerticalBox *panel);
            void BuildInfoPanel(UI::VerticalBox *panel);
            void RefreshAllUI();
            void RefreshProperties();
            void RefreshInfo();
            void RefreshPreview();
            void SetPreviewMesh(Render::Mesh *mesh, const String &name);
            void SetShader(Render::Shader *shader);
            void MarkDirty();
            void BuildProperty(UI::VerticalBox *panel, Render::ShaderPropertyInfo &property);

            Render::Material *_material = nullptr;
            Ref<Render::Mesh> _preview_mesh_ref;
            Render::Mesh *_preview_mesh = nullptr;
            AssetPreviewViewport3D _preview;
            UI::Image *_preview_image = nullptr;
            UI::ObjectAssetDropdown *_shader_dropdown = nullptr;
            UI::Dropdown *_surface_dropdown = nullptr;
            UI::Dropdown *_cull_dropdown = nullptr;
            UI::InputBlock *_render_queue_input = nullptr;
            UI::VerticalBox *_property_root = nullptr;
            UI::Text *_txt_shader = nullptr;
            UI::Text *_txt_surface = nullptr;
            UI::Text *_txt_render_queue = nullptr;
            UI::Text *_txt_keywords = nullptr;
            UI::Text *_txt_mesh = nullptr;
            UI::Button *_btn_sphere = nullptr;
            UI::Button *_btn_cube = nullptr;
            UI::Button *_btn_plane = nullptr;
            String _preview_mesh_name = "Sphere";
            bool _show_grid = true;
        };
    }
}

#endif // __MATERIAL_ASSET_EDITOR_H__
