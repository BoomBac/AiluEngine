#ifndef __ASSET_PREVIEW_GENERATOR_H__
#define __ASSET_PREVIEW_GENERATOR_H__
#include "GlobalMarco.h"
namespace Ailu
{
    namespace Render
    {
        class Mesh;
        class RenderTexture;
    }
    namespace Editor
    {
        class AssetPreviewGenerator
        {
        public:
            static void GeneratorMeshSnapshot(u16 w, u16 h, Render::Mesh *mesh, Ref<Render::RenderTexture>& target);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_PREVIEW_GENERATOR_H__
