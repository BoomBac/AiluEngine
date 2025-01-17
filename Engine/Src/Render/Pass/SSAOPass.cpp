#include "Render/Pass/SSAOPass.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "pch.h"

namespace Ailu
{
    SSAOPass::SSAOPass() : RenderPass("SSAOPass")
    {
        _ssao_computer = ComputeShader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/Compute/ssao.hlsl"));
        _ssao_gen = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/ssao.hlsl"), "Runtime/SSAOGen");
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }
    SSAOPass::~SSAOPass()
    {
    }
    void SSAOPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("SSAOCompute");
        cmd->Clear();
        {
            //Vector4f params = {(f32)rendering_data._width,(f32)rendering_data._height,1.0f,1.0f};
            //params.z /= params.x;
            //params.w /= params.y;
            //u16 group_num_x = std::ceilf(params.x / 16.0f), group_num_y = std::ceilf(params.y / 16.0f);
            //auto ao_result = RenderTexture::GetTempRT(params.x, params.y,"AO_Result", ERenderTargetFormat::kDefault,false,false,true);
            //_ssao_computer->SetVector("_AOScreenParams", params);
            //params = Vector4f{ 1.5f,29.98f,42.7f,0.05f };
            //_ssao_computer->SetVector("_HBAOParams", params);
            //_ssao_computer->SetTexture("_CameraNormalsTexture",rendering_data._gbuffers[0]);
            //_ssao_computer->SetTexture("_CameraDepthTexture",rendering_data._camera_depth_tex_handle);
            //_ssao_computer->SetTexture("_AO_Result", ao_result);
            //_ssao_computer->SetBuffer(RenderConstants::kCBufNamePerScene,rendering_data._p_per_scene_cbuf);
            //cmd->Dispatch(_ssao_computer.get(), group_num_x, group_num_y, 1);
            //cmd->Blit(ao_result, rendering_data._camera_color_target_handle);
            //RenderTexture::ReleaseTempRT(ao_result);
            Vector4f params = {(f32) (rendering_data._width >> 1), (f32) (rendering_data._height >> 1), 1.0f, 1.0f};
            params.z /= params.x;
            params.w /= params.y;
            u16 group_num_x = std::ceilf(params.x / 16.0f), group_num_y = std::ceilf(params.y / 16.0f);
            auto ao_result = RenderTexture::GetTempRT(params.x, params.y, "AO_Result", ERenderTargetFormat::kDefaultHDR, false, false, true);
            auto blur_temp = RenderTexture::GetTempRT(params.x, params.y, "BlurTemp", ERenderTargetFormat::kDefaultHDR, false, false, true);
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
            if (_is_debug_mode)
                cmd->Blit(ao_result, rendering_data._camera_color_target_handle);
            RenderTexture::ReleaseTempRT(ao_result);
            RenderTexture::ReleaseTempRT(blur_temp);
            Shader::SetGlobalTexture("_OcclusionTex", ao_result);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void SSAOPass::BeginPass(GraphicsContext *context)
    {
    }
    void SSAOPass::EndPass(GraphicsContext *context)
    {
    }
}// namespace Ailu