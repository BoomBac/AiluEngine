#include "Render/Features/ToneMappingPass.h"

#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/Material.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/RenderingData.h"

namespace Ailu::Render
{
    ToneMappingPass::ToneMappingPass()
    {
        _shader = Shader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/PostProcess/tone_mapping.hlsl"));
        if (_shader != nullptr)
            _shader->Compile();
        _material = MakeRef<Material>(_shader.get(), "ToneMapping");
    }

    RDG::RGHandle ToneMappingPass::Record(RDG::RenderGraph &graph, RDG::RGHandle source, const RenderingData &rendering_data,
                                          const ToneMappingSettings &settings)
    {
        TextureDesc desc(rendering_data._width, rendering_data._height, ERenderTargetFormat::kDefaultHDR);
        desc._is_color_target = true;
        desc._load = ELoadStoreAction::kNotCare;
        const RDG::RGHandle output = graph.CreateResource(desc, "ToneMappedColor");
        graph.AddPass("ToneMapping", RDG::PassDesc(), [source, output](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(source);
            (void) builder.Write(output);
        }, [this, source, output, settings](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &)
        {
            cmd->SetRenderTarget(graph.GetTexture("ToneMappedColor"));
            _material->SetFloat("_Exposure", settings._exposure);
            _material->SetInt("_ToneMappingMode", static_cast<i32>(settings._mode));
            _material->SetTexture("_SourceTex", graph.Resolve<Texture>(source));
            cmd->DrawFullScreenQuad(_material.get(), 0);
        });
        return graph.GetTexture("ToneMappedColor");
    }
}
