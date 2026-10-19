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
        _shader = ResourceMgr::Get().LoadAssetRef<Shader>(L"Shaders/hlsl/PostProcess/tone_mapping.alasset");
        if (_shader != nullptr)
            _shader->Compile();
        _material = MakeRef<Material>(_shader.get(), "ToneMapping");
    }

    RDG::RGHandle ToneMappingPass::Record(RDG::RenderGraph &graph, RDG::RGHandle source,
                                          const RenderingData &rendering_data,
                                          const ToneMappingSettings &settings, RDG::RGHandle exposure_ev,
                                          f32 scalar_exposure)
    {
        const TextureDesc source_desc = graph.GetTextureDesc(source);
        const RDG::RGHandle pre_exposure_ev = rendering_data._rg_handles._pre_exposure_ev;
        TextureDesc desc(source_desc._width, source_desc._height, ERenderTargetFormat::kDefaultHDR);
        desc._is_color_target = true;
        desc._load = ELoadStoreAction::kNotCare;
        const RDG::RGHandle output = graph.CreateResource(desc, "ToneMappedColor");
        graph.AddPass("ToneMapping", RDG::PassDesc(), [source, output, exposure_ev, pre_exposure_ev]
                      (RDG::RenderGraphBuilder &builder)
        {
            builder.Read(source, EResourceUsage::kReadSRV);
            if (exposure_ev.IsValid())
                builder.Read(exposure_ev, EResourceUsage::kReadSRV);
            if (pre_exposure_ev.IsValid())
                builder.Read(pre_exposure_ev, EResourceUsage::kReadSRV);
            (void) builder.Write(output);
        }, [this, source, output, exposure_ev, settings, scalar_exposure]
           (RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &)
        {
            cmd->SetRenderTarget(graph.GetTexture("ToneMappedColor"));
            _material->SetFloat("_Exposure", scalar_exposure);
            _material->SetFloat("_Gamma", settings._gamma);
            _material->SetInt("_ToneMappingMode", static_cast<i32>(settings._mode));
            _material->SetInt("_UseAutoExposureEV", exposure_ev.IsValid() ? 1 : 0);
            _material->SetTexture("_SourceTex", graph.Resolve<Texture>(source));
            _material->SetTexture("_AutoExposureEVTex", exposure_ev.IsValid()
                ? graph.Resolve<Texture>(exposure_ev) : graph.Resolve<Texture>(source));
            cmd->DrawFullScreenQuad(_material.get(), 0);
        });
        return graph.GetTexture("ToneMappedColor");
    }
}
