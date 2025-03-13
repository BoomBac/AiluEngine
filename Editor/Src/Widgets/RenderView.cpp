#include "Widgets/RenderView.h"
#include "Common/Selection.h"
#include "Common/Undo.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"

#include "Ext/ImGuizmo/ImGuizmo.h"//必须在imgui之后引入

#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Events/MouseEvent.h"
#include "Scene/Scene.h"

#include <Render/GraphicsContext.h>

#include "Render/CommandBuffer.h"
#include "Render/CommonRenderPipeline.h"
#include "Render/Gizmo.h"
#include "Render/Renderer.h"
#include "Widgets/InputLayer.h"


// https://zhuanlan.zhihu.com/p/689092580  gpu pickup
namespace Ailu
{
    namespace Editor
    {
        RenderView::RenderView(const String &name) : ImGuiWidget(name)
        {
            //_is_hide_common_widget_info = false;
            _allow_close = false;
        }
        RenderView::~RenderView()
        {
        }
        void RenderView::Open(const i32 &handle)
        {
            ImGuiWidget::Open(handle);
        }
        void RenderView::Close(i32 handle)
        {
            ImGuiWidget::Close(handle);
        }
        void RenderView::ShowImpl()
        {
            if (_src)
            {
                // 设置窗口背景颜色
                ImGuiStyle &style = ImGui::GetStyle();
                auto origin_color = style.Colors[ImGuiCol_WindowBg];
                style.Colors[ImGuiCol_WindowBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);// 设置窗口背景颜色为灰色
                auto w = std::min<f32>(_size.x, _size.y);
                f32 aspect = (f32) _src->Width() / (f32) _src->Height();
                ImVec2 img_size = ImVec2(w, w / aspect);
                ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(_src->ColorTexture()), img_size);
                style.Colors[ImGuiCol_WindowBg] = origin_color;
            }
            else
            {
                ImGui::LabelText("RenderView", "No Camera Rendering");
            }
        }
        void RenderView::OnEvent(Event &e)
        {
        }
        Vector3f RenderView::ScreenPosToWorldDir(Vector2f pos)
        {
            auto vp_size = GetViewportSize();
            auto p = Camera::sCurrent->GetProj();
            float vx = (2.0f * pos.x / vp_size.x - 1.0f) / p[0][0];
            float vy = (-2.0f * pos.y / vp_size.y + 1.0f) / p[1][1];
            auto inv_view = MatrixInverse(Camera::sCurrent->GetView());
            Vector3f ray_dir = Vector3f(vx, vy, 1.0f);
            TransformNormal(ray_dir, inv_view);
            return Normalize(ray_dir);
        }
        //----------------------------------------------------------------------------RenderView End--------------------------------------------------
        SceneView::SceneView() : RenderView("SceneView")
        {
        }
        SceneView::~SceneView()
        {
        }
        void SceneView::ShowImpl()
        {
            if (_src)
            {
                static const auto &cur_window = Application::Get()->GetWindow();
                ImVec2 vMin = ImGui::GetWindowContentRegionMin();
                ImVec2 vMax = ImGui::GetWindowContentRegionMax();
                vMin.x += ImGui::GetWindowPos().x;
                vMin.y += ImGui::GetWindowPos().y;
                vMax.x += ImGui::GetWindowPos().x;
                vMax.y += ImGui::GetWindowPos().y;
                ImVec2 gameview_size;
                //gameview_size.x = vMax.x - vMin.x;
                //gameview_size.y = vMax.y - vMin.y;
                gameview_size.x = _content_size.x;
                gameview_size.y = _content_size.y;
                Vector2D<u32> window_size = Vector2D<u32>{static_cast<u32>(cur_window.GetWidth()), static_cast<u32>(cur_window.GetHeight())};
                // 设置窗口背景颜色
                ImGuiStyle &style = ImGui::GetStyle();
                auto origin_color = style.Colors[ImGuiCol_WindowBg];
                style.Colors[ImGuiCol_WindowBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);// 设置窗口背景颜色为灰色
                ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(_src->ColorTexture()), gameview_size);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
                {
                    _is_focus = true;
                    ImGui::SetKeyboardFocusHere();
                }
                style.Colors[ImGuiCol_WindowBg] = origin_color;
                static bool s_is_begin_resize = false;
                static u32 s_resize_end_frame_count = 0;
                if (_pre_tick_width != gameview_size.x || _pre_tick_height != gameview_size.y)
                {
                    s_is_begin_resize = true;
                    if (!s_is_begin_resize)
                    {
                        _pre_tick_window_width = (f32) window_size.x;
                        _pre_tick_window_height = (f32) window_size.y;
                    }
                }
                else
                {
                    if (s_is_begin_resize)
                    {
                        ++s_resize_end_frame_count;
                    }
                }
                if (s_resize_end_frame_count > Application::kTargetFrameRate * 0.5f)
                {
                    Camera::sCurrent->Rect((u16) gameview_size.x, (u16) gameview_size.y);
                    //g_pRenderer->ResizeBuffer(static_cast<u32>(gameview_size.x), static_cast<u32>(gameview_size.y));
                    if (_pre_tick_window_width != window_size.x || _pre_tick_window_height != window_size.y)
                    {
                        g_pGfxContext->ResizeSwapChain(window_size.x, window_size.y);
                    }
                    s_is_begin_resize = false;
                    s_resize_end_frame_count = 0;
                }
                _pre_tick_width = gameview_size.x;
                _pre_tick_height = gameview_size.y;
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kDragAssetMesh.c_str()))
                    {
                        auto mesh = *(Asset **) (payload->Data);
                        g_pSceneMgr->ActiveScene()->AddObject(mesh->AsRef<Mesh>(), nullptr);
                    }
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(PlaceActors::kDragPlacementObj.c_str()))
                    {
                        OnActorPlaced(EPlaceActorsType::kObj);
                        auto type_str = (const char *) (payload->Data);
                        OnActorPlaced(EPlaceActorsType::FromString(type_str), true);
                    }
                    ImGui::EndDragDropTarget();
                }
                ProcessTransformGizmo();
            }
            else
            {
                ImGui::LabelText("RenderView", "No Render Target");
            }
            Input::BlockInput(!_is_focus);
        }

        void SceneView::OnEvent(Event &e)
        {
            ImGuiWidget::OnEvent(e);
            if (!_is_focus || !Hover(Input::GetMousePos()))
                return;

            if (e.GetEventType() == EEventType::kKeyPressed)
            {
                if (FirstPersonCameraController::s_inst._is_receive_input)
                    return;
                auto &key_e = static_cast<KeyPressedEvent &>(e);
                if (key_e.GetKeyCode() == AL_KEY_W)
                {
                    _transform_gizmo_type = (i16) ImGuizmo::OPERATION::TRANSLATE;
                }
                else if (key_e.GetKeyCode() == AL_KEY_E)
                {
                    _transform_gizmo_type = (i16) ImGuizmo::OPERATION::ROTATE;
                }
                else if (key_e.GetKeyCode() == AL_KEY_R)
                {
                    _transform_gizmo_type = (i16) ImGuizmo::OPERATION::SCALE;
                }
                else if (key_e.GetKeyCode() == AL_KEY_Q)
                {
                    _is_transform_gizmo_world = !_is_transform_gizmo_world;
                }
                else {}
            }
            if (e.GetEventType() == EEventType::kKeyReleased)
            {
                if (FirstPersonCameraController::s_inst._is_receive_input)
                    return;
                auto &key_e = static_cast<KeyReleasedEvent &>(e);
                //                if(key_e.GetKeyCode() == AL_KEY_CONTROL)
                //                {
                //                    _is_transform_gizmo_snap = false;
                //                }
            }
        }

        void SceneView::ProcessTransformGizmo()
        {
            auto &selected_entities = Selection::SelectedEntities();
            if (_transform_gizmo_type == -1 || selected_entities.empty())
                return;
            //snap
            f32 snap_value = 0.5f;//50cm
            if (_transform_gizmo_type == (i16) ImGuizmo::OPERATION::ROTATE)
            {
                snap_value = 22.5f;//degree
            }
            else if (_transform_gizmo_type == (i16) ImGuizmo::OPERATION::SCALE)
            {
                snap_value = 0.25f;
            }
            else {}
            Vector3f snap_values = Vector3f(snap_value);
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(_content_pos.x, _content_pos.y, _content_size.x, _content_size.y);
            const auto &view = Camera::sCurrent->GetView();
            auto proj = MatrixReverseZ(Camera::sCurrent->GetProjNoJitter());
            _is_transform_gizmo_snap = Input::IsKeyPressed(AL_KEY_CONTROL);
            bool is_pivot_point_center = true;
            bool is_single_mode = selected_entities.size() == 1;
            Vector3f pivot_point;

            if (is_pivot_point_center)
            {
                auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
                for (auto e: selected_entities)
                {
                    pivot_point += r.GetComponent<ECS::TransformComponent>(e)->_transform._position;
                }
                pivot_point /= (f32) selected_entities.size();
                Matrix4x4f world_mat = MatrixTranslation(pivot_point);
                if (is_single_mode)
                    world_mat = r.GetComponent<ECS::TransformComponent>(selected_entities.front())->_transform._world_matrix;
                else
                    _is_transform_gizmo_world = true;
                ImGuizmo::Manipulate(view, proj, (ImGuizmo::OPERATION) _transform_gizmo_type, _is_transform_gizmo_world ? ImGuizmo::WORLD : ImGuizmo::LOCAL, world_mat, nullptr,
                                     _is_transform_gizmo_snap ? snap_values.data : nullptr);
                static bool s_can_duplicate = true;
                if (ImGuizmo::IsUsing())
                {
                    if (!_is_begin_gizmo_transform)
                    {
                        _is_begin_gizmo_transform = true;
                        _is_end_gizmo_transform = false;
                        for (auto e: selected_entities)
                        {
                            _old_trans.push_back(r.GetComponent<ECS::TransformComponent>(e)->_transform);
                        }
                    }
                    Vector3f new_pos;
                    Vector3f new_scale;
                    Quaternion new_rot;
                    DecomposeMatrix(world_mat, new_pos, new_rot, new_scale);
                    static Vector3f s_pre_tick_new_scale = new_scale;
                    if (is_single_mode)
                    {
                        auto &t = r.GetComponent<ECS::TransformComponent>(selected_entities.front())->_transform;
                        t._position = new_pos;
                        t._rotation = new_rot;
                        t._scale = new_scale;
                    }
                    else
                    {

                        u16 index = 0;
                        for (auto e: selected_entities)
                        {
                            auto &t = r.GetComponent<ECS::TransformComponent>(e)->_transform;
                            Vector3f rela_pos = t._position - pivot_point;
                            rela_pos = new_rot * rela_pos;
                            Vector3f scaled_pos = rela_pos * (new_scale / s_pre_tick_new_scale);
                            Vector3f pivot_offset = new_pos - pivot_point;
                            t._position = pivot_point + scaled_pos + pivot_offset;
                            //TODO
                            t._scale = new_scale * _old_trans[index]._scale;
                            t._rotation = new_rot * _old_trans[index++]._rotation;
                        }
                    }
                    s_pre_tick_new_scale = new_scale;
                    /*
                    区分左侧和右侧的 Alt 键需要结合使用扫描码。在处理低级键盘输入时，可以通过 GetKeyState 或 GetAsyncKeyState 等函数来检测具体的按键位置。
                    对于左侧 Alt 键，它的扫描码为：0x38(扫描码)
                    右侧 Alt 键(也称 AltGr)：0xE038(扩展扫描码)
                    */
                    if (s_can_duplicate && Input::IsKeyPressed(AL_KEY_ALT) && _transform_gizmo_type == (i16) ImGuizmo::OPERATION::TRANSLATE)
                    {
                        s_can_duplicate = false;
                        List<ECS::Entity> new_entities;
                        for (auto e: selected_entities)
                        {
                            new_entities.push_back(g_pSceneMgr->ActiveScene()->DuplicateEntity(e));
                        }
                        Selection::RemoveSlection();
                        for (auto &e: new_entities)
                            Selection::AddSelection(e);
                    }
                }
                else
                {
                    if (_is_begin_gizmo_transform)
                    {
                        s_can_duplicate = true;
                        _is_end_gizmo_transform = true;
                        _is_begin_gizmo_transform = false;
                        if (is_single_mode)
                        {
                            auto e = selected_entities.front();
                            auto t = r.GetComponent<ECS::TransformComponent>(e);
                            std::unique_ptr<ICommand> moveCommand1 = std::make_unique<TransformCommand>(r.GetComponent<ECS::TagComponent>(e)->_name, t, _old_trans.front());
                            g_pCommandMgr->ExecuteCommand(std::move(moveCommand1));
                            _old_trans.clear();
                        }
                        else
                        {
                            Vector<String> obj_names;
                            Vector<ECS::TransformComponent *> comps;
                            u16 index = 0u;
                            for (auto e: selected_entities)
                            {
                                obj_names.emplace_back(r.GetComponent<ECS::TagComponent>(e)->_name);
                                comps.emplace_back(r.GetComponent<ECS::TransformComponent>(e));
                            }
                            std::unique_ptr<ICommand> moveCommand1 = std::make_unique<TransformCommand>(std::move(obj_names), std::move(comps), std::move(_old_trans));
                            g_pCommandMgr->ExecuteCommand(std::move(moveCommand1));
                        }
                    }
                }
                Selection::Active(!ImGuizmo::IsOver());
            }
        }
        void SceneView::OnActorPlaced(EPlaceActorsType::EPlaceActorsType type, bool dropped)
        {
            auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
            //LOG_INFO("SceneView::OnActorPlaced wait for impl");
            auto ray_dir = ScreenPosToWorldDir(GlobalToLocal(Input::GetMousePos()));
            Plane p(Vector3f::kUp, 0.0f);
            Vector3f start = Camera::sCurrent->Position();
            Vector3f place_pos;
            ECS::Entity closest_entity = g_pSceneMgr->ActiveScene()->Pick(Ray(start, ray_dir));
            if (closest_entity != ECS::kInvalidEntity && r.HasComponent<ECS::StaticMeshComponent>(closest_entity))
            {
                const auto &box = r.GetComponent<ECS::StaticMeshComponent>(closest_entity)->_transformed_aabbs[0];
                place_pos = box.Center();
                place_pos.y += box.Size().y * 0.5f + 2.0f;
                Plane p(Vector3f::kUp, place_pos.y);
                if (p.Intersect(Camera::sCurrent->Position(), ray_dir, place_pos))
                    Gizmo::DrawCube(place_pos, Vector3f{2.f, 2.f, 2.f}, Colors::kRed);
            }
            if (dropped)
            {
                auto new_entity = g_pSceneMgr->ActiveScene()->AddObject();
                auto tag = r.GetComponent<ECS::TagComponent>(new_entity);
                if (type == EPlaceActorsType::kLightProbe)
                {
                    tag->_name = std::format("light_probe_{}", r.EntityNum());
                    r.AddComponent<ECS::CLightProbe>(new_entity);
                }
                else if (type == EPlaceActorsType::kCube)
                {
                    tag->_name = std::format("cube_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_cube.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                    auto &c = r.AddComponent<ECS::CCollider>(new_entity);
                    c._type = ECS::EColliderType::kBox;
                    c._param = Vector3f::kOne;
                    r.AddComponent<ECS::CRigidBody>(new_entity);
                }
                else if (type == EPlaceActorsType::kCylinder)
                {
                    tag->_name = std::format("cylinder_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_cylinder.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                    auto &capsule = r.AddComponent<ECS::CCollider>(new_entity);
                    capsule._type = ECS::EColliderType::kCapsule;
                    capsule._param = {1.f, 2.f, 1.f};
                    r.AddComponent<ECS::CRigidBody>(new_entity);
                }
                else if (type == EPlaceActorsType::kCone)
                {
                    tag->_name = std::format("cone_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_cone.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                    auto &c = r.AddComponent<ECS::CCollider>(new_entity);
                    c._type = ECS::EColliderType::kBox;
                    c._param = Vector3f::kOne;
                    r.AddComponent<ECS::CRigidBody>(new_entity);
                }
                else if (type == EPlaceActorsType::kTorus)
                {
                    tag->_name = std::format("torus_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_torus.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                    auto &c = r.AddComponent<ECS::CCollider>(new_entity);
                    c._type = ECS::EColliderType::kBox;
                    c._param = Vector3f::kOne;
                    r.AddComponent<ECS::CRigidBody>(new_entity);
                }
                else if (type == EPlaceActorsType::kSphere)
                {
                    tag->_name = std::format("sphere_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_shpere.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                    r.AddComponent<ECS::CCollider>(new_entity)._type = ECS::EColliderType::kSphere;
                    r.AddComponent<ECS::CRigidBody>(new_entity);
                }
                else if (type == EPlaceActorsType::kCapsule)
                {
                    tag->_name = std::format("capsule_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_capsule.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                    auto &capsule = r.AddComponent<ECS::CCollider>(new_entity);
                    capsule._type = ECS::EColliderType::kCapsule;
                    capsule._param = {1.f, 2.f, 1.f};
                    r.AddComponent<ECS::CRigidBody>(new_entity);
                }
                else if (type == EPlaceActorsType::kPlane)
                {
                    tag->_name = std::format("plane_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_plane.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                    auto &box = r.AddComponent<ECS::CCollider>(new_entity);
                    box._param.xyz = Vector3f(1.f, 0.01f, 1.f);
                    r.AddComponent<ECS::CRigidBody>(new_entity);
                }
                else if (type == EPlaceActorsType::kDirectionalLight)
                {
                    tag->_name = std::format("directional_light_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::LightComponent>(new_entity);
                    comp._type = ECS::ELightType::kDirectional;
                }
                else if (type == EPlaceActorsType::kPointLight)
                {
                    tag->_name = std::format("point_light_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::LightComponent>(new_entity);
                    comp._type = ECS::ELightType::kPoint;
                }
                else if (type == EPlaceActorsType::kSpotLight)
                {
                    tag->_name = std::format("spot_light_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::LightComponent>(new_entity);
                    comp._type = ECS::ELightType::kSpot;
                }
                else if (type == EPlaceActorsType::kAreaLight)
                {
                    tag->_name = std::format("area_light_{}", r.EntityNum());
                    auto &comp = r.AddComponent<ECS::LightComponent>(new_entity);
                    comp._type = ECS::ELightType::kArea;
                }
                else {};
                r.GetComponent<ECS::TransformComponent>(new_entity)->_transform._position = place_pos;
                LOG_INFO("Place new LightProbe");
            }
        }

    }// namespace Editor
}// namespace Ailu
