#pragma once
#ifndef __SPRITE_RENDER_PASS_H__
#define __SPRITE_RENDER_PASS_H__

#include "Render/Features/RenderFeature.h"
#include "SpriteBatcher.h"
#include "SpriteRenderData.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }
}

namespace Ailu::Render
{
    class Camera;

    class AILU_API SpriteRenderPass : public RenderPass
    {
    public:
        SpriteRenderPass();

        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) override;
        void Execute(GraphicsContext *context, RenderingData &rendering_data) override;

    private:
        void CollectSprites(const SceneManagement::Scene &scene, const Camera &camera);
        void SortSprites();
        bool IsVisible(const SpriteRenderData &sprite, const Camera &camera) const;

    private:
        Scope<SpriteBatcher> _batcher;
        Vector<SpriteRenderData> _render_data;
    };
}// namespace Ailu::Render

#endif// !__SPRITE_RENDER_PASS_H__
