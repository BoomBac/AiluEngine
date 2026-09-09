#pragma once

#include "Dock/DockWindow.h"
#include "Assets/Asset.h"
#include "Framework/Interface/IParser.h"

namespace Ailu
{
    class Asset;
    namespace UI
    {
        class HorizontalBox;
    }

    namespace Editor
    {
        class AssetEditor : public DockWindow
        {
        public:
            AssetEditor(const String &title, Vector2f size);
            ~AssetEditor() override;

            bool Open(Asset *asset);
            virtual void Close();

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
            virtual void OnUpdate(f32) {}
            virtual void OnAssetReloaded() {}
            virtual void OnBeforeSave() {}
            virtual void OnAssetSaved() {}
            virtual void RefreshEditor() {}

            void SetEditorImportSetting(const ImportSetting &setting);
            ImportSetting *GetEditorImportSetting() const { return _editor_import_setting.get(); }
            void AddAssetMenu(UI::HorizontalBox *toolbar);
            void RefreshTitle();
            void ShowClosePrompt();

        private:
            void HandleAssetReloaded(Asset *asset);

            String _editor_title;
            Asset *_asset = nullptr;
            Scope<ImportSetting> _editor_import_setting;
            u64 _asset_reload_listener_id = 0u;
        };
    }// namespace Editor
}// namespace Ailu
