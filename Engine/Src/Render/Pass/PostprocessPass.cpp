#include "pch.h"
#include "Render/Pass/PostprocessPass.h"
#include "Render/CommandBuffer.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu
{
	PostProcessPass::PostProcessPass() : RenderPass("PostProcessPass")
	{
		_p_bloom_thread_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/bloom.alasset"),"BloomThread");
		_p_blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
		_p_obj_cb = ConstantBuffer::Create(256);
		memcpy(_p_obj_cb->GetData(),&BuildIdentityMatrix(),sizeof(Matrix4x4f));
		_bloom_thread_rect = Rect(0, 0, 800, 450);
		_p_quad_mesh = g_pResourceMgr->Get<Mesh>(L"Runtime/Mesh/FullScreenQuad");
	}
	PostProcessPass::~PostProcessPass()
	{
	}
	void PostProcessPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		_bloom_thread_rect.width = rendering_data._width >> 1;
		_bloom_thread_rect.height = rendering_data._height >> 1;
		auto cmd = CommandBufferPool::Get("PostProcessPass");
		RTHandle rt,blur_x,blur_y;
		cmd->Clear();
		{
			ProfileBlock p(cmd.get(), _name);
			cmd->SetName("Bloom");
			rt = RenderTexture::GetTempRT(_bloom_thread_rect.width, _bloom_thread_rect.height, "BloomThreshold", ERenderTargetFormat::kDefaultHDR);
			blur_x = RenderTexture::GetTempRT(_bloom_thread_rect.width, _bloom_thread_rect.height, "BlurX", ERenderTargetFormat::kDefaultHDR);
			blur_y = RenderTexture::GetTempRT(_bloom_thread_rect.width, _bloom_thread_rect.height, "BlurY", ERenderTargetFormat::kDefaultHDR);
			//提取高亮
			{
				cmd->SetViewport(_bloom_thread_rect);
				cmd->SetScissorRect(_bloom_thread_rect);
				cmd->SetRenderTarget(rt);
				_p_bloom_thread_mat->SetTexture("_SourceTex", rendering_data._camera_color_target_handle);
				cmd->DrawRenderer(_p_quad_mesh, _p_bloom_thread_mat.get());
			}
			//横向模糊
			{
				cmd->SetViewport(_bloom_thread_rect);
				cmd->SetScissorRect(_bloom_thread_rect);
				cmd->SetRenderTarget(blur_x);
				_p_bloom_thread_mat->SetTexture("_SourceTex", rt);
				cmd->DrawRenderer(_p_quad_mesh, _p_bloom_thread_mat.get(),1,1);
			}
			//纵向模糊
			{
				cmd->SetViewport(_bloom_thread_rect);
				cmd->SetScissorRect(_bloom_thread_rect);
				cmd->SetRenderTarget(blur_y);
				_p_bloom_thread_mat->SetTexture("_SourceTex", blur_x);
				cmd->DrawRenderer(_p_quad_mesh, _p_bloom_thread_mat.get(), 1, 2);
			}
			//合成
			{
				cmd->SetViewport(rendering_data._scissor_rect);
				cmd->SetScissorRect(rendering_data._scissor_rect);
				cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
				_p_bloom_thread_mat->SetTexture("_SourceTex", rendering_data._camera_opaque_tex_handle);
				_p_bloom_thread_mat->SetTexture("_BloomTex", blur_y);
				cmd->DrawRenderer(_p_quad_mesh, _p_bloom_thread_mat.get(),1,3);
			}
		}
		context->ExecuteCommandBuffer(cmd);
		RenderTexture::ReleaseTempRT(rt);
		RenderTexture::ReleaseTempRT(blur_x);
		RenderTexture::ReleaseTempRT(blur_y);
		CommandBufferPool::Release(cmd);
	}
	void PostProcessPass::BeginPass(GraphicsContext* context)
	{
	}
	void PostProcessPass::EndPass(GraphicsContext* context)
	{
		Shader::SetGlobalTexture("_SourceTex", nullptr);
	}
}
