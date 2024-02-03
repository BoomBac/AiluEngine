#include "pch.h"
#include "Render/Renderer.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/SceneMgr.h"
#include "Render/RenderCommand.h"
#include "Render/RenderingData.h"
#include "Render/Gizmo.h"
#include "Framework/Common/ResourceMgr.h"
#include "Objects/StaticMeshComponent.h"
#include "Framework/Parser/AssetParser.h"
#include <Framework/Common/LogMgr.h>
#include "Render/RenderQueue.h"
#include "Animation/Skeleton.h"

namespace Ailu
{
	static Skeleton s_test_sk;
	static Vector<Vector<Matrix4x4f>> s_anim_mat;
	int Renderer::Initialize()
	{
		_p_context = g_pGfxContext.get();
		_b_init = true;

		_p_timemgr = new TimeMgr();
		_p_timemgr->Initialize();
		_p_per_frame_cbuf_data = static_cast<D3DContext*>(_p_context)->GetPerFrameCbufDataStruct();
		_p_camera_color_attachment = RenderTexture::Create(1600, 900, "CameraColorAttachment", RenderConstants::kColorRange == EColorRange::kLDR ? 
			RenderConstants::kLDRFormat : RenderConstants::kHDRFormat);
		_p_camera_depth_attachment = RenderTexture::Create(1600, 900, "CameraDepthAttachment", EALGFormat::kALGFormatD24S8_UINT);
		_p_opaque_pass = MakeScope<OpaquePass>();
		_p_reslove_pass = MakeScope<ResolvePass>(_p_camera_color_attachment);
		_p_shadowcast_pass = MakeScope<ShadowCastPass>();
		auto tex0 =g_pResourceMgr->LoadTexture(EnginePath::kEngineTexturePath + "small_cave_1k.hdr", "SmallCave");
		TexturePool::Add(tex0->AssetPath(), tex0);
		auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);

		//MeshPool::AddMesh("anim", parser->Parser(GetResPath("Meshs/anim.fbx")).front());
		//static_cast<FbxParser*>(parser.get())->Parser(GetResPath("Meshs/anim.fbx"), s_test_sk,s_anim_mat);

		_p_cubemap_gen_pass = MakeScope<CubeMapGenPass>(512,"pure_sky","Textures/small_cave_1k.hdr");
		_p_task_render_passes.emplace_back(_p_cubemap_gen_pass.get());
		_p_test_cs = ComputeShader::Create(GetResPath("Shaders/Compute/cs_test.hlsl"));
		TextureDesc desc(64,64,1,EALGFormat::kALGFormatR8G8B8A8_UNORM,EALGFormat::kALGFormatR8G8B8A8_UNORM,EALGFormat::kALGFormatR32_UINT,false);
		_p_test_texture = Texture2D::Create(desc);
		_p_test_texture->Name("cs_out");
		TexturePool::Add("cs_out", std::dynamic_pointer_cast<Texture2D>(_p_test_texture));
		return 0;
	}
	void Renderer::Finalize()
	{
		INIT_CHECK(this, Renderer);
		_p_timemgr->Finalize();
		DESTORY_PTR(_p_timemgr);
	}
	void Renderer::Tick(const float& delta_time)
	{
		INIT_CHECK(this, Renderer);
		RenderingStates::Reset();
		ModuleTimeStatics::RenderDeltatime = _p_timemgr->GetElapsedSinceLastMark();
		_p_timemgr->Mark();
		_p_render_passes.clear();
		BeginScene();
		Render();
		EndScene();
	}

	void Renderer::BeginScene()
	{
		memset(reinterpret_cast<void*>(_p_per_frame_cbuf_data), 0, sizeof(ScenePerFrameData));
		_p_scene_camera = g_pSceneMgr->_p_current->GetActiveCamera();
		Camera::sCurrent = _p_scene_camera;
		PrepareCamera(_p_scene_camera);
		PrepareLight(g_pSceneMgr->_p_current);
		memcpy(_p_context->GetPerFrameCbufData(), _p_per_frame_cbuf_data, sizeof(ScenePerFrameData));

		_p_render_passes.emplace_back(_p_opaque_pass.get());
		_p_render_passes.emplace_back(_p_reslove_pass.get());


		//TODO:下一行存在绘制会崩溃，需要排查
		//cmd->DrawRenderer(MeshPool::GetMesh("FullScreenQuad"), BuildIdentityMatrix(), MaterialLibrary::GetMaterial("Blit"));
		//cmd->ResolveToBackBuffer(_p_camera_color_attachment);
	}
	void Renderer::EndScene()
	{
		_rendering_data.Reset();
		for (auto pass : _p_render_passes)
			pass->EndPass(_p_context);
		for (auto pass : _p_task_render_passes)
			pass->EndPass(_p_context);
		_p_task_render_passes.clear();
	}

	float Renderer::GetDeltaTime() const
	{
		return _p_timemgr->GetElapsedSinceLastMark();
	}
	void Renderer::Render()
	{
		_p_test_cs->SetTexture("gInputA",TexturePool::Get("Textures/MyImage01.jpg").get());
		//_p_test_cs->SetTexture("gInputB",TexturePool::Get("Runtime/default_black").get());
		_p_test_cs->SetTexture("gOut", _p_test_texture.get());
		auto cmd = CommandBufferPool::GetCommandBuffer();
		cmd->Clear();
		cmd->Dispatch(_p_test_cs.get(), 4, 4, 1);
		_p_shadowcast_pass->BeginPass(_p_context);
		_p_shadowcast_pass->Execute(_p_context, cmd.get(), _rendering_data);

		
		//cmd->SetViewProjectionMatrices(_p_scene_camera->GetView(), _p_scene_camera->GetProjection());
		for (auto task_pass : _p_task_render_passes)
		{
			task_pass->BeginPass(_p_context);
			task_pass->Execute(_p_context, cmd.get());
		}


		static uint32_t w = 1600, h = 900;
		cmd->SetRenderTarget(_p_camera_color_attachment, _p_camera_depth_attachment);
		cmd->ClearRenderTarget(_p_camera_color_attachment, _p_camera_depth_attachment, Colors::kBlack, 1.0f);
		cmd->SetViewports({ Rect{0,0,(uint16_t)w,(uint16_t)h} });
		cmd->SetScissorRects({ Rect{0,0,(uint16_t)w,(uint16_t)h} });
		cmd->SetViewProjectionMatrices(_p_scene_camera->GetView(), _p_scene_camera->GetProjection());
		_p_context->ExecuteCommandBuffer(cmd);

		Shader::SetGlobalTexture("SkyBox", _p_cubemap_gen_pass->_p_cube_map.get());
		Shader::SetGlobalTexture("DiffuseIBL", _p_cubemap_gen_pass->_p_env_map.get());
		//Shader::SetGlobalTexture("Albedo", _p_test_texture.get());
		_p_opaque_pass->BeginPass(_p_context);
		_p_opaque_pass->Execute(_p_context);


		//cmd->Clear();
		//for (auto pass : _p_render_passes)
		//{
		//	pass->BeginPass(_p_context);
		//	pass->Execute(_p_context);
		//}
		_p_reslove_pass->BeginPass(_p_context);
		_p_reslove_pass->Execute(_p_context);

		CommandBufferPool::ReleaseCommandBuffer(cmd);

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
			//Vector3f joint_pos[4];
			//memset(joint_pos, 0, sizeof(Vector3f) * 4);
			//u64 time = (u32)g_pTimeMgr->GetScaledWorldTime() % 75;
			//LOG_INFO("Time: {}", time);
			//Matrix4x4f transf = s_anim_mat[0][time];
			//for (int i = 1; i < s_test_sk.joint_num; i++)
			//{
			//	transf = s_anim_mat[i][time] * transf;
			//	joint_pos[i] = TransformCoord(transf, joint_pos[i]);
			//	Gizmo::DrawLine(joint_pos[i - 1], joint_pos[i],Colors::kYellow);
			//}
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
					_p_per_frame_cbuf_data->_DirectionalLights[direction_light_index]._LightDir = Vector3f::kZero;
					_p_per_frame_cbuf_data->_DirectionalLights[direction_light_index++]._LightColor = Colors::kBlack.xyz;
					continue;
				}
				_p_per_frame_cbuf_data->_DirectionalLights[direction_light_index]._LightColor = color.xyz;
				_p_per_frame_cbuf_data->_DirectionalLights[direction_light_index++]._LightDir = light_data._light_dir.xyz;
			}
			else if (light->_light_type == ELightType::kPoint)
			{
				if (!light->Active())
				{
					_p_per_frame_cbuf_data->_PointLights[point_light_index]._LightParam0 = 0.0;
					_p_per_frame_cbuf_data->_PointLights[point_light_index++]._LightColor = Colors::kBlack.xyz;
					continue;
				}
				_p_per_frame_cbuf_data->_PointLights[point_light_index]._LightColor = color.xyz;
				_p_per_frame_cbuf_data->_PointLights[point_light_index]._LightPos = light_data._light_pos.xyz;
				_p_per_frame_cbuf_data->_PointLights[point_light_index]._LightParam0 = light_data._light_param.x;
				_p_per_frame_cbuf_data->_PointLights[point_light_index++]._LightParam1 = light_data._light_param.y;
			}
			else if (light->_light_type == ELightType::kSpot)
			{
				if (!light->Active())
				{
					_p_per_frame_cbuf_data->_SpotLights[spot_light_index++]._LightColor = Colors::kBlack.xyz;
					continue;
				}
				_p_per_frame_cbuf_data->_SpotLights[spot_light_index]._LightColor = color.xyz;
				_p_per_frame_cbuf_data->_SpotLights[spot_light_index]._LightPos = light_data._light_pos.xyz;
				_p_per_frame_cbuf_data->_SpotLights[spot_light_index]._LightDir = light_data._light_dir.xyz;
				_p_per_frame_cbuf_data->_SpotLights[spot_light_index]._Rdius = light_data._light_param.x;
				_p_per_frame_cbuf_data->_SpotLights[spot_light_index]._InnerAngle = light_data._light_param.y;
				_p_per_frame_cbuf_data->_SpotLights[spot_light_index]._OuterAngle = light_data._light_param.z;
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
			_p_per_frame_cbuf_data->_DirectionalLights[0]._LightColor = Colors::kBlack.xyz;
			_p_per_frame_cbuf_data->_DirectionalLights[0]._LightDir = { 0.0f,0.0f,0.0f };
		}
	}
	void Renderer::PrepareCamera(Camera* p_camera)
	{
		//_p_scene_camera->RecalculateMarix();
		_p_per_frame_cbuf_data->_CameraPos = _p_scene_camera->Position();
	}
}