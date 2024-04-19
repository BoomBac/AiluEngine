#include "pch.h"
#include "Render/Pass/RenderPass.h"
#include "Render/RenderingData.h"
#include "Render/RenderQueue.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/CommandBuffer.h"
#include "Render/Buffer.h"

namespace Ailu
{
	//-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------
	OpaquePass::OpaquePass() : RenderPass("OpaquePass")
	{

	}
	OpaquePass::~OpaquePass()
	{
	}
	void OpaquePass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get();
		cmd->SetRenderTarget(rendering_data._p_camera_color_target, rendering_data._p_camera_depth_target);
		cmd->ClearRenderTarget(rendering_data._p_camera_color_target, rendering_data._p_camera_depth_target, Colors::kBlack, 1.0f);
		cmd->SetViewport(rendering_data._viewport);
		cmd->SetScissorRect(rendering_data._scissor_rect);

		u32 obj_index = 0u;
		if (RenderingStates::s_shadering_mode == EShaderingMode::kShader || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj.GetTransform(), sizeof(Matrix4x4f));
				cmd->DrawRenderer(obj.GetMesh(), obj.GetMaterial(), rendering_data._p_per_object_cbuf[obj_index], obj._submesh_index, obj._instance_count);
				++obj_index;
			}
		}
		obj_index = 0;
		if (RenderingStates::s_shadering_mode == EShaderingMode::kWireFrame || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			static auto wireframe_mat = MaterialLibrary::GetMaterial("Materials/WireFrame_new.alasset");
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj.GetTransform(), sizeof(Matrix4x4f));
				cmd->DrawRenderer(obj.GetMesh(), wireframe_mat.get(), rendering_data._p_per_object_cbuf[obj_index], obj._submesh_index, obj._instance_count);
				++obj_index;
			}
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void OpaquePass::BeginPass(GraphicsContext* context)
	{
	}
	void OpaquePass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------

	//-------------------------------------------------------------ReslovePass-------------------------------------------------------------
	ResolvePass::ResolvePass(Ref<RenderTexture>& source) : RenderPass("ReslovePass")
	{
		_p_src_color = source;
		_backbuf_rect = Rect(0, 0, 1600, 900);
	}
	void ResolvePass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get();
		cmd->Clear();
		cmd->SetViewport(_backbuf_rect);
		cmd->SetScissorRect(_backbuf_rect);
		cmd->ResolveToBackBuffer(_p_src_color);
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}

	void ResolvePass::BeginPass(GraphicsContext* context)
	{
	}

	void ResolvePass::BeginPass(GraphicsContext* context, Ref<RenderTexture>& source)
	{
		_p_src_color = source;
	}

	void ResolvePass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------ReslovePass-------------------------------------------------------------

	//-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------
	ShadowCastPass::ShadowCastPass() : RenderPass("ShadowCastPass"), _rect(Rect{ 0,0,kShadowMapSize,kShadowMapSize })
	{
		_p_shadow_map = RenderTexture::Create(kShadowMapSize, kShadowMapSize, "MainShadowMap", EALGFormat::kALGFormatD24S8_UINT);
		_p_shadowcast_material = MaterialLibrary::CreateMaterial(ShaderLibrary::Get(ShaderLibrary::kInternalShaderStringID.kDepthOnly), "ShadowCast");
	}

	void ShadowCastPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get();
		cmd->Clear();
		cmd->SetRenderTarget(nullptr, _p_shadow_map.get());
		cmd->ClearRenderTarget(_p_shadow_map.get(), 1.0f);
		cmd->SetScissorRect(_rect);
		cmd->SetViewport(_rect);
		u32 obj_index = 0u;
		for (auto& obj : RenderQueue::GetOpaqueRenderables())
		{
			cmd->DrawRenderer(obj.GetMesh(), _p_shadowcast_material.get(), rendering_data._p_per_object_cbuf[obj_index++], obj._submesh_index, obj._instance_count);
		}
		Shader::SetGlobalTexture("MainLightShadowMap", _p_shadow_map.get());
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}

	void ShadowCastPass::BeginPass(GraphicsContext* context)
	{
	}
	void ShadowCastPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------


	//-------------------------------------------------------------CubeMapGenPass-------------------------------------------------------------

	CubeMapGenPass::CubeMapGenPass(u16 size, String texture_name, String src_texture_name) : RenderPass("CubeMapGenPass"), _rect(Rect{ 0,0,size,size })
	{
		float x = 0.f, y = 0.f, z = 0.f;
		Vector3f center = { x,y,z };
		Vector3f world_up = Vector3f::kUp;
		_p_cube_map = RenderTexture::Create(size, size, texture_name, EALGFormat::kALGFormatR16G16B16A16_FLOAT, true);
		_p_env_map = RenderTexture::Create(size, size, texture_name, EALGFormat::kALGFormatR16G16B16A16_FLOAT, true);
		_p_gen_material = MaterialLibrary::GetMaterial("CubemapGen");
		_p_gen_material->SetTexture("env", src_texture_name);
		_p_cube_mesh = MeshPool::GetMesh("cube");
		_p_filter_material = MaterialLibrary::GetMaterial("EnvmapFilter");
		Matrix4x4f view, proj;
		BuildPerspectiveFovLHMatrix(proj, 90 * k2Radius, 1.0, 1.0, 100000);
		float scale = 0.5f;
		MatrixScale(_world_mat, scale, scale, scale);
		_per_obj_cb.reset(ConstantBuffer::Create(RenderConstants::kPeObjectDataSize));
		memcpy(_per_obj_cb->GetData(), &_world_mat, RenderConstants::kPeObjectDataSize);
		Vector3f targets[] =
		{
			{x + 1.f,y,z}, //+x
			{x - 1.f,y,z}, //-x
			{x,y + 1.f,z}, //+y
			{x,y - 1.f,z}, //-y
			{x,y,z + 1.f}, //+z
			{x,y,z - 1.f}  //-z
		};
		Vector3f ups[] =
		{
			{0.f,1.f,0.f}, //+x
			{0.f,1.f,0.f}, //-x
			{0.f,0.f,-1.f}, //+y
			{0.f,0.f,1.f}, //-y
			{0.f,1.f,0.f}, //+z
			{0.f,1.f,0.f}  //-z
		};
		for (u16 i = 0; i < 6; i++)
		{
			BuildViewMatrixLookToLH(view, center, targets[i], ups[i]);
			_pass_data[i]._PassMatrixVP = view * proj;
			_pass_data[i]._PassCameraPos = { (float)i,0.f,0.f,0.f };
			_per_pass_cb[i].reset(ConstantBuffer::Create(RenderConstants::kPePassDataSize));
			memcpy(_per_pass_cb[i]->GetData(), &_pass_data[i], RenderConstants::kPePassDataSize);
		}
	}

	void CubeMapGenPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get();
		cmd->Clear();
		cmd->SetScissorRect(_rect);
		cmd->SetViewport(_rect);
		for (u16 i = 0; i < 6; i++)
		{
			cmd->SetRenderTarget(_p_cube_map, i);
			cmd->ClearRenderTarget(_p_cube_map, Colors::kBlack, i);
			cmd->SubmitBindResource(_per_pass_cb[i].get(), EBindResDescType::kConstBuffer, _p_gen_material->GetShader()->GetPrePassBufferBindSlot());
			cmd->DrawRenderer(_p_cube_mesh.get(), _p_gen_material.get(), _per_obj_cb.get());
		}
		_p_filter_material->SetTexture("EnvMap", _p_cube_map);
		for (u16 i = 0; i < 6; i++)
		{
			cmd->SetRenderTarget(_p_env_map, i);
			cmd->ClearRenderTarget(_p_env_map, Colors::kBlack, i);
			cmd->SubmitBindResource(_per_pass_cb[i].get(), EBindResDescType::kConstBuffer, _p_gen_material->GetShader()->GetPrePassBufferBindSlot());
			cmd->DrawRenderer(_p_cube_mesh.get(), _p_filter_material.get(), _per_obj_cb.get());
		}
		Shader::SetGlobalTexture("SkyBox", _p_env_map.get());
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}

	void CubeMapGenPass::BeginPass(GraphicsContext* context)
	{
	}

	void CubeMapGenPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------CubeMapGenPass-------------------------------------------------------------

	//-------------------------------------------------------------DeferedGeometryPass-------------------------------------------------------------
	DeferredGeometryPass::DeferredGeometryPass(u16 width, u16 height) : RenderPass("DeferedGeometryPass")
	{
		_gbuffers.resize(3);
		_gbuffers[0] = RenderTexture::Create(width, height, "DeferredNormal", EALGFormat::kALGFormatR16G16_FLOAT);
		_gbuffers[1] = RenderTexture::Create(width, height, "DeferredAlbedo", EALGFormat::kALGFormatR8G8B8A8_UNORM);
		_gbuffers[2] = RenderTexture::Create(width, height, "DeferredNormal", EALGFormat::kALGFormatR8G8B8A8_UNORM);
		_p_lighting_material = MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/deferred_lighting.hlsl", "DeferredLightingVSMain", "DeferredLightingPSMain"), "DeferedGbufferLighting");
		_p_quad_mesh = MeshPool::GetMesh("FullScreenQuad");
		for (int i = 0; i < _rects.size(); i++)
			_rects[i] = Rect(0, 0, width, height);
	}
	void DeferredGeometryPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get();
		cmd->SetRenderTargets(_gbuffers, rendering_data._p_camera_depth_target);
		cmd->ClearRenderTarget(_gbuffers, rendering_data._p_camera_depth_target, Colors::kBlack, 1.0f);
		cmd->SetViewports({ _rects[0],_rects[1],_rects[2] });
		cmd->SetScissorRects({ _rects[0],_rects[1],_rects[2] });

		u32 obj_index = 0;
		for (auto& obj : RenderQueue::GetOpaqueRenderables())
		{
			memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj.GetTransform(), sizeof(Matrix4x4f));
			auto ret = cmd->DrawRenderer(obj.GetMesh(), obj.GetMaterial(), rendering_data._p_per_object_cbuf[obj_index], obj._submesh_index, obj._instance_count);
			if (ret != 0)
			{
				//LOG_WARNING("DeferredGeometryPass mesh {} not draw",obj.GetMesh()->Name());
			}
			else
				++obj_index;
		}
		_p_lighting_material->SetTexture("_GBuffer0", _gbuffers[0]);
		_p_lighting_material->SetTexture("_GBuffer1", _gbuffers[1]);
		_p_lighting_material->SetTexture("_GBuffer2", _gbuffers[2]);
		_p_lighting_material->SetTexture("_CameraDepthTexture", rendering_data._p_camera_depth_target);
		cmd->SetRenderTarget(rendering_data._p_camera_color_target);
		cmd->ClearRenderTarget(rendering_data._p_camera_color_target, Colors::kBlack);
		cmd->SetViewport(rendering_data._viewport);
		cmd->SetScissorRect(rendering_data._scissor_rect);
		cmd->DrawRenderer(_p_quad_mesh.get(), _p_lighting_material.get(), 1);
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void DeferredGeometryPass::BeginPass(GraphicsContext* context)
	{
	}
	void DeferredGeometryPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------DeferedGeometryPass-------------------------------------------------------------


	//-------------------------------------------------------------SkyboxPass-------------------------------------------------------------
	SkyboxPass::SkyboxPass() : RenderPass("SkyboxPass")
	{
		_p_skybox_material = MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/skybox._pp.hlsl"), "SkyboxPP");
		_p_quad_mesh = MeshPool::GetMesh("sphere");
	}
	void SkyboxPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get();
		cmd->Clear();
		cmd->SetViewport(rendering_data._viewport);
		cmd->SetScissorRect(rendering_data._scissor_rect);
		cmd->SetRenderTarget(rendering_data._p_camera_color_target, rendering_data._p_camera_depth_target);
		cmd->DrawRenderer(_p_quad_mesh.get(), _p_skybox_material.get(), 1);
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void SkyboxPass::BeginPass(GraphicsContext* context)
	{
	}
	void SkyboxPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------SkyboxPass-------------------------------------------------------------
}
