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
		//_bloom_thread_rect.width = rendering_data._width >> 1;
		//_bloom_thread_rect.height = rendering_data._height >> 1;
		//auto cmd = CommandBufferPool::Get("PostProcessPass");
		//RTHandle rt;
		//cmd->Clear();
		//{
		//	ProfileBlock p(cmd.get(), _name);
		//	cmd->SetViewport(_bloom_thread_rect);
		//	cmd->SetScissorRect(_bloom_thread_rect);
		//	rt = RenderTexture::GetTempRT(_bloom_thread_rect.width, _bloom_thread_rect.height, "BloomThreshold", ERenderTargetFormat::kDefaultHDR);
		//	cmd->SetRenderTarget(rt);
		//	_p_bloom_thread_mat->SetTexture("_SourceTex", rendering_data._camera_color_target_handle);
		//	cmd->DrawRenderer(_p_quad_mesh.get(), _p_bloom_thread_mat.get());
		//	cmd->SetViewport(rendering_data._scissor_rect);
		//	cmd->SetScissorRect(rendering_data._scissor_rect);
		//	cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
		//	//_p_blit_mat->SetTexture("_SourceTex", _p_tex_bloom_threshold.get());
		//	//将原至空，防止其他地方设置的值干扰这里，当前所有blit使用同一个材质
		//	_p_blit_mat->SetTexture("_SourceTex", nullptr);
		//	Shader::SetGlobalTexture("_SourceTex", g_pRenderTexturePool->Get(rt));
		//	cmd->DrawRenderer(_p_quad_mesh.get(), _p_blit_mat.get());
		//}
		//context->ExecuteCommandBuffer(cmd);
		//RenderTexture::ReleaseTempRT(rt);
		//CommandBufferPool::Release(cmd);
	}
	void PostProcessPass::BeginPass(GraphicsContext* context)
	{
	}
	void PostProcessPass::EndPass(GraphicsContext* context)
	{
		Shader::SetGlobalTexture("_SourceTex", nullptr);
	}
}
