#include "pch.h"

#include "Render/Features/StreamlineSrPass.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Camera.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Renderer.h"
#include "Render/Shader.h"
#include "Render/StreamlineSR.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/RenderingData.h"

#include <algorithm>
#include <cmath>

namespace Ailu::Render
{
    namespace
    {
        f32 SanitizePreExposure(f32 value)
        {
            return std::isfinite(value) && value > 0.0f ? value : 1.0f;
        }
    }

    StreamlineSrPass::StreamlineSrPass() : RenderPass("SuperResolution")
    {
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kBeforePostprocess) + 10u);
        _color_domain_shader = Shader::Create(ResourceMgr::GetResSysPath(
            L"Shaders/hlsl/pre_exposure_domain_transform.hlsl"));
        if (_color_domain_shader != nullptr && _color_domain_shader->Compile())
            _color_domain_material = MakeRef<Material>(_color_domain_shader.get(), "SuperResolutionColorDomain");
    }

    void StreamlineSrPass::OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data)
    {
        const RDG::RGHandle color = rendering_data._rg_handles._color_tex;
        const RDG::RGHandle depth = rendering_data._rg_handles._depth_tex;
        const RDG::RGHandle motion_vectors = rendering_data._rg_handles._motion_vector_tex;
        const RDG::RGHandle pre_exposure_ev = rendering_data._rg_handles._pre_exposure_ev;
        if (!color.IsValid() || !depth.IsValid() || !motion_vectors.IsValid() || rendering_data._camera == nullptr)
            return;

        const Camera* camera = rendering_data._camera;
        const Vector2D<u32> output_size = camera->OutputSize();
        if (output_size.x == 0u || output_size.y == 0u)
            return;

        const bool normalize_gpu_pre_exposure = pre_exposure_ev.IsValid();
        if (normalize_gpu_pre_exposure && _color_domain_material == nullptr)
            return;

        RDG::RGHandle vendor_input = color;
        if (normalize_gpu_pre_exposure)
        {
            TextureDesc input_desc = graph.GetTextureDesc(color);
            input_desc._format = EALGFormat::kALGFormatR32G32B32A32_FLOAT;
            input_desc._is_color_target = true;
            input_desc._is_random_access = false;
            input_desc._load = ELoadStoreAction::kNotCare;
            const RDG::RGHandle scene_linear_input = graph.CreateResource(
                input_desc, "SuperResolutionSceneLinearInput");
            auto scene_linear_output = std::make_shared<RDG::RGHandle>();
            graph.AddPass("SuperResolutionRemovePreExposure", RDG::PassDesc(),
                [color, pre_exposure_ev, scene_linear_input, scene_linear_output]
                (RDG::RenderGraphBuilder& builder)
                {
                    builder.Read(color, EResourceUsage::kReadSRV);
                    builder.Read(pre_exposure_ev, EResourceUsage::kReadSRV);
                    *scene_linear_output = builder.Write(scene_linear_input, EResourceUsage::kWriteRTV);
                },
                [this, color, pre_exposure_ev, scene_linear_output]
                (RDG::RenderGraph& pass_graph, CommandBuffer* cmd, const RenderingData&)
                {
                    Texture* source = pass_graph.Resolve<Texture>(color);
                    Texture* exposure = pass_graph.Resolve<Texture>(pre_exposure_ev);
                    RenderTexture* output = pass_graph.Resolve<RenderTexture>(*scene_linear_output);
                    if (source == nullptr || exposure == nullptr || output == nullptr)
                        return;

                    cmd->SetRenderTarget(output);
                    _color_domain_material->SetTexture("_SourceTex", source);
                    _color_domain_material->SetTexture("_PreExposureEVTex", exposure);
                    _color_domain_material->SetFloat("_PreExposure", 1.0f);
                    _color_domain_material->SetInt("_UsePreExposureEV", 1);
                    _color_domain_material->SetInt("_EncodeToPreExposed", 0);
                    cmd->DrawFullScreenQuad(_color_domain_material.get(), 0u);
                });
            vendor_input = *scene_linear_output;
        }

        TextureDesc vendor_output_desc(output_size.x, output_size.y,
            normalize_gpu_pre_exposure ? ERenderTargetFormat::kRGBAFloat : ERenderTargetFormat::kRGBAHalf);
        vendor_output_desc._is_color_target = true;
        vendor_output_desc._is_random_access = true;
        vendor_output_desc._load = ELoadStoreAction::kNotCare;
        const RDG::RGHandle vendor_output = graph.CreateResource(vendor_output_desc, "SuperResolutionVendorOutput");
        auto vendor_output_seed = std::make_shared<RDG::RGHandle>();
        graph.AddPass("SuperResolutionFallbackSeed", RDG::PassDesc(),
            [vendor_input, vendor_output, vendor_output_seed](RDG::RenderGraphBuilder& builder)
            {
                builder.Read(vendor_input, EResourceUsage::kReadSRV);
                *vendor_output_seed = builder.Write(vendor_output, EResourceUsage::kWriteRTV);
            },
            [vendor_input, vendor_output_seed](RDG::RenderGraph& pass_graph, CommandBuffer* cmd,
                                               const RenderingData&)
            {
                cmd->Blit(vendor_input, *vendor_output_seed);
            });

        const bool use_fsr = camera->IsFsrSRActive();
        const f32 vendor_pre_exposure = normalize_gpu_pre_exposure
            ? 1.0f : SanitizePreExposure(rendering_data._pre_exposure);
        const ESuperResolutionColorDomain input_domain = normalize_gpu_pre_exposure
            ? ESuperResolutionColorDomain::kSceneLinear
            : ESuperResolutionColorDomain::kPreExposedSceneLinear;
        const SuperResolutionFrameData frame_data = SuperResolution::Snapshot(
            *camera, g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u, vendor_pre_exposure,
            input_domain);
        auto vendor_output_written = std::make_shared<RDG::RGHandle>();
        graph.AddPass("SuperResolutionEvaluate", RDG::PassDesc(),
            [vendor_input, depth, motion_vectors, vendor_output_seed, vendor_output_written]
            (RDG::RenderGraphBuilder& builder)
            {
                builder.Read(vendor_input, EResourceUsage::kReadSRV);
                builder.Read(depth, EResourceUsage::kReadSRV);
                builder.Read(motion_vectors, EResourceUsage::kReadSRV);
                *vendor_output_written = builder.WriteAfter(*vendor_output_seed, EResourceUsage::kWriteUAV);
            },
            [vendor_input, depth, motion_vectors, vendor_output_written, camera, frame_data, use_fsr]
            (RDG::RenderGraph& pass_graph, CommandBuffer* cmd, const RenderingData&)
            {
                Texture* color_texture = pass_graph.Resolve<Texture>(vendor_input);
                Texture* depth_texture = pass_graph.Resolve<Texture>(depth);
                Texture* motion_texture = pass_graph.Resolve<Texture>(motion_vectors);
                Texture* output_texture = pass_graph.Resolve<Texture>(*vendor_output_written);
                if (color_texture == nullptr || depth_texture == nullptr || motion_texture == nullptr ||
                    output_texture == nullptr)
                    return;

                cmd->RecordNativeCallback([camera, frame_data, color_texture, depth_texture, motion_texture,
                                           output_texture, use_fsr](RHICommandBuffer* native_command_buffer)
                {
                    if (use_fsr)
                    {
                        SuperResolution::Evaluate(frame_data, color_texture, depth_texture, motion_texture,
                                                  output_texture, native_command_buffer);
                    }
                    else
                    {
                        StreamlineSR::Evaluate(*camera, frame_data, color_texture, depth_texture, motion_texture,
                                               output_texture, native_command_buffer);
                    }
                });
            });

        TextureDesc output_desc(output_size.x, output_size.y, ERenderTargetFormat::kRGBAHalf);
        output_desc._is_color_target = true;
        output_desc._load = ELoadStoreAction::kNotCare;
        const RDG::RGHandle output = graph.CreateResource(output_desc, "SuperResolutionOutput");
        auto output_written = std::make_shared<RDG::RGHandle>();
        graph.AddPass("SuperResolutionRestorePreExposure", RDG::PassDesc(),
            [vendor_output_written, pre_exposure_ev, output, output_written, normalize_gpu_pre_exposure]
            (RDG::RenderGraphBuilder& builder)
            {
                builder.Read(*vendor_output_written, EResourceUsage::kReadSRV);
                if (normalize_gpu_pre_exposure)
                    builder.Read(pre_exposure_ev, EResourceUsage::kReadSRV);
                *output_written = builder.Write(output, EResourceUsage::kWriteRTV);
            },
            [this, vendor_output_written, pre_exposure_ev, output_written, normalize_gpu_pre_exposure]
            (RDG::RenderGraph& pass_graph, CommandBuffer* cmd, const RenderingData&)
            {
                Texture* source = pass_graph.Resolve<Texture>(*vendor_output_written);
                RenderTexture* output_texture = pass_graph.Resolve<RenderTexture>(*output_written);
                if (source == nullptr || output_texture == nullptr)
                    return;

                if (!normalize_gpu_pre_exposure)
                {
                    cmd->Blit(*vendor_output_written, *output_written);
                    return;
                }

                Texture* exposure = pass_graph.Resolve<Texture>(pre_exposure_ev);
                if (exposure == nullptr || _color_domain_material == nullptr)
                    return;
                cmd->SetRenderTarget(output_texture);
                _color_domain_material->SetTexture("_SourceTex", source);
                _color_domain_material->SetTexture("_PreExposureEVTex", exposure);
                _color_domain_material->SetFloat("_PreExposure", 1.0f);
                _color_domain_material->SetInt("_UsePreExposureEV", 1);
                _color_domain_material->SetInt("_EncodeToPreExposed", 1);
                cmd->DrawFullScreenQuad(_color_domain_material.get(), 0u);
            });

        rendering_data._rg_handles._upscaled_color = *output_written;
    }

    StreamlineSrFeature::StreamlineSrFeature() : RenderFeature("SuperResolutionFeature") {}

    void StreamlineSrFeature::AddRenderPasses(Renderer& renderer, const RenderingData& rendering_data)
    {
        if (rendering_data._camera != nullptr && rendering_data._camera->IsTemporalUpscalingActive())
            renderer.EnqueuePass(&_pass);
    }
}
