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
#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/ResourceMgr.h"
#include "RHI/DX12/D3DTexture.h"
#include "Framework/Common/LogMgr.h"

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
	}

	static void DrawProperty(SerializableProperty& prop)
	{
		switch (prop._type)
		{
		case Ailu::ESerializablePropertyType::kString:
			break;
		case Ailu::ESerializablePropertyType::kFloat: ImGui::DragFloat(prop._name.c_str(), static_cast<float*>(prop._value_ptr));
			break;
		case Ailu::ESerializablePropertyType::kBool: ImGui::Checkbox(prop._name.c_str(), static_cast<bool*>(prop._value_ptr));
			break;
		case Ailu::ESerializablePropertyType::kVector3f: ImGui::DragFloat3(prop._name.c_str(), static_cast<Vector3f*>(prop._value_ptr)->data);
			break;
		case Ailu::ESerializablePropertyType::kVector4f:
			break;
		case Ailu::ESerializablePropertyType::kColor: ImGui::ColorEdit3(prop._name.c_str(), static_cast<Vector4f*>(prop._value_ptr)->data);
			break;
		case Ailu::ESerializablePropertyType::kTransform:
			break;
		case Ailu::ESerializablePropertyType::kTexture2D:
			break;
		case Ailu::ESerializablePropertyType::kStaticMesh:
			break;
		default: LOG_WARNING("prop: {} value type havn't be handle!", prop._name)
			break;
		}
	}

	static String OpenTextureSelector(int id)
	{
		static i16 s_pre_tex_selector_id = -1;
		static int s_selected_img_index = -1;
		static String selected_tex_name{ "none" };
		if (TreeStats::s_is_texture_selector_open)
		{
			ImGui::Begin("TextureSelector", &TreeStats::s_is_texture_selector_open);
			static float preview_tex_size = 128;
			//// 获取当前ImGui窗口的内容区域宽度
			u32 window_width = (u32)ImGui::GetWindowContentRegionWidth();
			int numImages = 10, imagesPerRow = window_width / (u32)preview_tex_size;
			imagesPerRow += imagesPerRow == 0 ? 1 : 0;
			if (s_pre_tex_selector_id != id) s_selected_img_index = -1;
			static ImVec2 uv0{ 0,0 }, uv1{ 1,1 };
			int tex_count = 0;

			for (auto& [tex_name, tex] : TexturePool::GetAllResourceMap())
			{
				auto& desc = std::static_pointer_cast<D3DTexture2D>(tex)->GetGPUHandle();
				ImGui::BeginGroup();
				ImGuiContext* context = ImGui::GetCurrentContext();
				auto drawList = context->CurrentWindow->DrawList;
				if (ImGui::ImageButton((void*)(intptr_t)desc.ptr, ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0))
				{
					s_selected_img_index = tex_count;
					LOG_INFO("selected img {}", s_selected_img_index);
				}
				if (s_selected_img_index == tex_count)
				{
					ImVec2 cur_img_pos = ImGui::GetCursorPos();
					ImVec2 imgMin = ImGui::GetItemRectMin();
					ImVec2 imgMax = ImGui::GetItemRectMax();
					drawList->AddRect(imgMin, imgMax, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
					selected_tex_name = tex_name;
				}
				ImGui::Text("TexName");
				ImGui::EndGroup();
				if ((tex_count + 1) % imagesPerRow != 0)
				{
					ImGui::SameLine();
				}
				++tex_count;
			}
			ImGui::End();
			s_pre_tex_selector_id = id;
		}
		return s_selected_img_index ==-1? "none" : selected_tex_name;
	}

	static void DrawInternalStandardMaterial(Material* mat)
	{
		u16 mat_prop_index = 0;
		u16 min_tex_size = 64;
		static String new_tex = "none";
		static bool use_textures[10]{};
		static auto func_show_prop = [&](u16& mat_prop_index, Material* mat, String name) {
			ImGui::PushID(mat_prop_index);
			ETextureUsage tex_usage{};
			String non_tex_prop_name;
			if (name == InternalStandardMaterialTexture::kAlbedo)
			{
				tex_usage = ETextureUsage::kAlbedo;
				non_tex_prop_name = "BaseColor";
			}
			else if (name == InternalStandardMaterialTexture::kEmssive)
			{
				tex_usage = ETextureUsage::kEmssive;
				non_tex_prop_name = "EmssiveColor";
			}
			else if (name == InternalStandardMaterialTexture::kMetallic)
			{
				tex_usage = ETextureUsage::kMetallic;
				non_tex_prop_name = "MetallicValue";
			}
			else if (name == InternalStandardMaterialTexture::kRoughness)
			{
				tex_usage = ETextureUsage::kRoughness;
				non_tex_prop_name = "RoughnessValue";
			}
			else if (name == InternalStandardMaterialTexture::kSpecular)
			{
				tex_usage = ETextureUsage::kSpecular;
				non_tex_prop_name = "SpecularColor";
			}
			else if (name == InternalStandardMaterialTexture::kNormal)
			{
				tex_usage = ETextureUsage::kNormal;
				non_tex_prop_name = "None";
			}
			else 
			{
				tex_usage = ETextureUsage::kAlbedo;
				non_tex_prop_name = "BaseColor";
			};
			use_textures[mat_prop_index] = mat->IsTextureUsed(tex_usage);
			ImGui::Checkbox("Texture", &use_textures[mat_prop_index]);
			mat->MarkTextureUsed({ tex_usage }, use_textures[mat_prop_index]);
			ImGui::PopID();
			ImGui::SameLine();
			if (use_textures[mat_prop_index])
			{
				auto prop = mat->GetProperty(name);
				ImGui::Text(" :%s", prop._name.c_str());
				String cur_tex_nameid = prop._value_ptr == nullptr ? "none" : *static_cast<String*>(prop._value_ptr);
				auto& desc = std::static_pointer_cast<D3DTexture2D>(prop._value_ptr == nullptr ? TexturePool::GetDefaultWhite() : TexturePool::Get(cur_tex_nameid))->GetGPUHandle();
				float contentWidth = ImGui::GetWindowContentRegionWidth();
				float centerX = (contentWidth - min_tex_size) * 0.5f;
				ImGui::SetCursorPosX(centerX);
				if (TreeStats::s_cur_opened_texture_selector == mat_prop_index)
				{
					if (new_tex != "none" && new_tex != cur_tex_nameid)
					{
						mat->SetTexture(name, new_tex);
					}
				}
				if (ImGui::ImageButton((void*)(intptr_t)desc.ptr, ImVec2(min_tex_size, min_tex_size)))
				{
					TreeStats::s_is_texture_selector_open = true;
					TreeStats::s_cur_opened_texture_selector = mat_prop_index;
				}
				if (TreeStats::s_cur_opened_texture_selector == mat_prop_index)
				{
					ImGuiContext* context = ImGui::GetCurrentContext();
					auto drawList = context->CurrentWindow->DrawList;
					ImVec2 cur_img_pos = ImGui::GetCursorPos();
					ImVec2 imgMin = ImGui::GetItemRectMin();
					ImVec2 imgMax = ImGui::GetItemRectMax();
					drawList->AddRect(imgMin, imgMax, IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);
				}
			}
			else
			{
				if (tex_usage == ETextureUsage::kAlbedo || tex_usage == ETextureUsage::kEmssive || tex_usage == ETextureUsage::kSpecular)
				{
					auto prop = mat->GetProperty(non_tex_prop_name);
					ImGui::ColorEdit4(prop._name.c_str(), static_cast<float*>(prop._value_ptr));
				}
				else if(tex_usage == ETextureUsage::kMetallic || tex_usage == ETextureUsage::kRoughness)
				{
					auto prop = mat->GetProperty(non_tex_prop_name);
					ImGui::SliderFloat(prop._name.c_str(), static_cast<float*>(prop._value_ptr), 0.0, 1.0);
				}
				else
				{
					ImGui::NewLine();
				}
				mat->RemoveTexture(name);
			}
			++mat_prop_index;
			};
		if (!TreeStats::s_is_texture_selector_open)
			TreeStats::s_cur_opened_texture_selector = -1;
		func_show_prop(mat_prop_index, mat, "Albedo");
		func_show_prop(mat_prop_index, mat, "Normal");
		func_show_prop(mat_prop_index, mat, "Specular");
		func_show_prop(mat_prop_index, mat, "Roughness");
		func_show_prop(mat_prop_index, mat, "Metallic");
		func_show_prop(mat_prop_index, mat, "Emssive");
		if (TreeStats::s_cur_opened_texture_selector != -1) 
			new_tex = OpenTextureSelector(TreeStats::s_cur_opened_texture_selector);
	}

	static void DrawComponentProperty(Component* comp)
	{
		if (ImGui::CollapsingHeader(comp->GetTypeName().c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (comp->GetTypeName() == LightComponent::GetStaticType())
			{
				auto light = static_cast<LightComponent*>(comp);
				static const char* items[] = { "Directional", "Point", "Spot" };
				int item_current_idx = 0;
				switch (light->_light_type)
				{
				case Ailu::ELightType::kDirectional: item_current_idx = 0;
					break;
				case Ailu::ELightType::kPoint: item_current_idx = 1;
					break;
				case Ailu::ELightType::kSpot: item_current_idx = 2;
					break;
				}
				const char* combo_preview_value = items[item_current_idx];
				if (ImGui::BeginCombo("LightType", combo_preview_value, 0))
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
				ImGui::SliderFloat("Intensity", &light->_intensity, 0.0f, 16.0f);
				ImGui::ColorEdit3("Color", static_cast<float*>(light->_light._light_color.data));
				if (item_current_idx == 0) light->_light_type = ELightType::kDirectional;
				else if (item_current_idx == 1) light->_light_type = ELightType::kPoint;
				else light->_light_type = ELightType::kSpot;
				if (light->_light_type == ELightType::kDirectional) return;
				else if (light->_light_type == ELightType::kPoint)
				{
					auto& light_data = light->_light;
					ImGui::DragFloat("Radius", &light_data._light_param.x, 1.0, 0.0, 5000.0f);
				}
				else
				{
					auto& light_data = light->_light;
					ImGui::DragFloat("Radius", &light_data._light_param.x);
					ImGui::SliderFloat("InnerAngle", &light_data._light_param.y, 0.0f, light_data._light_param.z);
					ImGui::SliderFloat("OuterAngle", &light_data._light_param.z, 1.0f, 180.0f);
				}
			}
			else if (comp->GetTypeName() == TransformComponent::GetStaticType())
			{
				auto transf = static_cast<TransformComponent*>(comp);
				auto& pos = transf->GetProperty("Position");
				ImGui::DragFloat3(pos._name.c_str(), static_cast<Vector3f*>(pos._value_ptr)->data);
				auto& rot = transf->GetProperty("Rotation");
				ImGui::SliderFloat3(rot._name.c_str(), static_cast<Vector3f*>(rot._value_ptr)->data, -180.f, 180.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				auto& scale = transf->GetProperty("Scale");
				ImGui::DragFloat3(scale._name.c_str(), static_cast<Vector3f*>(scale._value_ptr)->data);
			}
			else if (comp->GetTypeName() == StaticMeshComponent::GetStaticType())
			{
				auto static_mesh_comp = static_cast<StaticMeshComponent*>(comp);
				for (auto& prop : static_mesh_comp->GetAllProperties())
				{
					if (prop.second._type == ESerializablePropertyType::kStaticMesh)
					{
						ImGui::Text("StaticMesh: %s", prop.second._name.c_str());
					}
				}
				int mat_prop_index = 0;
				auto mat = static_mesh_comp->GetMaterial();
				ImGui::Text("Material: %s",mat->Name().c_str());
				if (mat->IsInternal())
				{
					DrawInternalStandardMaterial(mat.get());
					//for (auto& prop : mat->GetAllProperties())
					//{
					//	ImGui::PushID(mat_prop_index);
					//	ImGui::Checkbox("Texture", &use_textures[mat_prop_index++]);
					//	ImGui::PopID();
					//	ImGui::SameLine();
					//	if (mat->IsTextureUsed(ETextureUsage::kAlbedo))
					//	{
					//		ImGui::Text("Texture: %s", prop.first.c_str());
					//		auto name_ptr = reinterpret_cast<String*>(prop.second._value_ptr);
					//		auto name = *name_ptr;
					//		if (name == string{ "none" })
					//		{
					//			ImGui::Text("none texture");
					//		}
					//		else
					//		{
					//			auto& desc = std::static_pointer_cast<D3DTexture2D>(TexturePool::Get("MyImage01"))->GetGPUHandle();
					//			float contentWidth = ImGui::GetWindowContentRegionWidth();
					//			float centerX = (contentWidth - 256) * 0.5f;
					//			ImGui::SetCursorPosX(centerX);
					//			ImGui::Image((void*)(intptr_t)desc.ptr, ImVec2(256, 256));
					//		}
					//	}
					//	else
					//	{

					//	}
					//	if (prop.second._type == ESerializablePropertyType::kColor)
					//	{
					//		ImGui::ColorEdit4(prop.second._name.c_str(), static_cast<float*>(prop.second._value_ptr));
					//		//if (use_textures[mat_prop_index - 1])
					//		//{
					//		//    auto& desc = std::static_pointer_cast<D3DTexture2D>(TexturePool::Get("MyImage01"))->GetGPUHandle();
					//		//    float contentWidth = ImGui::GetWindowContentRegionWidth();
					//		//    float centerX = (contentWidth - 256) * 0.5f;
					//		//    ImGui::SetCursorPosX(centerX);
					//		//    ImGui::Image((void*)(intptr_t)desc.ptr, ImVec2(256, 256));
					//		//}
					//	}
					//	else if (prop.second._type == ESerializablePropertyType::kFloat)
					//	{
					//		//ImGui::DragFloat(prop.second._name.c_str(), static_cast<float*>(prop.second._value_ptr));
					//		ImGui::SliderFloat(prop.second._name.c_str(), static_cast<float*>(prop.second._value_ptr), 0.0f, 1.0f, "%.2f");
					//	}
					//	else if (prop.second._type == ESerializablePropertyType::kTexture2D)
					//	{
					//		ImGui::Text("Texture: %s", prop.first.c_str());
					//		auto name_ptr = reinterpret_cast<String*>(prop.second._value_ptr);
					//		auto name = *name_ptr;
					//		if (name == string{ "none" })
					//		{
					//			ImGui::Text("none texture");
					//		}
					//		else
					//		{
					//			auto& desc = std::static_pointer_cast<D3DTexture2D>(TexturePool::Get("MyImage01"))->GetGPUHandle();
					//			float contentWidth = ImGui::GetWindowContentRegionWidth();
					//			float centerX = (contentWidth - 256) * 0.5f;
					//			ImGui::SetCursorPosX(centerX);
					//			ImGui::Image((void*)(intptr_t)desc.ptr, ImVec2(256, 256));
					//		}

					//	}
					//}
				}
			}
		}
	}

	Ailu::ImGUILayer::ImGUILayer()
	{
	}

	Ailu::ImGUILayer::~ImGUILayer()
	{
		// Cleanup
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
	static ImFont* font0 = nullptr;

	void Ailu::ImGUILayer::OnAttach()
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
		font0 = io.Fonts->AddFontFromFileTTF(GetResPath("Fonts/VictorMono-Regular.ttf").c_str(), 13.0f);
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

	void Ailu::ImGUILayer::OnDetach()
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	void Ailu::ImGUILayer::OnEvent(Event& e)
	{

	}
	static bool show = false;

	void Ailu::ImGUILayer::OnImguiRender()
	{
		ImGui::PushFont(font0);
		ImGui::Begin("Performace Statics");                          // Create a window called "Hello, world!" and append into it.
		ImGui::Text("FrameRate: %.2f", ImGui::GetIO().Framerate);
		ImGui::Text("FrameTime: %.2f ms", ModuleTimeStatics::RenderDeltatime);
		ImGui::Text("Draw Call: %d", RenderingStates::s_draw_call);
		ImGui::Text("VertCount: %d", RenderingStates::s_vertex_num);
		ImGui::Text("TriCount: %d", RenderingStates::s_triangle_num);

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
		ImGui::SliderFloat("Game Time Scale:", &TimeMgr::TimeScale, 0.0f, 0.2f, "%.2f");

		ImGui::Checkbox("Expand", &show);
		ImGui::End();
		ImGui::PopFont();

		if (show) ImGui::ShowDemoWindow(&show);
		ShowWorldOutline();
		ShowObjectDetail();
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
						//ImGui::Text("占位符");
						//ImGui::SameLine();
						//if (ImGui::SmallButton("button")) {}
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

		DrawTreeNode(g_pSceneMgr->_p_current->GetSceneRoot());
		s_cur_frame_selected_actor_id = s_node_clicked;
		if (s_pre_frame_selected_actor_id != s_cur_frame_selected_actor_id)
		{
			s_cur_selected_actor = s_cur_frame_selected_actor_id == 0 ? nullptr : g_pSceneMgr->_p_current->GetSceneActorByIndex(s_cur_frame_selected_actor_id);
		}
		s_pre_frame_selected_actor_id = s_cur_frame_selected_actor_id;

		cur_tree_node_index = 0;
		ImGui::End();
	}

	void ImGUILayer::ShowObjectDetail()
	{
		ImGui::Begin("ObjectDetail");
		if (s_cur_selected_actor != nullptr)
		{
			ImGui::Text(s_cur_selected_actor->Name().c_str());
			u32 comp_index = 0;
			for (auto& comp : s_cur_selected_actor->GetAllComponent())
			{
				ImGui::PushID(comp_index);
				bool b_active = comp->Active();
				ImGui::Checkbox("", &b_active);
				ImGui::PopID();
				comp->Active(b_active);
				ImGui::SameLine();
				DrawComponentProperty(comp.get());
				++comp_index;
			}
		}
		ImGui::End();
	}
	void ImGUILayer::ShowTextureExplorer()
	{
		ImGui::Begin("TextureExplorer");
		static float preview_tex_size = 128;
		//// 获取当前ImGui窗口的内容区域宽度
		u32 window_width = (u32)ImGui::GetWindowContentRegionWidth();
		int numImages = 10, imagesPerRow = window_width / (u32)preview_tex_size;
		imagesPerRow += imagesPerRow == 0 ? 1 : 0;
		static int s_selected_img_index = -1;
		//int selected_img_index = 0;
		static ImVec2 uv0{ 0,0 }, uv1{ 1,1 };
		int tex_count = 0;
		for (auto& [tex_name, tex] : TexturePool::GetAllResourceMap())
		{
			auto& desc = std::static_pointer_cast<D3DTexture2D>(tex)->GetGPUHandle();
			ImGui::BeginGroup();
			ImGuiContext* context = ImGui::GetCurrentContext();
			auto drawList = context->CurrentWindow->DrawList;
			if (ImGui::ImageButton((void*)(intptr_t)desc.ptr, ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0))
			{
				s_selected_img_index = tex_count;
				LOG_INFO("selected img {}", s_selected_img_index);
			}
			if (s_selected_img_index == tex_count)
			{
				ImVec2 cur_img_pos = ImGui::GetCursorPos();
				// 获取当前图像的区域坐标
				ImVec2 imgMin = ImGui::GetItemRectMin(); // 图像区域的最小坐标
				ImVec2 imgMax = ImGui::GetItemRectMax(); // 图像区域的最大坐标
				drawList->AddRect(imgMin, imgMax, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
			}
			ImGui::Text("TexName");
			//LOG_WARNING("s_selected_img_index: {}", s_selected_img_index);
			ImGui::EndGroup();
			// 根据每行显示的图像数量添加换行
			if ((tex_count + 1) % imagesPerRow != 0)
			{
				ImGui::SameLine();
			}
			++tex_count;
		}
		ImGui::End();
	}
}


