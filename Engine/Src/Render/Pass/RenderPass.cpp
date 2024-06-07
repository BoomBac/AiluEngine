#include "pch.h"
#include "Render/Pass/RenderPass.h"
#include "Render/RenderingData.h"
#include "Render/RenderQueue.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/CommandBuffer.h"
#include "Render/Buffer.h"
#include "Framework/Common/Profiler.h"
#include "Render/Gizmo.h"
#include "Render/RenderConstants.h"
#include "Framework/Common/ResourceMgr.h"

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
		//auto cmd = CommandBufferPool::Get("OpaquePass");
		//cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
		//cmd->ClearRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle, Colors::kBlack, 1.0f);
		//cmd->SetViewport(rendering_data._viewport);
		//cmd->SetScissorRect(rendering_data._scissor_rect);

		//u32 obj_index = 0u;
		//if (RenderingStates::s_shadering_mode == EShaderingMode::kShader || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		//{
		//	for (auto& obj : RenderQueue::GetOpaqueRenderables())
		//	{
		//		memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj.GetTransform(), sizeof(Matrix4x4f));
		//		cmd->DrawRenderer(obj.GetMesh(), obj.GetMaterial(), rendering_data._p_per_object_cbuf[obj_index], obj._submesh_index, obj._instance_count);
		//		++obj_index;
		//	}
		//}
		//obj_index = 0;
		//if (RenderingStates::s_shadering_mode == EShaderingMode::kWireFrame || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		//{
		//	static auto wireframe_mat = MaterialLibrary::GetMaterial("Materials/WireFrame_new.alasset");
		//	for (auto& obj : RenderQueue::GetOpaqueRenderables())
		//	{
		//		memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj.GetTransform(), sizeof(Matrix4x4f));
		//		cmd->DrawRenderer(obj.GetMesh(), wireframe_mat.get(), rendering_data._p_per_object_cbuf[obj_index], obj._submesh_index, obj._instance_count);
		//		++obj_index;
		//	}
		//}
		//context->ExecuteCommandBuffer(cmd);
		//CommandBufferPool::Release(cmd);
	}
	void OpaquePass::BeginPass(GraphicsContext* context)
	{
	}
	void OpaquePass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------

	//-------------------------------------------------------------ReslovePass-------------------------------------------------------------
	ResolvePass::ResolvePass() : RenderPass("ReslovePass")
	{

	}
	void ResolvePass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get("ReslovePass");
		cmd->Clear();
		{
			ProfileBlock profile(cmd.get(), _name);
			cmd->SetViewport(rendering_data._viewport);
			cmd->SetScissorRect(rendering_data._viewport);

			//if (!rendering_data._p_final_rt)
			//	cmd->ResolveToBackBuffer(rendering_data._p_camera_color_target);
			//else
			//{
			//	cmd->ResolveToBackBuffer(rendering_data._p_camera_color_target, rendering_data._p_final_rt);
			//}
			//默认离屏渲染
			cmd->ResolveToBackBuffer(rendering_data._camera_color_target_handle, rendering_data._final_rt_handle);
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}

	void ResolvePass::BeginPass(GraphicsContext* context)
	{

	}

	void ResolvePass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------ReslovePass-------------------------------------------------------------

	//-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------
	ShadowCastPass::ShadowCastPass() : RenderPass("ShadowCastPass"), _rect(Rect{ 0,0,kShadowMapSize,kShadowMapSize }), _addlight_rect(Rect{ 0,0,(u16)(kShadowMapSize >> 1),(u16)(kShadowMapSize >> 1)})
	{
		_p_mainlight_shadow_map = RenderTexture::Create(kShadowMapSize, kShadowMapSize, "MainLightShadowMap", ERenderTargetFormat::kShadowMap);
		//_p_addlight_shadow_map = RenderTexture::Create(kShadowMapSize >> 1, kShadowMapSize >> 1, "AddLightShadowMap", ERenderTargetFormat::kShadowMap);
		_p_addlight_shadow_maps = RenderTexture::Create(kShadowMapSize >> 1, kShadowMapSize >> 1, RenderConstants::kMaxSpotLightNum,"AddLightShadowMaps", ERenderTargetFormat::kShadowMap);
		_p_point_light_shadow_map = RenderTexture::Create(kShadowMapSize >> 1,"PointLightShadowMap", ERenderTargetFormat::kShadowMap);
//		_p_shadowcast_material = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/depth_only.alasset"), "ShadowCast");
//		_p_shadowcast_material->SetUint("shadow_index",0);
//		_p_addshadowcast_material = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/depth_only.alasset"), "ShadowCast");
		for (int i = 0; i < 1 + RenderConstants::kMaxSpotLightNum + RenderConstants::kMaxPointLightNum * 6; i++)
		{
			_shadowcast_materials.emplace_back(MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/depth_only.alasset"), "ShadowCast"));
			_shadowcast_materials[i]->SetUint("shadow_index", 0);
		}
	}

	void ShadowCastPass::BeginPass(GraphicsContext* context)
	{
	}
	void ShadowCastPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get("MainLightShadowCastPass");
		cmd->Clear();
		{
			ProfileBlock profile(cmd.get(), _name);
			u32 obj_index = 0u;
			//方向光阴影，只有一个
			if (rendering_data._shadow_data[0]._shadow_index != -1)
			{
				cmd->SetRenderTarget(nullptr, _p_mainlight_shadow_map.get());
				//cmd->ClearRenderTarget(_p_mainlight_shadow_map.get(), 1.0f);
				for (auto& obj : RenderQueue::GetOpaqueRenderables())
				{
					cmd->DrawRenderer(obj.GetMesh(), _shadowcast_materials[0].get(), rendering_data._p_per_object_cbuf[obj_index++], obj._submesh_index, obj._instance_count);
				}
			}
			//投灯阴影
			if (rendering_data._addi_shadow_num > 0)
			{		
				for (int i = 0; i < rendering_data._addi_shadow_num; i++)
				{
					cmd->SetRenderTarget(nullptr, _p_addlight_shadow_maps.get(),0,i);
					//cmd->ClearRenderTarget(_p_addlight_shadow_maps.get(), i,1.0f);
					_shadowcast_materials[i + 1]->SetUint("shadow_index", rendering_data._shadow_data[i + 1]._shadow_index);
					for (auto& obj : RenderQueue::GetOpaqueRenderables())
					{
						cmd->DrawRenderer(obj.GetMesh(), _shadowcast_materials[i + 1].get(), rendering_data._p_per_object_cbuf[obj_index++], obj._submesh_index, obj._instance_count);
					}
					obj_index = 0;
				}
			}
			//点光阴影
			if (rendering_data._addi_point_shadow_num > 0)
			{
				static const u32 pointshadow_mat_start = 1 + RenderConstants::kMaxSpotLightNum;
				for (int i = 0; i < rendering_data._addi_point_shadow_num; i++)
				{
					for (int j = 0; j < 6; j++)
					{
						cmd->SetRenderTarget(nullptr, _p_point_light_shadow_map.get(), 0, j);
						cmd->ClearRenderTarget(_p_point_light_shadow_map.get(), j, 1.0f);
						Vector4f light_pos = rendering_data._point_shadow_data[0]._light_world_pos;
						_shadowcast_materials[pointshadow_mat_start + j]->SetVector("_point_light_wpos", light_pos);
						light_pos.x = rendering_data._point_shadow_data[0]._camera_near;
						light_pos.y = rendering_data._point_shadow_data[0]._camera_far;
						_shadowcast_materials[pointshadow_mat_start + j]->SetVector("_shadow_params", light_pos);
						_shadowcast_materials[pointshadow_mat_start + j]->SetUint("shadow_index", rendering_data._point_shadow_data[0]._shadow_indices[j]);
						for (auto& obj : RenderQueue::GetOpaqueRenderables())
						{
							cmd->DrawRenderer(obj.GetMesh(), _shadowcast_materials[pointshadow_mat_start + j].get(), rendering_data._p_per_object_cbuf[obj_index++], 
							obj._submesh_index,1,obj._instance_count);
						}
						obj_index = 0;
					}
				}
			}
		}
		Shader::SetGlobalTexture("MainLightShadowMap", _p_mainlight_shadow_map.get());
		Shader::SetGlobalTexture("AddLightShadowMaps", _p_addlight_shadow_maps.get());
		Shader::SetGlobalTexture("PointLightShadowMap", _p_point_light_shadow_map.get());
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}

	void ShadowCastPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------


	//-------------------------------------------------------------CubeMapGenPass-------------------------------------------------------------

	CubeMapGenPass::CubeMapGenPass(u16 size, String texture_name, String src_texture_name) : RenderPass("CubeMapGenPass"), _cubemap_rect(Rect{ 0,0,size,size }),_ibl_rect(Rect{ 0,0,(u16)(size / 4),(u16)(size / 4) })
	{
		float x = 0.f, y = 0.f, z = 0.f;
		Vector3f center = { x,y,z };
		Vector3f world_up = Vector3f::kUp;
		_p_cube_map = RenderTexture::Create(size, texture_name + "_cubemap", ERenderTargetFormat::kDefaultHDR, true,true,true);
		_p_cube_map->CreateView();
		_p_env_map = RenderTexture::Create(size / 4, texture_name + "_ibl_diffuse", ERenderTargetFormat::kDefaultHDR, false);
		_p_gen_material = g_pResourceMgr->Get<Material>(L"Runtime/Material/CubemapGen");
		_p_gen_material->SetTexture("env", ToWChar(src_texture_name));
		_p_cube_mesh = Mesh::s_p_cube;
		_p_filter_material = g_pResourceMgr->Get<Material>(L"Runtime/Material/EnvmapFilter");
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
		cmd->SetScissorRect(_cubemap_rect);
		cmd->SetViewport(_cubemap_rect);
		for (u16 i = 0; i < 6; i++)
		{
			cmd->SetRenderTarget(_p_cube_map.get(), i);
			cmd->ClearRenderTarget(_p_cube_map.get(), Colors::kBlack, i);
			cmd->SubmitBindResource(_per_pass_cb[i].get(), EBindResDescType::kConstBuffer, _p_gen_material->GetShader()->GetPrePassBufferBindSlot());
			cmd->DrawRenderer(_p_cube_mesh, _p_gen_material, _per_obj_cb.get());
		}
		_p_filter_material->SetTexture("EnvMap", _p_cube_map.get());
		cmd->SetScissorRect(_ibl_rect);
		cmd->SetViewport(_ibl_rect);
		for (u16 i = 0; i < 6; i++)
		{
			cmd->SetRenderTarget(_p_env_map.get(), i);
			cmd->ClearRenderTarget(_p_env_map.get(), Colors::kBlack, i);
			cmd->SubmitBindResource(_per_pass_cb[i].get(), EBindResDescType::kConstBuffer, _p_gen_material->GetShader()->GetPrePassBufferBindSlot());
			cmd->DrawRenderer(_p_cube_mesh, _p_filter_material, _per_obj_cb.get());
		}
		Shader::SetGlobalTexture("SkyBox", _p_env_map.get());
		context->ExecuteAndWaitCommandBuffer(cmd);
		_p_cube_map->GenerateMipmap(cmd.get());
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
		//ShaderImportSetting setting;
		//setting._vs_entry = "DeferredLightingVSMain";
		//setting._ps_entry = "DeferredLightingPSMain";
		_p_lighting_material = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/deferred_lighting.alasset"), "DeferedGbufferLighting");
		_p_quad_mesh = Mesh::s_p_quad;
		for (int i = 0; i < _rects.size(); i++)
			_rects[i] = Rect(0, 0, width, height);
		_p_ibllut = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineTexturePathW + L"ibl_brdf_lut.alasset");
	}
	void DeferredGeometryPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto w = rendering_data._width, h = rendering_data._height;
		for (int i = 0; i < _rects.size(); i++)
			_rects[i] = Rect(0, 0, w, h);
		_gbuffers[0] = RenderTexture::GetTempRT(w, h, "GBuffer0", ERenderTargetFormat::kRGHalf);
		_gbuffers[1] = RenderTexture::GetTempRT(w, h, "GBuffer1", ERenderTargetFormat::kDefault);
		_gbuffers[2] = RenderTexture::GetTempRT(w, h, "GBuffer2", ERenderTargetFormat::kDefault);

		auto cmd = CommandBufferPool::Get("DeferredRenderPass");
		{
			ProfileBlock profile(cmd.get(), _name);
			cmd->SetRenderTargets(_gbuffers, rendering_data._camera_depth_target_handle);
			cmd->ClearRenderTarget(_gbuffers, rendering_data._camera_depth_target_handle, Colors::kBlack, 1.0f);
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
			_p_lighting_material->SetTexture("_CameraDepthTexture", rendering_data._camera_depth_target_handle);
			_p_lighting_material->SetTexture("IBLLut", _p_ibllut);
			cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
			cmd->ClearRenderTarget(rendering_data._camera_color_target_handle, Colors::kBlack);
			cmd->SetViewport(rendering_data._viewport);
			cmd->SetScissorRect(rendering_data._scissor_rect);
			cmd->DrawRenderer(_p_quad_mesh, _p_lighting_material.get(), 1);
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void DeferredGeometryPass::BeginPass(GraphicsContext* context)
	{

	}
	void DeferredGeometryPass::EndPass(GraphicsContext* context)
	{
		RenderTexture::ReleaseTempRT(_gbuffers[0]);
		RenderTexture::ReleaseTempRT(_gbuffers[1]);
		RenderTexture::ReleaseTempRT(_gbuffers[2]);
	}
	//-------------------------------------------------------------DeferedGeometryPass-------------------------------------------------------------


	//-------------------------------------------------------------SkyboxPass-------------------------------------------------------------
	SkyboxPass::SkyboxPass() : RenderPass("SkyboxPass")
	{
		//_p_skybox_material = MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/skybox._pp.alasset"), "SkyboxPP");
		_p_skybox_material = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/skybox.alasset"), "Skybox");
		_p_sky_mesh = Mesh::s_p_shpere;
		Matrix4x4f world_mat;
		MatrixScale(world_mat, 10000.f, 10000.f, 10000.f);
		_p_cbuffer.reset(ConstantBuffer::Create(RenderConstants::kPeObjectDataSize));
		memcpy(_p_cbuffer->GetData(), &world_mat, RenderConstants::kPeObjectDataSize);
	}
	void SkyboxPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get("SkyboxPass");
		cmd->Clear();
		{
			ProfileBlock profile(cmd.get(), _name);
			cmd->SetViewport(rendering_data._viewport);
			cmd->SetScissorRect(rendering_data._scissor_rect);
			cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
			cmd->DrawRenderer(_p_sky_mesh, _p_skybox_material.get(), _p_cbuffer.get(), 0, 1);
		}
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



	//-------------------------------------------------------------GizmoPass-------------------------------------------------------------
	GizmoPass::GizmoPass() : RenderPass("GizmoPass")
	{
	}
	void GizmoPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get("GizmoPass");
		cmd->Clear();
		{
			ProfileBlock profile(cmd.get(), _name);
			cmd->SetViewport(rendering_data._viewport);
			cmd->SetScissorRect(rendering_data._scissor_rect);
			cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
			GraphicsPipelineStateMgr::s_gizmo_pso->Bind(cmd.get());
			GraphicsPipelineStateMgr::s_gizmo_pso->SetPipelineResource(cmd.get(), Shader::GetPerFrameConstBuffer(), EBindResDescType::kConstBuffer);
			Gizmo::Submit(cmd.get());
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void GizmoPass::BeginPass(GraphicsContext* context)
	{
		if (Gizmo::s_color.a > 0.0f)
		{
			int gridSize = 100;
			int gridSpacing = 100;
			Vector3f cameraPosition = Camera::sCurrent->Position();
			float grid_alpha = lerpf(0.0f, 1.0f, abs(cameraPosition.y) / 2000.0f);
			Color32 grid_color = Colors::kWhite;
			grid_color.a = 1.0f - grid_alpha;
			if (grid_color.a > 0)
			{
				Vector3f grid_center_mid(static_cast<float>(static_cast<int>(cameraPosition.x / gridSpacing) * gridSpacing),
					0.0f,
					static_cast<float>(static_cast<int>(cameraPosition.z / gridSpacing) * gridSpacing));
				Gizmo::DrawGrid(100, 100, grid_center_mid, grid_color);
			}
			grid_color.a = grid_alpha;
			if (grid_color.a > 0.7f)
			{
				gridSize = 10;
				gridSpacing = 1000;
				Vector3f grid_center_large(static_cast<float>(static_cast<int>(cameraPosition.x / gridSpacing) * gridSpacing),
					0.0f,
					static_cast<float>(static_cast<int>(cameraPosition.z / gridSpacing) * gridSpacing));
				//grid_color = Colors::kGreen;
				Gizmo::DrawGrid(10, 1000, grid_center_large, grid_color);
			}

			Gizmo::DrawLine(Vector3f::kZero, Vector3f{ 500.f,0.0f,0.0f }, Colors::kRed);
			Gizmo::DrawLine(Vector3f::kZero, Vector3f{ 0.f,500.0f,0.0f }, Colors::kGreen);
			Gizmo::DrawLine(Vector3f::kZero, Vector3f{ 0.f,0.0f,500.0f }, Colors::kBlue);
		}
	}
	void GizmoPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------GizmoPass-------------------------------------------------------------

	//-------------------------------------------------------------CopyColorPass-------------------------------------------------------------
	CopyColorPass::CopyColorPass(): RenderPass("CopyColor")
	{
		_p_blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
		_p_obj_cb = ConstantBuffer::Create(256);
		memcpy(_p_obj_cb->GetData(), &BuildIdentityMatrix(), sizeof(Matrix4x4f));
		_p_quad_mesh = g_pResourceMgr->Get<Mesh>(L"Runtime/Mesh/FullScreenQuad");
	}
	CopyColorPass::~CopyColorPass()
	{
	}
	void CopyColorPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get("CopyColor");
		cmd->Clear();
		{
			ProfileBlock profile(cmd.get(), _name);
			cmd->SetViewport(rendering_data._viewport);
			cmd->SetScissorRect(rendering_data._viewport);
			cmd->SetRenderTarget(rendering_data._camera_opaque_tex_handle);
			_p_blit_mat->SetTexture("_SourceTex", rendering_data._camera_color_target_handle);
			cmd->DrawRenderer(_p_quad_mesh,_p_blit_mat,1,1);
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void CopyColorPass::BeginPass(GraphicsContext* context)
	{

	}
	void CopyColorPass::EndPass(GraphicsContext* context)
	{
		_p_blit_mat->SetTexture("_SourceTex", nullptr);
	}
	//-------------------------------------------------------------CopyColorPass-------------------------------------------------------------
}
