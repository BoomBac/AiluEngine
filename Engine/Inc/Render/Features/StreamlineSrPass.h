#pragma once

#include "Render/Features/RenderFeature.h"

namespace Ailu::Render
{
    class StreamlineSrPass final : public RenderPass
    {
    public:
        StreamlineSrPass();
        void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;
    };

    class StreamlineSrFeature final : public RenderFeature
    {
    public:
        StreamlineSrFeature();
        void AddRenderPasses(Renderer& renderer, const RenderingData& rendering_data) final;

    private:
        StreamlineSrPass _pass;
    };
}
