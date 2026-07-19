#include "Render/Features/RTXDI.h"

#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/Renderer.h"
#include "Scene/Scene.h"

namespace Ailu::Render
{
    #define GET_MEMBER_NAME(member_name) (std::string_view(#member_name))
    RTXDI::RTXDI() : RenderFeature("RTXDI")
    {
        _rtxdi_compute_shader = ResourceMgr::Get().Load<ComputeShader>(L"Shaders/hlsl/ray_trace/RTXDI/rtxdi.alasset");
        _rtxdi_pass = MakeScope<RTXDIPass>(_rtxdi_compute_shader.get());
        _rtxdi_compute_shader->SetInt("_light_sample_count", static_cast<i32>(_light_sample_count));
        _rtxdi_compute_shader->SetInt("_brdf_sample_count", static_cast<i32>(_brdf_sample_count));
        _rtxdi_compute_shader->SetBool("_enable_resampling", _enable_resampling);
    }

    void RTXDI::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
    {
        renderer.EnqueuePass(_rtxdi_pass.get());
    }

    void Ailu::Render::RTXDI::OnPropertyChanged(const PropertyInfo &prop)
    {
        if (prop.Name() == GET_MEMBER_NAME(_light_sample_count) ||
            prop.Name() == GET_MEMBER_NAME(_brdf_sample_count))
        {
            if (_rtxdi_compute_shader)
            {
                _rtxdi_compute_shader->SetInt("_light_sample_count", static_cast<i32>(_light_sample_count));
                _rtxdi_compute_shader->SetInt("_brdf_sample_count", static_cast<i32>(_brdf_sample_count));
            }
        }
        else if (prop.Name() == GET_MEMBER_NAME(_enable_resampling))
        {
            if (_rtxdi_compute_shader)
            {
                _rtxdi_compute_shader->SetBool("_enable_resampling", _enable_resampling);
            }
        }
    }


    RTXDIPass::RTXDIPass(ComputeShader *shader) : _compute_shader(shader), RenderPass("RTXDIPass")
    {
        _kernel_ray_gen = _compute_shader ? _compute_shader->FindKernel("RayGen") : static_cast<u16>(-1);
        _event = static_cast<ERenderPassEvent::ERenderPassEvent>(ERenderPassEvent::kAfterTransparent - 5);
        _scene_rt_proxy = MakeScope<SceneRayTracingProxy>();

        if (_compute_shader)
        {
            _compute_shader->SetInts("_PickPixel", {0, 0});
            _compute_shader->SetInt("_debug_hit_box_idx", 0);
            _compute_shader->SetBool("_show_debug", false);
            _compute_shader->SetBool("_enable_ris", true);
            _compute_shader->SetBool("_enable_resampling", true);
            _compute_shader->SetInt("_prev_surface_buffer_idx", -1);
            _compute_shader->SetInt("_curr_surface_buffer_idx", -1);
        }
    }

    void RTXDIPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_compute_shader == nullptr || _kernel_ray_gen == static_cast<u16>(-1))
            return;
        if (!EnsureTarget(rendering_data))
            return;

        auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
        if (scene == nullptr || _scene_rt_proxy == nullptr)
            return;

        _scene_rt_proxy->SyncLightCache(scene);
        auto *light_data = _scene_rt_proxy->GetUnifiedLightData();
        if (light_data == nullptr || !light_data->IsReady())
            return;

        _output_handle = graph.Import(_output_texture.get());

        _compute_shader->SetInt("_curr_reservoir_buffer_handle", _use_reservoir_a ? _reservoir_a->GetBindlessUAVIndex() : _reservoir_b->GetBindlessUAVIndex());
        _compute_shader->SetInt("_prev_reservoir_buffer_handle", _use_reservoir_a ? _reservoir_b->GetBindlessUAVIndex() : _reservoir_a->GetBindlessUAVIndex());

        graph.AddPass("RTXDI", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(rendering_data._rg_handles._gbuffers[0]);
            builder.Read(rendering_data._rg_handles._gbuffers[1]);
            builder.Read(rendering_data._rg_handles._gbuffers[2]);
            builder.Read(rendering_data._rg_handles._gbuffers[3]);
            builder.Read(rendering_data._rg_handles._depth_tex);
            _output_handle = builder.Write(_output_handle, EResourceUsage::kWriteUAV);
        },
        [this, light_data](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
        {
            auto *output = graph.Resolve<Texture>(_output_handle);
            if (output == nullptr)
                return;

            const u16 width = static_cast<u16>(data._width);
            const u16 height = static_cast<u16>(data._height);
            auto [dispatch_x, dispatch_y, dispatch_z] = _compute_shader->CalculateDispatchNum(_kernel_ray_gen, width, height, 1);

            _compute_shader->SetTexture("_GBuffer0", graph.Resolve<Texture>(data._rg_handles._gbuffers[0]));
            _compute_shader->SetTexture("_GBuffer1", graph.Resolve<Texture>(data._rg_handles._gbuffers[1]));
            _compute_shader->SetTexture("_GBuffer2", graph.Resolve<Texture>(data._rg_handles._gbuffers[2]));
            _compute_shader->SetTexture("_GBuffer3", graph.Resolve<Texture>(data._rg_handles._gbuffers[3]));
            _compute_shader->SetTexture("_CameraDepthTexture", graph.Resolve<Texture>(data._rg_handles._depth_tex));
            _compute_shader->SetTexture("_GI_Texture", output);
            _compute_shader->SetBuffer("_UnifiedLights", light_data);
            _compute_shader->SetInt("_light_count", static_cast<i32>(_scene_rt_proxy->GetUnifiedLightCount()));
            _compute_shader->SetInts("_GI_TileOffset", {0, 0});
            _compute_shader->SetInts("_GI_TileSize", {static_cast<i32>(data._width), static_cast<i32>(data._height)});
            cmd->Dispatch(_compute_shader, _kernel_ray_gen, dispatch_x, dispatch_y, dispatch_z);
        });

        graph.AddPass("RTXDI Composite", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(_output_handle);
            builder.Read(rendering_data._rg_handles._color_target);
            rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
        },
        [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
        {
            cmd->Blit(_output_handle, data._rg_handles._color_target);
        });

        _use_reservoir_a = !_use_reservoir_a;
    }

    bool RTXDIPass::EnsureTarget(const RenderingData &rendering_data)
    {
        if (_output_texture != nullptr &&
            _output_texture->Width() == rendering_data._width &&
            _output_texture->Height() == rendering_data._height)
        {
            return true;
        }

        TextureDesc desc;
        desc._width = rendering_data._width;
        desc._height = rendering_data._height;
        desc._format = EALGFormat::kALGFormatR32G32B32A32_FLOAT;
        desc._is_random_access = true;
        desc._mip_num = 0;
        _output_texture = Texture2D::Create(desc);
        if (_output_texture == nullptr)
            return false;

        _output_texture->Apply();
        _output_texture->Name("RTXDI_Output");

        BufferDesc buf_desc;
        buf_desc._element_num = rendering_data._width * rendering_data._height;
        buf_desc._element_size = 24u;//sizeof(Reservoir) restir_di.hlsli
        buf_desc._size = buf_desc._element_num * buf_desc._element_size;
        buf_desc._is_random_write = true;
        buf_desc._format = EALGFormat::kALGFormatUNKOWN;
        buf_desc._target = EGPUBufferTarget::kRaw;
        _reservoir_a = GPUBuffer::Create(buf_desc);
        _reservoir_b = GPUBuffer::Create(buf_desc);
        _reservoir_a->Name("_ReservoirA");
        _reservoir_b->Name("_ReservoirB");

        return true;
    }
}