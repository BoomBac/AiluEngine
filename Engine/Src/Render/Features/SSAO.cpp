#include "Render/Features/SSAO.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/Renderer.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "pch.h"

namespace Ailu::Render
{
    #pragma region SSAO
    SSAO::SSAO() : RenderFeature("SSAO")
    {
    }

    SSAO::~SSAO()
    {
    }

    void Ailu::Render::SSAO::AddRenderPasses(Renderer &renderer, const RenderingData & rendering_data)
    {
        _pass._is_half_res = _is_half_res;
        _pass._is_cbr = _is_cbr;
        renderer.EnqueuePass(&_pass);
    }
#pragma endregion

    #pragma region SSAOPass
    SSAOPass::SSAOPass() : RenderPass("SSAOPass")
    {
        _ssao_computer = g_pResourceMgr->GetRef<ComputeShader>(L"Shaders/ssao_cs.alasset");
        _ssao_gen = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/ssao.hlsl"), "Runtime/SSAOGen");
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }
    SSAOPass::~SSAOPass()
    {
    }
    void SSAOPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        static Vector4f params;
        if (_is_half_res)
            params = {1.0f, 1.0f, (f32) (rendering_data._width >> 1), (f32) (rendering_data._height >> 1)};
        else
            params = {1.0f, 1.0f, (f32) (rendering_data._width), (f32) (rendering_data._height)};
        params.x /= params.z;
        params.y /= params.w;
        u16 w = (u16) params.z, h = (u16) params.w;
        TextureDesc desc(w, h, ERenderTargetFormat::kRFloat);
        desc._is_random_access = true;
        desc._mip_num = 1u;
        static RDG::RGHandle ao_result,blur_temp;
        graph.AddPass("SSAO Compute", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                {
                    ao_result = builder.AllocTexture(desc, "_OcclusionTex");
                    builder.Read(rendering_data._rg_handles._depth_tex);
                    builder.Read(rendering_data._rg_handles._gbuffers[0]);
                    ao_result = builder.Write(ao_result,EResourceUsage::kWriteUAV); 
            }, 
            [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                {
                Vector4f params;
                    if (_is_half_res)
                        params = {1.0f, 1.0f, (f32) (data._width >> 1), (f32) (data._height >> 1)};
                    else
                        params = {1.0f, 1.0f, (f32) (data._width), (f32) (data._height)};
                    params.x /= params.z;
                    params.y /= params.w;
                    u16 w = (u16) params.z, h = (u16) params.w;
                _ssao_computer->SetVector("_AOScreenParams", params);
                _ssao_computer->SetVector("_HBAOParams", _ao_params);
                _ssao_computer->SetFloat("_KernelSize", 2.5f);
                _ssao_computer->SetFloat("_Space_Sigma", 10.1f);
                _ssao_computer->SetFloat("_Range_Sigma", 0.31f);
                _ssao_computer->SetTexture("_CameraNormalsTexture", graph.Resolve<Texture>(data._rg_handles._gbuffers[0]));
                _ssao_computer->SetTexture("_CameraDepthTexture", graph.Resolve<Texture>(data._rg_handles._depth_tex));
                _ssao_computer->SetTexture("_AOResult", graph.Resolve<Texture>(graph.GetTexture("_OcclusionTex")));
                {
                    auto kernel = _ssao_computer->FindKernel("SSAOGen");
                    auto [x,y,z] = _ssao_computer->CalculateDispatchNum(kernel,_is_cbr? w >> 1 : w,_is_cbr? h >> 1 : h,1);
                    cmd->Dispatch(_ssao_computer.get(), kernel, x, y, 1);
                }
            });
        graph.AddPass("SSAO Blur", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                    {
                        blur_temp = builder.AllocTexture(desc, "AO_BlurTemp");
                        builder.Read(ao_result);
                        blur_temp = builder.Write(blur_temp); 
                    }, [&, this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                      { 
                _ssao_computer->SetVector("_AOScreenParams", params);
                _ssao_computer->SetVector("_HBAOParams", _ao_params);
                _ssao_computer->SetFloat("_KernelSize", 2.5f);
                _ssao_computer->SetFloat("_Space_Sigma", 10.1f);
                _ssao_computer->SetFloat("_Range_Sigma", 0.31f);
                {
                    auto kernel = _ssao_computer->FindKernel("SSAOBlurX");
                    auto [x,y,z] = _ssao_computer->CalculateDispatchNum(kernel, w, h, 1);
                    _ssao_computer->SetTexture("_SourceTex", graph.Resolve<Texture>(graph.GetTexture("_OcclusionTex")));
                    _ssao_computer->SetTexture("_DenoiseResult", graph.Resolve<Texture>(graph.GetTexture("AO_BlurTemp")));
                    cmd->Dispatch(_ssao_computer.get(), kernel, x, y, 1);
                }
            });
        graph.AddPass("SSAO Final", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                      {
                    builder.Read(blur_temp);
                    ao_result = builder.Write(ao_result);
                     }, [&, this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                      { 
                _ssao_computer->SetVector("_AOScreenParams", params);
                _ssao_computer->SetVector("_HBAOParams", _ao_params);
                _ssao_computer->SetFloat("_KernelSize", 2.5f);
                _ssao_computer->SetFloat("_Space_Sigma", 10.1f);
                _ssao_computer->SetFloat("_Range_Sigma", 0.31f);
                {
                    auto kernel = _ssao_computer->FindKernel("SSAOBlurY");
                    auto [x,y,z] = _ssao_computer->CalculateDispatchNum(kernel, w, h, 1);
                    _ssao_computer->SetTexture("_SourceTex", graph.Resolve<Texture>(graph.GetTexture("AO_BlurTemp")));
                    _ssao_computer->SetTexture("_DenoiseResult", graph.Resolve<Texture>(graph.GetTexture("_OcclusionTex")));
                    cmd->Dispatch(_ssao_computer.get(), kernel, x, y, 1);
                }
                Shader::SetGlobalTexture("_OcclusionTex", graph.Resolve<Texture>(graph.GetTexture("_OcclusionTex"))); });
        if (_is_debug_mode)
        {
            graph.AddPass("SSAO Debug", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                          {
                    auto handle = builder.GetTexture("_OcclusionTex");
                    builder.Read(handle);
                    rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                }, [&, this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                          { 
                        cmd->Blit(graph.GetTexture("_OcclusionTex"), data._rg_handles._color_target);
                    });
        }
    }
    void SSAOPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("SSAO");
        static bool is_compute = true;
        cmd->Clear();
        {
            PROFILE_BLOCK_GPU(cmd.get(), SSAOCompute);
            Vector4f params;
            if (_is_half_res)
                params = {1.0f, 1.0f,(f32) (rendering_data._width >> 1), (f32) (rendering_data._height >> 1)};
            else
                params = {1.0f, 1.0f,(f32) (rendering_data._width), (f32) (rendering_data._height)};
            params.x /= params.z;
            params.y /= params.w;
            u16 w = (u16)params.z, h = (u16)params.w;
            auto ao_result = RenderTexture::GetTempRT(w, h, "AO_Result", ERenderTargetFormat::kRFloat, false, false, true);
            auto blur_temp = RenderTexture::GetTempRT(w, h, "BlurTemp", ERenderTargetFormat::kRFloat, false, false, true);
            if (is_compute)
            {
                //RenderTexture* cur_ao = _is_cur_a? _ssao_result_a.get() : _ssao_result_b.get();
                //RenderTexture* history_ao = _is_cur_a? _ssao_result_b.get() : _ssao_result_a.get();
                _ssao_computer->SetVector("_AOScreenParams", params);
                _ssao_computer->SetVector("_HBAOParams", _ao_params);
                _ssao_computer->SetFloat("_KernelSize", 2.5f);
                _ssao_computer->SetFloat("_Space_Sigma", 10.1f);
                _ssao_computer->SetFloat("_Range_Sigma", 0.31f);
                _ssao_computer->SetTexture("_CameraNormalsTexture", rendering_data._gbuffers[0]);
                _ssao_computer->SetTexture("_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
                _ssao_computer->SetTexture("_AOResult", ao_result);
                {
                    auto kernel = _ssao_computer->FindKernel("SSAOGen");
                    auto [x,y,z] = _ssao_computer->CalculateDispatchNum(kernel,_is_cbr? w >> 1 : w,_is_cbr? h >> 1 : h,1);
                    cmd->Dispatch(_ssao_computer.get(), kernel, x, y, 1);
                }
                {
                    auto kernel = _ssao_computer->FindKernel("SSAOBlurX");
                    auto [x,y,z] = _ssao_computer->CalculateDispatchNum(kernel, w, h, 1);
                    _ssao_computer->SetTexture("_SourceTex", ao_result);
                    _ssao_computer->SetTexture("_DenoiseResult", blur_temp);
                    cmd->Dispatch(_ssao_computer.get(), kernel, x, y, 1);
                }
                {
                    auto kernel = _ssao_computer->FindKernel("SSAOBlurY");
                    auto [x,y,z] = _ssao_computer->CalculateDispatchNum(kernel, w, h, 1);
                    _ssao_computer->SetTexture("_SourceTex", blur_temp);
                    _ssao_computer->SetTexture("_DenoiseResult", ao_result);
                    cmd->Dispatch(_ssao_computer.get(), kernel, x, y, 1);
                }
                Shader::SetGlobalTexture("_OcclusionTex", ao_result);
                if (_is_debug_mode)
                    cmd->Blit(ao_result, rendering_data._camera_color_target_handle);
            }
            else
            {
                auto ao_result = RenderTexture::GetTempRT(w, h, "AO_Result", ERenderTargetFormat::kRFloat, false, false, true);
                _ssao_gen->SetVector("_AOScreenParams", params);
                params = Vector4f{1.5f, 29.98f, 42.7f, 0.05f};
                _ssao_gen->SetVector("_HBAOParams", _ao_params);
                _ssao_gen->SetFloat("_KernelSize", 2.5f);
                _ssao_gen->SetFloat("_Space_Sigma", 10.1f);
                _ssao_gen->SetFloat("_Range_Sigma", 0.31f);
                _ssao_gen->SetTexture("_CameraNormalsTexture", rendering_data._gbuffers[0]);
                _ssao_gen->SetTexture("_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
                cmd->Blit(rendering_data._camera_opaque_tex_handle, ao_result, _ssao_gen.get());
                cmd->Blit(ao_result, blur_temp, _ssao_gen.get(), 1);
                cmd->Blit(blur_temp, ao_result, _ssao_gen.get(), 2);
                RenderTexture::ReleaseTempRT(ao_result);
                if (_is_debug_mode)
                    cmd->Blit(ao_result, rendering_data._camera_color_target_handle);
                f32 aspect = (f32)rendering_data._width / (f32)rendering_data._height;
                //Gizmo::DrawTexture(Rect(0,0,256,256 / aspect),g_pRenderTexturePool->Get(ao_result));
                Shader::SetGlobalTexture("_OcclusionTex", ao_result);
            }
            RenderTexture::ReleaseTempRT(blur_temp);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void SSAOPass::BeginPass(GraphicsContext *context)
    {
        if (_is_cbr)
            _ssao_computer->EnableKeyword("CBR");
        else
            _ssao_computer->DisableKeyword("CBR");
    }
    void SSAOPass::EndPass(GraphicsContext *context)
    {
    }
    #pragma endregion
}// namespace Ailu
