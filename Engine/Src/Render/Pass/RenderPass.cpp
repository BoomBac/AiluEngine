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
	ForwardPass::ForwardPass() : RenderPass("TransparentPass")
	{
		shader_state_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/debug.hlsl"), "ShaderStateDebug");
		_error_shader_pass_id = 0;
		_compiling_shader_pass_id = 1;
		_forward_lit_shader = g_pResourceMgr->Get<Shader>(L"Shaders/forwardlit.alasset");
		AL_ASSERT(_forward_lit_shader == nullptr);
	}
	ForwardPass::~ForwardPass()
	{
	}
	void ForwardPass::BeginPass(GraphicsContext* context)
	{
		//for (auto& obj : RenderQueue::GetTransparentRenderables())
		//{
		//	if (!_transparent_replacement_materials.contains(obj.GetMaterial()->ID()))
		//	{
		//		auto mat = MakeRef<Material>(*(obj.GetMaterial()));
		//		mat->ChangeShader(_forward_lit_shader);
		//		_transparent_replacement_materials.emplace(std::make_pair(obj.GetMaterial()->ID(), mat));
		//	}
		//}
	}
	void ForwardPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto& all_renderable = *rendering_data._cull_results;
		u32 lowerBound = Shader::kRenderQueueOpaque, upperBound = Shader::kRenderQueueEnd;
		auto filtered = all_renderable| std::views::filter([lowerBound, upperBound](const auto& kv) {
			return kv.first >= lowerBound && kv.first <= upperBound;
			}) | std::views::values;
		u32 obj_num = 0;
		for (auto& r : filtered)
		{
			obj_num += r.size();
		}
		if (obj_num == 0)
			return;
		auto cmd = CommandBufferPool::Get(_name);
		{
			ProfileBlock b(cmd.get(), _name);
			cmd->SetRenderTarget(rendering_data._camera_color_target_handle,rendering_data._camera_depth_target_handle);
			u32 obj_index = 0u;
			for (auto& it : all_renderable)
			{
				auto& [queue, objs] = it;
				if (queue >= Shader::kRenderQueueTransparent)
				{
					for (auto& obj : objs)
					{
						memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj._world_matrix, sizeof(Matrix4x4f));
						cmd->DrawRenderer(obj._mesh, obj._material, rendering_data._p_per_object_cbuf[obj_index], obj._submesh_index, 0, obj._instance_count);
						++obj_index;
					}
				}
			}
			//for (auto& obj : RenderQueue::GetTransparentRenderables())
			//{

			//}
			//for (auto& obj : RenderQueue::GetErrorShaderRenderables())
			//{
			//	memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj.GetTransform(), sizeof(Matrix4x4f));
			//	cmd->DrawRenderer(obj.GetMesh(), shader_state_mat.get(), rendering_data._p_per_object_cbuf[obj_index], obj._submesh_index, _error_shader_pass_id, obj._instance_count);
			//	++obj_index;
			//}
			//obj_index = 0u;
			//for (auto& obj : RenderQueue::GetCompilingShaderRenderables())
			//{
			//	memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj.GetTransform(), sizeof(Matrix4x4f));
			//	cmd->DrawRenderer(obj.GetMesh(), shader_state_mat.get(), rendering_data._p_per_object_cbuf[obj_index], obj._submesh_index, _compiling_shader_pass_id, obj._instance_count);
			//	++obj_index;
			//}
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void ForwardPass::EndPass(GraphicsContext* context)
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
	ShadowCastPass::ShadowCastPass() : RenderPass("ShadowCastPass"), _rect(Rect{ 0,0,kShadowMapSize,kShadowMapSize }), _addlight_rect(Rect{ 0,0,(u16)(kShadowMapSize >> 1),(u16)(kShadowMapSize >> 1) })
	{
		_p_mainlight_shadow_map = RenderTexture::Create(kShadowMapSize, kShadowMapSize,RenderConstants::kMaxCascadeShadowMapSplitNum, "MainLightShadowMap",ERenderTargetFormat::kShadowMap);
		_p_addlight_shadow_maps = RenderTexture::Create(kShadowMapSize >> 1, kShadowMapSize >> 1, RenderConstants::kMaxSpotLightNum, "AddLightShadowMaps", ERenderTargetFormat::kShadowMap);
		_p_point_light_shadow_maps = RenderTexture::Create(kShadowMapSize >> 1, "PointLightShadowMap", ERenderTargetFormat::kShadowMap, RenderConstants::kMaxPointLightNum);
		for (int i = 0; i < RenderConstants::kMaxCascadeShadowMapSplitNum + RenderConstants::kMaxSpotLightNum + RenderConstants::kMaxPointLightNum * 6; i++)
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
				for (int i = 0; i < QuailtySetting::s_cascade_shadow_map_count; i++)
				{
					cmd->SetRenderTarget(nullptr, _p_mainlight_shadow_map.get(),0,i);
					cmd->ClearRenderTarget(_p_mainlight_shadow_map.get(),i,1.0f);
					_shadowcast_materials[i]->SetUint("shadow_index", rendering_data._shadow_data[i]._shadow_index);
					for (auto& it : *rendering_data._shadow_data[i]._cull_results)
					{
						auto& [queue, objs] = it;
						for (auto& obj : objs)
						{
							cmd->DrawRenderer(obj._mesh, _shadowcast_materials[i].get(), rendering_data._p_per_object_cbuf[obj._scene_id], obj._submesh_index, obj._instance_count);
						}
					}
				}
			}
			//投灯阴影
			if (rendering_data._addi_shadow_num > 0)
			{
				for (int i = 0; i < RenderConstants::kMaxSpotLightNum; i++)
				{
					int shadow_data_index = i + RenderConstants::kMaxCascadeShadowMapSplitNum;
					if (rendering_data._shadow_data[shadow_data_index]._shadow_index == -1)
						continue;
					cmd->SetRenderTarget(nullptr, _p_addlight_shadow_maps.get(), 0, i);
					cmd->ClearRenderTarget(_p_addlight_shadow_maps.get(), i, 1.0f);
					_shadowcast_materials[i + RenderConstants::kMaxCascadeShadowMapSplitNum]->SetUint("shadow_index", rendering_data._shadow_data[shadow_data_index]._shadow_index);
					for (auto& it : *rendering_data._shadow_data[shadow_data_index]._cull_results)
					{
						auto& [queue, objs] = it;
						for (auto& obj : objs)
						{
							cmd->DrawRenderer(obj._mesh, _shadowcast_materials[shadow_data_index].get(), rendering_data._p_per_object_cbuf[obj._scene_id], obj._submesh_index, obj._instance_count);
						}
					}
					obj_index = 0;
				}
			}
			//点光阴影
			if (rendering_data._addi_point_shadow_num > 0)
			{
				static const u32 pointshadow_mat_start = RenderConstants::kMaxCascadeShadowMapSplitNum + RenderConstants::kMaxSpotLightNum;
				for (int i = 0; i < RenderConstants::kMaxPointLightNum; i++)
				{
					if (rendering_data._point_shadow_data[i]._camera_far == 0)
						continue;
					for (int j = 0; j < 6; j++)
					{
						//j + i * 6 定位到cubearray
						u16 per_cube_slice_index = j + i * 6;
						cmd->SetRenderTarget(nullptr, _p_point_light_shadow_maps.get(), 0, per_cube_slice_index);
						cmd->ClearRenderTarget(_p_point_light_shadow_maps.get(), per_cube_slice_index, 1.0f);
						Vector4f light_pos = rendering_data._point_shadow_data[i]._light_world_pos;
						_shadowcast_materials[pointshadow_mat_start + per_cube_slice_index]->SetVector("_point_light_wpos", light_pos);
						light_pos.x = rendering_data._point_shadow_data[i]._camera_near;
						light_pos.y = rendering_data._point_shadow_data[i]._camera_far;
						_shadowcast_materials[pointshadow_mat_start + per_cube_slice_index]->SetVector("_shadow_params", light_pos);
						_shadowcast_materials[pointshadow_mat_start + per_cube_slice_index]->SetUint("shadow_index", rendering_data._point_shadow_data[i]._shadow_indices[j]);
						for (auto& it : *rendering_data._point_shadow_data[i]._cull_results[j])
						{
							auto& [queue, objs] = it;
							for (auto& obj : objs)
							{
								cmd->DrawRenderer(obj._mesh, _shadowcast_materials[pointshadow_mat_start + per_cube_slice_index].get(), rendering_data._p_per_object_cbuf[obj._scene_id],
								obj._submesh_index, 1,obj._instance_count);
							}
						}
						//for (auto& obj : RenderQueue::GetOpaqueRenderables())
						//{
						//	cmd->DrawRenderer(obj.GetMesh(), _shadowcast_materials[pointshadow_mat_start + per_cube_slice_index].get(), rendering_data._p_per_object_cbuf[obj_index++],
						//		obj._submesh_index, 1, obj._instance_count);
						//}
						//for (auto& obj : RenderQueue::GetTransparentRenderables())
						//{
						//	cmd->DrawRenderer(obj.GetMesh(), _shadowcast_materials[pointshadow_mat_start + per_cube_slice_index].get(), rendering_data._p_per_object_cbuf[obj_index++],
						//		obj._submesh_index, 1, obj._instance_count);
						//}
						obj_index = 0;
					}
				}
				//for (int i = 0; i < shaow_count; i++)
				//{

				//}
			}
		}
		Shader::SetGlobalTexture("MainLightShadowMap", _p_mainlight_shadow_map.get());
		Shader::SetGlobalTexture("AddLightShadowMaps", _p_addlight_shadow_maps.get());
		Shader::SetGlobalTexture("PointLightShadowMaps", _p_point_light_shadow_maps.get());
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}

	void ShadowCastPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------


	//-------------------------------------------------------------CubeMapGenPass-------------------------------------------------------------

	CubeMapGenPass::CubeMapGenPass(u16 size, String texture_name, String src_texture_name) : RenderPass("CubeMapGenPass"), _cubemap_rect(Rect{ 0,0,size,size }), _ibl_rect(Rect{ 0,0,(u16)(size / 4),(u16)(size / 4) })
	{
		is_source_by_texture = true;
		float x = 0.f, y = 0.f, z = 0.f;
		Vector3f center = { x,y,z };
		Vector3f world_up = Vector3f::kUp;
		_src_cubemap = RenderTexture::Create(size, texture_name + "_src_cubemap", ERenderTargetFormat::kRGBAHalf, true, true, true);
		_src_cubemap->CreateView();
		_prefilter_cubemap = RenderTexture::Create(size, texture_name + "_prefilter_cubemap", ERenderTargetFormat::kRGBAHalf, true, true, true);
		_prefilter_cubemap->CreateView();
		_radiance_map = RenderTexture::Create(size / 4, texture_name + "_radiance", ERenderTargetFormat::kRGBAHalf, false);
		_radiance_map->CreateView();
		_p_gen_material = g_pResourceMgr->Get<Material>(L"Runtime/Material/CubemapGen");
		_p_gen_material->SetTexture("env", ToWChar(src_texture_name));
		_p_cube_mesh = Mesh::s_p_cube;
		_p_filter_material = g_pResourceMgr->Get<Material>(L"Runtime/Material/EnvmapFilter");
		Matrix4x4f view, proj;
		BuildPerspectiveFovLHMatrix(proj, 90 * k2Radius, 1.0, 1.0, 100000);
		//BuildPerspectiveFovLHMatrix(proj, 2.0 * atan((f32)size / ((f32)size - 0.5)), 1.0, 1.0, 100000);
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
		f32 mipmap_level = _prefilter_cubemap->MipmapLevel() + 1;
		for (f32 i = 0.0f; i < mipmap_level; i++)
		{
			u16 cur_mipmap_size = size >> (u16)i;
			_reflection_prefilter_mateirals.emplace_back(MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/filter_irradiance.alasset"), "ReflectionPrefilter"));
			_reflection_prefilter_mateirals.back()->SetFloat("_roughness", i / mipmap_level);
			_reflection_prefilter_mateirals.back()->SetFloat("_width", cur_mipmap_size);
			//_reflection_prefilter_mateirals.back()->SetTexture("SrcTex", ToWChar(src_texture_name));
			_reflection_prefilter_mateirals.back()->SetTexture("EnvMap", _src_cubemap.get());
		}
	}

	void CubeMapGenPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get();
		cmd->Clear();
		if (is_source_by_texture)
		{
			u16 mipmap_level = _src_cubemap->MipmapLevel() + 1;
			//image tp cubemap
			for (u16 i = 0; i < 6; i++)
			{
				u16 rt_index = mipmap_level * i;
				cmd->SetRenderTarget(_src_cubemap.get(), rt_index);
				cmd->ClearRenderTarget(_src_cubemap.get(), Colors::kBlack, rt_index);
				cmd->SubmitBindResource(_per_pass_cb[i].get(), EBindResDescType::kConstBuffer, _p_gen_material->GetShader()->GetPerPassBufferBindSlot(0, _p_gen_material->ActiveVariantHash(0)));
				cmd->DrawRenderer(_p_cube_mesh, _p_gen_material, _per_obj_cb.get(), 0, 0, 1);
			}
			context->ExecuteAndWaitCommandBuffer(cmd);
			cmd->Clear();
			//实际上mipmap生成不使用传入的cmd，但是需要等待原始cubemap生成完毕
			_src_cubemap->GenerateMipmap(cmd.get());
			//gen radiance map
			_p_filter_material->SetTexture("EnvMap", _src_cubemap.get());
			for (u16 i = 0; i < 6; i++)
			{
				cmd->SetRenderTarget(_radiance_map.get(), i);
				cmd->ClearRenderTarget(_radiance_map.get(), Colors::kBlack, i);
				cmd->SubmitBindResource(_per_pass_cb[i].get(), EBindResDescType::kConstBuffer, _p_gen_material->GetShader()->GetPerPassBufferBindSlot(0,_p_gen_material->ActiveVariantHash(0)));
				cmd->DrawRenderer(_p_cube_mesh, _p_filter_material, _per_obj_cb.get());
			}
			//filter envmap
			mipmap_level = _prefilter_cubemap->MipmapLevel() + 1;
			for (u16 i = 0; i < 6; i++)
			{
				Rect r(0, 0, _prefilter_cubemap->Width(), _prefilter_cubemap->Height());
				for (u16 j = 0; j < mipmap_level; j++)
				{
					auto [w, h] = _prefilter_cubemap->CurMipmapSize(j);
					r.width = w;
					r.height = h;
					u16 rt_index = mipmap_level * i + j;
					cmd->SetViewport(r);
					cmd->SetScissorRect(r);
					cmd->SetRenderTarget(_prefilter_cubemap.get(), rt_index);
					cmd->ClearRenderTarget(_prefilter_cubemap.get(), Colors::kBlack, rt_index);
					cmd->SubmitBindResource(_per_pass_cb[i].get(), EBindResDescType::kConstBuffer, _reflection_prefilter_mateirals[0]->GetShader()->GetPerPassBufferBindSlot(1, _reflection_prefilter_mateirals[0]->ActiveVariantHash(1)));
					cmd->DrawRenderer(_p_cube_mesh, _reflection_prefilter_mateirals[j].get(), _per_obj_cb.get(), 0, 1, 1);
				}
			}
		}

		Shader::SetGlobalTexture("SkyBox", _src_cubemap.get());
		context->ExecuteCommandBuffer(cmd);
		//_p_cube_map->GenerateMipmap(cmd.get());
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
		for (int i = 0; i < _rects.size(); i++)
			_rects[i] = Rect(0, 0, width, height);
	}
	void DeferredGeometryPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		if (rendering_data._cull_results->size() == 0)
			return;
		auto w = rendering_data._width, h = rendering_data._height;
		for (int i = 0; i < _rects.size(); i++)
			_rects[i] = Rect(0, 0, w, h);

		auto cmd = CommandBufferPool::Get("DeferredRenderPass");
		{
			ProfileBlock profile(cmd.get(), _name);
			cmd->SetRenderTargets(rendering_data._gbuffers, rendering_data._camera_depth_target_handle);
			cmd->ClearRenderTarget(rendering_data._gbuffers, rendering_data._camera_depth_target_handle, Colors::kBlack, 1.0f);
			cmd->SetViewports({ _rects[0],_rects[1],_rects[2] });
			cmd->SetScissorRects({ _rects[0],_rects[1],_rects[2] });

			u32 obj_index = 0;
			for (auto& it : *rendering_data._cull_results)
			{
				auto& [queue, objs] = it;
				for (auto& obj : objs)
				{
					//memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj._world_matrix, sizeof(Matrix4x4f));
					auto ret = cmd->DrawRenderer(obj._mesh, obj._material, rendering_data._p_per_object_cbuf[obj._scene_id], obj._submesh_index, obj._instance_count);
					++obj_index;
				}
			}
			//for (auto& obj : RenderQueue::GetOpaqueRenderables())
			//{
			//	memcpy(rendering_data._p_per_object_cbuf[obj_index]->GetData(), obj.GetTransform(), sizeof(Matrix4x4f));
			//	auto ret = cmd->DrawRenderer(obj.GetMesh(), obj.GetMaterial(), rendering_data._p_per_object_cbuf[obj_index], obj._submesh_index, obj._instance_count);
			//	++obj_index;
			//}
		}
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

	//-------------------------------------------------------------DeferedLightingPass-------------------------------------------------------------
	DeferredLightingPass::DeferredLightingPass() : RenderPass("DeferredLightingPass")
	{
		_p_lighting_material = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/deferred_lighting.alasset"), "DeferedGbufferLighting");
		_brdf_lut = Texture2D::Create(128, 128, false, ETextureFormat::kRGFloat, false, true);
		_brdf_lut->Apply();
		_brdf_lut->Name("brdf_lut_tex");
		_brdflut_gen = ComputeShader::Create(PathUtils::GetResSysPath(L"Shaders/hlsl/Compute/brdflut_gen.alcp"));
		_brdflut_gen->SetTexture("_brdf_lut", _brdf_lut.get());
		auto cmd = CommandBufferPool::Get();
		cmd->Dispatch(_brdflut_gen.get(), 8, 8, 1);
		g_pGfxContext->ExecuteCommandBuffer(cmd);
	}
	void DeferredLightingPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		_p_lighting_material->SetTexture("_GBuffer0", rendering_data._gbuffers[0]);
		_p_lighting_material->SetTexture("_GBuffer1", rendering_data._gbuffers[1]);
		_p_lighting_material->SetTexture("_GBuffer2", rendering_data._gbuffers[2]);
		_p_lighting_material->SetTexture("_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
		Shader::SetGlobalTexture("IBLLut", _brdf_lut.get());
		auto cmd = CommandBufferPool::Get("DeferredLightingPass");
		{
			ProfileBlock profile(cmd.get(), _name);
			//cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
			cmd->SetRenderTarget(rendering_data._camera_color_target_handle,rendering_data._camera_depth_target_handle);
			cmd->ClearRenderTarget(rendering_data._camera_color_target_handle, Colors::kBlack);
			//cmd->SetViewport(rendering_data._viewport);
			//cmd->SetScissorRect(rendering_data._scissor_rect);
			cmd->DrawRenderer(Mesh::s_p_quad, _p_lighting_material.get(), 1);
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void DeferredLightingPass::BeginPass(GraphicsContext* context)
	{
	}
	void DeferredLightingPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------DeferedLightingPass-------------------------------------------------------------


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
		for (int i = 0; i < 10; i++)
		{
			_p_cbuffers.push_back(std::unique_ptr<ConstantBuffer>(ConstantBuffer::Create(256)));
		}
	}
	void GizmoPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get("GizmoPass");
		static Material* light_icon_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/PointLightBillboard");
		cmd->Clear();
		{
			ProfileBlock profile(cmd.get(), _name);
			cmd->SetViewport(rendering_data._viewport);
			cmd->SetScissorRect(rendering_data._scissor_rect);
			cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
			GraphicsPipelineStateMgr::s_gizmo_pso->Bind(cmd.get());
			GraphicsPipelineStateMgr::s_gizmo_pso->SetPipelineResource(cmd.get(), Shader::GetPerFrameConstBuffer(), EBindResDescType::kConstBuffer);
			Gizmo::Submit(cmd.get());
			u16 index = 0;
			for (auto& it : g_pSceneMgr->_p_current->GetAllGizmoPosition())
			{
				auto& [pos, type] = it;
				auto m = MatrixTranslation(pos);
				MatrixScale(m, 50, 50, 50);
				memcpy(_p_cbuffers[index]->GetData(), &m, sizeof(Matrix4x4f));
				cmd->DrawRenderer(Mesh::s_p_quad,light_icon_mat, _p_cbuffers[index++].get(), 0, 0, 1);
			}
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
			static const f32 s_axis_length = 50000.0f;
			Gizmo::DrawLine(Vector3f::kZero, Vector3f{ s_axis_length,0.0f,0.0f }, Colors::kRed);
			Gizmo::DrawLine(Vector3f::kZero, Vector3f{ 0.f,s_axis_length,0.0f }, Colors::kGreen);
			Gizmo::DrawLine(Vector3f::kZero, Vector3f{ 0.f,0.0f,s_axis_length }, Colors::kBlue);
		}
	}
	void GizmoPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------GizmoPass-------------------------------------------------------------

	//-------------------------------------------------------------CopyColorPass-------------------------------------------------------------
	CopyColorPass::CopyColorPass() : RenderPass("CopyColor")
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
			cmd->DrawRenderer(_p_quad_mesh, _p_blit_mat, 1, 1);
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

	//-------------------------------------------------------------CopyDepthPass-------------------------------------------------------------
	CopyDepthPass::CopyDepthPass() : RenderPass("CopyDepthPass")
	{
		_p_blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
		//_p_obj_cb = ConstantBuffer::Create(256);
		//memcpy(_p_obj_cb->GetData(), &BuildIdentityMatrix(), sizeof(Matrix4x4f));
	}
	CopyDepthPass::~CopyDepthPass()
	{
	}
	void CopyDepthPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get("CopyDepth");
		cmd->Clear();
		{
			ProfileBlock profile(cmd.get(), _name);
			//cmd->SetRenderTarget(rendering_data._camera_depth_target_handle);
			cmd->SetRenderTarget(rendering_data._camera_depth_tex_handle);
			_p_blit_mat->SetTexture("_SourceTex", rendering_data._camera_depth_target_handle);
			cmd->DrawRenderer(Mesh::s_p_quad, _p_blit_mat, 1,1);
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void CopyDepthPass::BeginPass(GraphicsContext* context)
	{
	}
	void CopyDepthPass::EndPass(GraphicsContext* context)
	{
		_p_blit_mat->SetTexture("_SourceTex", nullptr);
	}
	//-------------------------------------------------------------CopyDepthPass-------------------------------------------------------------
}
