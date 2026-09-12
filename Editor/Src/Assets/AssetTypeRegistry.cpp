#include "Assets/AssetTypeRegistry.h"

#include "Widgets/AssetBrowserOperations.h"

#include "Assets/Asset.h"
#include "Animation/BlendSpace.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/2D/Sprite.h"
#include "Render/AssetPreviewGenerator.h"
#include "Render/Material.h"
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

            // 这些 provider 都在 AssetBrowser 建 item widget 时被调用，而那时贴图/顶点缓冲
            // /shader 变体经常还在异步创建。生成器返回 false 时保留 target 交给上层的重试，
            // 等资源就绪后再渲染到同一张贴图上（图标会自己变好）。
            AssetPreviewResult FinishPreview(Ref<Render::RenderTexture> &target, bool is_ready)
            {
                AssetPreviewResult result;
                if (!is_ready)
                {
                    result._needs_retry = target != nullptr;
                    return result;
                }
                result._texture = std::static_pointer_cast<Render::Texture>(target);
                return result;
            }

            AssetPreviewResult GenerateMeshPreview(Asset *asset, Ref<Render::RenderTexture> &target)
            {
                if (asset == nullptr)
                    return {};

                if (asset->_p_obj == nullptr)
                {
                    if (asset->_asset_type == Render::SkeletonMesh::StaticType())
                        ResourceMgr::Get().Load<Render::SkeletonMesh>(asset->_asset_path);
                    else
                        ResourceMgr::Get().Load<Render::Mesh>(asset->_asset_path);
                }

                auto mesh = asset->As<Render::Mesh>();
                if (mesh == nullptr)
                    return {};

                const bool is_ready = AssetPreviewGenerator::GeneratorMeshSnapshot(
                        AssetPreviewGenerator::kDynamicPreviewSize, AssetPreviewGenerator::kDynamicPreviewSize, mesh, target);
                return FinishPreview(target, is_ready);
            }

            AssetPreviewResult GenerateSpritePreview(Asset *asset, Ref<Render::RenderTexture> &target)
            {
                if (asset == nullptr)
                    return {};

                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<Render::Sprite>(asset->_asset_path);

                auto sprite = asset->As<Render::Sprite>();
                if (sprite == nullptr)
                    return {};

                const bool is_ready = AssetPreviewGenerator::GeneratorSpriteSnapshot(
                        AssetPreviewGenerator::kDynamicPreviewSize, AssetPreviewGenerator::kDynamicPreviewSize, sprite, target);
                return FinishPreview(target, is_ready);
            }

            AssetPreviewResult GenerateMaterialPreview(Asset *asset, Ref<Render::RenderTexture> &target)
            {
                if (asset == nullptr)
                    return {};

                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<Render::Material>(asset->_asset_path);

                auto material = asset->As<Render::Material>();
                if (material == nullptr)
                    return {};

                const bool is_ready = AssetPreviewGenerator::GeneratorMaterialSnapshot(
                        AssetPreviewGenerator::kDynamicPreviewSize, AssetPreviewGenerator::kDynamicPreviewSize, material, target);
                return FinishPreview(target, is_ready);
            }

            AssetPreviewResult GetTexturePreview(Asset *asset, Ref<Render::RenderTexture> &target)
            {
                (void) target;
                if (asset == nullptr)
                    return {};

                auto texture = asset->AsRef<Render::Texture2D>();
                if (texture == nullptr)
                    texture = ResourceMgr::Get().Load<Render::Texture2D>(asset->_asset_path);
                AssetPreviewResult result;
                // 贴图本身不需要渲染，等它上传完自己就显示了，无需重试
                result._texture = texture == nullptr ? nullptr : std::static_pointer_cast<Render::Texture>(texture);
                return result;
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
            RegisterPreview(Render::SkeletonMesh::StaticType(), GenerateMeshPreview);
            RegisterPreview(Render::Sprite::StaticType(), GenerateSpritePreview);
            RegisterPreview(Render::Texture2D::StaticType(), GetTexturePreview);
            RegisterPreview(Render::Material::StaticType(), GenerateMaterialPreview);

            RegisterCreator({
                "New Scene", "Create Scene", "NewScene", "Scene already exists.", L".almap",
                [](const fs::path &directory, const String &name) { return CreateSceneAsset(directory, name); },
                nullptr
            });
            RegisterCreator({
                "New Sprite Atlas", "Create Sprite Atlas", "NewSpriteAtlas", "Sprite Atlas already exists.", L".alasset",
                [](const fs::path &directory, const String &name) { return CreateSpriteAtlasAsset(directory, name); },
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
                [](const fs::path &directory, Vector2f popup_pos, std::function<void()> on_created)
                {
                    ShowCreateMaterialDialog(popup_pos, directory, std::move(on_created));
                }
            });
            RegisterCreator({
                "New Input Action Asset", "Create Input Action Asset", "NewInputActions", "Input Action Asset already exists.", L".alasset",
                [](const fs::path &directory, const String &name) { return CreateInputActionAsset(directory, name); },
                nullptr
            });
            RegisterCreator({
                "New Animation Clip", "Create Animation Clip", "NewAnimationClip", "Animation Clip already exists.", L".alasset",
                [](const fs::path &directory, const String &name) { return CreateAnimationClipAsset(directory, name); },
                nullptr
            });
            RegisterCreator({
                "New Animation Controller", "Create Animation Controller", "NewAnimationController",
                "Animation Controller already exists.", L".alasset",
                [](const fs::path &directory, const String &name) { return CreateAnimationControllerAsset(directory, name); },
                nullptr
            });
            RegisterCreator({
                "New Blend Space", "Create Blend Space", "NewBlendSpace", "Blend Space already exists.", L".alasset",
                [](const fs::path &directory, const String &name) { return CreateBlendSpaceAsset(directory, name); },
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

            if (_preview_providers.find(asset->_asset_type) == _preview_providers.end())
                return GetStaticIcon(asset->_asset_type);

            if (auto cache_it = _preview_cache.find(asset); cache_it != _preview_cache.end() && cache_it->second != nullptr)
                return cache_it->second.get();

            // 预览生成会同步提交命令缓冲，不能在 UI 构建（RenderPass 录制中）期间做，
            // 否则拿到的是一张空贴图（黑图标）。这里只登记，真正的生成在 BeginFrame 里做；
            // 生成成功后会抬高版本号，AssetBrowser 据此重建 item 换成真预览。
            _pending_previews.try_emplace(asset, PendingPreview{nullptr, kPreviewRetryDelayFrames, nullptr, nullptr});
            return GetStaticIcon(asset->_asset_type);
        }

        Render::Texture *AssetTypeRegistry::GetIcon(const Guid &guid, const Type *type, const Ref<Object> &object)
        {
            if (guid.IsEmpty() || type == nullptr || object == nullptr)
                return GetStaticIcon(type);

            if (auto cache_it = _sub_asset_preview_cache.find(guid); cache_it != _sub_asset_preview_cache.end() &&
                cache_it->second != nullptr)
                return cache_it->second.get();

            if (_preview_providers.find(type) == _preview_providers.end())
                return GetStaticIcon(type);

            // 同 GetIcon(Asset*)：不在 UI 构建期间渲染，登记后先返回静态图标
            if (_pending_sub_previews.find(guid) == _pending_sub_previews.end())
            {
                PendingPreview pending;
                pending._frames_to_wait = kPreviewRetryDelayFrames;
                pending._object = object;
                pending._type = type;
                _pending_sub_previews.emplace(guid, std::move(pending));
            }

            return GetStaticIcon(type);
        }

        Render::Texture *AssetTypeRegistry::GetTypeIcon(Asset *asset)
        {
            return GetStaticIcon(asset == nullptr ? nullptr : asset->_asset_type);
        }

        Render::Texture *AssetTypeRegistry::GetTypeIcon(const Type *type)
        {
            return GetStaticIcon(type);
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
            RetryPendingPreviews();
        }

        void AssetTypeRegistry::CachePreview(Asset *asset, const Ref<Render::Texture> &preview)
        {
            if (asset == nullptr || preview == nullptr)
                return;
            auto cache_it = _preview_cache.find(asset);
            if (cache_it != _preview_cache.end() && cache_it->second != preview)
                RetirePreview(cache_it->second);
            _preview_cache[asset] = preview;
        }

        // 预览生成统一放在这里（每帧 UI 构建之前、RenderPass 之外）：输入资源（贴图/顶点缓冲
        // /shader 变体）是异步就绪的，没就绪就下一帧再来；生成成功的一律进缓存并抬高版本号，
        // AssetBrowser 据此重建一次 item 换上真预览（UI 永远不会拿到还没渲染过的空贴图）。
        void AssetTypeRegistry::RetryPendingPreviews()
        {
            bool any_generated = false;

            for (auto it = _pending_previews.begin(); it != _pending_previews.end();)
            {
                Asset *asset = it->first;
                PendingPreview &pending = it->second;
                if (pending._frames_to_wait > 0u)
                {
                    --pending._frames_to_wait;
                    ++it;
                    continue;
                }

                auto provider_it = (asset != nullptr && asset->_asset_type != nullptr)
                                           ? _preview_providers.find(asset->_asset_type)
                                           : _preview_providers.end();
                if (provider_it == _preview_providers.end())
                {
                    it = _pending_previews.erase(it);
                    continue;
                }

                AssetPreviewResult result = provider_it->second(asset, pending._target);
                if (result._texture != nullptr)
                {
                    CachePreview(asset, result._texture);
                    any_generated = true;
                    it = _pending_previews.erase(it);
                    continue;
                }
                if (result._needs_retry)
                {
                    ++it;
                    continue;
                }
                it = _pending_previews.erase(it);
            }

            for (auto it = _pending_sub_previews.begin(); it != _pending_sub_previews.end();)
            {
                PendingPreview &pending = it->second;
                if (pending._frames_to_wait > 0u)
                {
                    --pending._frames_to_wait;
                    ++it;
                    continue;
                }

                const Type *type = pending._type;
                auto provider_it = (pending._object != nullptr && type != nullptr)
                                           ? _preview_providers.find(type)
                                           : _preview_providers.end();
                if (provider_it == _preview_providers.end())
                {
                    it = _pending_sub_previews.erase(it);
                    continue;
                }

                Asset preview_asset(type, L"");
                preview_asset._p_obj = pending._object;
                AssetPreviewResult result = provider_it->second(&preview_asset, pending._target);
                if (result._texture != nullptr)
                {
                    auto cache_it = _sub_asset_preview_cache.find(it->first);
                    if (cache_it != _sub_asset_preview_cache.end() && cache_it->second != result._texture)
                        RetirePreview(cache_it->second);
                    _sub_asset_preview_cache[it->first] = result._texture;
                    any_generated = true;
                    it = _pending_sub_previews.erase(it);
                    continue;
                }
                if (result._needs_retry)
                {
                    ++it;
                    continue;
                }
                it = _pending_sub_previews.erase(it);
            }

            if (any_generated)
                ++_preview_revision;
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
