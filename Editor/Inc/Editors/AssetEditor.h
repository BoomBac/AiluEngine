#pragma once

#include "Dock/DockWindow.h"
#include "Assets/Asset.h"

namespace Ailu
{
    class Asset;

    namespace Editor
    {
        class AssetEditor : public DockWindow
        {
        public:
            AssetEditor(const String &title, Vector2f size);
            ~AssetEditor() override = default;

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

            void RefreshTitle();
            void ShowClosePrompt();

        private:
            String _editor_title;
            Asset *_asset = nullptr;
        };
    }// namespace Editor
}// namespace Ailu
