//#include "pch.h"
#include "Widgets/EditorLayer.h"
#include "Platform/WinWindow.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/backends/imgui_impl_win32.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Ext/imgui/imgui_internal.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/TimeMgr.h"
#include "Render/RenderingData.h"
#include "Render/Gizmo.h"

#include "Objects/Actor.h"
#include "Objects/TransformComponent.h"
#include "Objects/StaticMeshComponent.h"
#include "Objects/CameraComponent.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/ResourceMgr.h"
#include "RHI/DX12/D3DTexture.h"
#include "Framework/Common/LogMgr.h"

#include "Objects/SceneActor.h"
#include "Framework/Events/MouseEvent.h"
#include "Render/Gizmo.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/Input.h"

#include "Render/Mesh.h"
#include "Animation/Clip.h"
#include "Render/Renderer.h"
#include "Framework/Common/Profiler.h"

#include "Widgets/CommonTextureWidget.h"
#include "Widgets/OutputLog.h"
#include "Widgets/ObjectDetail.h"
#include "Widgets/AssetTable.h"
#include "Widgets/RenderView.h"
#include "Widgets/AssetBrowser.h"
#include "Widgets/WorldOutline.h"
#include "Widgets/EnvironmentSetting.h"
#include "Common/Selection.h"

namespace Ailu
{
	namespace Editor
	{
		//static SceneActor* s_cur_selected_actor = nullptr;
		//static int cur_object_index = 0u;
		//static u32 cur_tree_node_index = 0u;
		//static u16 s_mini_tex_size = 64;

		//namespace TreeStats
		//{
		//	static ImGuiTreeNodeFlags s_base_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
		//	static int s_selected_mask = 0;
		//	static bool s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
		//	static int s_node_clicked = -1;
		//	static u32 s_pre_frame_selected_actor_id = 10;
		//	static u32 s_cur_frame_selected_actor_id = 0;
		//	static i16 s_cur_opened_texture_selector = -1;
		//	static bool s_is_texture_selector_open = true;
		//	static u32 s_common_property_handle = 0u;
		//	static String s_texture_selector_ret = "none";
		//}


		EditorLayer::EditorLayer() : Layer("EditorLayer")
		{
			ImGuiIO& io = ImGui::GetIO(); (void)io;
			_font = io.Fonts->AddFontFromFileTTF(PathUtils::GetResSysPath("Fonts/VictorMono-Regular.ttf").c_str(), 13.0f);
			io.Fonts->Build();
		}

		EditorLayer::EditorLayer(const String& name) : Layer(name)
		{

		}

		EditorLayer::~EditorLayer()
		{

		}

		void EditorLayer::OnAttach()
		{
			auto tex_detail_view = _widgets.emplace_back(std::move(MakeScope<TextureDetailView>())).get();
			_widgets.emplace_back(std::move(MakeScope<AssetBrowser>(tex_detail_view)));
			_widgets.emplace_back(std::move(MakeScope<AssetTable>()));
			_widgets.emplace_back(std::move(MakeScope<WorldOutline>()));
			_widgets.emplace_back(std::move(MakeScope<ObjectDetail>()));
			_widgets.emplace_back(std::move(MakeScope<RenderTextureView>()));
			for (auto appender : g_pLogMgr->GetAppenders())
			{
				auto imgui_appender = dynamic_cast<ImGuiLogAppender*>(appender);
				if (imgui_appender)
				{
					_widgets.emplace_back(std::move(MakeScope<OutputLog>(imgui_appender)));
				}
			}

			_widgets.emplace_back(std::move(MakeScope<RenderView>()));
			_p_render_view = _widgets.back().get();
			_widgets.emplace_back(std::move(MakeScope<EnvironmentSetting>()));
			_p_env_setting = _widgets.back().get();
			for (auto& widget : _widgets)
			{
				widget->Open(ImGuiWidget::GetGlobalWidgetHandle());
			}
			//默认不显示纹理细节窗口
			tex_detail_view->Close(tex_detail_view->Handle());
			ImGuiWidget::SetFocus("AssetBrowser");
		}

		void EditorLayer::OnDetach()
		{

		}

		void EditorLayer::OnEvent(Event& e)
		{
			//LOG_INFO("{}", Input::GetMousePos().ToString());
			if (e.GetEventType() == EEventType::kMouseButtonPressed)
			{
				Vector2f vp_size = static_cast<RenderView*>(_p_render_view)->GetViewportSize();
				MouseButtonPressedEvent& event = static_cast<MouseButtonPressedEvent&>(e);
				if (event.GetButton() == 1)
				{
					auto p = Camera::sCurrent->GetProjection();
					auto m_pos = Input::GetMousePos();
					if (_p_render_view->Hover(m_pos))
					{
						LOG_INFO("Pick something...");
						m_pos = _p_render_view->GlobalToLocal(m_pos);
						//auto [wx, wy] = Application::GetInstance()->GetWindowPtr()->GetWindowPosition();
						//m_pos -= (_p_render_view->_pos - Vector2f((f32)wx,(f32)wy));

						float vx = (2.0f * m_pos.x / vp_size.x - 1.0f) / p[0][0];
						float vy = (-2.0f * m_pos.y / vp_size.y + 1.0f) / p[1][1];
						auto view = Camera::sCurrent->GetView();
						MatrixInverse(view);
						Vector3f ray_dir = Vector3f(vx, vy, 1.0f);
						TransformNormal(ray_dir, view);
						Vector3f start = Camera::sCurrent->Position();
						ray_dir = Normalize(ray_dir);

						SceneActor* p_closetest_obj = nullptr;
						for (auto& actor : g_pSceneMgr->_p_current->GetAllActor())
						{
							auto comp = actor->GetComponent<StaticMeshComponent>();
							if (comp)
							{
								auto& aabbs = comp->GetAABB();
								if (!AABB::Intersect(aabbs[0], start, ray_dir))
									continue;
								for (int i = 1; i < aabbs.size(); i++)
								{
									if (AABB::Intersect(aabbs[i], start, ray_dir))
										p_closetest_obj = actor;
								}
							}
						}
						Selection::AddAndRemovePreSelection(p_closetest_obj);
						//g_pSceneMgr->_p_selected_actor = p_closetest_obj;
					}
				}
			}
		}

		static bool show = false;
		static bool s_show_asset_table = false;
		static bool s_show_rt = false;
		static bool s_show_renderview = true;

		void EditorLayer::OnImguiRender()
		{
			ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
			ImGui::PushFont(_font);
			ImGui::Begin("Performace Statics");                          // Create a window called "Hello, world!" and append into it.
			ImGui::Text("FrameRate: %.2f", ImGui::GetIO().Framerate);
			ImGui::Text("FrameTime: %.2f ms", ModuleTimeStatics::RenderDeltatime);
			ImGui::Text("Draw Call: %d", RenderingStates::s_draw_call);
			ImGui::Text("VertCount: %d", RenderingStates::s_vertex_num);
			ImGui::Text("TriCount: %d", RenderingStates::s_triangle_num);
			if (ImGui::Button("Capture"))
			{
				g_pRenderer->TakeCapture();
			}
			for (auto pass : g_pRenderer->GetRenderPasses())
			{
				bool active = pass->IsActive();
				ImGui::Checkbox(pass->GetName().c_str(), &active);
				pass->SetActive(active);
			}

			String space = "";
			ImGui::Text("CPU Time:");
			while (!Profiler::g_Profiler._cpu_profiler_queue.empty())
			{
				auto [is_start, profiler_id] = Profiler::g_Profiler._cpu_profiler_queue.front();
				Profiler::g_Profiler._cpu_profiler_queue.pop();
				if (is_start)
				{
					space.append("-");
					const auto& profiler = Profiler::g_Profiler.GetCPUProfileData(profiler_id);
					ImGui::Text("%s%s,%.2f ms", space.c_str(), profiler->Name.c_str(), profiler->_avg_time);
				}
				else
					space = space.substr(0, space.size() - 1);
			}
			space = "";
			ImGui::Text("GPU Time:");
			while (!Profiler::g_Profiler._gpu_profiler_queue.empty())
			{
				auto [is_start, profiler_id] = Profiler::g_Profiler._gpu_profiler_queue.front();
				Profiler::g_Profiler._gpu_profiler_queue.pop();
				if (is_start)
				{
					space.append("-");
					const auto& profiler = Profiler::g_Profiler.GetProfileData(profiler_id);
					ImGui::Text("%s%s,%.2f ms", space.c_str(), profiler->Name.c_str(), profiler->_avg_time);
				}
				else
					space = space.substr(0, space.size() - 1);
			}

			static const char* items[] = { "Shadering", "WireFrame", "ShaderingWireFrame" };
			static int item_current_idx = 0; // Here we store our selection data as an index.
			const char* combo_preview_value = items[item_current_idx];  // Pass in the preview value visible before opening the combo (it could be anything)
			if (ImGui::BeginCombo("Shadering Mode", combo_preview_value, 0))
			{
				for (int n = 0; n < IM_ARRAYSIZE(items); n++)
				{
					const bool is_selected = (item_current_idx == n);
					if (ImGui::Selectable(items[n], is_selected))
						item_current_idx = n;
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (item_current_idx == 0) RenderingStates::s_shadering_mode = EShaderingMode::kShader;
			else if (item_current_idx == 1) RenderingStates::s_shadering_mode = EShaderingMode::kWireFrame;
			else RenderingStates::s_shadering_mode = EShaderingMode::kShaderedWireFrame;

			ImGui::SliderFloat("Gizmo Alpha:", &Gizmo::s_color.a, 0.01f, 1.0f, "%.2f");
			ImGui::SliderFloat("Game Time Scale:", &TimeMgr::TimeScale, 0.0f, 2.0f, "%.2f");
			float shadow_dis_m = g_pRenderer->_shadow_distance / 100.0f;
			ImGui::SliderFloat("ShadowDistance m", &shadow_dis_m, 0.f, 100.0f, "%.2f");
			g_pRenderer->_shadow_distance = shadow_dis_m * 100;
			f32 u = Application::s_target_framecount;
			ImGui::SliderFloat("TargetFrame m", &u, 1.f, 999.0f, "%.2f");
			Application::s_target_framecount = u;
			//g_pRenderer->_shadow_distance = shadow_dis_m * 100.0f;
			ImGui::Checkbox("Expand", &show);
			ImGui::Checkbox("ShowAssetTable", &s_show_asset_table);
			ImGui::Checkbox("ShowRT", &s_show_rt);
			for (auto& info : g_pResourceMgr->_import_infos)
			{
				float x = g_pTimeMgr->GetScaledWorldTime(0.25f);
				ImGui::Text("%s", info._msg.c_str());
				ImGui::SameLine();
				ImGui::ProgressBar(x - static_cast<int>(x), ImVec2(0.f, 0.f));
			}
			ImGui::End();
			ImGui::PopFont();

			if (show)
				ImGui::ShowDemoWindow(&show);
			for (auto& widget : _widgets)
			{
				widget->Show();
			}
		}

		void EditorLayer::Begin()
		{

		}

		void EditorLayer::End()
		{
			ImGuiWidget::EndFrame();
		}
	}
}


