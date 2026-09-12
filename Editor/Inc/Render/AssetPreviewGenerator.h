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
            // 生成/刷新预览贴图。target 为空时创建，非空时复用同一张（内容更新后图标会自动跟着变）。
            // 返回 false 表示这次没有画出有效内容，调用方应保留 target 稍后重试：
            // 输入资源（顶点/索引缓冲、贴图、shader variant）还没就绪，或者渲染时 PSO 还没建好
            // （绘制会被引擎丢掉），否则会把空预览烘死成永久黑图。
            static bool GeneratorMeshSnapshot(u16 w, u16 h, Render::Mesh *mesh, Ref<Render::RenderTexture> &target);
            static bool GeneratorMaterialSnapshot(u16 w, u16 h, Render::Material *material,
                                                  Ref<Render::RenderTexture>& target);
            static bool GeneratorSpriteSnapshot(u16 w, u16 h, Render::Sprite *sprite, Ref<Render::RenderTexture> &target);
            static void Shutdown();
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_PREVIEW_GENERATOR_H__
