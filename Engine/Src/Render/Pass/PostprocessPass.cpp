#include "pch.h"
#include "Render/Pass/PostprocessPass.h"
#include "Render/CommandBuffer.h"


namespace Ailu
{
	PostProcessPass::PostProcessPass() : RenderPass("PostProcessPass")
	{
		_p_tex_bloom_threshold = RenderTexture::Create(800,450,"BloomThreshold",ERenderTargetFormat::kDefaultHDR);
		_p_bloom_thread_mat = MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/PostProcess/bloom.hlsl"),"BloomThread");
		_p_blit_mat = MaterialLibrary::GetMaterial("Blit");
		_p_obj_cb = ConstantBuffer::Create(256);
		memcpy(_p_obj_cb->GetData(),&BuildIdentityMatrix(),sizeof(Matrix4x4f));
		_bloom_thread_rect = Rect(0, 0, 800, 450);
		_backbuf_rect = Rect(0, 0, 1600, 900);
		_p_quad_mesh = MeshPool::GetMesh("FullScreenQuad");
		//static const auto material = MaterialLibrary::GetMaterial("Blit");
	}
	PostProcessPass::~PostProcessPass()
	{
	}
	void PostProcessPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get();
		cmd->Clear();
		cmd->SetViewport(_bloom_thread_rect);
		cmd->SetScissorRect(_bloom_thread_rect);
		cmd->SetRenderTarget(_p_tex_bloom_threshold);
		_p_bloom_thread_mat->SetTexture("_SourceTex", rendering_data._p_camera_color_target);
		cmd->DrawRenderer(_p_quad_mesh.get(), _p_bloom_thread_mat.get());
		cmd->SetViewport(_backbuf_rect);
		cmd->SetScissorRect(_backbuf_rect);
		cmd->SetRenderTarget(rendering_data._p_camera_color_target);
		_p_blit_mat->SetTexture("_SourceTex", _p_tex_bloom_threshold);
		cmd->DrawRenderer(_p_quad_mesh.get(), _p_blit_mat.get());
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void PostProcessPass::BeginPass(GraphicsContext* context)
	{
	}
	void PostProcessPass::EndPass(GraphicsContext* context)
	{
	}
}
