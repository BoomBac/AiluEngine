#include "pch.h"
#include "Render/Pass/RenderPass.h"
#include "Render/RenderingData.h"
#include "Render/RenderQueue.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/CommandBuffer.h"

namespace Ailu
{
	static Ref<RenderTexture> s_p_shadow_map = nullptr;

	OpaquePass::OpaquePass() : _name("OpaquePass")
	{
	}
	//-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------
	void OpaquePass::Execute(GraphicsContext* context)
	{
		auto cmd = CommandBufferPool::GetCommandBuffer();
		if (RenderingStates::s_shadering_mode == EShaderingMode::kShader || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			//GraphicsPipelineStateMgr::ConfigureRenderTarget(RenderTargetState{}.Hash());
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				//obj._mat->SetTexture("MainLightShadowMap", s_p_shadow_map);
				//Quaternion q = Quaternion::AngleAxis(g_pTimeMgr->GetElapsedSinceLastMark(),Vector3f::kUp);
				cmd->DrawRenderer(obj._mesh, obj._mat, obj._transform, obj._instance_count);
			}
		}
		if (RenderingStates::s_shadering_mode == EShaderingMode::kWireFrame || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			static auto wireframe_mat = MaterialLibrary::GetMaterial("Materials/WireFrame_new.alasset");
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				cmd->DrawRenderer(obj._mesh, wireframe_mat.get(), obj._transform, obj._instance_count);
			}
		}
		//static Vector3f axis = Vector3f::kUp;
		//Quaternion q = Quaternion::AngleAxis(g_pTimeMgr->GetScaledWorldTime(), axis);
		//cmd->DrawRenderer(MeshPool::GetMesh("cube").get(), MaterialLibrary::GetMaterial("Materials/StandardPBR_new.alasset").get(), q.ToMat4f(), 1);
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::ReleaseCommandBuffer(cmd);
	}
	void OpaquePass::Execute(GraphicsContext* context, CommandBuffer* cmd)
	{
		if (RenderingStates::s_shadering_mode == EShaderingMode::kShader || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			//GraphicsPipelineStateMgr::ConfigureRenderTarget(RenderTargetState{}.Hash());
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				//obj._mat->SetTexture("MainLightShadowMap", s_p_shadow_map);
				cmd->DrawRenderer(obj._mesh, obj._mat, obj._transform, obj._instance_count);
			}
		}
		if (RenderingStates::s_shadering_mode == EShaderingMode::kWireFrame || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			static auto wireframe_mat = MaterialLibrary::GetMaterial("Materials/WireFrame_new.alasset");
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				cmd->DrawRenderer(obj._mesh, wireframe_mat.get(), obj._transform, obj._instance_count);
			}
		}
	}
	void OpaquePass::BeginPass(GraphicsContext* context)
	{
	}
	void OpaquePass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------

	//-------------------------------------------------------------ReslovePass-------------------------------------------------------------
	ResolvePass::ResolvePass(Ref<RenderTexture>& source) : _name("ReslovePass")
	{
		_p_src_color = source;
	}
	void ResolvePass::Execute(GraphicsContext* context)
	{
		auto cmd = CommandBufferPool::GetCommandBuffer();
		cmd->Clear();
		cmd->ResolveToBackBuffer(_p_src_color);
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::ReleaseCommandBuffer(cmd);
	}
	void ResolvePass::Execute(GraphicsContext* context, CommandBuffer* cmd)
	{
		cmd->ResolveToBackBuffer(_p_src_color);
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
	ShadowCastPass::ShadowCastPass() : _name("ShadowCastPass"), _rect(Rect{ 0,0,kShadowMapSize,kShadowMapSize })
	{
		_p_shadow_map = RenderTexture::Create(kShadowMapSize, kShadowMapSize, "MainShadowMap", EALGFormat::kALGFormatD24S8_UINT);
		_p_shadowcast_material = MaterialLibrary::CreateMaterial(ShaderLibrary::Get("DepthOnly"), "ShadowCast");
		s_p_shadow_map = _p_shadow_map;
	}

	void ShadowCastPass::Execute(GraphicsContext* context)
	{
	}

	void ShadowCastPass::Execute(GraphicsContext* context, CommandBuffer* cmd)
	{
	}


	void ShadowCastPass::Execute(GraphicsContext* context, CommandBuffer* cmd, const RenderingData& rendering_data)
	{
		for (size_t i = 0; i < rendering_data._actived_shadow_count; i++)
		{
			auto shadow = rendering_data._shadow_data[i];
			cmd->SetShadowMatrix(shadow._shadow_view * shadow._shadow_proj, 0);
		}
		cmd->SetRenderTarget(nullptr, _p_shadow_map.get());
		cmd->ClearRenderTarget(_p_shadow_map.get(), 1.0f);
		cmd->SetScissorRect(_rect);
		cmd->SetViewport(_rect);
		for (auto& obj : RenderQueue::GetOpaqueRenderables())
		{
			cmd->DrawRenderer(obj._mesh, _p_shadowcast_material.get(), obj._transform, obj._instance_count);
		}
		Shader::SetGlobalTexture("MainLightShadowMap", _p_shadow_map.get());
	}
	void ShadowCastPass::BeginPass(GraphicsContext* context)
	{
	}
	void ShadowCastPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------


	//-------------------------------------------------------------CubeMapGenPass-------------------------------------------------------------

	CubeMapGenPass::CubeMapGenPass(u16 size, String texture_name, String src_texture_name): _name("CubeMapGenPass"), _rect(Rect{ 0,0,size,size })
	{
		float x = 0.f, y = 0.f, z = 0.f;
		Vector3f center = { x,y,z };
		Vector3f world_up = Vector3f::kUp;
		_p_cube_map = RenderTexture::Create(size, size, texture_name, EALGFormat::kALGFormatR16G16B16A16_FLOAT,true);
		_p_env_map = RenderTexture::Create(size, size, texture_name, EALGFormat::kALGFormatR16G16B16A16_FLOAT,true);
		_p_gen_material = MaterialLibrary::GetMaterial("CubemapGen");
		_p_gen_material->SetTexture("env", src_texture_name);
		_p_cube_mesh = MeshPool::GetMesh("cube");
		_p_filter_material = MaterialLibrary::GetMaterial("EnvmapFilter");
		Matrix4x4f view,proj;
		BuildPerspectiveFovLHMatrix(proj, 90 * k2Radius, 1.0, 1.0, 100000);
		float scale = 0.5f;
		MatrixScale(_world_mat, scale, scale, scale);
		
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
			_pass_data[i]._PassCameraPos = {(float)i,0.f,0.f,0.f};
		}
	}

	void CubeMapGenPass::Execute(GraphicsContext* context)
	{
	}

	void CubeMapGenPass::Execute(GraphicsContext* context, CommandBuffer* cmd)
	{
		cmd->SetScissorRect(_rect);
		cmd->SetViewport(_rect);
		for (u16 i = 0; i < 6; i++)
		{
			cmd->SetRenderTarget(_p_cube_map, i);
			cmd->ClearRenderTarget(_p_cube_map, Colors::kBlack, i);
			cmd->SetPerPassCbufferData(i,&_pass_data[i]);
			cmd->SubmitBindResource(g_pGfxContext->GetCBufferPerPassGPUPtr(i),EBindResDescType::kConstBuffer,_p_gen_material->GetShader()->GetPrePassBufferBindSlot());
			cmd->DrawRenderer(_p_cube_mesh.get(), _p_gen_material.get(), _world_mat);
		}
		_p_filter_material->SetTexture("EnvMap", _p_cube_map);
		for (u16 i = 0; i < 6; i++)
		{
			cmd->SetRenderTarget(_p_env_map, i);
			cmd->ClearRenderTarget(_p_env_map, Colors::kBlack, i);
			cmd->SetPerPassCbufferData(i, &_pass_data[i]);
			cmd->SubmitBindResource(g_pGfxContext->GetCBufferPerPassGPUPtr(i), EBindResDescType::kConstBuffer, _p_filter_material->GetShader()->GetPrePassBufferBindSlot());
			cmd->DrawRenderer(_p_cube_mesh.get(), _p_filter_material.get(), _world_mat);
		}
		Shader::SetGlobalTexture("SkyBox", _p_env_map.get());
	}

	void CubeMapGenPass::Execute(GraphicsContext* context, CommandBuffer* cmd, const RenderingData& rendering_data)
	{
	}

	void CubeMapGenPass::BeginPass(GraphicsContext* context)
	{
	}

	void CubeMapGenPass::EndPass(GraphicsContext* context)
	{
	}

	//-------------------------------------------------------------CubeMapGenPass-------------------------------------------------------------
}
