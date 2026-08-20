#pragma once
#ifndef __ASSET_TYPE_REGISTRY_H__
#define __ASSET_TYPE_REGISTRY_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "Framework/Math/Guid.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/RenderConstants.h"

#include <filesystem>
#include <functional>

namespace Ailu
{
    namespace fs = std::filesystem;

    class Asset;
    class Object;
    class Type;

    namespace Render
    {
        class RenderTexture;
        class Texture;
        class Texture2D;
    }

    namespace Editor
    {
        using AssetPreviewProvider = std::function<Ref<Render::Texture>(Asset *)>;

        struct AssetCreatorDesc
        {
            String _menu_name;
            String _dialog_title;
            String _default_name;
            String _exists_message;
            WString _extension;
            // 简单文本输入创建；为空时表示该类型使用 _create_dialog。
            std::function<bool(const fs::path &, const String &)> _create;
            // 自定义创建（如 Material 需要选择 Shader）；优先于 _create 使用。
            std::function<void(const fs::path &, Vector2f)> _create_dialog;
        };

        class AssetTypeRegistry
        {
        public:
            static AssetTypeRegistry &Get();

            Render::Texture *GetIcon(Asset *asset);
            Render::Texture *GetIcon(const Guid &guid, const Type *type, const Ref<Object> &object);
            Render::Texture *GetTypeIcon(Asset *asset);
            Render::Texture *GetTypeIcon(const Type *type);
            void RegisterPreview(const Type *type, AssetPreviewProvider provider);
            void RegisterCreator(AssetCreatorDesc creator);
            const Vector<AssetCreatorDesc> &Creators() const;
            void BeginFrame();

        private:
            AssetTypeRegistry();
            Render::Texture *GetStaticIcon(const Type *type);
            WString ResolveIconPath(const Type *type);
            void RetirePreview(const Ref<Render::Texture> &preview);

            Map<const Type *, AssetPreviewProvider> _preview_providers;
            Map<const Type *, WString> _resolved_icon_paths;
            Map<const Type *, Ref<Render::Texture2D>> _icon_cache;
            HashMap<Asset *, Ref<Render::Texture>> _preview_cache;
            HashMap<Guid, Ref<Render::Texture>, GuidHasher> _sub_asset_preview_cache;
            Array<Vector<Ref<Render::RenderTexture>>, Render::RenderConstants::kFrameCount> _retired_previews;
            u16 _retired_preview_index = 0u;
            Vector<AssetCreatorDesc> _creators;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_TYPE_REGISTRY_H__
