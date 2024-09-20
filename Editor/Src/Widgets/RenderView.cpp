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
            _is_hide_common_widget_info = true;
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
                ImVec2 img_size = ImVec2(w, w);
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
            auto p = Camera::sCurrent->GetProjection();
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
                static_cast<CommonRenderPipeline *>(g_pGfxContext->GetPipeline())->GetRenderer()->AddFeature(&_pick);

                static const auto &cur_window = Application::GetInstance()->GetWindow();
                ImVec2 vMin = ImGui::GetWindowContentRegionMin();
                ImVec2 vMax = ImGui::GetWindowContentRegionMax();
                vMin.x += ImGui::GetWindowPos().x;
                vMin.y += ImGui::GetWindowPos().y;
                vMax.x += ImGui::GetWindowPos().x;
                vMax.y += ImGui::GetWindowPos().y;
                ImVec2 gameview_size;
                gameview_size.x = vMax.x - vMin.x;
                gameview_size.y = vMax.y - vMin.y;
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
                    OnActorPlaced(EPlaceActorsType::kObj);
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(PlaceActors::kDragPlacementObj.c_str()))
                    {
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
                //                else if(key_e.GetKeyCode() == AL_KEY_CONTROL)
                //                {
                //                    _is_transform_gizmo_snap = true;
                //                }
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
            if (e.GetEventType() == EEventType::kMouseButtonPressed)
            {
                Vector2f vp_size = GetViewportSize();
                MouseButtonPressedEvent &event = static_cast<MouseButtonPressedEvent &>(e);
                if (event.GetButton() == AL_KEY_LBUTTON)
                {
                    //LOG_INFO("Pick wait for impl");
                    auto m_pos = Input::GetMousePos();
                    if (Hover(m_pos))
                    {
                        auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
                        auto mpos_l = GlobalToLocal(Input::GetMousePos());

                        auto ray_dir = ScreenPosToWorldDir(mpos_l);
                        Vector3f start = Camera::sCurrent->Position();
                        Ray ray(start, ray_dir);
                        ECS::Entity closest_entity = _pick.GetPickID(mpos_l.x, mpos_l.y);
                        //;g_pSceneMgr->ActiveScene()->Pick(ray);
                        if (Input::IsKeyPressed(AL_KEY_SHIFT))
                            Selection::AddSelection(closest_entity);
                        else
                            Selection::AddAndRemovePreSelection(closest_entity);
                        if (closest_entity == ECS::kInvalidEntity)
                        {
                            //Selection::RemoveSlection();
                            LOG_INFO("Pick nothing...");
                        }
                    }
                }
            }
        }
        void SceneView::ProcessTransformGizmo()
        {
            auto selected_entity = Selection::FirstEntity();
            if (selected_entity != ECS::kInvalidEntity && _transform_gizmo_type != -1)
            {
                //snap
                f32 snap_value = 50.f;//50cm
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
                ImGuizmo::SetRect(_global_pos.x, _global_pos.y, _size.x, _size.y + ImGui::GetFrameHeight());
                //LOG_INFO("Rect: {},{},MousePos: {}",_global_pos.ToString(),_size.ToString(),Input::GetMousePos().ToString());
                TransformComponent *transf_comp = g_pSceneMgr->ActiveScene()->GetRegister().GetComponent<TransformComponent>(selected_entity);
                auto &world_mat = transf_comp->_world_matrix;
                const auto &view = Camera::sCurrent->GetView();
                const auto &proj = Camera::sCurrent->GetProjection();
                _is_transform_gizmo_snap = Input::IsKeyPressed(AL_KEY_CONTROL);
                ImGuizmo::Manipulate(view, proj, (ImGuizmo::OPERATION) _transform_gizmo_type, _is_transform_gizmo_world ? ImGuizmo::WORLD : ImGuizmo::LOCAL, world_mat, nullptr,
                                     _is_transform_gizmo_snap ? snap_values.data : nullptr);
                Selection::Active(!ImGuizmo::IsOver());
                static Transform old_transf;
                if (ImGuizmo::IsUsing())
                {
                    if (!_is_begin_gizmo_transform)
                    {
                        _is_begin_gizmo_transform = true;
                        _is_end_gizmo_transform = false;
                        old_transf = transf_comp->_transform;
                    }
                    Vector3f new_pos;
                    Vector3f new_scale;
                    Quaternion new_rot;
                    DecomposeMatrix(world_mat, new_pos, new_rot, new_scale);
                    transf_comp->_transform._position = new_pos;
                    transf_comp->_transform._rotation = new_rot;
                    transf_comp->_transform._scale = new_scale;
                }
                else
                {
                    if (_is_begin_gizmo_transform)
                    {
                        _is_end_gizmo_transform = true;
                        _is_begin_gizmo_transform = false;
                        std::unique_ptr<ICommand> moveCommand1 = std::make_unique<TransformCommand>(transf_comp, old_transf);
                        g_pCommandMgr->ExecuteCommand(std::move(moveCommand1));
                    }
                }
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
            if (closest_entity != ECS::kInvalidEntity && r.HasComponent<StaticMeshComponent>(closest_entity))
            {
                const auto &box = r.GetComponent<StaticMeshComponent>(closest_entity)->_transformed_aabbs[0];
                place_pos = box.Center();
                place_pos.y += box.Size().y * 0.5f + 2.0f;
                Plane p(Vector3f::kUp, place_pos.y);
                if (p.Intersect(Camera::sCurrent->Position(), ray_dir, place_pos))
                    Gizmo::DrawCube(place_pos, Vector3f{2.f, 2.f, 2.f}, Colors::kRed);
            }
            if (dropped)
            {
                auto new_entity = g_pSceneMgr->ActiveScene()->AddObject();
                auto tag = r.GetComponent<TagComponent>(new_entity);
                if (type == EPlaceActorsType::kLightProbe)
                {
                    tag->_name = std::format("light_probe_{}", r.EntityNum());
                    r.AddComponent<CLightProbe>(new_entity);
                }
                else if (type == EPlaceActorsType::kCube)
                {
                    tag->_name = std::format("cube_{}", r.EntityNum());
                    auto &comp = r.AddComponent<StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_cube.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                }
                else if (type == EPlaceActorsType::kSphere)
                {
                    tag->_name = std::format("sphere_{}", r.EntityNum());
                    auto &comp = r.AddComponent<StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_shpere.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                }
                else if (type == EPlaceActorsType::kPlane)
                {
                    tag->_name = std::format("plane_{}", r.EntityNum());
                    auto &comp = r.AddComponent<StaticMeshComponent>(new_entity);
                    comp._p_mesh = Mesh::s_p_plane.lock();
                    comp._p_mats.push_back(Material::s_checker.lock());
                    comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
                }
                else if (type == EPlaceActorsType::kDirectionalLight)
                {
                    tag->_name = std::format("directional_light_{}", r.EntityNum());
                    auto &comp = r.AddComponent<LightComponent>(new_entity);
                    comp._type = ELightType::kDirectional;
                }
                else if (type == EPlaceActorsType::kPointLight)
                {
                    tag->_name = std::format("point_light_{}", r.EntityNum());
                    auto &comp = r.AddComponent<LightComponent>(new_entity);
                    comp._type = ELightType::kPoint;
                }
                else if (type == EPlaceActorsType::kSpotLight)
                {
                    tag->_name = std::format("spot_light_{}", r.EntityNum());
                    auto &comp = r.AddComponent<LightComponent>(new_entity);
                    comp._type = ELightType::kSpot;
                }
                else if (type == EPlaceActorsType::kAreaLight)
                {
                    tag->_name = std::format("area_light_{}", r.EntityNum());
                    auto &comp = r.AddComponent<LightComponent>(new_entity);
                    comp._type = ELightType::kArea;
                }
                else {};
                r.GetComponent<TransformComponent>(new_entity)->_transform._position = place_pos;
                LOG_INFO("Place new LightProbe");
            }
        }

    }// namespace Editor
}// namespace Ailu
