#include "Render/Features/PostprocessStack.h"

#include "Render/RenderingData.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/Material.h"
#include "Render/Camera.h"

namespace Ailu::Render
{
    PostprocessStack::PostprocessStack() : RenderPass("PostprocessStack")
    {
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kBeforePostprocess) + 25u);
    }

    RenderTexture *PostprocessStack::GetPreExposureTexture(const Camera &camera) const
    {
        return _auto_exposure.GetPreExposureTexture(camera);
    }

    RenderTexture *PostprocessStack::GetPreviousPreExposureTexture(const Camera &camera) const
    {
        return _auto_exposure.GetPreviousPreExposureTexture(camera);
    }

    void PostprocessStack::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        const PostprocessSettings &settings = rendering_data._postprocess_settings;
        RDG::RGHandle color = rendering_data._rg_handles._upscaled_color.IsValid()
            ? rendering_data._rg_handles._upscaled_color
            : rendering_data._rg_handles._taa_color.IsValid()
                ? rendering_data._rg_handles._taa_color
                : rendering_data._rg_handles._color_tex.IsValid()
                    ? rendering_data._rg_handles._color_tex
                    : rendering_data._rg_handles._color_target;
        RDG::RGHandle exposure_ev{};
        f32 scalar_exposure = settings._tone_mapping._exposure;
        const Camera *camera = rendering_data._camera;
        if (camera != nullptr && camera->_postprocess_enabled)
        {
            const PhysicalCameraSettings &camera_settings = camera->PhysicalSettings();
            switch (camera_settings._exposure_mode)
            {
            case EExposureMode::kManualPhysical:
                scalar_exposure = camera->ComputeExposureMultiplier() *
                                   ResolveSceneLuminanceToCdM2(settings._auto_exposure);
                break;
            case EExposureMode::kAuto:
            case EExposureMode::kAperturePriority:
            case EExposureMode::kShutterPriority:
            case EExposureMode::kAutoISO:
                exposure_ev = _auto_exposure.Record(graph, color, rendering_data, *camera, settings._auto_exposure);
                break;
            case EExposureMode::kLegacyManual:
            default:
                break;
            }

            if (settings._depth_of_field._enabled)
                color = _depth_of_field.Record(graph, color, rendering_data._rg_handles._depth_tex, *camera,
                                               settings._depth_of_field);
        }
        if (settings._bloom._enabled)
            color = _bloom.Record(graph, color, rendering_data, settings._bloom);
        // Exposure is shared by all view transforms, including the uncompressed None mode.
        color = _tone_mapping.Record(graph, color, rendering_data, settings._tone_mapping, exposure_ev,
                                     scalar_exposure);
        // 只登记后处理链的最终输出，不改写 _color_target：注册在 PostprocessStack 之前的 Pass 会在
        // 录制阶段读取 _color_target，改写它会让这些 Pass 绑定到后处理资源上。
        rendering_data._rg_handles._final_color = color;
        const TextureDesc color_desc = graph.GetTextureDesc(color);
        const RDG::RGHandle scene_depth = rendering_data._rg_handles._depth_target;
        const TextureDesc depth_desc = graph.GetTextureDesc(scene_depth);
        if (color_desc._width != depth_desc._width || color_desc._height != depth_desc._height)
        {
            if (_overlay_depth_material == nullptr)
            {
                _overlay_depth_shader = Shader::Create(ResourceMgr::GetResSysPath(
                    L"Shaders/hlsl/postprocess_overlay_depth.hlsl"));
                if (!_overlay_depth_shader->Compile())
                    return;
                _overlay_depth_material = MakeRef<Material>(_overlay_depth_shader.get(), "PostprocessOverlayDepth");
            }
            TextureDesc desc(color_desc._width, color_desc._height, ERenderTargetFormat::kDepth);
            desc._load = ELoadStoreAction::kNotCare;
            const RDG::RGHandle overlay_depth = graph.CreateResource(desc, "PostprocessOverlayDepth");
            graph.AddPass("PostprocessOverlayDepth", RDG::PassDesc(),
                [scene_depth, overlay_depth](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(scene_depth);
                    (void) builder.Write(overlay_depth, EResourceUsage::kDSV);
                }, [this, scene_depth, overlay_depth](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &)
                {
                    auto *depth = graph.Resolve<RenderTexture>(overlay_depth);
                    cmd->SetRenderTarget(static_cast<RenderTexture *>(nullptr), depth);
                    cmd->SetGlobalTexture("_BlitSourceTex", graph.Resolve<Texture>(scene_depth));
                    cmd->DrawFullScreenQuad(_overlay_depth_material.get(), 0u);
                });
            rendering_data._rg_handles._final_depth = graph.GetTexture("PostprocessOverlayDepth");
        }
    }
}
