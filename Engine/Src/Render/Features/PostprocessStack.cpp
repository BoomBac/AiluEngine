#include "Render/Features/PostprocessStack.h"

#include "Render/RenderingData.h"

namespace Ailu::Render
{
    PostprocessStack::PostprocessStack() : RenderPass("PostprocessStack")
    {
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kBeforePostprocess) + 25u);
    }

    void PostprocessStack::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        const PostprocessSettings &settings = rendering_data._postprocess_settings;
        RDG::RGHandle color = rendering_data._rg_handles._color_tex.IsValid()
            ? rendering_data._rg_handles._color_tex : rendering_data._rg_handles._color_target;
        if (settings._bloom._enabled)
            color = _bloom.Record(graph, color, rendering_data, settings._bloom);
        if (settings._tone_mapping._mode != EToneMappingMode::kNone)
            color = _tone_mapping.Record(graph, color, rendering_data, settings._tone_mapping);
        rendering_data._rg_handles._color_target = color;
    }
}
