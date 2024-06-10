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

namespace Ailu
{
	namespace Editor
	{
		static SceneActor* s_cur_selected_actor = nullptr;
		static int cur_object_index = 0u;
		static u32 cur_tree_node_index = 0u;
		static u16 s_mini_tex_size = 64;

		namespace TreeStats
		{
			static ImGuiTreeNodeFlags s_base_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
			static int s_selected_mask = 0;
			static bool s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
			static int s_node_clicked = -1;
			static u32 s_pre_frame_selected_actor_id = 10;
			static u32 s_cur_frame_selected_actor_id = 0;
			static i16 s_cur_opened_texture_selector = -1;
			static bool s_is_texture_selector_open = true;
			static u32 s_common_property_handle = 0u;
			static String s_texture_selector_ret = "none";
		}


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
			_widgets.emplace_back(std::move(MakeScope<ObjectDetail>(&s_cur_selected_actor)));
			_widgets.emplace_back(std::move(MakeScope<RenderTextureView>()));
			_mesh_browser.Open(ImGuiWidget::GetGlobalWidgetHandle());
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

		//https://tavianator.com/2011/ray_box.html
		//https://www.cnblogs.com/amazonove/articles/7884465.html
		bool static Intersect(Vector3f origin, Vector3f dir, const AABB& aabb, float& intersectionDistance)
		{
			Vector3f invDir = Vector3f::kOne;
			invDir.x /= dir.x;
			invDir.y /= dir.y;
			invDir.z /= dir.z;

			Vector3f imin = (aabb._min - origin) * invDir;
			Vector3f imax = (aabb._max - origin) * invDir;

			if (dir.x < 0) { std::swap(imin.x, imax.x); }
			if (dir.y < 0) { std::swap(imin.y, imax.y); }
			if (dir.z < 0) { std::swap(imin.z, imax.z); }

			float n = std::max<float>(imin.x, std::max<float>(imin.y, imin.z));
			float f = std::min<float>(imax.x, std::min<float>(imax.y, imax.z));

			if (f >= n)
			{
				intersectionDistance = n;
				return true;
			}
			return false;
		}


		void EditorLayer::OnEvent(Event& e)
		{
			Vector2f vp_size = static_cast<RenderView*>(_p_render_view)->GetViewportSize();
			//auto m_pos = Input::GetMousePos();
			////m_pos -= _p_render_view->_pos;
			//LOG_INFO("mouse pos {},{}", m_pos.x, m_pos.y);
			//LOG_INFO("vo pos {}", _p_render_view->_pos.ToString());
			//LOG_INFO("{}",e.ToString());
			if (e.GetEventType() == EEventType::kMouseButtonPressed)
			{
				MouseButtonPressedEvent& event = static_cast<MouseButtonPressedEvent&>(e);
				static std::chrono::seconds duration(5);
				if (event.GetButton() == 1)
				{
					auto inv_vp = Camera::sCurrent->GetView() * Camera::sCurrent->GetProjection();
					MatrixInverse(inv_vp);
					auto p = Camera::sCurrent->GetProjection();
					auto m_pos = Input::GetMousePos();
					// 获取Win32窗口的位置
					RECT rect;
					GetWindowRect((HWND)(Application::GetInstance()->GetWindowPtr()->GetNativeWindowPtr()), &rect);
					Vector2f win32_window_pos = Vector2f((f32)rect.left, (f32)rect.top);

					m_pos -= (_p_render_view->_pos - win32_window_pos);


					LOG_INFO("mouse pos {},{}", m_pos.x, m_pos.y);
					float vx = (2.0f * m_pos.x / vp_size.x - 1.0f) / p[0][0];
					float vy = (-2.0f * m_pos.y / vp_size.y + 1.0f) / p[1][1];
					auto view = Camera::sCurrent->GetView();
					MatrixInverse(view);
					Vector3f ray_dir = Vector3f(vx, vy, 1.0f);
					Vector3f start = Camera::sCurrent->Position();
					Vector3f end = start + TransformNormal(ray_dir, view) * 10000.0f;
					Gizmo::DrawLine(start, end, duration, Colors::kRed);
					f32 min_dis = std::numeric_limits<f32>::max();
					SceneActor* p_closetest_obj = nullptr;
					for (auto& actor : g_pSceneMgr->_p_current->GetAllActor())
					{
						auto comp = actor->GetComponent<StaticMeshComponent>();
						if (comp)
						{
							for (auto& aabb : comp->GetAABB())
							{
								auto& [aabb_min, aabb_max] = aabb;
								f32 dis = 0;
								if (Intersect(start, Normalize(TransformNormal(ray_dir, view)), aabb, dis))
								{
									LOG_INFO("Intersect {}!",actor->Name());
									if (min_dis > dis)
									{
										min_dis = dis;
										g_pSceneMgr->_p_selected_actor = actor;
									}
								}
								//float dis = AABB::DistanceFromRayToAABB(start, Camera::sCurrent->Forward(), aabb_min, aabb_max);
								//if (dis < min_dis)
								//{
								//	min_dis = dis;

								//}
							}
						}
					}
					//g_pSceneMgr->_p_selected_actor = p_closetest_obj;
					//g_pLogMgr->LogFormat("Selected obj: {}", g_pSceneMgr->_p_selected_actor->Name());
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
			if (ImGui::Button("Capture"))
			{
				g_pRenderer->TakeCapture();
			}
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
			TreeStats::s_common_property_handle = 0;
			ShowWorldOutline();
			_mesh_browser.Show();
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

		bool s_show_outilineobj_rename_dialog = false;
		static bool s_global_modal_window_info[256]{ false };
		static void MarkModalWindoShow(const int& handle)
		{
			s_global_modal_window_info[handle] = true;
		}

		static String ShowModalDialog(const String& title, int handle = 0)
		{
			static char buf[256] = {};
			String str_id = std::format("{}-{}", title, handle);
			if (s_global_modal_window_info[handle])
			{
				ImGui::OpenPopup(str_id.c_str());
				memset(buf, 0, 256);
				s_global_modal_window_info[handle] = false;
			}
			if (ImGui::BeginPopupModal(str_id.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("Enter Text:");
				ImGui::InputText("##inputText", buf, IM_ARRAYSIZE(buf));
				if (ImGui::Button("OK", ImVec2(120, 0)))
				{
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return String{ buf };
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120, 0)))
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			return String{};
		}

		static String ShowModalDialog(const String& title, std::function<void()> show_widget, int handle = 0)
		{
			static char buf[256] = {};
			String str_id = std::format("{}-{}", title, handle);
			if (s_global_modal_window_info[handle])
			{
				ImGui::OpenPopup(str_id.c_str());
				s_global_modal_window_info[handle] = false;
			}
			if (ImGui::BeginPopupModal(str_id.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text(title.c_str());
				show_widget();
				ImGui::EndPopup();
			}
			return String{};
		}

		void DrawTreeNode(SceneActor* actor)
		{
			using namespace TreeStats;
			ImGuiTreeNodeFlags node_flags = s_base_flags;
			s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
			ImGui::PushID(cur_object_index);
			cur_tree_node_index++;
			if (s_b_selected) node_flags |= ImGuiTreeNodeFlags_Selected;
			if (cur_tree_node_index - 1 == 0) node_flags |= ImGuiTreeNodeFlags_DefaultOpen;
			//绘制根节点
			bool b_root_node_open = ImGui::TreeNodeEx((void*)(intptr_t)cur_tree_node_index, node_flags,
				std::format("Root node id:{},current selected: {},node mask: {}", cur_tree_node_index - 1, s_node_clicked, s_selected_mask).c_str());
			if (ImGui::IsItemClicked())
				s_node_clicked = cur_tree_node_index - 1;
			if (b_root_node_open)
			{
				s_cur_frame_selected_actor_id = actor->Id();
				for (auto node : actor->GetAllChildren())
				{
					if (node->GetChildNum() > 0)
					{
						auto scene_node = static_cast<SceneActor*>(node);
						DrawTreeNode(scene_node);
					}
					else
					{
						node_flags = s_base_flags;
						node_flags |= ImGuiTreeNodeFlags_Leaf;// | ImGuiTreeNodeFlags_NoTreePushOnOpen;
						s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
						if (s_b_selected)
							node_flags |= ImGuiTreeNodeFlags_Selected;
						ImGui::PushID(cur_tree_node_index); // PushID 需要在进入子节点前
						cur_tree_node_index++;
						if (ImGui::TreeNodeEx((void*)(intptr_t)cur_tree_node_index, node_flags, std::format("node id:{},actor name: {}", cur_tree_node_index - 1, node->Name()).c_str()))
						{
							if (ImGui::IsItemClicked())
								s_node_clicked = cur_tree_node_index - 1;
							s_cur_frame_selected_actor_id = node->Id();
							if (ImGui::BeginPopupContextItem()) // <-- use last item id as popup id
							{
								if (ImGui::MenuItem("Rename"))
								{
									//s_global_modal_window_info[cur_tree_node_index] = true;
									MarkModalWindoShow(cur_tree_node_index);
								}

								if (ImGui::MenuItem("Delete"))
								{
									g_pSceneMgr->DeleteSceneActor(dynamic_cast<SceneActor*>(node));
								}
								ImGui::EndPopup();
							}
							auto new_name = ShowModalDialog("Rename: " + node->Name(), cur_tree_node_index);
							if (!new_name.empty())
								node->Name(new_name);
							s_selected_mask = (1 << s_node_clicked);
							ImGui::TreePop(); // TreePop 需要在退出子节点后
						}
						ImGui::PopID(); // PopID 需要与对应的 PushID 配对
					}
				}
				ImGui::TreePop(); // TreePop 需要在退出当前节点后
			}
			ImGui::PopID(); // PopID 需要与对应的 PushID 配对
		}

		void EditorLayer::ShowWorldOutline()
		{
			using namespace TreeStats;
			ImGui::Begin("WorldOutline");                          // Create a window called "Hello, world!" and append into it.
			u32 index = 0;
			if (ImGui::Button("Reflesh Scene"))
			{
				g_pSceneMgr->MarkCurSceneDirty();
			}
			ImGui::SameLine();
			if (ImGui::Button("Add empty"))
			{
				g_pSceneMgr->AddSceneActor();
			}
			ImGui::Text("Scene: %s", g_pSceneMgr->_p_current->Name().c_str());
			//if (g_pSceneMgr->_p_selected_actor != nullptr)
			//{
			//	s_node_clicked = g_pSceneMgr->_p_selected_actor->ID();
			//}
			DrawTreeNode(g_pSceneMgr->_p_current->GetSceneRoot());
			s_cur_frame_selected_actor_id = s_node_clicked;
			if (s_pre_frame_selected_actor_id != s_cur_frame_selected_actor_id)
			{
				s_cur_selected_actor = s_cur_frame_selected_actor_id == 0 ? nullptr : g_pSceneMgr->_p_current->GetSceneActorByIndex(s_cur_frame_selected_actor_id);
			}
			s_pre_frame_selected_actor_id = s_cur_frame_selected_actor_id;
			//if (s_cur_selected_actor != nullptr && g_pSceneMgr->_p_selected_actor != s_cur_selected_actor)
			//{
			//	g_pSceneMgr->_p_selected_actor = s_cur_selected_actor;
			//}

			cur_tree_node_index = 0;
			ImGui::End();
		}

		void EditorLayer::ShowObjectDetail()
		{
		}


		//----------------------------------------------------------------------------MeshBrowser----------------------------------------------------------------------
		void MeshBrowser::Open(const int& handle)
		{
			ImguiWindow::Open(handle);
		}


		void MeshBrowser::Show()
		{
			//ImguiWindow::Show();
			//static int s_cur_mesh_index = 0;
			//static int object_id = 0;
			//ImGui::Begin("MeshBrowser", &_b_show);
			//if (ImGui::BeginListBox("Meshs"))
			//{
			//	for (auto it = g_pResourceMgr->ResourceBegin<Mesh>(); it != g_pResourceMgr->ResourceEnd<Mesh>(); it++)
			//	{

			//	}
			//	ImGui::EndListBox();
			//}
			//auto [meshs, mesh_count] = MeshPool::GetMeshForGUI();
			//if (ImGui::ListBox("Meshs", &s_cur_mesh_index, meshs, mesh_count))
			//{
			//	LOG_INFO(meshs[s_cur_mesh_index]);
			//}
			//if (ImGui::Button("Place in Scene"))
			//{
			//	g_pSceneMgr->AddSceneActor(std::format("object{}", object_id++), meshs[s_cur_mesh_index]);
			//}
			//if (ImGui::Button("Add Camera Test"))
			//{
			//	Camera cam(1.78f);
			//	g_pSceneMgr->AddSceneActor(std::format("camera{}", object_id++), cam);
			//}
			//ImGui::End();
		}
		//----------------------------------------------------------------------------MeshBrowser---------------------------------------------------------------------
	}
}


