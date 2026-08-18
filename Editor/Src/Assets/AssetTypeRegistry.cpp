#include "Assets/AssetTypeRegistry.h"

#include "Widgets/AssetBrowserOperations.h"

#include "Assets/Asset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/2D/Sprite.h"
#include "Render/AssetPreviewGenerator.h"
#include "Render/Mesh.h"
#include "Render/Texture.h"

#include <filesystem>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            constexpr const wchar_t *kAssetIconDirectory = L"editor://Icons/Assets/";
            constexpr const wchar_t *kDefaultAssetIconName = L"default.alasset";

            Ref<Render::Texture> GenerateMeshPreview(Asset *asset)
            {
                if (asset == nullptr || asset->_p_obj == nullptr)
                    return nullptr;

                auto mesh = asset->As<Render::Mesh>();
                if (mesh == nullptr)
                    return nullptr;

                Ref<Render::RenderTexture> preview;
                AssetPreviewGenerator::GeneratorMeshSnapshot(512u, 512u, mesh, preview);
                return std::static_pointer_cast<Render::Texture>(preview);
            }

            Ref<Render::Texture> GenerateSpritePreview(Asset *asset)
            {
                if (asset == nullptr || asset->_p_obj == nullptr)
                    return nullptr;

                auto sprite = asset->As<Render::Sprite>();
                if (sprite == nullptr)
                    return nullptr;

                Ref<Render::RenderTexture> preview;
                AssetPreviewGenerator::GeneratorSpriteSnapshot(256u, 256u, sprite, preview);
                return std::static_pointer_cast<Render::Texture>(preview);
            }

            Ref<Render::Texture> GetTexturePreview(Asset *asset)
            {
                if (asset == nullptr)
                    return nullptr;

                auto texture = asset->AsRef<Render::Texture2D>();
                if (texture == nullptr)
                    texture = ResourceMgr::Get().Load<Render::Texture2D>(asset->_asset_path);
                return texture == nullptr ? nullptr : std::static_pointer_cast<Render::Texture>(texture);
            }
        }

        AssetTypeRegistry &AssetTypeRegistry::Get()
        {
            static AssetTypeRegistry s_registry;
            return s_registry;
        }

        AssetTypeRegistry::AssetTypeRegistry()
        {
            RegisterPreview(Render::Mesh::StaticType(), GenerateMeshPreview);
            RegisterPreview(Render::Sprite::StaticType(), GenerateSpritePreview);
            RegisterPreview(Render::Texture2D::StaticType(), GetTexturePreview);

            RegisterCreator({
                "New Scene", "Create Scene", "NewScene", "Scene already exists.", L".almap",
                [](const fs::path &directory, const String &name) { return CreateSceneAsset(directory, name); },
                nullptr
            });
            RegisterCreator({
                "New Sprite", "Create Sprite", "NewSprite", "Sprite already exists.", L".alasset",
                [](const fs::path &directory, const String &name) { return CreateSpriteAsset(directory, name); },
                nullptr
            });
            RegisterCreator({
                "New Material", "Create Material", "NewMaterial", "Material already exists.", L".alasset",
                nullptr,
                [](const fs::path &directory, Vector2f popup_pos) { ShowCreateMaterialDialog(popup_pos, directory); }
            });
            RegisterCreator({
                "New Input Action Asset", "Create Input Action Asset", "NewInputActions", "Input Action Asset already exists.", L".alasset",
                [](const fs::path &directory, const String &name) { return CreateInputActionAsset(directory, name); },
                nullptr
            });
            RegisterCreator({
                "New Widget Asset", "Create Widget Asset", "NewWidget", "Widget Asset already exists.", L".alasset",
                [](const fs::path &directory, const String &name) { return CreateWidgetAsset(directory, name); },
                nullptr
            });
            RegisterCreator({
                "New Flow Graph", "Create Flow Graph", "NewFlowGraph", "Flow Graph already exists.", L".alasset",
                [](const fs::path &directory, const String &name) { return CreateFlowGraphAsset(directory, name); },
                nullptr
            });
            RegisterCreator({
                "New Script", "Create Script", "NewScript", "Script already exists.", L".lua",
                [](const fs::path &directory, const String &name) { return CreateScriptAsset(directory, name); },
                nullptr
            });
        }

        void AssetTypeRegistry::RegisterCreator(AssetCreatorDesc creator)
        {
            if (creator._menu_name.empty() || (!creator._create && !creator._create_dialog))
                return;
            _creators.push_back(std::move(creator));
        }

        const Vector<AssetCreatorDesc> &AssetTypeRegistry::Creators() const
        {
            return _creators;
        }

        Render::Texture *AssetTypeRegistry::GetIcon(Asset *asset)
        {
            if (asset == nullptr || asset->_asset_type == nullptr)
                return GetStaticIcon(nullptr);

            if (auto provider_it = _preview_providers.find(asset->_asset_type); provider_it != _preview_providers.end())
            {
                Ref<Render::Texture> preview = provider_it->second(asset);
                if (preview != nullptr)
                {
                    auto cache_it = _preview_cache.find(asset);
                    if (cache_it != _preview_cache.end() && cache_it->second != preview)
                        RetirePreview(cache_it->second);
                    _preview_cache[asset] = preview;
                    return preview.get();
                }
            }

            return GetStaticIcon(asset->_asset_type);
        }

        void AssetTypeRegistry::RegisterPreview(const Type *type, AssetPreviewProvider provider)
        {
            if (type == nullptr || !provider)
                return;
            _preview_providers[type] = std::move(provider);
        }

        void AssetTypeRegistry::BeginFrame()
        {
            _retired_previews[_retired_preview_index].clear();
            _retired_preview_index = (_retired_preview_index + 1u) % Render::RenderConstants::kFrameCount;
        }

        Render::Texture *AssetTypeRegistry::GetStaticIcon(const Type *type)
        {
            auto path_it = _resolved_icon_paths.find(type);
            if (path_it == _resolved_icon_paths.end())
                path_it = _resolved_icon_paths.emplace(type, ResolveIconPath(type)).first;
            const WString &icon_path = path_it->second;
            const WString default_icon_path = WString(kAssetIconDirectory) + kDefaultAssetIconName;
            const Type *cache_type = icon_path == default_icon_path ? nullptr : type;
            if (auto cache_it = _icon_cache.find(cache_type); cache_it != _icon_cache.end())
                return cache_it->second.get();

            if (icon_path.empty())
            {
                _icon_cache[cache_type] = nullptr;
                return nullptr;
            }
            Ref<Render::Texture2D> icon = ResourceMgr::Get().Load<Render::Texture2D>(icon_path);
            _icon_cache[cache_type] = icon;
            return icon.get();
        }

        WString AssetTypeRegistry::ResolveIconPath(const Type *type)
        {
            const WString type_name = type == nullptr ? L"" : ToWChar(type->Name().c_str());
            const WString type_path = WString(kAssetIconDirectory) + type_name + L".alasset";
            if (!type_name.empty() && std::filesystem::exists(ResourceMgr::GetResSysPath(type_path)))
                return type_path;

            const WString default_path = WString(kAssetIconDirectory) + kDefaultAssetIconName;
            return std::filesystem::exists(ResourceMgr::GetResSysPath(default_path)) ? default_path : WString{};
        }

        void AssetTypeRegistry::RetirePreview(const Ref<Render::Texture> &preview)
        {
            auto render_texture = std::dynamic_pointer_cast<Render::RenderTexture>(preview);
            if (render_texture != nullptr)
                _retired_previews[_retired_preview_index].push_back(std::move(render_texture));
        }
    }// namespace Editor
}// namespace Ailu
