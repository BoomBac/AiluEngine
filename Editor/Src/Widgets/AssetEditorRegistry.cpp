#include "Widgets/AssetEditorRegistry.h"
#include "Assets/Asset.h"

namespace Ailu
{
    namespace Editor
    {
        AssetEditorRegistry &AssetEditorRegistry::Get()
        {
            static AssetEditorRegistry s_registry;
            return s_registry;
        }

        void AssetEditorRegistry::RegisterEditor(const Type *asset_type, CreateEditorFunc create_editor)
        {
            if (asset_type == nullptr || !create_editor)
                return;
            _editors[asset_type] = std::move(create_editor);
        }

        Ref<DockWindow> AssetEditorRegistry::CreateEditor(Asset *asset) const
        {
            if (asset == nullptr || asset->_asset_type == nullptr)
                return nullptr;
            auto iter = _editors.find(asset->_asset_type);
            if (iter == _editors.end())
                return nullptr;
            return iter->second(asset);
        }

        bool AssetEditorRegistry::HasEditor(const Type *asset_type) const
        {
            return asset_type != nullptr && _editors.find(asset_type) != _editors.end();
        }
    }// namespace Editor
}// namespace Ailu
