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
        // 预览生成结果：_texture 交给 UI 显示；_needs_retry 表示输入资源（贴图/顶点缓冲/shader 变体）
        // 还没就绪，此时必须保留 target 并稍后重试，否则会把空预览烘死成永久黑图标。
        struct AssetPreviewResult
        {
            Ref<Render::Texture> _texture;
            bool _needs_retry = false;
        };
        // target 为空时创建，非空时复用同一张贴图（内容更新后图标会自动跟着变）
        using AssetPreviewProvider = std::function<AssetPreviewResult(Asset *, Ref<Render::RenderTexture> &target)>;

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
            std::function<void(const fs::path &, Vector2f, std::function<void()>)> _create_dialog;
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
            // 启动时预览可能因为贴图/顶点缓冲/shader 变体还没就绪而生不出来。就绪并重试成功后
            // 这个版本号会自增，AssetBrowser 据此重建一次 item，把图标换成真正的预览。
            u32 PreviewRevision() const { return _preview_revision; }

        private:
            AssetTypeRegistry();
            Render::Texture *GetStaticIcon(const Type *type);
            WString ResolveIconPath(const Type *type);
            void RetirePreview(const Ref<Render::Texture> &preview);
            void CachePreview(Asset *asset, const Ref<Render::Texture> &preview);
            void RetryPendingPreviews();

            // 预览还没生成（或输入资源还没就绪）；先给静态图标占位，成功后通知 UI 重建
            struct PendingPreview
            {
                Ref<Render::RenderTexture> _target;
                u32 _frames_to_wait = 0u;// 首次登记后先等几帧，避免资源刚"看起来就绪"就出图
                // 子资产没有稳定的 Asset 句柄，重试时用它重建临时的 asset
                Ref<Object> _object;
                const Type *_type = nullptr;
            };
            inline static constexpr u32 kPreviewRetryDelayFrames = 4u;

            Map<const Type *, AssetPreviewProvider> _preview_providers;
            Map<const Type *, WString> _resolved_icon_paths;
            Map<const Type *, Ref<Render::Texture2D>> _icon_cache;
            HashMap<Asset *, Ref<Render::Texture>> _preview_cache;
            HashMap<Asset *, PendingPreview> _pending_previews;
            HashMap<Guid, PendingPreview, GuidHasher> _pending_sub_previews;
            HashMap<Guid, Ref<Render::Texture>, GuidHasher> _sub_asset_preview_cache;
            Array<Vector<Ref<Render::RenderTexture>>, Render::RenderConstants::kFrameCount> _retired_previews;
            u16 _retired_preview_index = 0u;
            u32 _preview_revision = 0u;
            Vector<AssetCreatorDesc> _creators;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_TYPE_REGISTRY_H__
