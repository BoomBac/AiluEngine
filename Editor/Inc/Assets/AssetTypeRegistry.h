#pragma once
#ifndef __ASSET_TYPE_REGISTRY_H__
#define __ASSET_TYPE_REGISTRY_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Core/Containers/Map.h"
#include "Render/RenderConstants.h"

#include <functional>

namespace Ailu
{
    class Asset;
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

        class AssetTypeRegistry
        {
        public:
            static AssetTypeRegistry &Get();

            Render::Texture *GetIcon(Asset *asset);
            void RegisterPreview(const Type *type, AssetPreviewProvider provider);
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
            Array<Vector<Ref<Render::RenderTexture>>, Render::RenderConstants::kFrameCount> _retired_previews;
            u16 _retired_preview_index = 0u;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_TYPE_REGISTRY_H__
