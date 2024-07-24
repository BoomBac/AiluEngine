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

#include "Objects/CameraComponent.h"

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
			for (auto& it : all_renderable)
			{
				auto& [queue, objs] = it;
				if (queue >= Shader::kRenderQueueTransparent)
				{
					for (auto& obj : objs)
					{
						cmd->DrawRenderer(obj._mesh, obj._material, rendering_data._p_per_object_cbuf[obj._scene_id], obj._submesh_index, 0, obj._instance_count);
					}
				}
			}
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void ForwardPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------

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
					u16 dsv_rt_index = _p_mainlight_shadow_map->CalculateViewIndex(Texture::ETextureViewType::kDSV, 0, i);
					cmd->SetRenderTarget(nullptr, _p_mainlight_shadow_map.get(),0, dsv_rt_index);
					cmd->ClearRenderTarget(_p_mainlight_shadow_map.get(), dsv_rt_index,1.0f);
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
					u16 dsv_rt_index = _p_addlight_shadow_maps->CalculateViewIndex(Texture::ETextureViewType::kDSV, 0, i);
					cmd->SetRenderTarget(nullptr, _p_addlight_shadow_maps.get(), 0, dsv_rt_index);
					cmd->ClearRenderTarget(_p_addlight_shadow_maps.get(), dsv_rt_index, 1.0f);
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
						u16 dsv_rt_index = _p_point_light_shadow_maps->CalculateViewIndex(Texture::ETextureViewType::kDSV,(ECubemapFace::ECubemapFace)(j+1), 0, i);
						cmd->SetRenderTarget(nullptr, _p_point_light_shadow_maps.get(), 0, dsv_rt_index);
						cmd->ClearRenderTarget(_p_point_light_shadow_maps.get(), dsv_rt_index, 1.0f);
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
		_per_obj_cb.reset(IConstantBuffer::Create(RenderConstants::kPeObjectDataSize));
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
			_per_pass_cb[i].reset(IConstantBuffer::Create(RenderConstants::kPePassDataSize));
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
				u16 rt_index = _src_cubemap->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace::ECubemapFace)(i + 1), 0, 0);
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
				u16 rt_index = _radiance_map->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace::ECubemapFace)(i + 1), 0, 0);
				cmd->SetRenderTarget(_radiance_map.get(), rt_index);
				cmd->ClearRenderTarget(_radiance_map.get(), Colors::kBlack, rt_index);
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
					u16 rt_index = _prefilter_cubemap->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace::ECubemapFace)(i + 1), j, 0);
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
	DeferredGeometryPass::DeferredGeometryPass() : RenderPass("DeferedGeometryPass")
	{

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
		cmd->Dispatch(_brdflut_gen.get(), _brdflut_gen->FindKernel("cs_main"),8, 8, 1);
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
			cmd->SetRenderTarget(rendering_data._camera_color_target_handle,rendering_data._camera_depth_target_handle);
			cmd->ClearRenderTarget(rendering_data._camera_color_target_handle, Colors::kBlack);
			cmd->DrawFullScreenQuad(_p_lighting_material.get());
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
		_p_lut_gen = ComputeShader::Create(PathUtils::GetResSysPath(L"Shaders/hlsl/Compute/atmosphere_lut_gen.hlsl"));
		//_p_skybox_material = MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/skybox._pp.alasset"), "SkyboxPP");
		_p_skybox_material = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/skybox.alasset"), "Skybox");
		_p_sky_mesh = Mesh::s_p_shpere;
		Matrix4x4f world_mat;
		MatrixScale(world_mat, 1000.f, 1000.f, 1000.f);
		_p_cbuffer.reset(IConstantBuffer::Create(RenderConstants::kPeObjectDataSize));
		memcpy(_p_cbuffer->GetData(), &world_mat, RenderConstants::kPeObjectDataSize);
	}
	void SkyboxPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		u16 transmittance_lut_gen_kernel = _p_lut_gen->FindKernel("TransmittanceGen");
		u16 mult_scatter_lut_gen_kernel = _p_lut_gen->FindKernel("MultiScattGen");
		u16 sky_lut_gen_kernel = _p_lut_gen->FindKernel("SkyLightGen");

		auto cmd = CommandBufferPool::Get("SkyboxPass");
		cmd->Clear();
		{
			ProfileBlock profile(cmd.get(), _name);
			auto t_lut = cmd->GetTempRT(_transmittance_lut_size.x,_transmittance_lut_size.y,"_TransmittanceLUT",ERenderTargetFormat::kRGBAHalf,false,false,true);
			auto ms_lut = cmd->GetTempRT(_mult_scatter_lut_size.x,_mult_scatter_lut_size.y,"_MultScatterLUT",ERenderTargetFormat::kRGBAHalf,false,false,true);
			auto sv_lut = cmd->GetTempRT(_sky_lut_size.x,_sky_lut_size.y,"_SkyLightLUT",ERenderTargetFormat::kRGBAHalf,false,false,true);

			_p_lut_gen->SetTexture("_TransmittanceLUT",t_lut);
			cmd->Dispatch(_p_lut_gen.get(),transmittance_lut_gen_kernel,_transmittance_lut_size.x / 16, _transmittance_lut_size.y / 16, 1);

			_p_lut_gen->SetTexture("_TexTransmittanceLUT",t_lut);
			_p_lut_gen->SetTexture("_MultScatterLUT",ms_lut);
			cmd->Dispatch(_p_lut_gen.get(),mult_scatter_lut_gen_kernel,_mult_scatter_lut_size.x / 16, _mult_scatter_lut_size.y / 16, 1);

			_p_lut_gen->SetTexture("_TexTransmittanceLUT",t_lut);
			_p_lut_gen->SetTexture("_TexMultScatterLUT",ms_lut);
			_p_lut_gen->SetTexture("_SkyLightLUT",sv_lut);
			_p_lut_gen->SetVector("_MainLightPosition",rendering_data._mainlight_world_position);
			cmd->Dispatch(_p_lut_gen.get(),sky_lut_gen_kernel,_sky_lut_size.x / 16, _sky_lut_size.y / 16, 1);

			//cmd->SetViewport(rendering_data._viewport);
			//cmd->SetScissorRect(rendering_data._scissor_rect);
			_p_skybox_material->SetTexture("_TexSkyViewLUT",sv_lut);
			cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
			cmd->DrawRenderer(_p_sky_mesh, _p_skybox_material.get(), _p_cbuffer.get(), 0, 1);

			cmd->ReleaseTempRT(t_lut);
			cmd->ReleaseTempRT(ms_lut);
			cmd->ReleaseTempRT(sv_lut);
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
			_p_cbuffers.push_back(std::unique_ptr<IConstantBuffer>(IConstantBuffer::Create(256)));
		}
		auto grid_plane_pos = MatrixScale(1000.0f,1000.f,1000.f);
		memcpy(_p_cbuffers[0]->GetData(), &grid_plane_pos, sizeof(Matrix4x4f));
	}
	void GizmoPass::Execute(GraphicsContext* context, RenderingData& rendering_data)
	{
		auto cmd = CommandBufferPool::Get("GizmoPass");
        static auto mat_point_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/PointLightBillboard");
        static auto mat_directional_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/DirectionalLightBillboard");
        static auto mat_spot_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/SpotLightBillboard");
        static auto mat_camera_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/CameraBillboard");
        static auto mat_gird_plane = g_pResourceMgr->Get<Material>(L"Runtime/Material/GridPlane");
		cmd->Clear();
		{
			ProfileBlock profile(cmd.get(), _name);
			cmd->SetViewport(rendering_data._viewport);
			cmd->SetScissorRect(rendering_data._scissor_rect);
			cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
			GraphicsPipelineStateMgr::s_gizmo_pso->Bind(cmd.get());
			GraphicsPipelineStateMgr::s_gizmo_pso->SetPipelineResource(cmd.get(), Shader::GetPerFrameConstBuffer(), EBindResDescType::kConstBuffer);
			Gizmo::Submit(cmd.get());
			cmd->DrawRenderer(Mesh::s_p_plane,mat_gird_plane,_p_cbuffers[0].get(),0,0,1);
			u16 index = 1;
			for (auto it : g_pSceneMgr->_p_current->GetAllComponents())
			{
				if (index >= 10)
					break;
                if(it == nullptr)
                    continue ;
                auto light_comp = dynamic_cast<LightComponent*>(it);
                auto world_pos = it->GetOwner()->GetComponent<TransformComponent>()->GetPosition();
                auto m = MatrixTranslation(world_pos);
                f32 scale = dynamic_cast<SceneActor*>(it->GetOwner())->BaseAABB().Size().x;
                m = MatrixScale(scale, scale, scale) * m;
                memcpy(_p_cbuffers[index]->GetData(), &m, sizeof(Matrix4x4f));
                if(light_comp)
                {
                    switch (light_comp->LightType())
                    {
                        case ELightType::kDirectional:
                            cmd->DrawRenderer(Mesh::s_p_quad,mat_directional_light, _p_cbuffers[index++].get(), 0, 0, 1);
                            break;
                        case ELightType::kPoint:
                            cmd->DrawRenderer(Mesh::s_p_quad,mat_point_light, _p_cbuffers[index++].get(), 0, 0, 1);
                            break;
                        case ELightType::kSpot:
                            cmd->DrawRenderer(Mesh::s_p_quad,mat_spot_light, _p_cbuffers[index++].get(), 0, 0, 1);
                            break;
                    }
                    continue;
                }
                auto cam_comp = dynamic_cast<CameraComponent*>(it);
                if(cam_comp)
                {
                    cmd->DrawRenderer(Mesh::s_p_quad,mat_camera_light, _p_cbuffers[index++].get(), 0, 0, 1);
                }
			}
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}
	void GizmoPass::BeginPass(GraphicsContext* context)
	{
		// if (Gizmo::s_color.a > 0.0f)
		// {
		// 	int gridSize = 100;
		// 	int gridSpacing = 100;
		// 	Vector3f cameraPosition = Camera::sCurrent->Position();
		// 	float grid_alpha = lerpf(0.0f, 1.0f, abs(cameraPosition.y) / 2000.0f);
		// 	Color32 grid_color = Colors::kWhite;
		// 	grid_color.a = 1.0f - grid_alpha;
		// 	if (grid_color.a > 0)
		// 	{
		// 		Vector3f grid_center_mid(static_cast<float>(static_cast<int>(cameraPosition.x / gridSpacing) * gridSpacing),
		// 			0.0f,
		// 			static_cast<float>(static_cast<int>(cameraPosition.z / gridSpacing) * gridSpacing));
		// 		Gizmo::DrawGrid(100, 100, grid_center_mid, grid_color);
		// 	}
		// 	grid_color.a = grid_alpha;
		// 	if (grid_color.a > 0.7f)
		// 	{
		// 		gridSize = 10;
		// 		gridSpacing = 1000;
		// 		Vector3f grid_center_large(static_cast<float>(static_cast<int>(cameraPosition.x / gridSpacing) * gridSpacing),
		// 			0.0f,
		// 			static_cast<float>(static_cast<int>(cameraPosition.z / gridSpacing) * gridSpacing));
		// 		//grid_color = Colors::kGreen;
		// 		Gizmo::DrawGrid(10, 1000, grid_center_large, grid_color);
		// 	}
		// 	static const f32 s_axis_length = 50000.0f;
		// 	Gizmo::DrawLine(Vector3f::kZero, Vector3f{ s_axis_length,0.0f,0.0f }, Colors::kRed);
		// 	Gizmo::DrawLine(Vector3f::kZero, Vector3f{ 0.f,s_axis_length,0.0f }, Colors::kGreen);
		// 	Gizmo::DrawLine(Vector3f::kZero, Vector3f{ 0.f,0.0f,s_axis_length }, Colors::kBlue);
		// }
	}
	void GizmoPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------GizmoPass-------------------------------------------------------------

	//-------------------------------------------------------------CopyColorPass-------------------------------------------------------------
	CopyColorPass::CopyColorPass() : RenderPass("CopyColor")
	{
		_p_blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
		_p_obj_cb = IConstantBuffer::Create(256);
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
			cmd->Blit(rendering_data._camera_color_target_handle, rendering_data._camera_opaque_tex_handle);
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
			//cmd->SetRenderTarget(rendering_data._camera_depth_tex_handle);
			//_p_blit_mat->SetTexture("_SourceTex", rendering_data._camera_depth_target_handle);
			//cmd->DrawRenderer(Mesh::s_p_quad, _p_blit_mat, 1,1);
			cmd->Blit(rendering_data._camera_depth_target_handle, rendering_data._camera_depth_tex_handle);
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
