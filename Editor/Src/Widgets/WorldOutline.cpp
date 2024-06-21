#include "Widgets/WorldOutline.h"
#include "Common/Selection.h"
//----engine--------
#include "Ext/imgui/imgui.h"
#include "Framework/Common/SceneMgr.h"
//----engine--------
namespace Ailu
{
	namespace Editor
	{
		WorldOutline::WorldOutline() : ImGuiWidget("WorldOutline")
		{
			//_selected_actor = &g_pSceneMgr->_p_selected_actor;
		}
		WorldOutline::~WorldOutline()
		{
		}
		void WorldOutline::Open(const i32& handle)
		{
			ImGuiWidget::Open(handle);
		}
		void WorldOutline::Close(i32 handle)
		{
			ImGuiWidget::Close(handle);
		}
		void WorldOutline::ShowImpl()
		{
			SceneActor* old_selected_actor = nullptr;
			_selected_actor = Selection::First<SceneActor>();
			old_selected_actor = _selected_actor;
			//for (auto obj : Selection::Selections())
			//{
			//	auto scene_obj = dynamic_cast<SceneActor*>(obj);
			//	if (scene_obj)
			//	{
			//		_selected_actor = scene_obj;
			//		old_selected_actor = scene_obj;
			//	}
			//}
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
			DrawTreeNode(g_pSceneMgr->_p_current->GetSceneRoot(),true);
			if (old_selected_actor != _selected_actor)
			{
				Selection::AddAndRemovePreSelection(_selected_actor);
			}
			g_pSceneMgr->_p_selected_actor = _selected_actor;
		}

		void WorldOutline::DrawTreeNode(SceneActor* actor, bool is_root)
		{
			if (actor == nullptr)
				return;
			static const ImGuiTreeNodeFlags s_base_node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;;
			ImGuiTreeNodeFlags node_flags = s_base_node_flags;
			bool is_cur_node_selected = actor == _selected_actor;
			ImGui::PushID(actor->ID());
			if (is_cur_node_selected) 
				node_flags |= ImGuiTreeNodeFlags_Selected;
			if (is_root) //根节点默认打开
				node_flags |= ImGuiTreeNodeFlags_DefaultOpen;
			//绘制根节点
			u32 cur_id = actor->ID();
			const void* ptr_id = reinterpret_cast<void*>(&cur_id);
			bool b_root_node_open = ImGui::TreeNodeEx(ptr_id, node_flags,"%s",actor->Name().c_str());
			if (ImGui::IsItemClicked())
			{
				_selected_actor = actor;
			}
			if (b_root_node_open)
			{
				//s_cur_frame_selected_actor_id = actor->Id();
				for (auto child : actor->GetAllChildren())
				{
					if (child == nullptr)
						continue;
					if (child->GetChildNum() > 0)
					{
						auto scene_node = static_cast<SceneActor*>(child);
						DrawTreeNode(scene_node,false);
					}
					else
					{
						is_cur_node_selected = child == _selected_actor;
						node_flags = s_base_node_flags;
						node_flags |= ImGuiTreeNodeFlags_Leaf;// | ImGuiTreeNodeFlags_NoTreePushOnOpen;
						if (is_cur_node_selected)
							node_flags |= ImGuiTreeNodeFlags_Selected;
						ImGui::PushID(child->ID()); // PushID 需要在进入子节点前
						u32 cur_id = child->ID();
						const void* ptr_id = reinterpret_cast<void*>(&cur_id);
						if (ImGui::TreeNodeEx(ptr_id, node_flags, "%s", child->Name().c_str()))
						{
							if (ImGui::IsItemClicked())
							{
								_selected_actor = static_cast<SceneActor*>(child);
							}
							if (ImGui::BeginPopupContextItem()) // <-- use last item id as popup id
							{
								if (ImGui::MenuItem("Rename"))
								{
									//s_global_modal_window_info[cur_tree_node_index] = true;
									MarkModalWindoShow((_selected_actor)->ID());
								}

								if (ImGui::MenuItem("Delete"))
								{
									g_pSceneMgr->DeleteSceneActor(dynamic_cast<SceneActor*>(child));
								}
								ImGui::EndPopup();
							}
							if (_selected_actor)
							{
								auto new_name = ShowModalDialog("Rename: " + child->Name(), (_selected_actor)->ID());
								if (!new_name.empty())
									child->Name(new_name);
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
}
