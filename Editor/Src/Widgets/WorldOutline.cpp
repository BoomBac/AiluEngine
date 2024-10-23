#include "Widgets/WorldOutline.h"
#include "Common/Selection.h"
#include "Widgets/InputLayer.h"
//----engine--------
#include "Ext/imgui/imgui.h"
#include "Framework/Common/KeyCode.h"
#include "Framework/Events/KeyEvent.h"
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
        void WorldOutline::Open(const i32 &handle)
        {
            ImGuiWidget::Open(handle);
        }
        void WorldOutline::Close(i32 handle)
        {
            ImGuiWidget::Close(handle);
        }
        void WorldOutline::ShowImpl()
        {
            while (!_pending_deleted_entities.empty())
            {
                Selection::RemoveSlection(_pending_deleted_entities.front());
                g_pSceneMgr->ActiveScene()->RemoveObject(_pending_deleted_entities.front());
                _pending_deleted_entities.pop();
            }
            ECS::Entity old_selected_entity = ECS::kInvalidEntity;
            _selected_entity = Selection::FirstEntity();
            old_selected_entity = _selected_entity;
            _scene_register = &g_pSceneMgr->ActiveScene()->GetRegister();
            u32 index = 0;
            if (ImGui::Button("Reflesh Scene"))
            {
                g_pSceneMgr->MarkCurSceneDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add empty"))
            {
                g_pSceneMgr->ActiveScene()->AddObject();
            }
            ImGui::Text("Scene: %s", g_pSceneMgr->ActiveScene()->Name().c_str());
            _drawed_entity.clear();
            for (auto entity: g_pSceneMgr->ActiveScene()->EntityView())
                DrawTreeNode(entity);
            if (old_selected_entity != _selected_entity)
            {
                Selection::AddAndRemovePreSelection(_selected_entity);
            }
            //g_pSceneMgr->_p_selected_actor = _selected_actor;
        }

        void WorldOutline::DrawTreeNode(ECS::Entity entity)
        {
            if (entity == ECS::kInvalidEntity || _drawed_entity.contains(entity))
                return;
            _drawed_entity.insert(entity);
            static const ImGuiTreeNodeFlags s_base_node_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
            ImGuiTreeNodeFlags node_flags = s_base_node_flags;
            bool is_cur_node_selected = entity == _selected_entity;
            ImGui::PushID(entity);
            if (is_cur_node_selected)
                node_flags |= ImGuiTreeNodeFlags_Selected;
            auto hiera_comp = _scene_register->GetComponent<CHierarchy>(entity);
            auto tag_comp = _scene_register->GetComponent<TagComponent>(entity);
            if (hiera_comp->_children_num == 0)
                node_flags |= ImGuiTreeNodeFlags_Leaf;
            bool b_root_node_open = false;
            if (_rename_entity == entity)
            {
                memset(_rename_buffer, 0, 256);
                ImGui::OpenPopup("Rename");
                ImGui::SetNextWindowSize(ImVec2(300, 100));
                if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::InputText("##Rename", _rename_buffer, 256);
                    if (ImGui::Button("OK", ImVec2(120, 0)))
                    {
                        tag_comp->_name = _rename_buffer;
                        ImGui::CloseCurrentPopup();
                        _rename_entity = ECS::kInvalidEntity;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    {
                        ImGui::CloseCurrentPopup();
                        _rename_entity = ECS::kInvalidEntity;
                    }
                    ImGui::EndPopup();
                }
            }
            else
                b_root_node_open = ImGui::TreeNodeEx(hiera_comp, node_flags, "%s", tag_comp->_name.c_str());
            if (ImGui::IsItemClicked())
            {
                _selected_entity = entity;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                OnOutlineDoubleClicked(entity);
            }
            if (b_root_node_open)
            {
                if (hiera_comp->_children_num > 0)
                {
                    auto child = _scene_register->GetComponent<CHierarchy>(hiera_comp->_first_child);
                    ECS::Entity child_entity = hiera_comp->_first_child;
                    do
                    {
                        if (child->_children_num > 0)
                        {
                            DrawTreeNode(child_entity);
                        }
                        else
                        {
                            _drawed_entity.insert(child_entity);
                            is_cur_node_selected = child_entity == _selected_entity;
                            node_flags = s_base_node_flags;
                            node_flags |= ImGuiTreeNodeFlags_Leaf;// | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                            if (is_cur_node_selected)
                                node_flags |= ImGuiTreeNodeFlags_Selected;
                            ImGui::PushID(child_entity);// PushID 需要在进入子节点前
                            auto hiera_comp = _scene_register->GetComponent<CHierarchy>(child_entity);
                            auto tag_comp = _scene_register->GetComponent<TagComponent>(child_entity);
                            if (ImGui::TreeNodeEx(hiera_comp, node_flags, "%s", tag_comp->_name.c_str()))
                            {
                                if (ImGui::IsItemClicked())
                                {
                                    _selected_entity = child_entity;
                                }
                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                {
                                    OnOutlineDoubleClicked(_selected_entity);
                                }
                                if (ImGui::BeginPopupContextItem())// <-- use last item id as popup id
                                {
                                    if (ImGui::MenuItem("Rename"))
                                    {
                                        _rename_entity = entity;
                                    }
                                    if (ImGui::MenuItem("Delete"))
                                    {
                                        _pending_deleted_entities.push((child_entity));
                                    }
                                    memset(_rename_buffer, 0, 256);
                                    ImGui::EndPopup();
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        child_entity = child->_next_sibling;
                        child = _scene_register->GetComponent<CHierarchy>(child_entity);
                    } while (child != nullptr);
                }
                ImGui::TreePop();
            }
            else
            {
                if (hiera_comp->_children_num > 0)
                {
                    auto child = _scene_register->GetComponent<CHierarchy>(hiera_comp->_first_child);
                    ECS::Entity child_entity = hiera_comp->_first_child;
                    do
                    {
                        _drawed_entity.insert(child_entity);
                        child_entity = child->_next_sibling;
                        child = _scene_register->GetComponent<CHierarchy>(child_entity);
                    } while (child != nullptr);
                }
            }
            ImGui::PopID();
        }

        void WorldOutline::OnOutlineDoubleClicked(ECS::Entity entity)
        {
            auto&r = g_pSceneMgr->ActiveScene()->GetRegister();
            auto t = r.GetComponent<TransformComponent>(entity)->_transform;
            auto target_pos = t._position;
            f32 dis = 2.0f;
            if (StaticMeshComponent *sm = r.GetComponent<StaticMeshComponent>(entity); sm != nullptr)
                dis = sm->_transformed_aabbs[0].Diagon() * 1.1f;
            target_pos += -Camera::sCurrent->Forward() * dis;
            FirstPersonCameraController::s_inst.SetTargetPosition(target_pos, true);
        }
        void WorldOutline::OnEvent(Event &e)
        {
            ImGuiWidget::OnEvent(e);
            if (e.GetEventType() == EEventType::kKeyPressed)
            {
                auto &key_e = static_cast<KeyPressedEvent &>(e);
                if (key_e.GetKeyCode() == AL_KEY_DELETE)
                {
                    _pending_deleted_entities.push((_selected_entity));
                }
                else if (key_e.GetKeyCode() == AL_KEY_F2)
                {
                    _rename_entity = _selected_entity;
                }
            }
        }
    }// namespace Editor
}// namespace Ailu
