#ifndef __ASSET_PREVIEW_GENERATOR_H__
#define __ASSET_PREVIEW_GENERATOR_H__
#include "Framework/Core/CoreMinimal.h"
namespace Ailu
{
    namespace Render
    {
        class Mesh;
        class RenderTexture;
        class Sprite;
    }
    namespace Editor
    {
        class AssetPreviewGenerator
        {
        public:
            static void GeneratorMeshSnapshot(u16 w, u16 h, Render::Mesh *mesh, Ref<Render::RenderTexture>& target);
            static void GeneratorSpriteSnapshot(u16 w, u16 h, Render::Sprite *sprite, Ref<Render::RenderTexture>& target);
            static void Shutdown();
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_PREVIEW_GENERATOR_H__
