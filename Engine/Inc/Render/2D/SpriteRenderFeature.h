#pragma once
#ifndef __SPRITE_RENDER_FEATURE_H__
#define __SPRITE_RENDER_FEATURE_H__

#include "Render/Features/RenderFeature.h"
#include "SpriteRenderPass.h"

namespace Ailu::Render
{
    class AILU_API SpriteRenderFeature : public RenderFeature
    {
    public:
        SpriteRenderFeature();

        void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) override;

    private:
        Scope<SpriteRenderPass> _sprite_render_pass;
    };
}// namespace Ailu::Render

#endif// !__SPRITE_RENDER_FEATURE_H__
