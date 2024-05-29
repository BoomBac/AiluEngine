#include "pch.h"
#include "Framework/ImGui/ImGuiLayer.h"
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

#include "Render/Mesh.h"
#include "Animation/Clip.h"
#include "Render/Renderer.h"
#include "Framework/ImGui/Widgets/AssetBrowser.h"
#include "Framework/ImGui/Widgets/AssetTable.h"
#include "Framework/ImGui/Widgets/ObjectDetail.h"
#include "Framework/ImGui/Widgets/Outputlog.h"
#include "Framework/ImGui/Widgets/CommonTextureWidget.h"
#include "Framework/Common/Profiler.h"

namespace ImguiTree
{
	struct TreeNode
	{
		std::string name;
		std::vector<TreeNode> children;
	};

	// 递归绘制树节点
	void DrawTreeNode(TreeNode& node)
	{
		ImGui::PushID(&node);

		bool nodeOpen = ImGui::TreeNodeEx(node.name.c_str(), ImGuiTreeNodeFlags_OpenOnArrow);

		//// 处理拖拽行为
		//if (ImGui::BeginDragDropSource()) {
		//    ImGui::SetDragDropPayload("DND_TREENODE", &node, sizeof(TreeNode));
		//    ImGui::Text("Drag %s", node.name.c_str());
		//    ImGui::EndDragDropSource();
		//}

		//if (ImGui::BeginDragDropTarget()) {
		//    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_TREENODE")) {
		//        // 在此处处理拖拽的节点
		//        TreeNode* draggedNode = (TreeNode*)payload->Data;
		//        node.children.push_back(*draggedNode);
		//    }
		//    ImGui::EndDragDropTarget();
		//}

		if (nodeOpen) {
			for (TreeNode& child : node.children) {
				DrawTreeNode(child);
			}
			ImGui::TreePop();
		}

		ImGui::PopID();
	}
}

namespace Ailu
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


	Ailu::ImGUILayer::ImGUILayer() : ImGUILayer("ImGUILayer")
	{
	}

	ImGUILayer::ImGUILayer(const String& name) : Layer(name)
	{
		IMGUI_CHECKVERSION();
		auto context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		//io.MouseDrawCursor = true;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
		_font = io.Fonts->AddFontFromFileTTF(PathUtils::GetResSysPath("Fonts/VictorMono-Regular.ttf").c_str(), 13.0f);
		io.Fonts->Build();
		ImGuiStyle& style = ImGui::GetStyle();
		// Setup Dear ImGui style
		ImGui::StyleColorsDark(&style);
		style.WindowPadding = ImVec2(3.0f, 3.0f);
		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 10.0f;
			style.ChildRounding = 10.0f;
			style.FrameRounding = 10.0f;
			style.Colors[ImGuiCol_WindowBg].w = 0.8f;
		}

		ImGui_ImplWin32_Init(Application::GetInstance()->GetWindow().GetNativeWindowPtr());
	}

	Ailu::ImGUILayer::~ImGUILayer()
	{
		// Cleanup
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	void Ailu::ImGUILayer::OnAttach()
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
		for (auto& widget : _widgets)
		{
			widget->Open(ImGuiWidget::GetGlobalWidgetHandle());
		}
		//默认不显示纹理细节窗口
		tex_detail_view->Close(tex_detail_view->Handle());
		ImGuiWidget::SetFocus("AssetBrowser");
	}

	void Ailu::ImGUILayer::OnDetach()
	{

	}

	void Ailu::ImGUILayer::OnEvent(Event& e)
	{

	}
	static bool show = false;
	static bool s_show_asset_table = false;
	static bool s_show_rt = false;
	static bool s_show_renderview = true;

	void Ailu::ImGUILayer::OnImguiRender()
	{
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
		f32 u = Application::s_target_framecount;
		ImGui::SliderFloat("TargetFrame m", &u, 1.f, 999.0f, "%.2f");
		Application::s_target_framecount = u;
		g_pRenderer->_shadow_distance = shadow_dis_m * 100.0f;
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
		for(auto& widget : _widgets)
		{
			widget->Show();
		}
	}

	void Ailu::ImGUILayer::Begin()
	{
		// Start the Dear ImGui frame
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void Ailu::ImGUILayer::End()
	{
		const Window& window = Application::GetInstance()->GetWindow();
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()));
		ImGuiWidget::EndFrame();
		ImGui::EndFrame();
		ImGui::Render();
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

	void ImGUILayer::ShowWorldOutline()
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
		DrawTreeNode(g_pSceneMgr->_p_current->GetSceneRoot());
		s_cur_frame_selected_actor_id = s_node_clicked;
		if (s_pre_frame_selected_actor_id != s_cur_frame_selected_actor_id)
		{
			s_cur_selected_actor = s_cur_frame_selected_actor_id == 0 ? nullptr : g_pSceneMgr->_p_current->GetSceneActorByIndex(s_cur_frame_selected_actor_id);
		}
		s_pre_frame_selected_actor_id = s_cur_frame_selected_actor_id;
		g_pSceneMgr->_p_selected_actor = s_cur_selected_actor;
		cur_tree_node_index = 0;
		ImGui::End();
	}

	void ImGUILayer::ShowObjectDetail()
	{
	}


	//----------------------------------------------------------------------------MeshBrowser----------------------------------------------------------------------
	void MeshBrowser::Open(const int& handle)
	{
		ImguiWindow::Open(handle);
	}


	void MeshBrowser::Show()
	{
		ImguiWindow::Show();
		static int s_cur_mesh_index = 0;
		static int object_id = 0;
		u32 mesh_count = 0;
		Ref<Mesh> selected_mesh = nullptr;
		ImGui::Begin("MeshBrowser", &_b_show);
		if (ImGui::BeginListBox("Meshs"))
		{
			for (auto it = g_pResourceMgr->ResourceBegin<Mesh>(); it != g_pResourceMgr->ResourceEnd<Mesh>(); it++)
			{
				auto mesh = ResourceMgr::IterToRefPtr<Mesh>(it);
				bool is_selected = mesh_count == s_cur_mesh_index;
				if (ImGui::Selectable(mesh->Name().c_str(), &is_selected))
				{
					s_cur_mesh_index = mesh_count;
					selected_mesh = mesh;
				}
				++mesh_count;
			}
			ImGui::EndListBox();
		}
		if (ImGui::Button("Place in Scene"))
		{
			g_pSceneMgr->AddSceneActor(std::format("object{}", object_id++), selected_mesh);
		}
		if (ImGui::Button("Add Camera Test"))
		{
			Camera cam(1.78f);
			g_pSceneMgr->AddSceneActor(std::format("camera{}", object_id++), cam);
		}
		ImGui::End();
	}
	//----------------------------------------------------------------------------MeshBrowser---------------------------------------------------------------------
}


