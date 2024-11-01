#include "Render/Pass/PostprocessPass.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "pch.h"

namespace Ailu
{
    PostProcessPass::PostProcessPass() : RenderPass("PostProcessPass")
    {
        _p_bloom_thread_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/bloom.alasset"), "BloomThread");
        _p_blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
        _p_obj_cb = IConstantBuffer::Create(256);
        memcpy(_p_obj_cb->GetData(), &BuildIdentityMatrix(), sizeof(Matrix4x4f));
        _bloom_thread_rect = Rect(0, 0, 800, 450);
        _p_quad_mesh = g_pResourceMgr->Get<Mesh>(L"Runtime/Mesh/FullScreenQuad");
        for (u16 i = 0; i < _bloom_iterator_count; i++)
        {
            _bloom_mats.emplace_back(MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/bloom.alasset"), std::format("bloom_mip_{}", i)));
        }
        _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kBeforePostprocess + 25);
        _nose_tex = g_pResourceMgr->Get<Texture2D>(L"Textures/noise_medium.png");
        _noise_texel_size = {(f32) _nose_tex->Width(), (f32) _nose_tex->Height(), 0.0f, 0.0f};
        _noise_texel_size.z = 1.0f / _noise_texel_size.x;
        _noise_texel_size.w = 1.0f / _noise_texel_size.y;
    }
    PostProcessPass::~PostProcessPass()
    {
    }
    void PostProcessPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        _bloom_thread_rect.width = rendering_data._width >> 1;
        _bloom_thread_rect.height = rendering_data._height >> 1;
        auto cmd = CommandBufferPool::Get("PostProcessPass");
        RTHandle rt, blur_x, blur_y;

        u16 iterator_count = std::min<u16>(Texture::MaxMipmapCount(rendering_data._width, rendering_data._height), _bloom_iterator_count);
        Vector<RTHandle> bloom_mips;
        for (u16 i = 1; i <= iterator_count; i++)
        {
            u16 cur_mip_width = rendering_data._width >> i;
            u16 cur_mip_height = rendering_data._height >> i;
            bloom_mips.emplace_back(RenderTexture::GetTempRT(cur_mip_width, cur_mip_height, std::format("bloom_mip_{}", i), ERenderTargetFormat::kDefaultHDR));
        }
        cmd->Clear();
        {
            ProfileBlock p(cmd.get(), _name);
            cmd->SetName("Bloom");
            //down sample
            for (u16 i = 0; i < iterator_count; i++)
            {
                cmd->SetRenderTarget(bloom_mips[i]);
                f32 blur_radius = _upsample_radius / (f32) i;
                Vector4f v{1.0f / (f32) (rendering_data._width >> i), 1.0f / (f32) (rendering_data._height >> i), blur_radius, _bloom_intensity};
                _bloom_mats[i]->SetVector("_SampleParams", v);
                if (i == 0)
                {
                    _bloom_mats[i]->SetTexture("_SourceTex", rendering_data._camera_color_target_handle);
                }
                else
                {
                    _bloom_mats[i]->SetTexture("_SourceTex", bloom_mips[i - 1]);
                }
                cmd->DrawFullScreenQuad(_bloom_mats[i].get(), 1);
            }
            //up sample
            for (u16 i = bloom_mips.size() - 1; i > 0; i--)
            {
                auto cur_mip = bloom_mips[i], next_mip = bloom_mips[i - 1];
                cmd->SetRenderTarget(next_mip);
                _bloom_mats[i]->SetTexture("_SourceTex", cur_mip);
                cmd->DrawFullScreenQuad(_bloom_mats[i].get(), 2);
            }
            //合成
            {
                cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
                Vector4f light_pos = -rendering_data._mainlight_world_position * 10000;
                light_pos.w = 1.0f;
                Matrix4x4f vp = rendering_data._camera->GetView() * rendering_data._camera->GetProjection();
                TransformVector(light_pos, vp);
                light_pos.xy /= light_pos.w;
                light_pos.w = 1.0f;
                light_pos.xy = light_pos.xy * 0.5f + 0.5f;
                light_pos.y = 1.0 - light_pos.y;
                //LOG_INFO("light_pos {}", light_pos.ToString());
                _bloom_mats[0]->SetVector("_SunScreenPos", light_pos);

                _bloom_mats[0]->SetVector("_NoiseTex_TexelSize", _noise_texel_size);
                _bloom_mats[0]->SetTexture("_SourceTex", rendering_data._camera_opaque_tex_handle);
                _bloom_mats[0]->SetTexture("_BloomTex", bloom_mips[0]);
                _bloom_mats[0]->SetTexture("_NoiseTex", _nose_tex);
                cmd->DrawFullScreenQuad(_bloom_mats[0].get(), 3);
                //cmd->DrawRenderer(_p_quad_mesh, _bloom_mats[0].get(), 1, 3);
            }
        }
        context->ExecuteCommandBuffer(cmd);
        for (auto &handle: bloom_mips)
        {
            RenderTexture::ReleaseTempRT(handle);
        }
        CommandBufferPool::Release(cmd);
    }
    void PostProcessPass::BeginPass(GraphicsContext *context)
    {
    }
    void PostProcessPass::EndPass(GraphicsContext *context)
    {
        Shader::SetGlobalTexture("_SourceTex", nullptr);
    }
}// namespace Ailu
