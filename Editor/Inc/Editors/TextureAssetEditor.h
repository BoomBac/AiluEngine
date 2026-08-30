#pragma once
#ifndef __TEXTURE_ASSET_EDITOR_H__
#define __TEXTURE_ASSET_EDITOR_H__

#include "Editors/AssetEditor.h"
#include "Editors/TexturePreviewWidget.h"
#include "Framework/Core/SmartPtr.h"

namespace Ailu
{
    struct TextureImportSetting;

    namespace Render
    {
        class Texture2D;
    }

    namespace UI
    {
        class Button;
        class HorizontalBox;
        class ScrollView;
        class Text;
        class VerticalBox;
    }

    namespace Editor
    {
        class ReflectedPropertyPanel;

        class TextureAssetEditor final : public AssetEditor
        {
        public:
            TextureAssetEditor();
            ~TextureAssetEditor() override;

            void Update(f32 dt) override;

        protected:
            bool OnOpen() override;
            void OnClose() override;
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
            void SelectChannel(u32 channel);
            void NextMip();
            void Reimport();
            void MarkDirty();

            Render::Texture2D *_texture = nullptr;
            TextureImportSetting *_import_setting = nullptr;
            Scope<ReflectedPropertyPanel> _import_panel;
            TexturePreviewWidget *_preview = nullptr;
            UI::VerticalBox *_info_root = nullptr;
            UI::VerticalBox *_import_root = nullptr;
            UI::Button *_btn_channel = nullptr;
            UI::Button *_btn_mip = nullptr;
            UI::Text *_txt_asset_name = nullptr;
            UI::Text *_txt_asset_path = nullptr;
            UI::Text *_txt_source_path = nullptr;
            UI::Text *_txt_asset_guid = nullptr;
            UI::Text *_txt_width = nullptr;
            UI::Text *_txt_height = nullptr;
            UI::Text *_txt_format = nullptr;
            UI::Text *_txt_mips = nullptr;
            UI::Text *_txt_dimension = nullptr;
            UI::Text *_txt_memory = nullptr;
            UI::Text *_txt_readable = nullptr;
            UI::Text *_txt_srgb = nullptr;
            u32 _channel = 0u;
        };
    }
}

#endif // __TEXTURE_ASSET_EDITOR_H__
