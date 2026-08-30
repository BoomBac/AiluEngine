#ifndef __ASSET_PREVIEW_GENERATOR_H__
#define __ASSET_PREVIEW_GENERATOR_H__
#include "Framework/Core/CoreMinimal.h"
namespace Ailu
{
    namespace Render
    {
        class Mesh;
        class Material;
        class RenderTexture;
        class Sprite;
    }
    namespace Editor
    {
        class AssetPreviewGenerator
        {
        public:
            static constexpr u16 kDynamicPreviewSize = 128u;
            static void GeneratorMeshSnapshot(u16 w, u16 h, Render::Mesh *mesh, Ref<Render::RenderTexture> &target);
            static void GeneratorMaterialSnapshot(u16 w, u16 h, Render::Material *material,
                                                   Ref<Render::RenderTexture>& target);
            static void GeneratorSpriteSnapshot(u16 w, u16 h, Render::Sprite *sprite, Ref<Render::RenderTexture> &target);
            static void Shutdown();
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_PREVIEW_GENERATOR_H__
