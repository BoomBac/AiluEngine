#include "Render/Features/VolumetricClouds.h"
#include "Inc/Framework/Common/Application.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Inc/Framework/Common/JobSystem.h"
#include "Inc/Framework/Common/Profiler.h"
#include "Inc/Framework/Common/ResourceMgr.h"
#include "Inc/Framework/Math/Random.hpp"
#include "Inc/Render/Gizmo.h"
#include "Render/Camera.h"
#include "Inc/Render/Renderer.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "pch.h"

#pragma region NoiseUtils
namespace
{
    using Ailu::u32;
    using Ailu::Math::Vector3UInt;

    static Vector3UInt FromLinearIndex(u32 id, Vector3UInt dim)
    {
        Vector3UInt index;
        index.z = id / (dim.x * dim.y);
        index.y = (id % (dim.x * dim.y)) / dim.x;
        index.x = id % dim.x;
        return index;
    }
}// namespace
#pragma endregion
namespace Ailu::Render
{
#pragma region VolumetricCloudsPass
    VolumetricCloudsPass::VolumetricCloudsPass() : RenderPass("VolumetricCloudsPass")
    {
        //r覆盖率,g降雨概率,b类型(0.0层云，0.5积云，1.0积雨云)
        TextureImportSetting setting;
        setting._generate_mipmap = true;
        _weather_map = ResourceMgr::Get().Load<Texture2D>(EnginePath::kEngineTexturePathW + L"weather_map.alasset", &setting);
        auto cloud_shader = ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/global_volumetric_cloud.alasset");
        _global_cloud = MakeRef<Material>(cloud_shader.get(), "Runtime/VolumetricClouds");
        _event = ERenderPassEvent::kAfterMotionVector;
        _noise_gen = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/volumetric_noise_generator.alasset");
        _cloud_gen = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/volumetric_cloud.alasset");
        _cloud_main_kernel = _cloud_gen->FindKernel("CloudMain");
        _cloud_reprojection_kernel = _cloud_gen->FindKernel("CloudReprojection");
        _cloud_gen->EnableKeyword("_QUALITY_HIGH");
        u16 w = 128, h = w, d = w;
        TextureDesc desc;
        desc._width = w;
        desc._height = h;
        desc._depth = d;
        desc._mip_num = Texture::MaxMipmapCount(w,h,d);
        desc._format = ConvertTextureFormatToPixelFormat(ETextureFormat::kRGBA32);
        desc._is_readable = false;
        desc._is_random_access = true;
        desc._dimension = ETextureDimension::kTex3D;
        _shape_noise = Texture3D::Create(desc);
        _shape_noise->Name("VolumetricCloudsShapeNoise");
        _shape_noise->Apply();
        u16 thread_size_x, thread_size_y, thread_size_z;
        auto shape_kernel = _noise_gen->FindKernel("ShapeNoiseMain");
        _noise_gen->SetVector("_Size", Vector4f((f32) w, (f32) w, (f32) w, 0.f));
        _noise_gen->SetTexture(shape_kernel, "_OutNoise3D", _shape_noise.get());
        _noise_gen->GetThreadNum(shape_kernel, thread_size_x, thread_size_y, thread_size_z);
        auto cmd = CommandBufferPool::Get("CloudNoiseGen");
        cmd->Dispatch(_noise_gen.get(), shape_kernel, w / thread_size_x, h / thread_size_y, d / thread_size_z);
        w = h = d = 32;
        desc._width = w;
        desc._height = h;
        desc._depth = d;
        _detail_noise = Texture3D::Create(desc);
        _detail_noise->Name("VolumetricCloudsDetailNoise");
        _detail_noise->Apply();
        shape_kernel = _noise_gen->FindKernel("DetailNoiseMain");
        _noise_gen->SetVector("_Size", Vector4f((f32) w, (f32) w, (f32) w, 0.f));
        _noise_gen->SetTexture(shape_kernel, "_OutNoise3D", _detail_noise.get());
        cmd->Dispatch(_noise_gen.get(), shape_kernel, w / thread_size_x, h / thread_size_y, d / thread_size_z);
        TextureDesc curl_desc;
        curl_desc._width = 128;
        curl_desc._height = 128;
        curl_desc._mip_num = 1;
        curl_desc._format = ConvertTextureFormatToPixelFormat(ETextureFormat::kRGBA32);
        curl_desc._is_readable = false;
        curl_desc._is_random_access = true;
        _curl_noise = Texture2D::Create(curl_desc);
        _curl_noise->Apply();
        _curl_noise->Name("CurlNoise");
        auto curl_kernel = _noise_gen->FindKernel("CurlNoiseMain");
        _noise_gen->SetVector("_Size", Vector4f(128.0f, 128.0f, 1.0f / 128.0f, 1.0f / 128.0f));
        _noise_gen->SetTexture(curl_kernel, "_CurlOut", _curl_noise.get());
        _noise_gen->GetThreadNum(curl_kernel, thread_size_x, thread_size_y, thread_size_z);
        cmd->Dispatch(_noise_gen.get(), curl_kernel, 128 / thread_size_x, 128 / thread_size_y, 1);
        g_pGfxContext->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        _shape_noise->GenerateMipmap();
        _detail_noise->GenerateMipmap();
        ResourceMgr::Get().RegisterResource(L"Runtime/CloudTex", _shape_noise);
        _blue_noise = ResourceMgr::Get().Load<Texture2D>(EnginePath::kEngineTexturePathW + L"blue_noise.alasset");
    }
    VolumetricCloudsPass::~VolumetricCloudsPass()
    {
    }
    void VolumetricCloudsPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (rendering_data._camera == nullptr)
            return;

        const auto depth_handle = rendering_data._rg_handles._depth_tex;
        const auto sky_view_lut_handle = rendering_data._rg_handles._sky_view_lut;
        const auto motion_handle = rendering_data._rg_handles._motion_vector_tex;
        if (!depth_handle.IsValid() || !sky_view_lut_handle.IsValid())
        {
            _history_valid = false;
            return;
        }

        EnsureRenderTargets(rendering_data);
        UpdateShaderParams();

        const Camera &camera = *rendering_data._camera;
        const u64 camera_hash = camera.HashCode();
        const u64 frame_index = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
        const Vector3f camera_position = camera.Position();
        const Vector3f camera_forward = camera.Forward();
        const Vector3f camera_up = camera.Up();
        const Vector4f camera_jitter = camera.GetJitter();
        const Vector2f current_uv_jitter(camera_jitter.z, camera_jitter.w);
        const bool changed_camera = _has_rendered && _history_camera_hash != camera_hash;
        const bool changed_dimensions = _history_width != rendering_data._width ||
                                        _history_height != rendering_data._height;
        const bool skipped_frame = _has_rendered && frame_index != _last_render_frame + 1u;
        const bool camera_cut = _has_rendered &&
            (Math::Magnitude(camera_position - _previous_position) > 1.0f ||
             Math::DotProduct(camera_forward, _previous_forward) < 0.5f ||
             Math::DotProduct(camera_up, _previous_up) < 0.5f ||
             static_cast<u8>(camera.Type()) != _previous_projection_type ||
             std::abs(camera.FovH() - _previous_fov) > 1.0f ||
             std::abs(camera.Aspect() - _previous_aspect) > 1.0e-3f ||
             std::abs(camera.Near() - _previous_near) > std::max(_previous_near, 1.0e-3f) * 0.1f ||
             std::abs(camera.Far() - _previous_far) > std::max(_previous_far, 1.0e-3f) * 0.1f);
        const bool cloud_settings_changed = _has_rendered &&
            (_params._height != _previous_height || _params._thickness != _previous_thickness ||
             _params._density_multply != _previous_density_multiplier || _params._threshold != _previous_threshold ||
             _params._params.w != _previous_type_offset || _params._params.z != _previous_speed ||
             _params._exposure != _previous_exposure || _params._base_pos.x != _previous_base_pos.x ||
             _params._base_pos.y != _previous_base_pos.y || _params._base_pos.z != _previous_base_pos.z ||
             _params._absorption.x != _previous_absorption.x || _params._absorption.y != _previous_absorption.y ||
             _params._absorption.z != _previous_absorption.z || _params._scattering.x != _previous_scattering.x ||
             _params._scattering.y != _previous_scattering.y || _params._scattering.z != _previous_scattering.z ||
             _params._is_tile_render != _previous_tile_render ||
             _params._is_high_quailty != _previous_high_quality);
        if (changed_camera || changed_dimensions || skipped_frame || camera_cut || cloud_settings_changed ||
            rendering_data._is_res_changed)
            _history_valid = false;

        _history_width = rendering_data._width;
        _history_height = rendering_data._height;
        _history_write_executed = false;
        _reprojection_executed = false;
        _rg_commit_done = false;
        const bool can_reproject = _history_valid;
        const bool tile_render = _params._is_tile_render;
        const Vector2f previous_uv_jitter = can_reproject ? _previous_uv_jitter : current_uv_jitter;
        const Vector3f current_position = camera_position;
        const Vector3f current_forward = camera_forward;
        const Vector3f current_up = camera_up;
        const f32 current_height = _params._height;
        const f32 current_thickness = _params._thickness;
        const f32 current_density_multiplier = _params._density_multply;
        const f32 current_threshold = _params._threshold;
        const f32 current_type_offset = _params._params.w;
        const f32 current_speed = _params._params.z;
        const f32 current_exposure = _params._exposure;
        const Vector3f current_base_pos = _params._base_pos;
        const Vector3f current_absorption = _params._absorption;
        const Vector3f current_scattering = _params._scattering;
        const bool current_high_quality = _params._is_high_quailty;
        const f32 current_fov = camera.FovH();
        const f32 current_aspect = camera.Aspect();
        const f32 current_near = camera.Near();
        const f32 current_far = camera.Far();
        const u8 current_projection_type = static_cast<u8>(camera.Type());

        if (tile_render && can_reproject)
            _cloud_gen->EnableKeyword("_TILE_RENDER");
        else
            _cloud_gen->DisableKeyword("_TILE_RENDER");

        static const Vector2UInt kOffsetTable[16] = {
                Vector2UInt(0u, 0u),
                Vector2UInt(2u, 2u),
                Vector2UInt(2u, 0u),
                Vector2UInt(0u, 2u),
                Vector2UInt(1u, 1u),
                Vector2UInt(3u, 3u),
                Vector2UInt(3u, 1u),
                Vector2UInt(1u, 3u),
                Vector2UInt(1u, 0u),
                Vector2UInt(3u, 2u),
                Vector2UInt(3u, 0u),
                Vector2UInt(1u, 2u),
                Vector2UInt(0u, 1u),
                Vector2UInt(2u, 3u),
                Vector2UInt(2u, 1u),
                Vector2UInt(0u, 3u),
        };
        const auto cur_offset = kOffsetTable[g_pGfxContext->GetFrameCount() % 16];

        graph.AddPass("VolumetricClouds_Compute", RDG::PassDesc{RDG::EPassType::kCompute}, [&, this](RDG::RenderGraphBuilder &builder)
        {
            _cloud_cur_handle = builder.Import(_is_cur_a ? _cloud_rt_a.get() : _cloud_rt_b.get());
            _cloud_history_handle = builder.Import(_is_cur_a ? _cloud_rt_b.get() : _cloud_rt_a.get());
            builder.Read(depth_handle);
            builder.Read(sky_view_lut_handle);
            if (can_reproject)
                builder.Read(_cloud_history_handle);
            _cloud_cur_handle = builder.Write(_cloud_cur_handle, EResourceUsage::kWriteUAV);
            }, [this, cur_offset, tile_render, can_reproject, depth_handle, sky_view_lut_handle]
               (RDG::RenderGraph &graph, CommandBuffer *cmd,
                                                               const RenderingData &data)
        {
            auto *cur_rt = graph.Resolve<RenderTexture>(_cloud_cur_handle);
            auto *history_rt = graph.Resolve<RenderTexture>(_cloud_history_handle);
            auto *depth_tex = graph.Resolve<Texture>(depth_handle);
            auto *sky_view_lut = graph.Resolve<Texture>(sky_view_lut_handle);
            if (cur_rt == nullptr || history_rt == nullptr || depth_tex == nullptr || sky_view_lut == nullptr)
                return;

            auto rt_desc = data._camera_data._camera_color_target_desc;
            _cloud_gen->SetTexture(_cloud_main_kernel, "_ShapeNoise", _shape_noise.get());
            _cloud_gen->SetTexture(_cloud_main_kernel, "_NoiseTex", _blue_noise.get());
            _cloud_gen->SetTexture(_cloud_main_kernel, "_DetailNoise", _detail_noise.get());
            _cloud_gen->SetTexture(_cloud_main_kernel, "_WeatherMap", _weather_map.get());
            _cloud_gen->SetVector("_CloudTex_TexelSize", cur_rt->TexelSize());
            _cloud_gen->SetTexture(_cloud_main_kernel, "_CameraDepthTexture", depth_tex);
            _cloud_gen->SetTexture(_cloud_main_kernel, "_TexSkyViewLUT", sky_view_lut);
            const Vector2UInt offset = tile_render && can_reproject ? cur_offset : Vector2UInt(0u, 0u);
            _cloud_gen->SetVector("_pixel_offset", Vector4f((f32) offset.x, (f32) offset.y, 0.f, 0.f));
            _cloud_gen->SetTexture(_cloud_main_kernel, "_CloudTex", cur_rt);
            {
                auto [x, y, z] = _cloud_gen->CalculateDispatchNum(
                    _cloud_main_kernel, rt_desc._width >> (tile_render && can_reproject ? 2 : 0),
                    rt_desc._height >> (tile_render && can_reproject ? 2 : 0), 1);
                cmd->Dispatch(_cloud_gen.get(), _cloud_main_kernel, x, y, 1);
            }
            _history_write_executed = true;
        });
        if (tile_render && can_reproject)
        {
            graph.AddPass("VolumetricClouds_TileReproject", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(_cloud_cur_handle);
                builder.Read(_cloud_history_handle);
                builder.Read(motion_handle);
                _cloud_cur_handle = builder.Write(_cloud_cur_handle, EResourceUsage::kWriteUAV);
            }, [this, previous_uv_jitter, current_uv_jitter, motion_handle](RDG::RenderGraph &graph, CommandBuffer *cmd,
                                                             const RenderingData &data)
            {
                auto rt_desc = data._camera_data._camera_color_target_desc;
                auto *cloud_rt = graph.Resolve<RenderTexture>(_cloud_cur_handle);
                auto *history_rt = graph.Resolve<RenderTexture>(_cloud_history_handle);
                auto *motion_texture = graph.Resolve<Texture>(motion_handle);
                if (!_history_write_executed || cloud_rt == nullptr || history_rt == nullptr || motion_texture == nullptr)
                    return;
                _cloud_gen->SetTexture(_cloud_reprojection_kernel, "_CloudTex", cloud_rt);
                _cloud_gen->SetTexture(_cloud_reprojection_kernel, "_CloudHistoryTex", history_rt);
                _cloud_gen->SetTexture(_cloud_reprojection_kernel, "_MotionVectorTexture", motion_texture);
                _cloud_gen->SetVector("_JitterPrev", Vector4f(previous_uv_jitter.x, previous_uv_jitter.y, 0.0f, 0.0f));
                _cloud_gen->SetVector("_JitterCur", Vector4f(current_uv_jitter.x, current_uv_jitter.y, 0.0f, 0.0f));
                auto [x, y, z] = _cloud_gen->CalculateDispatchNum(_cloud_reprojection_kernel, rt_desc._width, rt_desc._height, 1);
                cmd->Dispatch(_cloud_gen.get(), _cloud_reprojection_kernel, x, y, 1);
                _reprojection_executed = true;
            });
        }


        graph.AddPass("VolumetricClouds_Compose", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(_cloud_cur_handle);
            if (rendering_data._rg_handles._pre_exposure_ev.IsValid())
                builder.Read(rendering_data._rg_handles._pre_exposure_ev, EResourceUsage::kReadSRV);
            builder.Read(rendering_data._rg_handles._color_target, EResourceUsage::kWriteRTV);
            rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
        }, [this, camera_hash, frame_index, current_uv_jitter, current_position, current_forward, current_up,
            current_height, current_thickness, current_density_multiplier, current_threshold, current_type_offset,
            current_speed, current_exposure, current_base_pos, current_absorption, current_scattering,
            current_high_quality, current_fov, current_aspect, current_near, current_far, current_projection_type,
            tile_render, can_reproject]
           (RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
        {
            auto *cur_rt = graph.Resolve<RenderTexture>(_cloud_cur_handle);
            if (cur_rt == nullptr || !_history_write_executed ||
                (tile_render && can_reproject && !_reprojection_executed))
            {
                _history_valid = false;
                return;
            }

            cmd->SetRenderTargetLoadAction(data._rg_handles._color_target, ELoadStoreAction::kLoad);
            cmd->SetRenderTarget(data._rg_handles._color_target);
            _global_cloud->SetTexture("_CloudTex", cur_rt);
            cmd->DrawFullScreenQuad(_global_cloud.get(), 1);
            if (_history_write_executed && (!tile_render || !can_reproject || _reprojection_executed))
            {
                _is_cur_a = !_is_cur_a;
                _history_valid = true;
                _history_camera_hash = camera_hash;
                _last_render_frame = frame_index;
                _previous_uv_jitter = current_uv_jitter;
                _previous_position = current_position;
                _previous_forward = current_forward;
                _previous_up = current_up;
                _previous_height = current_height;
                _previous_thickness = current_thickness;
                _previous_density_multiplier = current_density_multiplier;
                _previous_threshold = current_threshold;
                _previous_type_offset = current_type_offset;
                _previous_speed = current_speed;
                _previous_exposure = current_exposure;
                _previous_base_pos = current_base_pos;
                _previous_absorption = current_absorption;
                _previous_scattering = current_scattering;
                _previous_tile_render = tile_render;
                _previous_high_quality = current_high_quality;
                _previous_fov = current_fov;
                _previous_aspect = current_aspect;
                _previous_near = current_near;
                _previous_far = current_far;
                _previous_projection_type = current_projection_type;
                _has_rendered = true;
                _rg_commit_done = true;
            }
        });
    }
    void VolumetricCloudsPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        _rg_commit_done = false;
        EnsureRenderTargets(rendering_data);
        //https://www.diva-portal.org/smash/get/diva2:1223894/FULLTEXT01.pdf  Real-time rendering of volumetric clouds 3.2.1.4
        static const Vector2UInt kOffsetTable[16] = {
                Vector2UInt(0u, 0u),
                Vector2UInt(2u, 2u),
                Vector2UInt(2u, 0u),
                Vector2UInt(0u, 2u),
                Vector2UInt(1u, 1u),
                Vector2UInt(3u, 3u),
                Vector2UInt(3u, 1u),
                Vector2UInt(1u, 3u),
                Vector2UInt(1u, 0u),
                Vector2UInt(3u, 2u),
                Vector2UInt(3u, 0u),
                Vector2UInt(1u, 2u),
                Vector2UInt(0u, 1u),
                Vector2UInt(2u, 3u),
                Vector2UInt(2u, 1u),
                Vector2UInt(0u, 3u),
        };
        UpdateShaderParams();
        
        if (_params._is_tile_render)
            _cloud_gen->EnableKeyword("_TILE_RENDER");
        else
            _cloud_gen->DisableKeyword("_TILE_RENDER");
        auto cmd = CommandBufferPool::Get(_name);
        {
            PROFILE_BLOCK_GPU(cmd.get(), "VolumetricCloudsPass");
            auto rt_desc = rendering_data._camera_data._camera_color_target_desc;
            auto cur_offset = kOffsetTable[g_pGfxContext->GetFrameCount() % 16];
            RenderTexture* cur_rt = _is_cur_a ? _cloud_rt_a.get() : _cloud_rt_b.get();
            RenderTexture* history_rt = _is_cur_a ? _cloud_rt_b.get() : _cloud_rt_a.get();
            _cloud_gen->SetTexture(_cloud_main_kernel, "_ShapeNoise", _shape_noise.get());
            _cloud_gen->SetTexture(_cloud_main_kernel, "_NoiseTex", _blue_noise.get());
            _cloud_gen->SetTexture(_cloud_main_kernel, "_DetailNoise", _detail_noise.get());
            _cloud_gen->SetTexture(_cloud_main_kernel, "_WeatherMap", _weather_map.get());
            _cloud_gen->SetVector("_CloudTex_TexelSize", cur_rt->TexelSize());
            _cloud_gen->SetTexture(_cloud_main_kernel, "_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
            _cloud_gen->SetVector("_pixel_offset", Vector4f((f32) cur_offset.x, (f32) cur_offset.y, 0.f, 0.f));
            _cloud_gen->SetTexture(_cloud_main_kernel, "_CloudTex", cur_rt);
            {
                auto [x, y, z] = _cloud_gen->CalculateDispatchNum(_cloud_main_kernel, rt_desc._width >> (_params._is_tile_render? 2 : 0), rt_desc._height >> (_params._is_tile_render? 2 : 0), 1);
                cmd->Dispatch(_cloud_gen.get(), _cloud_main_kernel, x, y, 1);
            }
            if (_params._is_tile_render)
            {
                //re-projection
                {
                    _cloud_gen->SetTexture(_cloud_reprojection_kernel, "_CloudTex", cur_rt);
                    _cloud_gen->SetTexture(_cloud_reprojection_kernel, "_CloudHistoryTex", history_rt);
                    auto [x, y, z] = _cloud_gen->CalculateDispatchNum(_cloud_reprojection_kernel, rt_desc._width, rt_desc._height, 1);
                    cmd->Dispatch(_cloud_gen.get(), _cloud_reprojection_kernel, x, y, 1);
                }
            }
            cmd->SetRenderTargetLoadAction(rendering_data._camera_color_target_handle,ELoadStoreAction::kNotCare);
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
            _global_cloud->SetTexture("_CloudTex", cur_rt);
            cmd->DrawFullScreenQuad(_global_cloud.get(), 1);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void VolumetricCloudsPass::BeginPass(GraphicsContext *context)
    {
    }
    void VolumetricCloudsPass::EndPass(GraphicsContext *context)
    {
        if (!_rg_commit_done)
            _is_cur_a = !_is_cur_a;
        _rg_commit_done = false;
    }
    void VolumetricCloudsPass::Setup(f32 speed)
    {
        //_cloud_mat->SetFloat("_Speed", speed);
    }
    void VolumetricCloudsPass::EnsureRenderTargets(const RenderingData &rendering_data)
    {
        if (_cloud_rt_a == nullptr || _cloud_rt_b == nullptr || rendering_data._is_res_changed)
        {
            _cloud_rt_a = RenderTexture::Create(rendering_data._width, rendering_data._height, "CloudTexA", ERenderTargetFormat::kRGBAHalf, false, false, true);
            _cloud_rt_b = RenderTexture::Create(rendering_data._width, rendering_data._height, "CloudTexB", ERenderTargetFormat::kRGBAHalf, false, false, true);
        }
    }
    void VolumetricCloudsPass::UpdateShaderParams()
    {
        // _global_cloud->SetVector("_Params", _params._params);
        // _global_cloud->SetVector("_Absorption", _params._absorption);
        // _global_cloud->SetVector("_Scattering", _params._scattering);
        // _global_cloud->SetFloat("_Exposure", _params._exposure);
        // _global_cloud->SetFloat("_DensityMultply", _params._density_multply);
        // _global_cloud->SetFloat("_Threshold", _params._threshold);
        if (_params._is_high_quailty)
            _cloud_gen->EnableKeyword("_QUALITY_HIGH");
        else
            _cloud_gen->EnableKeyword("_QUALITY_LOW");
        _cloud_gen->SetVector("_BaseWorldPos", _params._base_pos);
        _cloud_gen->SetVector("_Params", _params._params);
        _cloud_gen->SetVector("_Absorption", _params._absorption);
        _cloud_gen->SetVector("_Scattering", _params._scattering);
        _cloud_gen->SetFloat("_Exposure", _params._exposure);
        _cloud_gen->SetFloat("_DensityMultply", _params._density_multply);
        _cloud_gen->SetFloat("_Threshold", _params._threshold);
        _cloud_gen->SetFloat("_thickness", _params._thickness);
        _cloud_gen->SetFloat("_height", _params._height);
    }

#pragma endregion

#pragma region VolumetricClouds
    Ailu::Render::VolumetricClouds::VolumetricClouds() : RenderFeature("VolumetricClouds")
    {
    }

    Ailu::Render::VolumetricClouds::~VolumetricClouds()
    {
    }

    void Ailu::Render::VolumetricClouds::AddRenderPasses(Renderer &renderer, const RenderingData & rendering_data)
    {
        if (renderer.IsRenderingProbe())
            return;
        static const int kGridSize = 1000;
        //snap to grid
        Vector3Int pos;
        pos.x = (i32) rendering_data._camera->Position().x;
        pos.y = (i32) rendering_data._camera->Position().y;
        pos.z = (i32) rendering_data._camera->Position().z;
        pos = (pos / kGridSize) * kGridSize + kGridSize / 2;
        pos.y = 0;
        Vector3f fpos = {(f32) pos.x, (f32) pos.y, (f32) pos.z};
        _pass.Setup(_speed);
        _pass._params._base_pos = fpos;
        _pass._params._absorption = _absorbtion.xyz;
        _pass._params._density_multply = _density_multiplier * _density_multiplier;
        _pass._params._exposure = _exposure;
        _pass._params._params.w = _type_offset;
        _pass._params._params.z = _speed;
        _pass._params._scattering = _scattering.xyz;
        _pass._params._threshold = _threshold;
        _pass._params._is_tile_render = _is_tile_render;
        _pass._params._is_high_quailty = _is_high_quality;
        _pass._params._height = _height;
        _pass._params._thickness = _thickness;
        renderer.EnqueuePass(&_pass);
    }
#pragma endregion
}// namespace Ailu
