//#define DEAR_IMGUI
//#ifdef DEAR_IMGUI
//#include "Ext/imgui/imgui.h"
//#include "Ext/imgui/backends/imgui_impl_win32.h"
//#include "Ext/imgui/backends/imgui_impl_dx12.h"
//#include "Ext/imgui/imgui_internal.h"
//#include "Objects/SceneActor.h"
//
//#endif // DEAR_IMGUI
//
//
//static void DrawTreeNode(Ailu::SceneActor* actor)
//{
//	using Ailu::SceneActor;
//	static SceneActor* s_cur_selected_actor = nullptr;
//	static int cur_object_index = 0u;
//	static u32 cur_tree_node_index = 0u;
//
//	static ImGuiTreeNodeFlags s_base_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
//	static int s_selected_mask = 0;
//	static bool s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
//	static int s_node_clicked = -1;
//	static u32 s_pre_frame_selected_actor_id = 10;
//	static u32 s_cur_frame_selected_actor_id = 0;
//#ifdef DEAR_IMGUI
//	ImGuiTreeNodeFlags node_flags = s_base_flags;
//	s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
//	ImGui::PushID(cur_object_index);
//	cur_tree_node_index++;
//	if (s_b_selected) node_flags |= ImGuiTreeNodeFlags_Selected;
//	if (cur_tree_node_index - 1 == 0) node_flags |= ImGuiTreeNodeFlags_DefaultOpen;
//	//绘制根节点
//	bool b_root_node_open = ImGui::TreeNodeEx((void*)(intptr_t)cur_tree_node_index, node_flags,
//		std::format("Root node id:{},current selected: {},node mask: {}", cur_tree_node_index - 1, s_node_clicked, s_selected_mask).c_str());
//	if (ImGui::IsItemClicked())
//		s_node_clicked = cur_tree_node_index - 1;
//	if (b_root_node_open)
//	{
//		s_cur_frame_selected_actor_id = actor->Id();
//		for (auto node : actor->GetAllChildren())
//		{
//			if (node->GetChildNum() > 0)
//			{
//				auto scene_node = static_cast<SceneActor*>(node);
//				DrawTreeNode(scene_node);
//			}
//			else
//			{
//				node_flags = s_base_flags;
//				node_flags |= ImGuiTreeNodeFlags_Leaf;// | ImGuiTreeNodeFlags_NoTreePushOnOpen;
//				s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
//				if (s_b_selected)
//					node_flags |= ImGuiTreeNodeFlags_Selected;
//				ImGui::PushID(cur_tree_node_index); // PushID 需要在进入子节点前
//				cur_tree_node_index++;
//				if (ImGui::TreeNodeEx((void*)(intptr_t)cur_tree_node_index, node_flags, std::format("node id:{},actor name: {}", cur_tree_node_index - 1, node->Name()).c_str()))
//				{
//					if (ImGui::IsItemClicked())
//						s_node_clicked = cur_tree_node_index - 1;
//					s_cur_frame_selected_actor_id = node->Id();
//					//ImGui::Text("占位符");
//					//ImGui::SameLine();
//					//if (ImGui::SmallButton("button")) {}
//					s_selected_mask = (1 << s_node_clicked);
//					ImGui::TreePop(); // TreePop 需要在退出子节点后
//				}
//				ImGui::PopID(); // PopID 需要与对应的 PushID 配对
//			}
//		}
//		ImGui::TreePop(); // TreePop 需要在退出当前节点后
//	}
//	ImGui::PopID(); // PopID 需要与对应的 PushID 配对
//#endif // DEAR_IMGUI
//}