#pragma once

#include "Framework/Core/SmartPtr.h"
#include "Render/Features/RenderFeature.h"

namespace Ailu::Render
{
    class Material;
    class Shader;

    class StreamlineSrPass final : public RenderPass
    {
    public:
        StreamlineSrPass();
        void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;

    private:
        Ref<Shader> _color_domain_shader;
        Ref<Material> _color_domain_material;
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
