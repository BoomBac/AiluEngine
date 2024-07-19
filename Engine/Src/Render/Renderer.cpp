#include "pch.h"
#include "Render/Renderer.h"
#include "Framework/Common/SceneMgr.h"
#include "Render/RenderingData.h"
#include "Render/Gizmo.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Framework/Common/Profiler.h"


namespace Ailu
{
	int Renderer::Initialize()
	{
		return Initialize(1600, 900);
	}
	int Renderer::Initialize(u32 width, u32 height)
	{
		_width = width;
		_height = height;
		_p_context = g_pGfxContext;
		_b_init = true;
		Profiler::g_Profiler.Initialize();
		_p_timemgr = new TimeMgr();
		_p_timemgr->Initialize();

		_camera_color_handle = RenderTexture::GetTempRT(_width, _height, "CameraColorAttachment", ERenderTargetFormat::kDefaultHDR);
		if (_is_offscreen)
		{
			_gameview_rt_handle = RenderTexture::GetTempRT(_width, _height, "CameraColorFinalAttachment", ERenderTargetFormat::kDefaultHDR);
		}

		_camera_depth_handle = RenderTexture::GetTempRT(_width, _height, "CameraDepthAttachment", ERenderTargetFormat::kDepth);
		//Depth格式的纹理会绑定DSV，DSV视图无法绑定到color target，但是这个纹理需要用作blit的目标，所以使用R32格式
		_camera_depth_tex_handle = RenderTexture::GetTempRT(_width, _height, "CameraDepthTexture", ERenderTargetFormat::kRFloat);
		_rendering_data._camera_opaque_tex_handle = RenderTexture::GetTempRT(_width, _height, "CameraColorOpaqueTex", ERenderTargetFormat::kDefaultHDR);
		_rendering_data._gbuffers.resize(3);
		_rendering_data._gbuffers[0] = RenderTexture::GetTempRT(_width, _height, "GBuffer0", ERenderTargetFormat::kRGHalf);
		_rendering_data._gbuffers[1] = RenderTexture::GetTempRT(_width, _height, "GBuffer1", ERenderTargetFormat::kDefault);
		_rendering_data._gbuffers[2] = RenderTexture::GetTempRT(_width, _height, "GBuffer2", ERenderTargetFormat::kDefault);

		_p_forward_pass = MakeScope<ForwardPass>();
		_p_reslove_pass = MakeScope<ResolvePass>();
		_p_shadowcast_pass = MakeScope<ShadowCastPass>();
		_p_postprocess_pass = MakeScope<PostProcessPass>();
		_p_gbuffer_pass = MakeScope<DeferredGeometryPass>(_width, _height);
		_p_lighting_pass = MakeScope<DeferredLightingPass>();
		_p_skybox_pass = MakeScope<SkyboxPass>();
		_p_gizmo_pass = MakeScope<GizmoPass>();
		_p_copycolor_pass = MakeScope<CopyColorPass>();
		_p_copydepth_pass = MakeScope<CopyDepthPass>();

		_p_cubemap_gen_pass = MakeScope<CubeMapGenPass>(512, "pure_sky", "Textures/small_cave_1k.alasset");
		_p_task_render_passes.emplace_back(_p_cubemap_gen_pass.get());

		_p_per_frame_cbuf.reset(ConstantBuffer::Create(RenderConstants::kPerFrameDataSize));
		Shader::ConfigurePerFrameConstBuffer(_p_per_frame_cbuf.get());
		for (int i = 0; i < RenderConstants::kMaxRenderObjectCount; i++)
			_p_per_object_cbufs[i] = ConstantBuffer::Create(RenderConstants::kPeObjectDataSize);
		_rendering_data._width = _width;
		_rendering_data._height = _height;
		_rendering_data._viewport = Rect{ 0,0,(uint16_t)_width,(uint16_t)_height };
		_rendering_data._scissor_rect = _rendering_data._viewport;
		_rendering_data._camera_color_target_handle = _camera_color_handle;
		_rendering_data._camera_depth_target_handle = _camera_depth_handle;
		_rendering_data._camera_depth_tex_handle = _camera_depth_tex_handle;
		_rendering_data._final_rt_handle = _gameview_rt_handle;

		_render_passes.emplace_back(_p_shadowcast_pass.get());
		//_p_opaque_pass->SetActive(false);
		//_render_passes.emplace_back(_p_opaque_pass.get());
		_render_passes.emplace_back(_p_gbuffer_pass.get());
		_render_passes.emplace_back(_p_copydepth_pass.get());
		_render_passes.emplace_back(_p_lighting_pass.get());
		_render_passes.emplace_back(_p_skybox_pass.get());
		_render_passes.emplace_back(_p_forward_pass.get());
		_render_passes.emplace_back(_p_copycolor_pass.get());
		_render_passes.emplace_back(_p_postprocess_pass.get());
		_render_passes.emplace_back(_p_gizmo_pass.get());
		_render_passes.emplace_back(_p_reslove_pass.get());
		RegisterEventBeforeTick([]() {GraphicsPipelineStateMgr::UpdateAllPSOObject(); });
		RegisterEventAfterTick([]() {Profiler::g_Profiler.EndFrame(); });
		_p_radiance_tex = _p_cubemap_gen_pass->_radiance_map.get();
		_p_prefilter_env_tex = _p_cubemap_gen_pass->_prefilter_cubemap.get();
		_p_env_tex = _p_cubemap_gen_pass->_src_cubemap.get();
		return 0;
	}
	void Renderer::Finalize()
	{
		INIT_CHECK(this, Renderer);
		_p_timemgr->Finalize();
		DESTORY_PTR(_p_timemgr);
		for (int i = 0; i < RenderConstants::kMaxRenderObjectCount; i++)
			DESTORY_PTR(_p_per_object_cbufs[i]);
	}
	void Renderer::Tick(const float& delta_time)
	{
		INIT_CHECK(this, Renderer);
		RenderingStates::Reset();
		ModuleTimeStatics::RenderDeltatime = _p_timemgr->GetElapsedSinceLastMark();
		_p_timemgr->Mark();
		for (auto& e : _events_before_tick)
		{
			e();
		}
		BeginScene();
		Render();
		EndScene();
		for (auto& e : _events_after_tick)
		{
			e();
		}
	}

	void Renderer::BeginScene()
	{
		if(!_resize_events.empty())
			DoResize();
		memset(reinterpret_cast<void*>(&_per_frame_cbuf_data), 0, sizeof(ScenePerFrameData));
		_rendering_data._shadow_data[0]._shadow_index = -1;
		for (int i = 0; i < RenderConstants::kMaxSpotLightNum; i++)
		{
			_per_frame_cbuf_data._SpotLights[i]._ShadowDataIndex = -1;
			_rendering_data._shadow_data[i + RenderConstants::kMaxCascadeShadowMapSplitNum]._shadow_index = -1;
		}
		for (int i = 0; i < RenderConstants::kMaxPointLightNum; i++)
		{
			_per_frame_cbuf_data._PointLights[i]._ShadowDataIndex = -1;
			_rendering_data._point_shadow_data[i]._camera_far = 0; //标记不投影
		}
		PrepareShaderConstants();
		PrepareCamera(_p_scene_camera);
		PrepareScene(g_pSceneMgr->_p_current);
		PrepareLight(g_pSceneMgr->_p_current);
		_rendering_data._p_per_frame_cbuf = _p_per_frame_cbuf.get();
		_rendering_data._p_per_object_cbuf = _p_per_object_cbufs;
		memcpy(_p_per_frame_cbuf->GetData(), &_per_frame_cbuf_data, sizeof(ScenePerFrameData));
		_rendering_data._width = _width;
		_rendering_data._height = _height;
		_rendering_data._viewport = Rect{ 0,0,(uint16_t)_width,(uint16_t)_height };
		_rendering_data._scissor_rect = _rendering_data._viewport;
		_rendering_data._camera_color_target_handle = _camera_color_handle;
		_rendering_data._camera_depth_target_handle = _camera_depth_handle;
		_rendering_data._camera_depth_tex_handle = _camera_depth_tex_handle;
		_rendering_data._final_rt_handle = _gameview_rt_handle;
	}
	void Renderer::EndScene()
	{
		_rendering_data.Reset();
		//Camera::sSelected = nullptr;
		Gizmo::EndFrame();
		//for (auto pass : _p_render_passes)
		//	pass->EndPass(_p_context);
		//for (auto pass : _p_task_render_passes)
		//	pass->EndPass(_p_context);
	}

	static u32 s_frame_index = 0u;
	float Renderer::GetDeltaTime() const
	{
		return _p_timemgr->GetElapsedSinceLastMark();
	}
	void Renderer::TakeCapture()
	{
		_captures.push(0);
	}
	void Renderer::SubmitTaskPass(Scope<RenderPass> task)
	{
		//_p_task_render_passes.emplace_back(task.get());
	}
	void Renderer::ResizeBuffer(u32 width, u32 height)
	{
		_resize_events.emplace(Vector2f{ (f32)width,(f32)height });
	}
	void Renderer::RegisterEventBeforeTick(BeforeTickEvent e)
	{
		_events_before_tick.emplace_back(e);
	}
	void Renderer::RegisterEventAfterTick(AfterTickEvent e)
	{
		_events_after_tick.emplace_back(e);
	}
	//void Renderer::RegisterEventAfterTick(AfterTickEvent e)
	//{
	//	events_after_tick.emplace_back(e);
	//}
	//void Renderer::UnRegisterEventBeforeTick(BeforeTickEvent e)
	//{
	//	_events_before_tick.remove(e);
	//}
	//void Renderer::UnRegisterEventAfterTick(AfterTickEvent e)
	//{
	//	_events_after_tick.remove(e);
	//}
	void Renderer::Render()
	{
		//TODO:第一帧不渲染，否则会有taskpass执行结果异常，原因未知
		if (s_frame_index++ > 0)
		{
			//_p_gbuffer_pass->SetActive(!_p_opaque_pass->IsActive());
			//_p_test_cs->SetTexture("gInputA", TexturePool::Get("Textures/MyImage01.jpg").get());
			//_p_test_cs->SetTexture("gOut", _p_test_texture.get());
			//cmd->Dispatch(_p_test_cs.get(), 4, 4, 1);

			if (!_captures.empty())
			{
				_p_context->TakeCapture();
				_captures.pop();
			}

			if (!_p_task_render_passes.empty())
			{
				for (auto task_pass : _p_task_render_passes)
				{
					task_pass->BeginPass(_p_context);
					task_pass->Execute(_p_context, _rendering_data);
				}
				_p_task_render_passes.clear();
			}

			//Shader::SetGlobalTexture("SkyBox", _p_cubemap_gen_pass->_src_cubemap.get());
			Shader::SetGlobalTexture("RadianceTex", _p_cubemap_gen_pass->_radiance_map.get());
			Shader::SetGlobalTexture("PrefilterEnvTex", _p_cubemap_gen_pass->_prefilter_cubemap.get());

			_p_reslove_pass->SetActive(true);

			for (auto pass : _render_passes)
			{
				if (pass->IsActive())
				{
					CPUProfileBlock cblock(pass->GetName());
					pass->BeginPass(_p_context);
					pass->Execute(_p_context, _rendering_data);
					pass->EndPass(_p_context);
				}
			}
		}
		_p_context->Present();
	}

	void Renderer::PrepareScene(Scene* p_scene)
	{
		u16 obj_index = 0;
		for (auto static_mesh : p_scene->GetAllStaticRenderable())
		{
			if (static_mesh->GetMesh())
			{
				auto& aabbs = static_mesh->GetAABB();
				Vector3f center;
				u16 submesh_count = static_mesh->GetMesh()->SubmeshCount();
				auto& materials = static_mesh->GetMaterials();
				for (int i = 0; i < submesh_count; i++)
				{
					memcpy(_p_per_object_cbufs[obj_index]->GetData(), &static_mesh->GetOwner()->GetComponent<TransformComponent>()->GetMatrix(), sizeof(Matrix4x4f));
					++obj_index;
				}
			}
		}
		f32 cascade_shaodw_max_dis = (f32)QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[QuailtySetting::s_cascade_shadow_map_count-1];
		_per_frame_cbuf_data._CascadeShadowParams = Vector4f((f32)QuailtySetting::s_cascade_shadow_map_count,
		1.0f / QuailtySetting::s_shadow_fade_out_factor,1.0f / cascade_shaodw_max_dis,0.f);
		_per_frame_cbuf_data.g_IndirectLightingIntensity = p_scene->_light_data._indirect_lighting_intensity;
	}

	void Renderer::PrepareLight(Scene* p_scene)
	{
		const static f32 s_shadow_bias_factor = 0.01f;
		auto& light_comps = p_scene->GetAllLight();
		uint16_t updated_light_num = 0u;
		uint16_t direction_light_index = 0, point_light_index = 0, spot_light_index = 0;
		u16 total_shadow_matrix_count = RenderConstants::kMaxCascadeShadowMapSplitNum;//阴影绘制和采样时来索引虚拟摄像机的矩阵，对于点光源，其采样不需要该值，只用作标志位确认是否需要处理阴影
		for (auto light : light_comps)
		{
			auto& light_data = light->_light;
			Color32 color = light_data._light_color;
			color.r *= color.a;
			color.g *= color.a;
			color.b *= color.a;
			bool is_exist_directional_shaodw = false;
			if (light->LightType() == ELightType::kDirectional)
			{
				_per_frame_cbuf_data._DirectionalLights[direction_light_index]._ShadowDataIndex = -1;
				if (!light->Active())
				{
					_per_frame_cbuf_data._DirectionalLights[direction_light_index]._LightDir = Vector3f::kZero;
					_per_frame_cbuf_data._DirectionalLights[direction_light_index]._LightColor = Colors::kBlack.xyz;
					continue;
				}
				if (light->CastShadow() && !is_exist_directional_shaodw)
				{
					for (int i = 0; i < QuailtySetting::s_cascade_shadow_map_count; i++)
					{
						auto shadow_cam = light->ShadowCamera(i);
						_per_frame_cbuf_data._DirectionalLights[direction_light_index]._ShadowDataIndex = i;
						_per_frame_cbuf_data._DirectionalLights[direction_light_index]._ShadowDistance = QuailtySetting::s_main_light_shaodw_distance;
						_per_frame_cbuf_data._ShadowMatrix[i] = shadow_cam->GetView() * shadow_cam->GetProjection();
						_rendering_data._shadow_data[i]._shadow_index = i;
						_rendering_data._shadow_data[i]._shadow_matrix = _per_frame_cbuf_data._ShadowMatrix[i];
						_rendering_data._shadow_data[i]._cull_results = &shadow_cam->CullResults();
						_per_frame_cbuf_data._CascadeShadowSplit[i] = light->CascadeShadowMData(i);
						_per_frame_cbuf_data._DirectionalLights[direction_light_index]._constant_bias = light->_shadow._constant_bias * light->_shadow._constant_bias * s_shadow_bias_factor;
						_per_frame_cbuf_data._DirectionalLights[direction_light_index]._slope_bias = light->_shadow._slope_bias * light->_shadow._slope_bias * s_shadow_bias_factor;
					}
					is_exist_directional_shaodw = true;
				}
				_per_frame_cbuf_data._DirectionalLights[direction_light_index]._LightColor = color.xyz;
				_per_frame_cbuf_data._DirectionalLights[direction_light_index]._LightDir = light_data._light_dir.xyz;
				++direction_light_index;
			}
			else if (light->LightType() == ELightType::kPoint)
			{
				_per_frame_cbuf_data._PointLights[point_light_index]._ShadowDataIndex = -1;
				if (!light->Active())
				{
					_per_frame_cbuf_data._PointLights[point_light_index]._LightParam0 = 0.0;
					_per_frame_cbuf_data._PointLights[point_light_index++]._LightColor = Colors::kBlack.xyz;
					continue;
				}
				if (light->CastShadow())
				{
					u32 shadow_index = total_shadow_matrix_count;
					_per_frame_cbuf_data._PointLights[point_light_index]._ShadowDataIndex = _rendering_data._addi_point_shadow_num;//点光源使用这个值来索引cubearray
					_per_frame_cbuf_data._PointLights[point_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
					_per_frame_cbuf_data._PointLights[point_light_index]._constant_bias = light->_shadow._constant_bias;
					_per_frame_cbuf_data._PointLights[point_light_index]._slope_bias = light->_shadow._slope_bias;
					//_per_frame_cbuf_data._PointLights[point_light_index]._ShadowNear = 10;
					for (int i = 0; i < 6; i++)
					{
						Camera* shadow_cam = light->ShadowCamera(i);
						_per_frame_cbuf_data._ShadowMatrix[shadow_index + i] = shadow_cam->GetView() * shadow_cam->GetProjection();
						_rendering_data._point_shadow_data[point_light_index]._shadow_indices[i] = shadow_index + i;
						_rendering_data._point_shadow_data[point_light_index]._cull_results[i] = &shadow_cam->CullResults();
						++total_shadow_matrix_count;
					}
					_rendering_data._point_shadow_data[point_light_index]._light_world_pos = light_data._light_pos.xyz;
					_rendering_data._point_shadow_data[point_light_index]._camera_near = 10;
					_rendering_data._point_shadow_data[point_light_index]._camera_far = light_data._light_param.x * 1.5f;
					++_rendering_data._addi_point_shadow_num;
				}
				_per_frame_cbuf_data._PointLights[point_light_index]._LightColor = color.xyz;
				_per_frame_cbuf_data._PointLights[point_light_index]._LightPos = light_data._light_pos.xyz;
				_per_frame_cbuf_data._PointLights[point_light_index]._LightParam0 = light_data._light_param.x;
				_per_frame_cbuf_data._PointLights[point_light_index++]._LightParam1 = light_data._light_param.y;
			}
			else if (light->LightType() == ELightType::kSpot)
			{
				_per_frame_cbuf_data._SpotLights[spot_light_index]._ShadowDataIndex = -1;
				if (!light->Active())
				{
					_per_frame_cbuf_data._SpotLights[spot_light_index++]._LightColor = Colors::kBlack.xyz;
					continue;
				}
				if (light->CastShadow())
				{
					u32 shadow_index = total_shadow_matrix_count++;
					_per_frame_cbuf_data._SpotLights[spot_light_index]._ShadowDataIndex = shadow_index;
					_per_frame_cbuf_data._SpotLights[spot_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
					_per_frame_cbuf_data._SpotLights[spot_light_index]._constant_bias = light->_shadow._constant_bias * light->_shadow._constant_bias * s_shadow_bias_factor;
					_per_frame_cbuf_data._SpotLights[spot_light_index]._slope_bias = light->_shadow._slope_bias * light->_shadow._slope_bias * s_shadow_bias_factor;
					Camera* shadow_cam = light->ShadowCamera();
					_per_frame_cbuf_data._ShadowMatrix[shadow_index] = shadow_cam->GetView() * shadow_cam->GetProjection();
					_rendering_data._shadow_data[shadow_index]._shadow_index = shadow_index;
					_rendering_data._shadow_data[shadow_index]._cull_results = &shadow_cam->CullResults();
					++_rendering_data._addi_shadow_num;
				}
				_per_frame_cbuf_data._SpotLights[spot_light_index]._LightColor = color.xyz;
				_per_frame_cbuf_data._SpotLights[spot_light_index]._LightPos = light_data._light_pos.xyz;
				_per_frame_cbuf_data._SpotLights[spot_light_index]._LightDir = light_data._light_dir.xyz;
				_per_frame_cbuf_data._SpotLights[spot_light_index]._Rdius = light_data._light_param.x;
				float inner_cos = cos(light_data._light_param.y / 2 * k2Radius);
				float outer_cos = cos(light_data._light_param.z / 2 * k2Radius);
				_per_frame_cbuf_data._SpotLights[spot_light_index]._LightAngleScale = 1.0f / std::max(0.001f,(inner_cos - outer_cos));
				_per_frame_cbuf_data._SpotLights[spot_light_index]._LightAngleOffset = -outer_cos * _per_frame_cbuf_data._SpotLights[spot_light_index]._LightAngleScale;
				++spot_light_index;
			}
			++updated_light_num;
		}
		if (updated_light_num == 0)
		{
			_per_frame_cbuf_data._DirectionalLights[0]._LightColor = Colors::kBlack.xyz;
			_per_frame_cbuf_data._DirectionalLights[0]._LightDir = { 0.0f,0.0f,0.0f };
		}
	}

	void Renderer::PrepareCamera(Camera* p_camera)
	{
		_p_scene_camera = Camera::sCurrent;
		_p_scene_camera->RecalculateMarix();
		_per_frame_cbuf_data._CameraPos = _p_scene_camera->Position();
		_per_frame_cbuf_data._MatrixV = _p_scene_camera->GetView();
		_per_frame_cbuf_data._MatrixP = _p_scene_camera->GetProjection();
		_per_frame_cbuf_data._MatrixVP = _per_frame_cbuf_data._MatrixV * _per_frame_cbuf_data._MatrixP;
		_per_frame_cbuf_data._MatrixIVP = MatrixInverse(_per_frame_cbuf_data._MatrixVP);
		_rendering_data._cull_results = &p_camera->CullResults();
	}

	void Renderer::PrepareShaderConstants()
	{
		_per_frame_cbuf_data._ScreenParams.x = (f32)_width;
		_per_frame_cbuf_data._ScreenParams.y = (f32)_height;
		_per_frame_cbuf_data._ScreenParams.z = 1.0f / (f32)_width;
		_per_frame_cbuf_data._ScreenParams.w = 1.0f / (f32)_height;
	}

	void Renderer::DoResize()
	{
		while (_resize_events.size() > 1)
		{
			_resize_events.pop();
		}
		auto new_size = _resize_events.front();
		_resize_events.pop();
		RenderTexture::ReleaseTempRT(_camera_color_handle);
		RenderTexture::ReleaseTempRT(_camera_depth_handle);
		RenderTexture::ReleaseTempRT(_gameview_rt_handle);
		RenderTexture::ReleaseTempRT(_rendering_data._camera_opaque_tex_handle);
		RenderTexture::ReleaseTempRT(_camera_depth_tex_handle);
		for (u16 i = 0; i < 3; i++)
		{
			RenderTexture::ReleaseTempRT(_rendering_data._gbuffers[i]);
		}
		_width =  static_cast<u32>(new_size.x);
		_height = static_cast<u32>(new_size.y);
		_camera_color_handle = RenderTexture::GetTempRT(_width, _height, "CameraColorAttachment", ERenderTargetFormat::kDefaultHDR);
		if (_is_offscreen)
		{
			_gameview_rt_handle = RenderTexture::GetTempRT(_width, _height, "CameraColorFinalAttachment", ERenderTargetFormat::kDefaultHDR);
		}
		_camera_depth_handle = RenderTexture::GetTempRT(_width, _height, "CameraDepthAttachment", ERenderTargetFormat::kDepth);
		_camera_depth_tex_handle = RenderTexture::GetTempRT(_width, _height, "CameraDepthTexture", ERenderTargetFormat::kRFloat);
		_rendering_data._camera_opaque_tex_handle = RenderTexture::GetTempRT(_width, _height, "CameraColorOpaqueTex", ERenderTargetFormat::kDefaultHDR);
		_rendering_data._gbuffers[0] = RenderTexture::GetTempRT(_width, _height, "GBuffer0", ERenderTargetFormat::kRGHalf);
		_rendering_data._gbuffers[1] = RenderTexture::GetTempRT(_width, _height, "GBuffer1", ERenderTargetFormat::kDefault);
		_rendering_data._gbuffers[2] = RenderTexture::GetTempRT(_width, _height, "GBuffer2", ERenderTargetFormat::kDefault);
		_p_scene_camera->Aspect(new_size.x / new_size.y);
		g_pLogMgr->LogFormat("Resize game view to {},{}",_width,_height);
	}
}