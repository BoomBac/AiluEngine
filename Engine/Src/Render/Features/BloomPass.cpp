#include "Render/Features/BloomPass.h"

#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/Material.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/RenderingData.h"

namespace Ailu::Render
{
    BloomPass::BloomPass()
    {
        Shader *shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/PostProcess/bloom.alasset");
        for (u16 i = 0u; i < _mip_count; ++i)
            _bloom_mats.emplace_back(MakeRef<Material>(shader, std::format("BloomMip_{}", i)));
        _bloom_compose_mat = MakeRef<Material>(shader, "BloomCompose");
    }

    RDG::RGHandle BloomPass::Record(RDG::RenderGraph &graph, RDG::RGHandle source, const RenderingData &rendering_data,
                                    const BloomSettings &settings)
    {
        (void) rendering_data;
        const TextureDesc source_desc = graph.GetTextureDesc(source);
        const u32 source_width = source_desc._width;
        const u32 source_height = source_desc._height;
        const u16 iterator_count = std::min<u16>(Texture::MaxMipmapCount(source_width, source_height), _mip_count);
        Vector<RDG::RGHandle> bloom_mips;
        bloom_mips.reserve(iterator_count);
        for (u16 i = 1u; i <= iterator_count; ++i)
        {
            TextureDesc desc;
            desc._width = std::max<u32>(1u, source_width >> i);
            desc._height = std::max<u32>(1u, source_height >> i);
            desc._format = ConvertRenderTextureFormatToPixelFormat(ERenderTargetFormat::kDefaultHDR);
            desc._is_color_target = true;
            desc._load = ELoadStoreAction::kNotCare;
            bloom_mips.emplace_back(graph.CreateResource(desc, std::format("BloomMip_{}", i - 1u)));
        }

        for (u16 i = 0u; i < iterator_count; ++i)
        {
            const RDG::RGHandle input = i == 0u ? source : bloom_mips[i - 1u];
            const RDG::RGHandle output = bloom_mips[i];
            graph.AddPass(std::format("BloomDownsample_{}", i), RDG::PassDesc(),
                          [input, output](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(input);
                (void) builder.Write(output);
            }, [this, input, output, settings, i, source_width, source_height](RDG::RenderGraph &graph,
                                                                               CommandBuffer *cmd, const RenderingData &)
            {
                cmd->SetRenderTarget(graph.GetTexture(std::format("BloomMip_{}", i)));
                const f32 mip_scale = static_cast<f32>(i + 1u);
                const f32 blur_radius = settings._radius / mip_scale;
                const f32 input_width = static_cast<f32>(std::max<u32>(1u, source_width >> i));
                const f32 input_height = static_cast<f32>(std::max<u32>(1u, source_height >> i));
                _bloom_mats[i]->SetVector("_SampleParams",
                                          Vector4f(1.0f / input_width, 1.0f / input_height, blur_radius, settings._intensity));
                _bloom_mats[i]->SetTexture("_SourceTex", graph.Resolve<Texture>(input));
                cmd->DrawFullScreenQuad(_bloom_mats[i].get(), 1);
            });
            bloom_mips[i] = graph.GetTexture(std::format("BloomMip_{}", i));
        }

        for (u16 i = iterator_count; i > 1u; --i)
        {
            const RDG::RGHandle current = bloom_mips[i - 1u];
            const RDG::RGHandle output = bloom_mips[i - 2u];
            graph.AddPass(std::format("BloomUpsample_{}", i - 1u), RDG::PassDesc(), [current, output](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(current);
                (void) builder.Write(output);
            }, [this, current, output, settings, i, source_width, source_height](RDG::RenderGraph &graph,
                                                                                 CommandBuffer *cmd, const RenderingData &)
            {
                cmd->SetRenderTarget(graph.GetTexture(std::format("BloomMip_{}", i - 2u)));
                const f32 width = static_cast<f32>(std::max<u32>(1u, source_width >> (i - 1u)));
                const f32 height = static_cast<f32>(std::max<u32>(1u, source_height >> (i - 1u)));
                _bloom_mats[i - 1u]->SetVector("_SampleParams",
                                               Vector4f(1.0f / width, 1.0f / height,
                                                        settings._radius / static_cast<f32>(i), settings._intensity));
                _bloom_mats[i - 1u]->SetTexture("_SourceTex", graph.Resolve<Texture>(current));
                cmd->DrawFullScreenQuad(_bloom_mats[i - 1u].get(), 2);
            });
            bloom_mips[i - 2u] = graph.GetTexture(std::format("BloomMip_{}", i - 2u));
        }

        TextureDesc output_desc(source_width, source_height, ERenderTargetFormat::kDefaultHDR);
        output_desc._is_color_target = true;
        output_desc._load = ELoadStoreAction::kNotCare;
        const RDG::RGHandle output = graph.CreateResource(output_desc, "BloomColor");
        const RDG::RGHandle bloom = bloom_mips.front();
        graph.AddPass("BloomCompose", RDG::PassDesc(), [source, bloom, output](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(source);
            builder.Read(bloom);
            (void) builder.Write(output);
        }, [this, source, bloom, output, settings](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &)
        {
            cmd->SetRenderTarget(graph.GetTexture("BloomColor"));
            _bloom_compose_mat->SetVector("_SampleParams", Vector4f(0.0f, 0.0f, 0.0f, settings._intensity));
            _bloom_compose_mat->SetTexture("_SourceTex", graph.Resolve<Texture>(source));
            _bloom_compose_mat->SetTexture("_BloomTex", graph.Resolve<Texture>(bloom));
            cmd->DrawFullScreenQuad(_bloom_compose_mat.get(), 3);
        });
        return graph.GetTexture("BloomColor");
    }
}
