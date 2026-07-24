#include "Render/2D/SpriteRenderFeature.h"
#include "Render/Renderer.h"
#include "pch.h"

namespace Ailu::Render
{
    SpriteRenderFeature::SpriteRenderFeature()
        : RenderFeature("SpriteRenderFeature")
    {
        _sprite_render_pass = MakeScope<SpriteRenderPass>();
        _is_active = true;
    }

    void SpriteRenderFeature::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
    {
        if (!_is_active || !rendering_data._scene || !rendering_data._camera)
            return;

        renderer.EnqueuePass(_sprite_render_pass.get());
    }

}// namespace Ailu::Render
