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

            static AssetEditorRegistry &Get();
            void RegisterEditor(const Type *asset_type, CreateEditorFunc create_editor);
            Ref<DockWindow> CreateEditor(Asset *asset) const;
            bool HasEditor(const Type *asset_type) const;

        private:
            Map<const Type *, CreateEditorFunc> _editors;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSETEDITORREGISTRY_H__
