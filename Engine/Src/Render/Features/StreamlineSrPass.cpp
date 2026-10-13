#include "pch.h"

#include "Render/Features/StreamlineSrPass.h"

#include "Render/Camera.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Renderer.h"
#include "Render/StreamlineSR.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/RenderingData.h"
#include "Framework/Common/Log.h"

namespace Ailu::Render
{
    StreamlineSrPass::StreamlineSrPass() : RenderPass("SuperResolution")
    {
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kBeforePostprocess) + 10u);
    }

    void StreamlineSrPass::OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data)
    {
        const RDG::RGHandle color = rendering_data._rg_handles._color_tex;
        const RDG::RGHandle depth = rendering_data._rg_handles._depth_tex;
        const RDG::RGHandle motion_vectors = rendering_data._rg_handles._motion_vector_tex;
        if (!color.IsValid() || !depth.IsValid() || !motion_vectors.IsValid() || rendering_data._camera == nullptr)
            return;

        const Vector2D<u32> output_size = rendering_data._camera->OutputSize();
        if (output_size.x == 0u || output_size.y == 0u)
            return;

        TextureDesc output_desc(output_size.x, output_size.y, ERenderTargetFormat::kRGBAHalf);
        output_desc._is_color_target = true;
        output_desc._is_random_access = true;
        output_desc._load = ELoadStoreAction::kNotCare;
        const RDG::RGHandle output = graph.CreateResource(output_desc, "SuperResolutionOutput");
        graph.AddPass("SuperResolutionFallback", RDG::PassDesc(), [color, output](RDG::RenderGraphBuilder& builder)
        {
            builder.Read(color, EResourceUsage::kReadSRV);
            (void) builder.Write(output, EResourceUsage::kWriteRTV);
        }, [color, output](RDG::RenderGraph& graph, CommandBuffer* cmd, const RenderingData&)
        {
            cmd->Blit(color, output);
        });

        Camera* camera = const_cast<Camera*>(rendering_data._camera);
        const bool use_fsr = camera->IsFsrSRActive();
        const SuperResolutionFrameData frame_data = use_fsr
            ? SuperResolution::Snapshot(*camera, g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u)
            : SuperResolutionFrameData{};
        graph.AddPass("SuperResolutionEvaluate", RDG::PassDesc(),
            [color, depth, motion_vectors, output](RDG::RenderGraphBuilder& builder)
            {
                builder.Read(color, EResourceUsage::kReadSRV);
                builder.Read(depth, EResourceUsage::kReadSRV);
                builder.Read(motion_vectors, EResourceUsage::kReadSRV);
                (void) builder.Write(output, EResourceUsage::kWriteUAV);
            }, [color, depth, motion_vectors, output, camera, frame_data, use_fsr](RDG::RenderGraph& graph, CommandBuffer* cmd,
                                                              const RenderingData&)
            {
                Texture* color_texture = graph.Resolve<Texture>(color);
                Texture* depth_texture = graph.Resolve<Texture>(depth);
                Texture* motion_texture = graph.Resolve<Texture>(motion_vectors);
                Texture* output_texture = graph.Resolve<Texture>(output);
                const u64 frame_index = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
                cmd->RecordNativeCallback([camera, color_texture, depth_texture, motion_texture, output_texture,
                                          frame_index, frame_data, use_fsr](RHICommandBuffer* native_command_buffer)
                {
                    const bool succeeded = use_fsr
                        ? SuperResolution::Evaluate(frame_data, color_texture, depth_texture, motion_texture,
                                                    output_texture, native_command_buffer)
                        : StreamlineSR::Evaluate(*camera, color_texture, depth_texture, motion_texture, output_texture,
                                                 native_command_buffer, frame_index);
                    if (!succeeded && !use_fsr)
                        LOG_WARNING("Streamline: DLSS evaluation failed; retaining the spatial fallback for frame {}.",
                                    frame_index);
                });
            });

        rendering_data._rg_handles._upscaled_color = graph.GetTexture("SuperResolutionOutput");
    }

    StreamlineSrFeature::StreamlineSrFeature() : RenderFeature("SuperResolutionFeature") {}

    void StreamlineSrFeature::AddRenderPasses(Renderer& renderer, const RenderingData& rendering_data)
    {
        if (rendering_data._camera != nullptr && rendering_data._camera->IsTemporalUpscalingActive())
            renderer.EnqueuePass(&_pass);
    }
}
