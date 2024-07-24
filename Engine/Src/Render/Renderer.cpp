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
	Renderer::Renderer()
	{
		_p_context = g_pGfxContext;
		_b_init = true;
		Profiler::g_Profiler.Initialize();
		_p_timemgr = new TimeMgr();
		_p_timemgr->Initialize();

		//_p_cubemap_gen_pass = MakeScope<CubeMapGenPass>(512, "pure_sky", "Textures/small_cave_1k.alasset");
		//_p_task_render_passes.emplace_back(_p_cubemap_gen_pass.get());

		_p_per_frame_cbuf.reset(IConstantBuffer::Create(RenderConstants::kPerFrameDataSize));
		Shader::ConfigurePerFrameConstBuffer(_p_per_frame_cbuf.get());
		for (int i = 0; i < RenderConstants::kMaxRenderObjectCount; i++)
			_p_per_object_cbufs[i] = IConstantBuffer::Create(RenderConstants::kPeObjectDataSize);
		//_rendering_data._width = _width;
		//_rendering_data._height = _height;
		//_rendering_data._viewport = Rect{ 0,0,(uint16_t)_width,(uint16_t)_height };
		//_rendering_data._scissor_rect = _rendering_data._viewport;
		//_rendering_data._camera_color_target_handle = _camera_color_handle;
		//_rendering_data._camera_depth_target_handle = _camera_depth_handle;
		//_rendering_data._camera_depth_tex_handle = _camera_depth_tex_handle;
		//_rendering_data._final_rt_handle = _gameview_rt_handle;
		_rendering_data._gbuffers.resize(3);
		_render_passes.emplace_back(MakeScope<ShadowCastPass>());
		_render_passes.emplace_back(MakeScope<DeferredGeometryPass>());
		_render_passes.emplace_back(MakeScope<CopyDepthPass>());
		_render_passes.emplace_back(MakeScope<DeferredLightingPass>());
		_render_passes.emplace_back(MakeScope<SkyboxPass>());
		_render_passes.emplace_back(MakeScope<ForwardPass>());
		_render_passes.emplace_back(MakeScope<SSAOPass>());
		_render_passes.emplace_back(MakeScope<CopyColorPass>());
		_render_passes.emplace_back(MakeScope<PostProcessPass>());
		//_render_passes.emplace_back(_p_gizmo_pass.get());
		//_render_passes.emplace_back(_p_reslove_pass.get());
		RegisterEventBeforeTick([]() {GraphicsPipelineStateMgr::UpdateAllPSOObject(); });
		RegisterEventAfterTick([]() {Profiler::g_Profiler.EndFrame(); });
	}

	Renderer::~Renderer()
	{
		_p_timemgr->Finalize();
		DESTORY_PTR(_p_timemgr);
		for (int i = 0; i < RenderConstants::kMaxRenderObjectCount; i++)
			DESTORY_PTR(_p_per_object_cbufs[i]);
	}

	void Renderer::Render(const Camera& cam, Scene& s)
	{
		RenderingStates::Reset();
		ModuleTimeStatics::RenderDeltatime = _p_timemgr->GetElapsedSinceLastMark();
		_p_timemgr->Mark();
		for (auto& e : _events_before_tick)
		{
			e();
		}
		BeginScene(cam,s);
		{
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
			//Shader::SetGlobalTexture("RadianceTex", _p_cubemap_gen_pass->_radiance_map.get());
			//Shader::SetGlobalTexture("PrefilterEnvTex", _p_cubemap_gen_pass->_prefilter_cubemap.get());

			for (auto& pass : _render_passes)
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
		EndScene();
		for (auto& e : _events_after_tick)
		{
			e();
		}
	}

    void Renderer::EnqueuePass(ERenderPassEvent::ERenderPassEvent event, Scope<RenderPass> pass)
    {
        if (event == ERenderPassEvent::kAfterPostprocess)
        {
            auto it = std::find_if(_render_passes.begin(), _render_passes.end(), [](Scope<RenderPass>& p) {
                                       return dynamic_cast<PostProcessPass*>(p.get()) != nullptr;
                                   });
            it++;
            _render_passes.emplace(it, std::move(pass));
        }
        else if (event == ERenderPassEvent::kBeforePostprocess)
        {
            auto it = std::find_if(_render_passes.begin(), _render_passes.end(), [](Scope<RenderPass>& p) {
                                       return dynamic_cast<PostProcessPass*>(p.get()) != nullptr;
                                   });
            it;
            _render_passes.emplace(it, std::move(pass));
        }
    }

	void Renderer::BeginScene(const Camera& cam, const Scene& s)
	{
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
		u32 pixel_width = cam.Rect().x, pixel_height = cam.Rect().y;
		//PrepareShaderConstants
		{
			_per_frame_cbuf_data._ScreenParams.x = (f32)pixel_width;
			_per_frame_cbuf_data._ScreenParams.y = (f32)pixel_height;
			_per_frame_cbuf_data._ScreenParams.z = 1.0f / (f32)pixel_width;
			_per_frame_cbuf_data._ScreenParams.w = 1.0f / (f32)pixel_height;
		}
		{
			RenderTexture::ReleaseTempRT(_camera_color_handle);
			RenderTexture::ReleaseTempRT(_camera_depth_handle);
			RenderTexture::ReleaseTempRT(_gameview_rt_handle);
			RenderTexture::ReleaseTempRT(_rendering_data._camera_opaque_tex_handle);
			RenderTexture::ReleaseTempRT(_camera_depth_tex_handle);
			for (u16 i = 0; i < _rendering_data._gbuffers.size(); i++)
			{
				RenderTexture::ReleaseTempRT(_rendering_data._gbuffers[i]);
			}
			_camera_color_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraColorAttachment", ERenderTargetFormat::kDefaultHDR);
			//if (_is_offscreen)
			//{
			//	_gameview_rt_handle = RenderTexture::GetTempRT(pixel_width, _height, "CameraColorFinalAttachment", ERenderTargetFormat::kDefaultHDR);
			//}
			_camera_depth_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraDepthAttachment", ERenderTargetFormat::kDepth);
			_camera_depth_tex_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraDepthTexture", ERenderTargetFormat::kRFloat);
			_rendering_data._camera_opaque_tex_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraColorOpaqueTex", ERenderTargetFormat::kDefaultHDR);
			_rendering_data._gbuffers[0] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer0", ERenderTargetFormat::kRGHalf);
			_rendering_data._gbuffers[1] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer1", ERenderTargetFormat::kDefault);
			_rendering_data._gbuffers[2] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer2", ERenderTargetFormat::kDefault);
		}
		PrepareCamera(cam);
		PrepareScene(*g_pSceneMgr->_p_current);
		PrepareLight(*g_pSceneMgr->_p_current);
		_rendering_data._p_per_frame_cbuf = _p_per_frame_cbuf.get();
		_rendering_data._p_per_object_cbuf = _p_per_object_cbufs;
		memcpy(_p_per_frame_cbuf->GetData(), &_per_frame_cbuf_data, sizeof(ScenePerFrameData));
		_rendering_data._width = pixel_width;
		_rendering_data._height = pixel_height;
		_rendering_data._viewport = Rect{ 0,0,(uint16_t)pixel_width,(uint16_t)pixel_height };
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

	void Renderer::PrepareScene(const Scene& s)
	{
		u16 obj_index = 0;
		for (auto static_mesh : s.GetAllStaticRenderable())
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
		_per_frame_cbuf_data.g_IndirectLightingIntensity = s._light_data._indirect_lighting_intensity;
	}

	void Renderer::PrepareLight(const Scene& s)
	{
		const static f32 s_shadow_bias_factor = 0.01f;
		auto& light_comps = s.GetAllLight();
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
				_per_frame_cbuf_data._MainlightWorldPosition = light_data._light_dir;//Normalize(light_data._light_dir);
				_rendering_data._mainlight_world_position = _per_frame_cbuf_data._MainlightWorldPosition;
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

	void Renderer::PrepareCamera(const Camera& cam)
	{
		_per_frame_cbuf_data._CameraPos = cam.Position();
		_per_frame_cbuf_data._MatrixV = cam.GetView();
		_per_frame_cbuf_data._MatrixP = cam.GetProjection();
		_per_frame_cbuf_data._MatrixVP = _per_frame_cbuf_data._MatrixV * _per_frame_cbuf_data._MatrixP;
		_per_frame_cbuf_data._MatrixIVP = MatrixInverse(_per_frame_cbuf_data._MatrixVP);
		_rendering_data._cull_results = &cam.CullResults();
	}
}