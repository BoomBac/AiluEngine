#include "pch.h"
#include "Render/Renderer.h"
#include "Framework/Common/SceneMgr.h"
#include "Render/RenderingData.h"
#include "Render/Gizmo.h"
#include "Framework/Common/ResourceMgr.h"


namespace Ailu
{
	int Renderer::Initialize()
	{
		_p_context = g_pGfxContext;
		_b_init = true;

		_p_timemgr = new TimeMgr();
		_p_timemgr->Initialize();
		_p_camera_color_attachment = RenderTexture::Create(kRendererWidth, kRendererHeight, "CameraColorAttachment", RenderConstants::kColorRange == EColorRange::kLDR ?
			RenderConstants::kLDRFormat : RenderConstants::kHDRFormat);
		_p_camera_depth_attachment = RenderTexture::Create(kRendererWidth, kRendererHeight, "CameraDepthAttachment", EALGFormat::kALGFormatD24S8_UINT);
		_p_opaque_pass = MakeScope<OpaquePass>();
		_p_reslove_pass = MakeScope<ResolvePass>(_p_camera_color_attachment);
		_p_shadowcast_pass = MakeScope<ShadowCastPass>();
		_p_postprocess_pass = MakeScope<PostProcessPass>();
		_p_gbuffer_pass = MakeScope<DeferredGeometryPass>(kRendererWidth, kRendererHeight);
		_p_skybox_pass = MakeScope<SkyboxPass>();
		auto tex = g_pResourceMgr->LoadTexture(WString{ ToWChar(EnginePath::kEngineTexturePath) } + L"small_cave_1k.hdr");
		TexturePool::Add("Textures/small_cave_1k.hdr", std::dynamic_pointer_cast<Texture2D>(tex));

		_p_cubemap_gen_pass = MakeScope<CubeMapGenPass>(512, "pure_sky", "Textures/small_cave_1k.hdr");
		_p_task_render_passes.emplace_back(_p_cubemap_gen_pass.get());
		//_p_test_cs = ComputeShader::Create(PathUtils::GetResSysPath("Shaders/Compute/cs_test.hlsl"));
		//TextureDesc desc(64,64,1,EALGFormat::kALGFormatR8G8B8A8_UNORM,EALGFormat::kALGFormatR8G8B8A8_UNORM,EALGFormat::kALGFormatR32_UINT,false);
		//_p_test_texture = Texture2D::Create(desc);
		//_p_test_texture->Name("cs_out");
		//TexturePool::Add("cs_out", std::dynamic_pointer_cast<Texture2D>(_p_test_texture));

		_p_per_frame_cbuf.reset(ConstantBuffer::Create(RenderConstants::kPerFrameDataSize));
		Shader::ConfigurePerFrameConstBuffer(_p_per_frame_cbuf.get());
		for (int i = 0; i < RenderConstants::kMaxRenderObjectCount; i++)
			_p_per_object_cbufs[i] = ConstantBuffer::Create(RenderConstants::kPeObjectDataSize);
		_rendering_data.width = kRendererWidth;
		_rendering_data.height = kRendererHeight;
		_rendering_data._p_camera_color_target = _p_camera_color_attachment;
		_rendering_data._p_camera_depth_target = _p_camera_depth_attachment;
		_rendering_data._viewport = Rect{ 0,0,(uint16_t)kRendererWidth,(uint16_t)kRendererHeight };
		_rendering_data._scissor_rect = _rendering_data._viewport;

		_render_passes.emplace_back(_p_shadowcast_pass.get());
		_p_opaque_pass->SetActive(false);
		_render_passes.emplace_back(_p_opaque_pass.get());
		_render_passes.emplace_back(_p_gbuffer_pass.get());
		_render_passes.emplace_back(_p_skybox_pass.get());
		_render_passes.emplace_back(_p_reslove_pass.get());
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
		BeginScene();
		Render();
		EndScene();
	}

	void Renderer::BeginScene()
	{
		memset(reinterpret_cast<void*>(&_per_frame_cbuf_data), 0, sizeof(ScenePerFrameData));
		_p_scene_camera = g_pSceneMgr->_p_current->GetActiveCamera();
		Camera::sCurrent = _p_scene_camera;
		PrepareCamera(_p_scene_camera);
		PrepareLight(g_pSceneMgr->_p_current);
		_rendering_data._p_per_frame_cbuf = _p_per_frame_cbuf.get();
		_rendering_data._p_per_object_cbuf = _p_per_object_cbufs;
		memcpy(_p_per_frame_cbuf->GetData(), &_per_frame_cbuf_data, sizeof(ScenePerFrameData));
	}
	void Renderer::EndScene()
	{
		_rendering_data.Reset();
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
	void Renderer::Render()
	{
		//TODO:第一帧不渲染，否则会有taskpass执行结果异常，原因未知
		if (s_frame_index++ > 0)
		{
			_p_gbuffer_pass->SetActive(!_p_opaque_pass->IsActive());
			//_p_test_cs->SetTexture("gInputA", TexturePool::Get("Textures/MyImage01.jpg").get());
			//_p_test_cs->SetTexture("gOut", _p_test_texture.get());
			//cmd->Dispatch(_p_test_cs.get(), 4, 4, 1);


			if (!_p_task_render_passes.empty())
			{
				for (auto task_pass : _p_task_render_passes)
				{
					task_pass->BeginPass(_p_context);
					task_pass->Execute(_p_context, _rendering_data);
				}
				_p_task_render_passes.clear();
			}

			Shader::SetGlobalTexture("SkyBox", _p_cubemap_gen_pass->_p_cube_map.get());
			Shader::SetGlobalTexture("DiffuseIBL", _p_cubemap_gen_pass->_p_env_map.get());

			for (auto pass : _render_passes)
			{
				if (pass->IsActive())
				{
					pass->BeginPass(_p_context);
					pass->Execute(_p_context, _rendering_data);
				}
			}



			//_p_opaque_pass->BeginPass(_p_context);
			//_p_opaque_pass->Execute(_p_context, _rendering_data);

			//_p_gbuffer_pass->BeginPass(_p_context);
			//_p_gbuffer_pass->Execute(_p_context, _rendering_data);


			//_p_reslove_pass->BeginPass(_p_context);
			//_p_reslove_pass->Execute(_p_context, _rendering_data);
		}


		DrawRendererGizmo();
		_p_context->Present();
	}
	void Renderer::DrawRendererGizmo()
	{
		if (Gizmo::s_color.a > 0.0f)
		{
			int gridSize = 100;
			int gridSpacing = 100;
			Vector3f cameraPosition = Camera::sCurrent->Position();
			float grid_alpha = lerpf(0.0f, 1.0f, abs(cameraPosition.y) / 2000.0f);
			Color grid_color = Colors::kWhite;
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

	void Renderer::PrepareLight(Scene* p_scene)
	{
		auto& light_comps = p_scene->GetAllLight();
		uint16_t updated_light_num = 0u;
		uint16_t direction_light_index = 0, point_light_index = 0, spot_light_index = 0;
		for (auto light : light_comps)
		{
			auto& light_data = light->_light;
			Color color = light_data._light_color;
			color.r *= color.a;
			color.g *= color.a;
			color.b *= color.a;
			if (light->_light_type == ELightType::kDirectional)
			{
				if (!light->Active())
				{
					_per_frame_cbuf_data._DirectionalLights[direction_light_index]._LightDir = Vector3f::kZero;
					_per_frame_cbuf_data._DirectionalLights[direction_light_index++]._LightColor = Colors::kBlack.xyz;
					continue;
				}
				_per_frame_cbuf_data._DirectionalLights[direction_light_index]._LightColor = color.xyz;
				_per_frame_cbuf_data._DirectionalLights[direction_light_index++]._LightDir = light_data._light_dir.xyz;
			}
			else if (light->_light_type == ELightType::kPoint)
			{
				if (!light->Active())
				{
					_per_frame_cbuf_data._PointLights[point_light_index]._LightParam0 = 0.0;
					_per_frame_cbuf_data._PointLights[point_light_index++]._LightColor = Colors::kBlack.xyz;
					continue;
				}
				_per_frame_cbuf_data._PointLights[point_light_index]._LightColor = color.xyz;
				_per_frame_cbuf_data._PointLights[point_light_index]._LightPos = light_data._light_pos.xyz;
				_per_frame_cbuf_data._PointLights[point_light_index]._LightParam0 = light_data._light_param.x;
				_per_frame_cbuf_data._PointLights[point_light_index++]._LightParam1 = light_data._light_param.y;
			}
			else if (light->_light_type == ELightType::kSpot)
			{
				if (!light->Active())
				{
					_per_frame_cbuf_data._SpotLights[spot_light_index++]._LightColor = Colors::kBlack.xyz;
					continue;
				}
				_per_frame_cbuf_data._SpotLights[spot_light_index]._LightColor = color.xyz;
				_per_frame_cbuf_data._SpotLights[spot_light_index]._LightPos = light_data._light_pos.xyz;
				_per_frame_cbuf_data._SpotLights[spot_light_index]._LightDir = light_data._light_dir.xyz;
				_per_frame_cbuf_data._SpotLights[spot_light_index]._Rdius = light_data._light_param.x;
				_per_frame_cbuf_data._SpotLights[spot_light_index]._InnerAngle = light_data._light_param.y;
				_per_frame_cbuf_data._SpotLights[spot_light_index]._OuterAngle = light_data._light_param.z;
			}
			++updated_light_num;
			if (light->CastShadow())
			{
				_rendering_data._shadow_data[_rendering_data._actived_shadow_count]._shadow_view = light->ShadowCamera()->GetView();
				_rendering_data._shadow_data[_rendering_data._actived_shadow_count]._shadow_proj = light->ShadowCamera()->GetProjection();
				_rendering_data._shadow_data[_rendering_data._actived_shadow_count++]._shadow_bias = light->_shadow._depth_bias;
			}
		}
		if (updated_light_num == 0)
		{
			_per_frame_cbuf_data._DirectionalLights[0]._LightColor = Colors::kBlack.xyz;
			_per_frame_cbuf_data._DirectionalLights[0]._LightDir = { 0.0f,0.0f,0.0f };
		}
		_per_frame_cbuf_data._MainLightShadowMatrix = _rendering_data._shadow_data[0]._shadow_view * _rendering_data._shadow_data[0]._shadow_proj;
	}

	void Renderer::PrepareCamera(Camera* p_camera)
	{
		//_p_scene_camera->RecalculateMarix();
		_per_frame_cbuf_data._CameraPos = _p_scene_camera->Position();
		_per_frame_cbuf_data._MatrixV = _p_scene_camera->GetView();
		_per_frame_cbuf_data._MatrixP = _p_scene_camera->GetProjection();
		_per_frame_cbuf_data._MatrixVP = _per_frame_cbuf_data._MatrixV * _per_frame_cbuf_data._MatrixP;
		auto vp = _per_frame_cbuf_data._MatrixVP;
		MatrixInverse(vp);
		_per_frame_cbuf_data._MatrixIVP = vp;
	}
}