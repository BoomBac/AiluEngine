#include "Render/Features/DepthOfFieldPass.h"

#include "Framework/Common/ResourceMgr.h"
#include "Render/Camera.h"
#include "Render/CommandBuffer.h"
#include "Render/Material.h"
#include "Render/RenderGraph/RenderGraph.h"

namespace Ailu::Render
{
    DepthOfFieldPass::DepthOfFieldPass()
    {
        _shader = ResourceMgr::Get().LoadAssetRef<Shader>(L"Shaders/hlsl/PostProcess/depth_of_field.alasset");
        if (_shader != nullptr && _shader->Compile())
            for (u32 i = 0; i < 6u; ++i)
                _materials[i] = MakeRef<Material>(_shader.get(), std::format("DepthOfField_{}", i));
    }

    RDG::RGHandle DepthOfFieldPass::Record(RDG::RenderGraph &graph, RDG::RGHandle source, RDG::RGHandle depth,
                                          const Camera &camera, const DepthOfFieldSettings &settings)
    {
        if (_materials[0] == nullptr || !source.IsValid() || !depth.IsValid() ||
            settings._maximum_coc_diameter_px <= 0.0f)
            return source;

        const TextureDesc source_desc = graph.GetTextureDesc(source);
        const TextureDesc depth_desc = graph.GetTextureDesc(depth);
        if (source_desc._width == 0u || source_desc._height == 0u || depth_desc._width == 0u ||
            depth_desc._height == 0u)
            return source;

        const PhysicalCameraSettings &lens = camera.PhysicalSettings();
        const f32 focal_length_mm = lens._lens_projection_mode == ELensProjectionMode::kPhysicalFocalLength
            ? lens._focal_length_mm : Camera::ComputeFocalLength(lens._sensor_height_mm, camera.VerticalFov());
        const f32 focal_length_m = std::clamp(focal_length_mm * 0.001f, 0.001f, 10.0f);
        const f32 aperture = std::clamp(lens._aperture_f_number, 0.1f, 128.0f);
        const f32 focus_distance_m = std::clamp(lens._focus_distance_m, focal_length_m + 0.001f, 1.0e6f);
        const f32 sensor_height_m = std::clamp(lens._sensor_height_mm * 0.001f, 0.001f, 10.0f);

        const u32 work_width = std::max(1u, (source_desc._width + 1u) / 2u);
        const u32 work_height = std::max(1u, (source_desc._height + 1u) / 2u);
        const char *names[] = {"DepthOfFieldPrefilter", "DepthOfFieldDilateX", "DepthOfFieldDilateY",
                               "DepthOfFieldFar", "DepthOfFieldNear", "DepthOfField"};
        RDG::RGHandle outputs[6];
        for (u32 i = 0; i < 6u; ++i)
        {
            const bool composite = i == 5u;
            const ERenderTargetFormat format = i == 1u || i == 2u ? ERenderTargetFormat::kRFloat :
                composite ? ERenderTargetFormat::kDefaultHDR : ERenderTargetFormat::kRGBAHalf;
            TextureDesc desc(composite ? source_desc._width : work_width,
                             composite ? source_desc._height : work_height, format);
            desc._is_color_target = true;
            desc._load = ELoadStoreAction::kNotCare;
            const String resource_name = composite ? "DepthOfFieldColor" : names[i];
            const RDG::RGHandle output = graph.CreateResource(desc, resource_name);
            const RDG::RGHandle prepared = i > 0u ? outputs[0] : RDG::RGHandle{};
            const RDG::RGHandle radius = i == 2u ? outputs[1] : (i == 3u || i == 4u) ? outputs[2] : RDG::RGHandle{};
            const RDG::RGHandle far_layer = composite ? outputs[3] : RDG::RGHandle{};
            const RDG::RGHandle near_layer = composite ? outputs[4] : RDG::RGHandle{};
            graph.AddPass(names[i], RDG::PassDesc(), [=](RDG::RenderGraphBuilder &builder)
            {
                if (i == 0u || composite)
                {
                    builder.Read(source, EResourceUsage::kReadSRV);
                    builder.Read(depth, EResourceUsage::kReadSRV);
                }
                if (i == 1u || i == 3u || i == 4u)
                    builder.Read(prepared, EResourceUsage::kReadSRV);
                if (radius.IsValid())
                    builder.Read(radius, EResourceUsage::kReadSRV);
                if (composite)
                {
                    builder.Read(far_layer, EResourceUsage::kReadSRV);
                    builder.Read(near_layer, EResourceUsage::kReadSRV);
                }
                (void) builder.Write(output);
            }, [=, this, near_clip = camera.Near(), far_clip = camera.Far()]
               (RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
            {
                Material *material = _materials[i].get();
                cmd->SetRenderTarget(output);
                if (i == 0u || composite)
                {
                    material->SetTexture("_SourceTex", pass_graph.Resolve<Texture>(source));
                    material->SetTexture("_CameraDepthTexture", pass_graph.Resolve<Texture>(depth));
                }
                if (i == 1u || i == 3u || i == 4u)
                    material->SetTexture("_PreparedTex", pass_graph.Resolve<Texture>(prepared));
                if (radius.IsValid())
                    material->SetTexture("_RadiusTex", pass_graph.Resolve<Texture>(radius));
                if (composite)
                {
                    material->SetTexture("_FarTex", pass_graph.Resolve<Texture>(far_layer));
                    material->SetTexture("_NearTex", pass_graph.Resolve<Texture>(near_layer));
                }
                material->SetVector("_SourceSize", Vector4f(static_cast<f32>(source_desc._width),
                    static_cast<f32>(source_desc._height), 1.0f / source_desc._width, 1.0f / source_desc._height));
                material->SetVector("_WorkSize", Vector4f(static_cast<f32>(work_width),
                    static_cast<f32>(work_height), 1.0f / work_width, 1.0f / work_height));
                material->SetVector("_LensParams",
                                    Vector4f(focal_length_m, aperture, focus_distance_m, sensor_height_m));
                material->SetVector("_CameraParams", Vector4f(near_clip, far_clip, 0.0f, 0.0f));
                material->SetFloat("_MaximumCoCDiameterPx",
                                   std::clamp(settings._maximum_coc_diameter_px, 0.0f, 64.0f));
                material->SetInt("_ApertureBladeCount", std::clamp(settings._aperture_blade_count, 3, 9));
                cmd->DrawFullScreenQuad(material, static_cast<u16>(i));
            });
            outputs[i] = graph.GetTexture(resource_name);
        }
        return outputs[5];
    }
}
