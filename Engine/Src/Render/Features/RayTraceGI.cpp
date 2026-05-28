#include "Render/Features/RayTraceGI.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Scene/Scene.h"
#include "Render/Renderer.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"

namespace Ailu
{
    namespace Render
    {
        static u32 s_global_frame_counter = 0u;
        static u32 s_debug_collect_begin_frame = 0u;
        static bool s_is_debug_collecting = false;
        static u32 kCollectDebugFrameNum = 1u;
#pragma region RayTraceGI
        RayTraceGI::RayTraceGI() : RenderFeature("RayTraceGI")
        {
            _use_hardware_ray_tracing = GraphicsContext::Get().IsHardwareRayTracingSupported();
            if (_use_hardware_ray_tracing)
                _gi_raytracing_shader = RayTracingShader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/DXR/Raytracing.hlsl"), "RayTraceGI_RayTracingShader");
            _gi_compute_shader = ResourceMgr::Get().Load<ComputeShader>(L"Shaders/raytrace_gi.alasset");
            _gi_pass = MakeScope<GIPass>(_gi_compute_shader.get(), _gi_raytracing_shader.get());
            _gi_compute_shader->SetInts("_PickPixel",{200,200});
            _gi_compute_shader->SetBool("_enable_ris", _enable_ris);
            _gi_compute_shader->SetBool("_enable_resampling", _enable_resampling);
        }
        void RayTraceGI::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
        {
            if (s_is_debug_collecting && s_global_frame_counter - s_debug_collect_begin_frame >= kCollectDebugFrameNum)
            {
                s_is_debug_collecting = false;
                _debug_pos = {-1.0f, -1.0f};
            }
            _gi_pass->_debug_pos = _debug_pos;
            _gi_pass->_is_temporal_denoise = _is_temporal_denoise;
            _gi_pass->_use_hardware_ray_tracing = _use_hardware_ray_tracing;
            _gi_compute_shader->SetInts("_PickPixel", {(i32)_debug_pos.x, (i32)_debug_pos.y});
            if (_debug_pos.x >= 0 && _debug_pos.y >= 0)
            {
                if (s_debug_collect_begin_frame == 0u)
                    s_debug_collect_begin_frame = s_global_frame_counter;
                s_is_debug_collecting = true;
            }
            else
            {
                s_debug_collect_begin_frame = 0u;
                s_is_debug_collecting = false;
                _debug_pos = {-1.0f, -1.0f};
            }
            _gi_compute_shader->SetInt("_debug_hit_box_idx", _debug_hit_box);

            if (_gi_pass->CanUseHardwareRayTracing())
            {
                _gi_pass->PrepareHardwareRayTracingScene();
            }

            _gi_pass->_debug_line_mat->SetInt("_debug_hit_box_idx", _debug_hit_box);
            renderer.EnqueuePass(_gi_pass.get());
            ++s_global_frame_counter;
        }

        void Ailu::Render::RayTraceGI::OnPropertyChanged(const PropertyInfo &prop)
        {
            if (prop.Name() == "_is_show_debug")
            {
                _gi_compute_shader->SetBool("_show_debug", _is_show_debug);
            }
            else if (prop.Name() == "_enable_ris")
            {
                _gi_compute_shader->SetBool("_enable_ris", _enable_ris);
            }
            else if (prop.Name() == "_enable_resampling")
            {
                _gi_compute_shader->SetBool("_enable_resampling", _enable_resampling);
            }
            else {}
        }

#pragma endregion

#pragma region GIPass

        Ref<ConstantBuffer> g_perCamData;
        CBufferPerCameraData g_camData;

        GIPass::GIPass(ComputeShader *cs, RayTracingShader *rt_shader) : _gi_compute_shader(cs), _gi_raytracing_shader(rt_shader), RenderPass("GIPass")
        {
            g_perCamData.reset(ConstantBuffer::Create(sizeof(CBufferPerCameraData), "PerCamData"));


            _debug_line_mat = MakeRef<Material>(ResourceMgr::Get().Load<Shader>(L"Shaders/raytrace_debug_draw.alasset").get(), "RayDebugLineMat");
            // _kernel_ray_gen = cs->FindKernel("PrimaryRay");
            _kernel_ray_gen = cs->FindKernel("RayGen");
            _kernel_denoise = cs->FindKernel("Denoise");
            _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kAfterTransparent - 5);//before copy color
            _scene_rt_proxy = MakeScope<SceneRayTracingProxy>();
            _raygen_data.reset(ConstantBuffer::Create(sizeof(RayGenConstantBuffer), "RayTraceGI_RayGenData"));
            BufferDesc desc;
            desc._is_random_write = true;
            desc._is_readable = false;
            desc._format = EALGFormat::kALGFormatUNKOWN;
            desc._element_num = 5000;
            desc._element_size = 32u;//rt_common.hlsli  sizeof(DebugRay)
            desc._size = desc._element_size * desc._element_num;
            desc._target = EGPUBufferTarget::kAppend;
            _debug_buffer = GPUBuffer::Create(desc);
            desc._format = EALGFormat::kALGFormatR32_UINT;
            desc._element_size = sizeof(u32);
            _debug_index_buffer = GPUBuffer::Create(desc);
            _arg_buffer = GPUBuffer::Create(EGPUBufferTarget::kIndirectArguments, sizeof(DrawArguments), 1u, "RayDebugArgsBuffer");
            DrawArguments draw_arg{0u, 1u, 0, 0u};
            _arg_buffer->SetData((const u8 *) &draw_arg, sizeof(DrawArguments));
            _gi_compute_shader->SetBuffer("_debug_rays", _debug_buffer.get());
            _gi_compute_shader->SetBuffer("_debug_ray_indices", &*_debug_index_buffer);
        }

        bool GIPass::CanUseHardwareRayTracing() const
        {
            return _use_hardware_ray_tracing && g_pGfxContext != nullptr && g_pGfxContext->IsHardwareRayTracingSupported() && _gi_raytracing_shader != nullptr;
        }

        void GIPass::PrepareHardwareRayTracingScene()
        {
            auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
            if (scene == nullptr || _scene_rt_proxy == nullptr)
                return;

            _scene_rt_proxy->Sync(scene);
        }

        void GIPass::UpdateRayGenData(const RenderingData &rendering_data)
        {
            _raygen_cb.viewport = {-1.0f, -1.0f, 1.0f, 1.0f};
            const f32 border = 0.1f;
            const f32 aspect_ratio = rendering_data._height == 0u ? 1.0f : static_cast<f32>(rendering_data._width) / static_cast<f32>(rendering_data._height);
            _raygen_cb.stencil = {
                -1.0f + border,
                -1.0f + border * aspect_ratio,
                1.0f - border,
                1.0f - border * aspect_ratio};
            _raygen_data->SetData(reinterpret_cast<const u8 *>(&_raygen_cb), sizeof(RayGenConstantBuffer));
        }

        void GIPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
        {
            static Matrix4x4f s_prev_view_proj = Matrix4x4f::Identity();
            static u32 s_frame_counter = 0u;
            static bool s_prev_temporal_denoise = false;
            const auto& cur_vp_mat = rendering_data._camera->GetViewProj();
            bool is_camera_dirty = false;
            for (u32 i = 0; i < 4; i++)
            {
                for (u32 j = 0; j < 4; j++)
                {
                    if (!NearbyEqual(s_prev_view_proj[i][j], cur_vp_mat[i][j],1e-4f))
                    {
                        is_camera_dirty = true;
                        break;
                    }
                }
                if (is_camera_dirty)
                    break;
            }
            const bool denoise_just_enabled = _is_temporal_denoise && !s_prev_temporal_denoise;
            const bool target_recreated = MakesureTarget(rendering_data);
            if (is_camera_dirty || denoise_just_enabled || target_recreated)
            {
                s_frame_counter = 0u;
            }
            else
                ++s_frame_counter;
            s_prev_temporal_denoise = _is_temporal_denoise;
            s_prev_view_proj = rendering_data._camera->GetViewProj();
            _cur_target_handle = graph.Import(_is_cur_a ? _gi_texture_a.get() : _gi_texture_b.get());
            _history_target_handle = graph.Import(_is_cur_a ? _gi_texture_b.get() : _gi_texture_a.get());
            _gi_compute_shader->SetInt("_frame_index", s_frame_counter);
            _gi_compute_shader->SetInt("_prev_surface_buffer_idx", _is_cur_a ? (_surface_buffer_b ? _surface_buffer_b->GetBindlessUAVIndex() : -1) : (_surface_buffer_a ? _surface_buffer_a->GetBindlessUAVIndex() : -1));
            _gi_compute_shader->SetInt("_curr_surface_buffer_idx", _is_cur_a ? (_surface_buffer_a ? _surface_buffer_a->GetBindlessUAVIndex() : -1) : (_surface_buffer_b ? _surface_buffer_b->GetBindlessUAVIndex() : -1));

            const bool use_hardware_ray_tracing = CanUseHardwareRayTracing();
            if (!use_hardware_ray_tracing && _scene_rt_proxy != nullptr)
            {
                _scene_rt_proxy->SyncLightCache(SceneManagement::SceneMgr::Get().ActiveScene());
                auto light_data = _scene_rt_proxy->GetUnifiedLightData();
                if (!light_data->IsReady())
                    return;
                _gi_compute_shader->SetBuffer("_UnifiedLights", _scene_rt_proxy->GetUnifiedLightData());
            }
            {
                graph.AddPass(use_hardware_ray_tracing ? "RayTrace GI DXR" : "RayTrace GI", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
                    {
                        builder.Read(rendering_data._rg_handles._gbuffers[0]);
                        builder.Read(rendering_data._rg_handles._gbuffers[1]);
                        builder.Read(rendering_data._rg_handles._gbuffers[2]);
                        builder.Read(rendering_data._rg_handles._gbuffers[3]);
                        builder.Read(rendering_data._rg_handles._depth_tex);
                        builder.Read(rendering_data._rg_handles._motion_vector_tex);
                        _cur_target_handle = builder.Write(_cur_target_handle,EResourceUsage::kWriteUAV);
                    },
                    [this, use_hardware_ray_tracing](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                    {
                    if (use_hardware_ray_tracing)
                    {
                        auto *rt_scene = _scene_rt_proxy->GetScene();
                        auto *output = graph.Resolve<Texture>(_cur_target_handle);
                        if (rt_scene == nullptr || !_scene_rt_proxy->HasRenderableScene() || output == nullptr || !rt_scene->IsReady())
                            return;

                        auto cam = *Camera::sCurrent;
                        f32 f = cam.Far(), n = cam.Near();
                        u32 pixel_width = cam.OutputSize().x, pixel_height = cam.OutputSize().y;
                        g_camData._ScreenParams = Vector4f(1.0f / (f32) pixel_width, 1.0f / (f32) pixel_height, (f32) pixel_width, (f32) pixel_height);
                        Camera::CalculateZBUfferAndProjParams(cam, g_camData._ZBufferParams, g_camData._ProjectionParams);
                        g_camData._CameraPos = cam.Position();
                        g_camData._MatrixV = cam.GetView();
                        g_camData._MatrixP = cam.GetProj();
                        g_camData._MatrixVP = cam.GetViewProj();
                        g_camData._MatrixVP_NoJitter = cam.GetViewProj();
                        g_camData._MatrixIVP = MatrixInverse(g_camData._MatrixVP);
                        g_perCamData->SetData((const u8 *) data._p_per_camera_cbuf->GetData(), sizeof(CBufferPerCameraData));
            
                        _gi_raytracing_shader->SetScene(rt_scene);
                        _gi_raytracing_shader->SetBuffer("rt_instance_data", _scene_rt_proxy->GetInstanceData());
                        _gi_raytracing_shader->SetBuffer("g_rayGenCB", _raygen_data.get());
                        _gi_raytracing_shader->SetBuffer("g_perSceneData", data._p_per_scene_cbuf);
                        _gi_raytracing_shader->SetBuffer("g_perCamData", g_perCamData.get());
                        _gi_raytracing_shader->SetBuffer("g_material_data", _scene_rt_proxy->GetMaterialData());
                        _gi_raytracing_shader->SetBuffer("_UnifiedLights", _scene_rt_proxy->GetUnifiedLightData());
                        _gi_raytracing_shader->SetBuffer("_UnifiedLightConfig", _scene_rt_proxy->GetUnifiedLightConfig());
                        //_gi_raytracing_shader->SetBuffer("g_perCamData", data._p_per_camera_cbuf);
                        _gi_raytracing_shader->SetTexture("RenderTarget", output);
                        cmd->DispatchRays(_gi_raytracing_shader, rt_scene, static_cast<u16>(data._width), static_cast<u16>(data._height), 1u);
                        return;
                    }

                    Vector4f params = {1.0f, 1.0f, (f32) (data._width), (f32) (data._height)};
                    params.x /= params.z;
                    params.y /= params.w;
                    u16 w = (u16) params.z, h = (u16) params.w;
                    {
                        constexpr u32 kTileCountX = 1u;
                        constexpr u32 kTileCountY = 1u;
                        const u16 tile_w = (u16) ((w + kTileCountX - 1u) / kTileCountX);
                        const u16 tile_h = (u16) ((h + kTileCountY - 1u) / kTileCountY);
                        const u32 tile_index = _tile_frame_counter % (kTileCountX * kTileCountY);
                        const u32 tile_x = tile_index % kTileCountX;
                        const u32 tile_y = tile_index / kTileCountX;
                        const u16 tile_offset_x = (u16) (tile_x * tile_w);
                        const u16 tile_offset_y = (u16) (tile_y * tile_h);
                        const u16 tile_size_x = (u16) ((tile_offset_x + tile_w > w) ? (w - tile_offset_x) : tile_w);
                        const u16 tile_size_y = (u16) ((tile_offset_y + tile_h > h) ? (h - tile_offset_y) : tile_h);
                        if (s_is_debug_collecting && s_debug_collect_begin_frame == s_global_frame_counter - 1)
                            _debug_buffer->SetCounter(0u);
                        auto [x, y, z] = _gi_compute_shader->CalculateDispatchNum(_kernel_ray_gen, tile_size_x, tile_size_y, 1);
                        _gi_compute_shader->SetTexture("_GBuffer0", graph.Resolve<Texture>(data._rg_handles._gbuffers[0]));
                        _gi_compute_shader->SetTexture("_GBuffer1", graph.Resolve<Texture>(data._rg_handles._gbuffers[1]));
                        _gi_compute_shader->SetTexture("_GBuffer2", graph.Resolve<Texture>(data._rg_handles._gbuffers[2]));
                        _gi_compute_shader->SetTexture("_GBuffer3", graph.Resolve<Texture>(data._rg_handles._gbuffers[3]));
                        _gi_compute_shader->SetTexture("_CameraDepthTexture", graph.Resolve<Texture>(data._rg_handles._depth_tex));
                        _gi_compute_shader->SetTexture("_MotionVectorTexture", graph.Resolve<Texture>(data._rg_handles._motion_vector_tex));
                        //_gi_compute_shader->SetBuffer("_debug_rays", _debug_buffer.get());
                        //_gi_compute_shader->SetBuffer("_debug_ray_indices", &*_debug_index_buffer);
                        auto light_count = _scene_rt_proxy->GetUnifiedLightCount();
                        _gi_compute_shader->SetInt("_light_count", light_count);
                        _gi_compute_shader->SetInts("_GI_TileOffset", {(i32) tile_offset_x, (i32) tile_offset_y});
                        _gi_compute_shader->SetInts("_GI_TileSize", {(i32) tile_size_x, (i32) tile_size_y});
                        _gi_compute_shader->SetTexture("_GI_Texture", graph.Resolve<Texture>(_cur_target_handle));
                        _gi_compute_shader->SetBuffer("g_prev_reservoir", _is_cur_a ? _reservoir_b.get() : _reservoir_a.get());
                        _gi_compute_shader->SetBuffer("g_curr_reservoir", _is_cur_a ? _reservoir_a.get() : _reservoir_b.get());
                        cmd->Dispatch(_gi_compute_shader, _kernel_ray_gen, x, y, 1);
                    } });
            }
            ++_tile_frame_counter;
            if (_is_temporal_denoise)
            {
                graph.AddPass("Denoise", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(_cur_target_handle);
                    builder.Read(_history_target_handle);
                    builder.Read(rendering_data._rg_handles._motion_vector_tex);
                    _cur_target_handle = builder.Write(_cur_target_handle,EResourceUsage::kWriteUAV);
                },
                [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                {
                Vector4f params = {1.0f, 1.0f, (f32) (data._width), (f32) (data._height)};
                params.x /= params.z;
                params.y /= params.w;
                u16 w = (u16) params.z, h = (u16) params.w;
                {
                    auto [x, y, z] = _gi_compute_shader->CalculateDispatchNum(_kernel_denoise, w, h, 1);
                    _gi_compute_shader->SetTexture("_CurrentTarget", graph.Resolve<Texture>(_cur_target_handle));
                    _gi_compute_shader->SetTexture("_HistoryTarget", graph.Resolve<Texture>(_history_target_handle));
                    cmd->Dispatch(_gi_compute_shader, _kernel_denoise, x, y, 1);
                } });
            }
            _is_cur_a = !_is_cur_a;
            graph.AddPass("Debug GI", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                          {
                    builder.Read(_cur_target_handle);
                    builder.Read(rendering_data._rg_handles._color_target);
                    rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target); 
                    }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                { 
                    cmd->Blit(_cur_target_handle,data._rg_handles._color_target);
                });
            if (!use_hardware_ray_tracing)
            {
                graph.AddPass("Debug Ray", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                              {
                        builder.Read(rendering_data._rg_handles._color_target);
                        builder.Read(rendering_data._rg_handles._depth_target);
                        rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                        rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target,EResourceUsage::kDSV);
                    }, 
                    [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                              { 
                        _debug_line_mat->SetBuffer("_ray_list", _debug_buffer.get());
                        cmd->SetRenderTarget(data._rg_handles._color_target,data._rg_handles._depth_target);
                        cmd->CopyCounterValue(&*_debug_buffer, &*_arg_buffer, offsetof(DrawArguments, DrawArguments::_vertex_count_per_instance));
                        cmd->DrawProceduralIndirect(&*_debug_line_mat,0,&*_arg_buffer);
                    });
            }
        }

        bool GIPass::MakesureTarget(const RenderingData &rendering_data)
        {
            bool recreated = false;
            if (_gi_texture_a == nullptr || _gi_texture_a->Width() != rendering_data._width || _gi_texture_a->Height() != rendering_data._height)
            {
                TextureDesc desc;
                desc._width = rendering_data._width;
                desc._height = rendering_data._height;
                desc._format = EALGFormat::kALGFormatR32G32B32A32_FLOAT;
                desc._is_random_access = true;
                desc._mip_num = 0;
                _gi_texture_a = Texture2D::Create(desc);
                _gi_texture_a->Apply();
                _gi_texture_a->Name("RayTraceGI_A");
                _gi_texture_b = Texture2D::Create(desc);
                _gi_texture_b->Apply();
                _gi_texture_b->Name("RayTraceGI_B");
                recreated = true;
                BufferDesc buf_desc;
                buf_desc._element_num = rendering_data._width * rendering_data._height;
                buf_desc._element_size = 40u;//sizeof(Reservoir) restir_di.hlsli
                buf_desc._size = buf_desc._element_num * buf_desc._element_size;
                buf_desc._is_random_write = true;
                buf_desc._format = EALGFormat::kALGFormatUNKOWN;
                buf_desc._target = EGPUBufferTarget::kStructured;
                _reservoir_a = GPUBuffer::Create(buf_desc);
                _reservoir_b = GPUBuffer::Create(buf_desc);
                _reservoir_a->Name("RayTraceGI_ReservoirA");
                _reservoir_b->Name("RayTraceGI_ReservoirB");
                // Vector<u8> reservoir_zero(buf_desc._size, 0u);
                // _reservoir_a->SetData(reservoir_zero);
                // _reservoir_b->SetData(reservoir_zero);
                buf_desc._element_size = 64u;//sizeof(Surface) restir_di.hlsli
                buf_desc._size = buf_desc._element_num * buf_desc._element_size;
                _surface_buffer_a = GPUBuffer::Create(buf_desc);
                _surface_buffer_b = GPUBuffer::Create(buf_desc);
                _surface_buffer_a->Name("RayTraceGI_SurfaceBufferA");
                _surface_buffer_b->Name("RayTraceGI_SurfaceBufferB");
                // Vector<u8> surface_zero(buf_desc._size, 0u);
                // _surface_buffer_a->SetData(surface_zero);
                // _surface_buffer_b->SetData(surface_zero);
            }
            return recreated;
        }
#pragma endregion
    }// namespace Render
} // namespace Ailu