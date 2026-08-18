#pragma once
#ifndef __ASSETEDITORREGISTRY_H__
#define __ASSETEDITORREGISTRY_H__

#include "Dock/DockWindow.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/SmartPtr.h"

#include <functional>

namespace Ailu
{
    class Asset;
    class Type;

    namespace Editor
    {
        class AssetEditorRegistry
        {
        public:
            using CreateEditorFunc = std::function<Ref<DockWindow>(Asset *)>;
            using AssetOpenHandler = std::function<void(Asset *)>;

            static AssetEditorRegistry &Get();
            void RegisterEditor(const Type *asset_type, CreateEditorFunc create_editor);
            Ref<DockWindow> CreateEditor(Asset *asset) const;
            bool HasEditor(const Type *asset_type) const;

            // 普通打开行为（不创建 DockWindow，例如 Scene / Prefab / Script / Mesh）。
            void RegisterOpenHandler(const Type *asset_type, AssetOpenHandler handler);
            bool HasOpenHandler(const Type *asset_type) const;
            bool CanOpen(const Type *asset_type) const;
            bool Open(Asset *asset);

        private:
            AssetEditorRegistry();

        private:
            Map<const Type *, CreateEditorFunc> _editors;
            Map<const Type *, AssetOpenHandler> _open_handlers;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSETEDITORREGISTRY_H__
