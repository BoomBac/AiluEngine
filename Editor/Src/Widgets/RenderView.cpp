#include "Widgets/RenderView.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "Render/Camera.h"
#include "Render/RenderPipeline.h"
#include "EditorApp.h"
#include "Widgets/EditorLayer.h"
#include "Common/Selection.h"
#include "Framework/Common/Input.h"
#include "Common/Undo.h"
#include "Common/TransformGizmo.h"
#include "UI/DragDrop.h"

#include "Render/Gizmo.h"
#include "Render/Material.h"
#include "Physics/Collision.h"

using namespace Ailu::UI;

namespace Ailu
{

    Render::Renderer *s_renderer = nullptr;
    namespace Editor
    {
#pragma region FirstPersonCameraController
        FirstPersonCameraController FirstPersonCameraController::s_inst;

        FirstPersonCameraController::FirstPersonCameraController()
            : FirstPersonCameraController(nullptr) {}

        FirstPersonCameraController::FirstPersonCameraController(Render::Camera *camera)
            : _p_camera(camera) {}
        void FirstPersonCameraController::Attach(Render::Camera *camera)
        {
            _p_camera = camera;
            _rot_world_y = Quaternion::AngleAxis(_rotation.y, Vector3f::kUp);
            Vector3f new_camera_right = _rot_world_y * Vector3f::kRight;
            _rot_object_x = Quaternion::AngleAxis(_rotation.x, new_camera_right);
            _target_pos = camera->Position();
        }

        void FirstPersonCameraController::Move(const Vector3f &d)
        {
            if (!_is_receive_input)
                return;
            _p_camera->Position(d);
        }
        void FirstPersonCameraController::SetTargetRotation(f32 x, f32 y, bool is_force)
        {
            if (!_is_receive_input && !is_force)
                return;
            _rotation.x = x;
            _rotation.y = y;
        }
        void FirstPersonCameraController::Interpolate(float speed)
        {
            _fast_camera_move_speed = _base_camera_move_speed * 3.0f;
            Quaternion new_quat_y = Quaternion::AngleAxis(_rotation.y, Vector3f::kUp);
            Vector3f new_camera_right = new_quat_y * Vector3f::kRight;
            auto new_quat_x = Quaternion::AngleAxis(_rotation.x, new_camera_right);
            _rot_object_x = Quaternion::NLerp(_rot_object_x, new_quat_x, speed);
            _rot_world_y = Quaternion::NLerp(_rot_world_y, new_quat_y, speed);
            _p_camera->Position(Lerp(_p_camera->Position(), _target_pos, speed));
            auto r = _rot_world_y * _rot_object_x;
            _p_camera->Rotation(r);
            _p_camera->RecalculateMatrix(true);
        }
        void FirstPersonCameraController::SetTargetPosition(const Vector3f &position, bool is_force)
        {
            if (!_is_receive_input && !is_force)
                return;
            _target_pos = position;
        }
        #pragma endregion

        static class EditorLayer* s_editor_layer;
        RenderView::RenderView() : DockWindow("RenderView")
        {
            _vb = _content_root->AddChild<UI::VerticalBox>();
            _vb->SlotSizePolicy(UI::ESizePolicy::kFixed);
            _vb->SlotPadding(_content_root->Thickness());
            _source = _vb->AddChild<UI::Image>();
            _source->SlotSizePolicy(UI::ESizePolicy::kFill);
            _source->SlotAlignmentH(UI::EAlignment::kFill);
            _on_size_change += [this](Vector2f new_size)
            {
                auto t = _content_root->Thickness();
                _vb->SlotSize(new_size.x, new_size.y - kTitleBarHeight);
                if (Render::Camera::sCurrent)
                    Render::Camera::sCurrent->OutputSize((u16) (new_size.x - t.x - t.z), (u16) (new_size.y - kTitleBarHeight - t.y - t.w));
            };
            _on_size_change_delegate.Invoke(_size);
            s_renderer = Render::RenderPipeline::Get().GetRenderer();
            for (auto &layer: EditorApp::Get().GetLayerStack())
            {
                if (s_editor_layer = dynamic_cast<EditorLayer *>(layer); s_editor_layer != nullptr)
                {
                    break;
                }
            }
        }
        void RenderView::Update(f32 dt)
        {
            DockWindow::Update(dt);
            _source->SetTexture(s_renderer->TargetTexture());
        }
        void RenderView::SetSource(Render::Texture *tex)
        {
            if (tex)
            {
                _source->SetTexture(tex);
            }
        }
        SceneView::SceneView()
        {
            if (Camera::sCurrent == nullptr)
            {
                LOG_WARNING("Current Camera is null");
                Camera::GetDefaultCamera();
                Camera::sCurrent->_anti_aliasing = Render::EAntiAliasing::kNone;
            }
            FirstPersonCameraController::s_inst.Attach(Camera::sCurrent);
            FirstPersonCameraController::s_inst._is_receive_input = true;
            FirstPersonCameraController::s_inst._camera_near = Camera::sCurrent->Near();
            FirstPersonCameraController::s_inst._camera_far = Camera::sCurrent->Far();
            Camera::sCurrent->_is_scene_camera = true;
            Camera::sCurrent->_is_gen_voxel = true;
            Camera::sCurrent->_is_enable = true;

            _source->OnMouseMove() += [this](UI::UIEvent &e) {
                Vector4f rect = e._current_target->GetArrangeRect();
                _mouse_pos = e._mouse_position - rect.xy;
                if (DragDropManager::Get().IsDragging(EDragType::kMesh))
                {
                    auto &payload = DragDropManager::Get().GetPayload();
                    _drag_preview_mesh = reinterpret_cast<Asset*>(payload->_data)->AsRef<Render::Mesh>();
                    Ray ray{Camera::sCurrent->Position(), Camera::sCurrent->ScreenToWorld(_mouse_pos)};
                    if (auto hit = g_pSceneMgr->ActiveScene()->Pick(ray); hit != ECS::kInvalidEntity)
                    {
                        auto &box = g_pSceneMgr->ActiveScene()->GetRegister().GetComponent<ECS::StaticMeshComponent>(hit)->_transformed_aabbs[0];
                        Vector3f p = box.Center();
                        p.y += box.GetHalfAxisLength().y;
                        Plane plane{p, Vector3f::kUp};
                        _drag_preview_pos = CollisionDetection::Intersect(ray, plane)._point;
                        _drag_preview_pos.y += _drag_preview_mesh->BoundBox()[0].GetHalfAxisLength().y;
                    }
                }
                else
                    _drag_preview_mesh = nullptr;
            };
            // 新增：按下开始拖拽
            _source->OnMouseDown() += [this](UI::UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    Vector4f rect = e._current_target->GetArrangeRect();
                    Vector2f local_pos = e._mouse_position - rect.xy;
                    if (_transform_gizmo->IsHover(local_pos,Camera::sCurrent))
                        _transform_gizmo->BeginDrag(local_pos);
                    // 可视需要决定是否拦截
                    // e._is_handled = true;
                }
                else if (e._key_code == EKey::kRBUTTON)
                {
                    FirstPersonCameraController::s_inst._is_receive_input = true;
                    SetCursor(NULL);
                }
            };
            // 新增：抬起结束拖拽
            _source->OnMouseUp() += [this](UI::UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    if (_transform_gizmo->IsDragging())
                    {
                        _transform_gizmo->EndDrag();
                    }
                    else
                    {
                        Vector4f rect = e._current_target->GetArrangeRect();
                        Vector2f local_pos = e._mouse_position - rect.xy;
                        ECS::Entity closest_entity = s_editor_layer->_pick.GetPickID((u16) local_pos.x, (u16) local_pos.y);
                        LOG_INFO("Pick entity: {} on pos {}", closest_entity, local_pos.ToString());
                        Selection::AddAndRemovePreSelection(closest_entity);
                        auto tcomp = g_pSceneMgr->ActiveScene()->GetRegister().GetComponent<ECS::TransformComponent>(closest_entity);
                        _transform_gizmo->SetTarget(&tcomp->_transform);
                    }
                }
                else if (e._key_code == EKey::kRBUTTON)
                {
                    FirstPersonCameraController::s_inst._is_receive_input = false;
                    while (::ShowCursor(TRUE) < 0);
                }
            };
            _source->OnKeyDown() += [this](UI::UIEvent &e)
            {
                if (e._key_code == EKey::kW)
                {
                    _transform_gizmo->SetMode(EGizmoMode::kTranslate);
                }
                else if (e._key_code == EKey::kE)
                {
                    _transform_gizmo->SetMode(EGizmoMode::kRotate);
                }
                else if (e._key_code == EKey::kR)
                {
                    _transform_gizmo->SetMode(EGizmoMode::kScale);
                }
                else if (e._key_code == EKey::kSHIFT)
                {
                    FirstPersonCameraController::s_inst.Accelerate(true);
                }
            };
            _source->OnKeyUp() += [this](UI::UIEvent &e) {
                if (e._key_code == EKey::kSHIFT)
                {
                    FirstPersonCameraController::s_inst.Accelerate(false);
                }
            };

            _transform_gizmo = MakeScope<TransformGizmo>();
            DropHandler handler;
            handler._can_drop = [](const DragPayload &payload) -> bool
            {
                return payload._type == EDragType::kMesh;
            };
            handler._on_drop = [this](const DragPayload &payload, f32 x, f32 y)
            {
                LOG_INFO("SceneView {} drop", StaticEnum<EDragType>()->GetNameByEnum(payload._type));
                Vector<Ref<Render::Material>> mats;
                for (auto &m: _drag_preview_mesh->GetCacheMaterials())
                {
                    auto cur_mat = MakeRef<Render::StandardMaterial>(m._name);
                    cur_mat->SetVector(Render::StandardMaterial::StandardPropertyName::kAlbedo._value_name, m._diffuse);
                    mats.push_back(cur_mat);
                }
                g_pSceneMgr->ActiveScene()->AddObject(_drag_preview_mesh, mats);
            };
            _source->SetDropHandler(handler);
        }
        void SceneView::Update(f32 dt)
        {
            RenderView::Update(dt);
            if (Render::Camera::sCurrent)
            {
                auto rect = _source->GetArrangeRect();
                s_editor_layer->_viewport_size = Vector2f{rect.z, rect.w};
                s_editor_layer->_scene_vp_rect = rect;
            }
            _transform_gizmo->Update(dt, _mouse_pos,Camera::sCurrent);
            _transform_gizmo->Draw();
            if (_drag_preview_mesh)
            {
                Render::Gizmo::DrawMesh(_drag_preview_mesh.get(), MatrixTranslation(_drag_preview_pos), Render::Material::s_standard_forward_lit.lock().get());
            }
            ProcessCameraInput(dt);
        }
        void SceneView::ProcessCameraInput(f32 dt)
        {
            Camera::sCurrent->FovH(FirstPersonCameraController::s_inst._camera_fov_h);
            Camera::sCurrent->Near(FirstPersonCameraController::s_inst._camera_near);
            Camera::sCurrent->Far(FirstPersonCameraController::s_inst._camera_far);
            if (Camera::sCurrent && !Input::IsInputBlock())
            {
                static Vector2f target_rotation = {0.f, 0.f};
                static Vector2f pre_mouse_pos;
                target_rotation = FirstPersonCameraController::s_inst._rotation;
                auto cur_mouse_pos = Input::GetMousePos();
                if (Input::IsKeyPressed(EKey::kRBUTTON))
                {
                    if (abs(cur_mouse_pos.x - pre_mouse_pos.x) < 100.0f &&
                        abs(cur_mouse_pos.y - pre_mouse_pos.y) < 100.0f)
                    {
                        float angle_offset = FirstPersonCameraController::s_inst._camera_wander_speed * FirstPersonCameraController::s_inst._camera_wander_speed;
                        target_rotation.y += (cur_mouse_pos.x - pre_mouse_pos.x) * angle_offset;
                        target_rotation.x += (cur_mouse_pos.y - pre_mouse_pos.y) * angle_offset;
                    }
                }
                pre_mouse_pos = cur_mouse_pos;
                FirstPersonCameraController::s_inst.SetTargetRotation(target_rotation.x, target_rotation.y);
                FirstPersonCameraController::s_inst.Accelerate(Input::IsKeyPressed(EKey::kSHIFT));
                static const f32 move_distance = 1.0f;// 1 m
                f32 final_move_distance = move_distance * FirstPersonCameraController::s_inst._cur_move_speed * FirstPersonCameraController::s_inst._cur_move_speed;
                Vector3f move_dis{0, 0, 0};
                if (Input::IsKeyPressed(EKey::kW))
                {
                    move_dis += Camera::sCurrent->Forward();
                }
                if (Input::IsKeyPressed(EKey::kS))
                {
                    move_dis -= Camera::sCurrent->Forward();
                }
                if (Input::IsKeyPressed(EKey::kD))
                {
                    move_dis += Camera::sCurrent->Right();
                }
                if (Input::IsKeyPressed(EKey::kA))
                {
                    move_dis -= Camera::sCurrent->Right();
                }
                if (Input::IsKeyPressed(EKey::kE))
                {
                    move_dis += Camera::sCurrent->Up();
                }
                if (Input::IsKeyPressed(EKey::kQ))
                {
                    move_dis -= Camera::sCurrent->Up();
                }
                auto target_pos = FirstPersonCameraController::s_inst._target_pos;
                target_pos += move_dis * final_move_distance;
                //LOG_INFO("{}", target_pos.ToString());
                FirstPersonCameraController::s_inst.SetTargetPosition(target_pos);
            }
            f32 lerp_factor = std::clamp(dt * FirstPersonCameraController::s_inst._lerp_speed_multifactor, 0.0f, 1.0f);
            lerp_factor = dt * FirstPersonCameraController::s_inst._lerp_speed_multifactor * FirstPersonCameraController::s_inst._lerp_speed_multifactor;
            lerp_factor = std::clamp(lerp_factor, 0.0f, 1.5f);
            FirstPersonCameraController::s_inst.Interpolate(lerp_factor);
        }
    }// namespace Editor
}