#include "pch.h"
#include "Render/Renderer.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/SceneMgr.h"
#include "Render/RenderCommand.h"
#include "Render/RenderingData.h"
#include "Render/Gizmo.h"
#include "Objects/StaticMeshComponent.h"
#include "Framework/Parser/AssetParser.h"
#include <Framework/Common/LogMgr.h>
#include "Render/RenderQueue.h"

namespace Ailu
{
	int Renderer::Initialize()
	{
		_p_context = new D3DContext(dynamic_cast<WinWindow*>(Application::GetInstance()->GetWindowPtr()));
		_b_init = true;
		_p_context->Init();
		_p_timemgr = new TimeMgr();
		_p_timemgr->Initialize();
		_p_per_frame_cbuf_data = static_cast<D3DContext*>(_p_context)->GetPerFrameCbufDataStruct();
		_p_camera_color_attachment = RenderTexture::Create(1600, 900, "CameraColorAttachment", RenderConstants::kColorRange == EColorRange::kLDR ? 
			RenderConstants::kLDRFormat : RenderConstants::kHDRFormat);
		_p_camera_depth_attachment = RenderTexture::Create(1600, 900, "CameraDepthAttachment", EALGFormat::kALGFormatD24S8_UINT);
		_p_opaque_pass = MakeScope<OpaquePass>();
		_p_reslove_pass = MakeScope<ReslovePass>(_p_camera_color_attachment);
		_p_shadowcast_pass = MakeScope<ShadowCastPass>();
		return 0;
	}
	void Renderer::Finalize()
	{
		INIT_CHECK(this, Renderer);
		DESTORY_PTR(_p_context);
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
	}
	void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, uint32_t instance_count)
	{
		vertex_buf->Bind();
		index_buffer->Bind();
		RenderCommand::DrawIndexedInstanced(index_buffer, instance_count);
	}
	void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, uint32_t instance_count)
	{
		vertex_buf->Bind();
		RenderCommand::DrawInstanced(vertex_buf, instance_count);
	}
	void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, Ref<Material> mat, uint32_t instance_count)
	{
		vertex_buf->Bind();
		index_buffer->Bind();
		mat->Bind();
		RenderCommand::DrawIndexedInstanced(index_buffer, instance_count);
	}
	void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, Ref<Material> mat, uint32_t instance_count)
	{
		vertex_buf->Bind();
		mat->Bind();
		RenderCommand::DrawInstanced(vertex_buf, instance_count);
	}
	void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, Ref<Material> mat, Matrix4x4f transform, uint32_t instance_count)
	{
		vertex_buf->Bind();
		index_buffer->Bind();
		mat->Bind();
		RenderCommand::DrawIndexedInstanced(index_buffer, instance_count, transform);
	}
	void Renderer::Submit(const Ref<Mesh>& mesh, Ref<Material>& mat, Matrix4x4f transform, uint32_t instance_count)
	{

	}

	float Renderer::GetDeltaTime() const
	{
		return _p_timemgr->GetElapsedSinceLastMark();
	}
	void Renderer::Render()
	{
		auto cmd = CommandBufferPool::GetCommandBuffer();
		cmd->Clear();
		_p_shadowcast_pass->BeginPass(_p_context);
		_p_shadowcast_pass->Execute(_p_context, cmd.get(), _rendering_data);

		static uint32_t w = 1600, h = 900;
		cmd->SetRenderTarget(_p_camera_color_attachment, _p_camera_depth_attachment);
		cmd->ClearRenderTarget(_p_camera_color_attachment, _p_camera_depth_attachment, Colors::kBlack, 1.0f);
		cmd->SetViewports({ Rect{0,0,(uint16_t)w,(uint16_t)h} });
		cmd->SetScissorRects({ Rect{0,0,(uint16_t)w,(uint16_t)h} });
		cmd->SetViewProjectionMatrices(_p_scene_camera->GetView(), _p_scene_camera->GetProjection());
		_p_context->ExecuteCommandBuffer(cmd);

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

			Gizmo::DrawLine(Vector3f::Zero, Vector3f{ 500.f,0.0f,0.0f }, Colors::kRed);
			Gizmo::DrawLine(Vector3f::Zero, Vector3f{ 0.f,500.0f,0.0f }, Colors::kGreen);
			Gizmo::DrawLine(Vector3f::Zero, Vector3f{ 0.f,0.0f,500.0f }, Colors::kBlue);
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
					_p_per_frame_cbuf_data->_DirectionalLights[direction_light_index]._LightDir = Vector3f::Zero;
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
		_p_scene_camera->Update();
		_p_per_frame_cbuf_data->_CameraPos = _p_scene_camera->Position();
	}
}