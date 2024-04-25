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
		DESTORY_PTR(_asset_browser);
		DESTORY_PTR(_asset_table);
		DESTORY_PTR(_render_view);
		DESTORY_PTR(_object_detail);
		DESTORY_PTR(_p_outputlog);
	}

	void Ailu::ImGUILayer::OnAttach()
	{
		_asset_browser = new AssetBrowser();
		_asset_browser->Open(21);
		_asset_table = new AssetTable();
		_render_view = new RenderView();
		_render_view->Open(22);
		_object_detail = new ObjectDetail(&s_cur_selected_actor);
		_object_detail->Open(23);
		_p_outputlog = new OutputLog();
		_p_outputlog->Open(24);
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
		ImGui::Text("GPU Latency: %.2f ms", RenderingStates::s_gpu_latency);

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

		ImGui::Checkbox("Expand", &show);
		ImGui::Checkbox("ShowAssetTable", &s_show_asset_table);
		ImGui::Checkbox("ShowRT", &s_show_rt);
		ImGui::Checkbox("ShowGameView", &s_show_renderview);
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

		if (show) ImGui::ShowDemoWindow(&show);
		if (s_show_asset_table) _asset_table->Open(-2);
		else _asset_table->Close();
		if (s_show_rt) _rt_view.Open(-3);
		else _rt_view.Close();
		TreeStats::s_common_property_handle = 0;

		ShowWorldOutline();
		//ShowObjectDetail();
		//_texture_selector.Show();
		_mesh_browser.Open(20);
		_mesh_browser.Show();
		_asset_browser->Show();
		_asset_table->Show();
		_rt_view.Show();
		_object_detail->Show();
		_p_outputlog->Show();
		//render view焦点时才不会阻塞输入，所以它必须放在最后渲染
		_render_view->Show();
		//ShowTextureExplorer();
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
		//ImGui::Begin("ObjectDetail");
		//if (s_cur_selected_actor != nullptr)
		//{
		//	ImGui::BulletText(s_cur_selected_actor->Name().c_str());
		//	ImGui::SameLine();
		//	i32 new_comp_count = 0;
		//	i32 selected_new_comp_index = -1;
		//	if (ImGui::BeginCombo(" ", "+ Add Component"))
		//	{
		//		auto& type_str = Component::GetAllComponentTypeStr();
		//		for (auto& comp_type : type_str)
		//		{
		//			if (ImGui::Selectable(comp_type.c_str(), new_comp_count == selected_new_comp_index))
		//				selected_new_comp_index = new_comp_count;
		//			if (selected_new_comp_index == new_comp_count)
		//			{
		//				if (comp_type == Component::GetTypeName(StaticMeshComponent::GetStaticType()))
		//					s_cur_selected_actor->AddComponent<StaticMeshComponent>();
		//				else if (comp_type == Component::GetTypeName(SkinedMeshComponent::GetStaticType()))
		//					s_cur_selected_actor->AddComponent<SkinedMeshComponent>();
		//				else if (comp_type == Component::GetTypeName(LightComponent::GetStaticType()))
		//				{
		//					s_cur_selected_actor->AddComponent<LightComponent>();
		//				}
		//			}
		//			++new_comp_count;
		//		}
		//		ImGui::EndCombo();
		//	}
		//	u32 comp_index = 0;
		//	Component* comp_will_remove = nullptr;
		//	for (auto& comp : s_cur_selected_actor->GetAllComponent())
		//	{
		//		//if (comp->GetTypeName() == StaticMeshComponent::GetStaticType()) continue;
		//		ImGui::PushID(comp_index);
		//		bool b_active = comp->Active();
		//		ImGui::Checkbox("", &b_active);
		//		ImGui::PopID();
		//		comp->Active(b_active);
		//		ImGui::SameLine();
		//		ImGui::PushID(comp_index);
		//		if (ImGui::Button("x"))
		//			comp_will_remove = comp.get();
		//		ImGui::PopID();
		//		ImGui::SameLine();
		//		DrawComponentProperty(comp.get(), _texture_selector);
		//		++comp_index;
		//	}
		//	if (comp_will_remove && comp_will_remove->GetType() != EComponentType::kTransformComponent)
		//	{
		//		s_cur_selected_actor->RemoveComponent(comp_will_remove);
		//		g_pSceneMgr->MarkCurSceneDirty();
		//	}
		//}
		//ImGui::End();
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
		ImGui::Begin("MeshBrowser", &_b_show);
		//LOG_INFO("max:({},{}),min:({},{})", ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y, ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y)
		auto [meshs, mesh_count] = MeshPool::GetMeshForGUI();
		if (ImGui::ListBox("Meshs", &s_cur_mesh_index, meshs, mesh_count))
		{
			LOG_INFO(meshs[s_cur_mesh_index]);
		}
		if (ImGui::Button("Place in Scene"))
		{
			g_pSceneMgr->AddSceneActor(std::format("object{}", object_id++), meshs[s_cur_mesh_index]);
		}
		if (ImGui::Button("Add Camera Test"))
		{
			Camera cam(1.78f);
			g_pSceneMgr->AddSceneActor(std::format("camera{}", object_id++), cam);
		}
		ImGui::End();
	}
	//----------------------------------------------------------------------------MeshBrowser---------------------------------------------------------------------

	// 
	//----------------------------------------------------------------------------RTDebugWindow-----------------------------------------------------------------------
	void RTDebugWindow::Open(const int& handle)
	{
		ImguiWindow::Open(handle);
	}
	void RTDebugWindow::Show()
	{
		if (!_b_show)
		{
			_handle = -1;
			return;
		}
		ImGui::Begin("RTDebugWindow");
		static float preview_tex_size = 128;
		//// 获取当前ImGui窗口的内容区域宽度
		u32 window_width = (u32)ImGui::GetWindowContentRegionWidth();
		int numImages = 10, imagesPerRow = window_width / (u32)preview_tex_size;
		imagesPerRow += imagesPerRow == 0 ? 1 : 0;
		static ImVec2 uv0{ 0,0 }, uv1{ 1,1 };
		int tex_count = 0;
		auto cmd = CommandBufferPool::Get();
		for (auto& rt : RenderTexture::s_all_render_texture)
		{
			rt->Transition(cmd.get(), ETextureResState::kShaderResource);
		}
		g_pGfxContext->ExecuteCommandBuffer(cmd);
		static const char* s_cube_dirs[] = { "+Y", "-X", "+Z", "+X","-Z","-Y"};
		static int s_selected_dir = 0;
		for (auto& rt : RenderTexture::s_all_render_texture)
		{
			ImGui::BeginGroup();
			//ImGuiContext* context = ImGui::GetCurrentContext();
			//auto drawList = context->CurrentWindow->DrawList;
			//if (rt->IsCubemap())
			//{
			//	if (ImGui::BeginCombo("##combo", s_cube_dirs[s_selected_dir])) // ##combo 可以用来隐藏标签
			//	{
			//		for (int i = 0; i < 6; i++)
			//		{
			//			const bool isSelected = (s_selected_dir == i);
			//			if (ImGui::Selectable(s_cube_dirs[i], isSelected))
			//				s_selected_dir = i;
			//			if (isSelected)
			//				ImGui::SetItemDefaultFocus(); // Make the selected item the default focus so it appears in the box initially.
			//		}
			//		ImGui::EndCombo();
			//	}
			//	ImGui::Image(rt->GetGPUNativePtr(s_selected_dir + 1), ImVec2(preview_tex_size, preview_tex_size));
			//	//ImGui::BeginTable(rt->Name().c_str(), 4);
			//	//// +Y
			//	//ImGui::TableNextRow();
			//	//ImGui::TableSetColumnIndex(1);
			//	//ImGui::Image(rt->GetGPUNativePtr(3), ImVec2(preview_tex_size, preview_tex_size));
			//	//// -X
			//	//ImGui::TableNextRow();
			//	//ImGui::TableSetColumnIndex(0);
			//	//ImGui::Image(rt->GetGPUNativePtr(2), ImVec2(preview_tex_size, preview_tex_size));
			//	//// +Z
			//	//ImGui::TableSetColumnIndex(1);
			//	//ImGui::Image(rt->GetGPUNativePtr(5), ImVec2(preview_tex_size, preview_tex_size));
			//	//// +X
			//	//ImGui::TableSetColumnIndex(2);
			//	//ImGui::Image(rt->GetGPUNativePtr(1), ImVec2(preview_tex_size, preview_tex_size));
			//	//// -Z
			//	//ImGui::TableSetColumnIndex(3);
			//	//ImGui::Image(rt->GetGPUNativePtr(6), ImVec2(preview_tex_size, preview_tex_size));
			//	//// -Y
			//	//ImGui::TableNextRow();
			//	//ImGui::TableSetColumnIndex(1);
			//	//ImGui::Image(rt->GetGPUNativePtr(4), ImVec2(preview_tex_size, preview_tex_size));
			//	//ImGui::EndTable();
			//	ImGui::Text("TextureCube: %s", rt->Name().c_str());
			//}
			//else
			//{
			//	ImGui::Image(rt->GetGPUNativePtr(), ImVec2(preview_tex_size, preview_tex_size));
			//	ImGui::Text("Texture2D: %s", rt->Name().c_str());
			//}
			ImGui::Image(rt->GetGPUNativePtr(), ImVec2(preview_tex_size, preview_tex_size));
			ImGui::Text("Texture2D: %s", rt->Name().c_str());
			ImGui::EndGroup();
			if ((tex_count + 1) % imagesPerRow != 0)
			{
				ImGui::SameLine();
			}
			++tex_count;
		}
		//ImGui::Begin("RenderPassWindow");
		//u16 pass_index = 0;
		//for (auto& pass : g_pRenderer->GetRenderPasses())
		//{
		//	bool active = pass->IsActive();
		//	ImGui::PushID(pass_index++);
		//	ImGui::Checkbox(" ", &active);
		//	ImGui::SameLine();
		//	ImGui::Text("PassName: %s",pass->GetName().c_str());
		//	pass->SetActive(active);
		//	ImGui::PopID();
		//}
		ImGui::End();
	}
	//----------------------------------------------------------------------------RTDebugWindow-----------------------------------------------------------------------

}


