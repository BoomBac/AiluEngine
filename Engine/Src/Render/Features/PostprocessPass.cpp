#include "Render/Features/PostprocessPass.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "pch.h"

namespace Ailu::Render
{
    PostProcessPass::PostProcessPass() : RenderPass("PostProcessPass")
    {
        _cs_blur = ComputeShader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/Compute/blur.hlsl"));
        _p_bloom_thread_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/bloom.alasset"), "BloomThread");
        _p_blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
        _p_obj_cb = ConstantBuffer::Create(256);
        memcpy(_p_obj_cb->GetData(), &BuildIdentityMatrix(), sizeof(Matrix4x4f));
        _bloom_thread_rect = Rect(0, 0, 800, 450);
        _p_quad_mesh = g_pResourceMgr->Get<Mesh>(L"Runtime/Mesh/FullScreenQuad");
        for (u16 i = 0; i < _bloom_iterator_count; i++)
        {
            _bloom_mats.emplace_back(MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/bloom.alasset"), std::format("bloom_mip_{}", i)));
        }
        _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kBeforePostprocess + 25);
        _nose_tex = g_pResourceMgr->Get<Texture2D>(L"Textures/noise_medium.png");
        _noise_texel_size = {0.0f, 0.0f,(f32) _nose_tex->Width(), (f32) _nose_tex->Height()};
        _noise_texel_size.x = 1.0f / _noise_texel_size.z;
        _noise_texel_size.y = 1.0f / _noise_texel_size.w;
    }
    PostProcessPass::~PostProcessPass()
    {
    }

    void Ailu::Render::PostProcessPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData & rendering_data)
    {
        RenderTexture *scene_color = rendering_data._postprocess_input ? rendering_data._postprocess_input : g_pRenderTexturePool->Get(rendering_data._camera_color_target_handle);
        u16 iterator_count = std::min<u16>(Texture::MaxMipmapCount(rendering_data._width, rendering_data._height), _bloom_iterator_count);
        Vector<RDG::RGHandle> bloom_mips;
        for (u16 i = 1; i <= iterator_count; i++)
        {
            u16 cur_mip_width = rendering_data._width >> i;
            u16 cur_mip_height = rendering_data._height >> i;
            TextureDesc desc;
            desc._width = cur_mip_width;
            desc._height = cur_mip_height;
            desc._format = ConvertRenderTextureFormatToPixelFormat(ERenderTargetFormat::kDefaultHDR);
            desc._is_color_target = true;
            desc._load = ELoadStoreAction::kNotCare;
            bloom_mips.emplace_back(graph.GetOrCreate(desc, std::format("BloomMip_{}", i - 1)));
        }
        //down sample
        for (u16 i = 0; i < iterator_count; i++)
        {
            auto& input = i == 0 ? rendering_data._rg_handles._color_target : bloom_mips[i - 1];
            auto& output = bloom_mips[i];
            graph.AddPass(std::format("Downsample_{}",i), RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
            { 
                builder.Read(input);
                builder.Write(output);
            }, [=,this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            { 
                cmd->SetRenderTarget(output);
                f32 blur_radius = _upsample_radius / (f32) i;
                Vector4f v{1.0f / (f32) (rendering_data._width >> i), 1.0f / (f32) (rendering_data._height >> i), blur_radius, _bloom_intensity};
                _bloom_mats[i]->SetVector("_SampleParams", v);
                _bloom_mats[i]->SetTexture("_SourceTex", graph.Resolve<Texture>(input));
                cmd->DrawFullScreenQuad(_bloom_mats[i].get(), 1);
            });
        }
        //up sample
        for (u16 i = (u16) bloom_mips.size() - 1; i > 0; i--)
        {
            auto &cur_mip = bloom_mips[i];
            auto &next_mip = bloom_mips[i - 1];
            graph.AddPass(std::format("Upsample_{}", i), RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
            { 
                builder.Read(cur_mip);
                builder.Write(next_mip); 
            }, [=, this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            { 
                cmd->SetRenderTarget(next_mip);
                _bloom_mats[i]->SetTexture("_SourceTex", graph.Resolve<Texture>(cur_mip));
                cmd->DrawFullScreenQuad(_bloom_mats[i].get(), 2); 
            });
        }
        //TODO:TAA input
        auto& final_input = rendering_data._rg_handles._color_tex;
        graph.AddPass("Compose", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
        { 
            builder.Read(final_input);
            builder.Read(bloom_mips[0]);
            builder.Write(rendering_data._rg_handles._color_target); 
        }, [=, this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
        { 
            cmd->SetRenderTarget(data._rg_handles._color_target);
            //Vector4f light_pos = -rendering_data._mainlight_world_position * 10000;
            //light_pos.w = 1.0f;
            //Matrix4x4f vp = rendering_data._camera->GetView() * rendering_data._camera->GetProj();
            //TransformVector(light_pos, vp);
            //light_pos.xy /= light_pos.w;
            //light_pos.w = 1.0f;
            //light_pos.xy = light_pos.xy * 0.5f + 0.5f;
            //light_pos.y = 1.0f - light_pos.y;
            ////LOG_INFO("light_pos {}", light_pos.ToString());
            //_bloom_mats[0]->SetVector("_SunScreenPos", light_pos);

            //_bloom_mats[0]->SetVector("_NoiseTex_TexelSize", _noise_texel_size);
            //_bloom_mats[0]->SetTexture("_NoiseTex", _nose_tex);
            _bloom_mats[0]->SetTexture("_SourceTex", graph.Resolve<Texture>(final_input));
            _bloom_mats[0]->SetTexture("_BloomTex", graph.Resolve<Texture>(bloom_mips[0]));
            cmd->DrawFullScreenQuad(_bloom_mats[0].get(), 3);
        });
    }
    void PostProcessPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        _bloom_thread_rect.width = rendering_data._width >> 1;
        _bloom_thread_rect.height = rendering_data._height >> 1;
        auto cmd = CommandBufferPool::Get("PostProcess");
        RTHandle rt, blur_x, blur_y;

        u16 iterator_count = std::min<u16>(Texture::MaxMipmapCount(rendering_data._width, rendering_data._height), _bloom_iterator_count);
        Vector<RTHandle> bloom_mips;
        for (u16 i = 1; i <= iterator_count; i++)
        {
            u16 cur_mip_width = rendering_data._width >> i;
            u16 cur_mip_height = rendering_data._height >> i;
            TextureDesc desc;
            desc._width = cur_mip_width;
            desc._height = cur_mip_height;
            desc._format = ConvertRenderTextureFormatToPixelFormat(ERenderTargetFormat::kDefaultHDR);
            desc._is_color_target = true;
            desc._load = ELoadStoreAction::kNotCare;
            bloom_mips.emplace_back(RenderTexture::GetTempRT(desc, std::format("bloom_mip_{}", i)));
        }
        cmd->Clear();
        RenderTexture* scene_color = rendering_data._postprocess_input? rendering_data._postprocess_input : 
            g_pRenderTexturePool->Get(rendering_data._camera_color_target_handle);
        {
            GpuProfileBlock p(cmd.get(), cmd->Name());
            if (_is_use_blur)
            {
                auto blur_x = cmd->GetTempRT(rendering_data._width, rendering_data._height, "blur_x", ERenderTargetFormat::kDefault, false, false, true);
                auto blur_y = cmd->GetTempRT(rendering_data._width, rendering_data._height, "blur_y", ERenderTargetFormat::kDefault, false, false, true);
                _cs_blur->SetTexture("_SourceTex", scene_color);
                _cs_blur->SetTexture("_OutTex", blur_x);
                auto kernel = _cs_blur->FindKernel("blur_x");
                auto [group_num_x,group_num_y,group_num_z] = _cs_blur->CalculateDispatchNum(kernel,rendering_data._width,rendering_data._height,1u);
                cmd->Dispatch(_cs_blur.get(), kernel, group_num_x, group_num_y, 1);
                _cs_blur->SetTexture("_SourceTex", blur_x);
                _cs_blur->SetTexture("_OutTex", blur_y);
                kernel = _cs_blur->FindKernel("blur_y");
                cmd->Dispatch(_cs_blur.get(), kernel, group_num_x, group_num_y, 1);
                cmd->Blit(blur_y, rendering_data._camera_opaque_tex_handle);
                cmd->ReleaseTempRT(blur_x);
                cmd->ReleaseTempRT(blur_y);
            }
            //down sample
            for (u16 i = 0; i < iterator_count; i++)
            {
                cmd->SetRenderTarget(bloom_mips[i]);
                f32 blur_radius = _upsample_radius / (f32) i;
                Vector4f v{1.0f / (f32) (rendering_data._width >> i), 1.0f / (f32) (rendering_data._height >> i), blur_radius, _bloom_intensity};
                _bloom_mats[i]->SetVector("_SampleParams", v);
                if (i == 0)
                {
                    _bloom_mats[i]->SetTexture("_SourceTex", scene_color);
                }
                else
                {
                    _bloom_mats[i]->SetTexture("_SourceTex", bloom_mips[i - 1]);
                }
                cmd->DrawFullScreenQuad(_bloom_mats[i].get(), 1);
            }
            //up sample
            for (u16 i = (u16)bloom_mips.size() - 1; i > 0; i--)
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
                Matrix4x4f vp = rendering_data._camera->GetView() * rendering_data._camera->GetProj();
                TransformVector(light_pos, vp);
                light_pos.xy /= light_pos.w;
                light_pos.w = 1.0f;
                light_pos.xy = light_pos.xy * 0.5f + 0.5f;
                light_pos.y = 1.0f - light_pos.y;
                //LOG_INFO("light_pos {}", light_pos.ToString());
                _bloom_mats[0]->SetVector("_SunScreenPos", light_pos);

                _bloom_mats[0]->SetVector("_NoiseTex_TexelSize", _noise_texel_size);
                if (rendering_data._postprocess_input)
                    _bloom_mats[0]->SetTexture("_SourceTex", rendering_data._postprocess_input);
                else
                    _bloom_mats[0]->SetTexture("_SourceTex", rendering_data._camera_opaque_tex_handle);
                _bloom_mats[0]->SetTexture("_BloomTex", bloom_mips[0]);
                _bloom_mats[0]->SetTexture("_NoiseTex", _nose_tex);
                cmd->DrawFullScreenQuad(_bloom_mats[0].get(), 3);
                //cmd->DrawMesh(_p_quad_mesh, _bloom_mats[0].get(), 1, 3);
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
