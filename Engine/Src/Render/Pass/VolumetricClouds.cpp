#include "Render/Pass/VolumetricClouds.h"
#include "Inc/Framework/Common/Application.h"
#include "Inc/Framework/Common/JobSystem.h"
#include "Inc/Framework/Common/Profiler.h"
#include "Inc/Framework/Common/ResourceMgr.h"
#include "Inc/Framework/Math/Random.hpp"
#include "Inc/Render/Gizmo.h"
#include "Inc/Render/Renderer.h"
#include "Render/CommandBuffer.h"
#include "pch.h"

#pragma region NoiseUtils
namespace
{
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
namespace Ailu
{
#pragma region VolumetricCloudsPass
    VolumetricCloudsPass::VolumetricCloudsPass() : RenderPass("VolumetricCloudsPass")
    {
        //r覆盖率,g降雨概率,b类型(0.0层云，0.5积云，1.0积雨云)
        TextureImportSetting setting;
        setting._generate_mipmap = true;
        _weather_map = g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineTexturePathW + L"weather_map.alasset", &setting);
        auto cloud_shader = g_pResourceMgr->Load<Shader>(L"Shaders/global_volumetric_cloud.alasset");
        _global_cloud = MakeRef<Material>(cloud_shader.get(), "Runtime/VolumetricClouds");
        _event = ERenderPassEvent::kAfterSkybox;
        _noise_gen = g_pResourceMgr->Load<ComputeShader>(L"Shaders/volumetric_noise_generator.alasset");
        _cloud_gen = g_pResourceMgr->Load<ComputeShader>(L"Shaders/volumetric_cloud.alasset");
        _cloud_gen->EnableKeyword("_QUALITY_HIGH");
        u16 w = 128, h = w, d = w;
        Texture3DInitializer desc;
        desc._width = w;
        desc._height = h;
        desc._depth = d;
        desc._is_mipmap_chain = true;
        desc._format = ETextureFormat::kRGBA32;
        desc._is_readble = true;
        desc._is_random_access = true;
        _shape_noise = Texture3D::Create(desc);
        _shape_noise->Name("VolumetricCloudsShapeNoise");
        _shape_noise->Apply();
        _noise_gen->SetVector("_Size", Vector4f((f32) w, (f32) w, (f32) w, 0.f));
        _noise_gen->SetTexture("_OutNoise3D", _shape_noise.get());
        u16 thread_size_x, thread_size_y, thread_size_z;
        auto shape_kernel = _noise_gen->FindKernel("ShapeNoiseMain");
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
        _noise_gen->SetVector("_Size", Vector4f((f32) w, (f32) w, (f32) w, 0.f));
        _noise_gen->SetTexture("_OutNoise3D", _detail_noise.get());
        shape_kernel = _noise_gen->FindKernel("DetailNoiseMain");
        cmd->Dispatch(_noise_gen.get(), shape_kernel, w / thread_size_x, h / thread_size_y, d / thread_size_z);
        Texture2DInitializer curl_desc;
        curl_desc._width = 128;
        curl_desc._height = 128;
        curl_desc._is_mipmap_chain = false;
        curl_desc._format = ETextureFormat::kRGBA32;
        curl_desc._is_readble = true;
        curl_desc._is_random_access = true;
        _curl_noise = Texture2D::Create(curl_desc);
        _curl_noise->Apply();
        _curl_noise->Name("CurlNoise");
        _noise_gen->SetVector("_Size", Vector4f(128.0f, 128.0f, 1.0f / 128.0f, 1.0f / 128.0f));
        _noise_gen->SetTexture("_CurlOut", _curl_noise.get());
        auto curl_kernel = _noise_gen->FindKernel("CurlNoiseMain");
        _noise_gen->GetThreadNum(curl_kernel, thread_size_x, thread_size_y, thread_size_z);
        cmd->Dispatch(_noise_gen.get(), curl_kernel, 128 / thread_size_x, 128 / thread_size_y, 1);
        g_pGfxContext->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        _shape_noise->GenerateMipmap();
        _detail_noise->GenerateMipmap();
        g_pResourceMgr->RegisterResource(L"Runtime/CloudTex", _shape_noise);
        _blue_noise = g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineTexturePathW + L"blue_noise.alasset");
        _is_cur_a = true;
    }
    VolumetricCloudsPass::~VolumetricCloudsPass()
    {
    }
    void VolumetricCloudsPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        if (rendering_data._is_res_changed)
        {
            _cloud_rt_a = RenderTexture::Create(rendering_data._width, rendering_data._height, "CloudTexA", ERenderTargetFormat::kRGBAHalf, false, false, true);
            _cloud_rt_b = RenderTexture::Create(rendering_data._width, rendering_data._height, "CloudTexB", ERenderTargetFormat::kRGBAHalf, false, false, true);
        }
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
            PROFILE_BLOCK_GPU(cmd.get(), VolumetricCloudsPass);
            auto rt_desc = rendering_data._camera_data._camera_color_target_desc;
            auto cur_offset = kOffsetTable[g_pGfxContext->GetFrameCount() % 16];
            RenderTexture* cur_rt = _is_cur_a ? _cloud_rt_a.get() : _cloud_rt_b.get();
            RenderTexture* history_rt = _is_cur_a ? _cloud_rt_b.get() : _cloud_rt_a.get();
            _cloud_gen->SetTexture("_ShapeNoise", _shape_noise.get());
            _cloud_gen->SetTexture("_NoiseTex", _blue_noise.get());
            _cloud_gen->SetTexture("_DetailNoise", _detail_noise.get());
            _cloud_gen->SetTexture("_WeatherMap", _weather_map.get());
            _cloud_gen->SetVector("_CloudTex_TexelSize", cur_rt->TexelSize());
            _cloud_gen->SetTexture("_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
            _cloud_gen->SetVector("_pixel_offset", Vector4f((f32) cur_offset.x, (f32) cur_offset.y, 0.f, 0.f));
            auto kernel = _cloud_gen->FindKernel("CloudMain");
            _cloud_gen->SetTexture("_CloudTex", cur_rt);
            {
                auto [x, y, z] = _cloud_gen->CalculateDispatchNum(kernel, rt_desc._width >> (_params._is_tile_render? 2 : 0), rt_desc._height >> (_params._is_tile_render? 2 : 0), 1);
                cmd->Dispatch(_cloud_gen.get(), kernel, x, y, 1);
            }
            if (_params._is_tile_render)
            {
                //re-projection
                {
                    _cloud_gen->SetTexture("_CloudHistoryTex", history_rt);
                    kernel = _cloud_gen->FindKernel("CloudReprojection");
                    auto [x, y, z] = _cloud_gen->CalculateDispatchNum(kernel, rt_desc._width, rt_desc._height, 1);
                    cmd->Dispatch(_cloud_gen.get(), kernel, x, y, 1);
                }
            }
            cmd->SetRenderTargetLoadAction(rendering_data._camera_color_target_handle,ELoadStoreAction::kNotCare);
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
            _global_cloud->SetTexture("_CloudTex", _cloud_rt_a.get());
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
        _is_cur_a = !_is_cur_a;
    }
    void VolumetricCloudsPass::Setup(f32 speed)
    {
        //_cloud_mat->SetFloat("_Speed", speed);
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
    VolumetricClouds::VolumetricClouds() : RenderFeature("VolumetricClouds")
    {
    }

    VolumetricClouds::~VolumetricClouds()
    {
    }

    void VolumetricClouds::AddRenderPasses(Renderer &renderer, RenderingData &rendering_data)
    {
        if (renderer._is_render_light_probe)
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
