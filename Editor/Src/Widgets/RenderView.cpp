#include "Widgets/RenderView.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "Render/Camera.h"
#include "Render/RenderPipeline.h"
#include "EditorApp.h"
#include "Widgets/EditorLayer.h"
#include "Common/Selection.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Common/Undo.h"
#include "Common/TransformGizmo.h"
#include "UI/DragDrop.h"
#include "UI/Composite.h"
#include <cmath>

#include "Render/Gizmo.h"
#include "Render/Material.h"
#include "Render/Features/MiscPasses.h"
#include "Physics/Collision.h"

#include "Render/Features/RayTraceGI.h"

using namespace Ailu::UI;

namespace Ailu
{

    Render::Renderer *s_renderer = nullptr;
    namespace Editor
    {
        using SceneManagement::SceneMgr;
        #pragma region RenderView
        static class EditorLayer* s_editor_layer;
        RenderView::RenderView() : DockWindow("RenderView")
        {
            _vb = _content_root->AddChild<UI::VerticalBox>();
            _vb->SlotSizePolicy(UI::ESizePolicy::kFixed);
            _vb->SlotPadding(_content_root->Thickness());
            _source = _vb->AddChild<UI::Image>();
            _source->SlotSizePolicy(UI::ESizePolicy::kFill);
            _source->SlotAlignmentH(UI::EAlignment::kFill);
        }
        void RenderView::Update(f32 dt)
        {
            DockWindow::Update(dt);
        }
        void RenderView::SetSource(Render::Texture *tex)
        {
            if (tex)
            {
                _source->SetTexture(tex);
            }
        }
        #pragma endregion

        #pragma region SceneView
        SceneView::SceneView()
        {
            if (Camera::sCurrent == nullptr)
            {
                LOG_WARNING("Current Camera is null");
                Camera::GetDefaultCamera();
                Camera::sCurrent->_anti_aliasing = Render::EAntiAliasing::kNone;
            }
            _camera_controller = &Editor::EditorApp::GetEditor()->GetSceneCameraController();
            _camera_controller->Attach(Camera::sCurrent);
            _camera_controller->_is_receive_input = true;
            _camera_controller->_camera_near = Camera::sCurrent->Near();
            _camera_controller->_camera_far = Camera::sCurrent->Far();
            Camera::sCurrent->_is_scene_camera = true;
            Camera::sCurrent->_is_gen_voxel = true;
            Camera::sCurrent->_is_enable = true;
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
            _source->OnMouseMove() += [this](UI::UIEvent &e) {
                Vector4f rect = e._current_target->GetArrangeRect();
                _mouse_pos = e._mouse_position - rect.xy;
                if (DragDropManager::Get().IsDragging(EDragType::kMesh))
                {
                    auto &payload = DragDropManager::Get().GetPayload();
                    _drag_preview_mesh = reinterpret_cast<Asset*>(payload->_data)->AsRef<Render::Mesh>();
                    Ray ray{Camera::sCurrent->Position(), Camera::sCurrent->ScreenToWorld(_mouse_pos)};
                    if (auto hit = SceneMgr::Get().ActiveScene()->Pick(ray); hit != ECS::kInvalidEntity)
                    {
                        auto &box = SceneMgr::Get().ActiveScene()->GetRegister().GetComponent<ECS::StaticMeshComponent>(hit)->_transformed_aabbs[0];
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
                    _camera_controller->_is_receive_input = true;
                    SetCursor(NULL);
                }
            };
            static Render::RayTraceGI *ray_trace = nullptr;
            for (auto f : Render::RenderPipeline::Get().GetRenderer()->GetFeatures())
            {
                if (f->GetType() == Render::RayTraceGI::StaticType())
                {
                    ray_trace = dynamic_cast<Render::RayTraceGI *>(f);
                    break;
                }
            }
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
                        s_editor_layer->_pick.GetPickID((u16) local_pos.x, (u16) local_pos.y, [this, local_pos](u32 closest_entity,u32 submesh_index)
                        {
                            LOG_INFO("Pick entity: {},subidex: {} on pos {}", closest_entity, submesh_index, local_pos.ToString());
                            Selection::AddAndRemovePreSelection(closest_entity,submesh_index);
                            _transform_gizmo->SetTarget(SceneMgr::Get().ActiveScene(), closest_entity);
                            ray_trace->_debug_pos = local_pos;
                        });
                    }
                }
                else if (e._key_code == EKey::kRBUTTON)
                {
                    _camera_controller->_is_receive_input = false;
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
                    _camera_controller->Accelerate(true);
                }
            };
            _source->OnKeyUp() += [this](UI::UIEvent &e) {
                if (e._key_code == EKey::kSHIFT)
                {
                    _camera_controller->Accelerate(false);
                }
            };
            _source->OnMouseScroll() += [this](UI::UIEvent &e)
            {
                f32 d = _camera_controller->_base_camera_move_speed * 0.1f;
                _camera_controller->_base_camera_move_speed += e._scroll_delta>0.0f? d : -d;
                LOG_INFO("Camera move speed: {}", _camera_controller->_base_camera_move_speed);
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
                for (u16 i = 0; i < _drag_preview_mesh->SubmeshCount(); i++)
                {
                    auto mat = g_pResourceMgr->GetEmbeddedMaterial(_drag_preview_mesh.get(), i);
                    mats.push_back(mat? mat : Render::Material::s_checker.lock());
                }
                auto new_entity = SceneMgr::Get().ActiveScene()->AddObject(_drag_preview_mesh, mats);
                SceneMgr::Get().ActiveScene()->GetRegister().GetComponent<ECS::TransformComponent>(new_entity)->_transform._position = _drag_preview_pos;
            };
            _source->SetDropHandler(handler);
        }
        void SceneView::Update(f32 dt)
        {
            RenderView::Update(dt);
            SetSource(s_renderer->TargetTexture());
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
            Camera::sCurrent->FovH(_camera_controller->_camera_fov_h);
            Camera::sCurrent->Near(_camera_controller->_camera_near);
            Camera::sCurrent->Far(_camera_controller->_camera_far);
            if (Camera::sCurrent && !Input::IsInputBlock())
            {
                static Vector2f target_rotation = {0.f, 0.f};
                static Vector2f pre_mouse_pos;
                target_rotation = _camera_controller->_rotation;
                auto cur_mouse_pos = Input::GetGlobalMousePosAccurate();
                if (Input::IsKeyDown(EKey::kRBUTTON))
                {
                    if (abs(cur_mouse_pos.x - pre_mouse_pos.x) < 100.0f &&
                        abs(cur_mouse_pos.y - pre_mouse_pos.y) < 100.0f)
                    {
                        float angle_offset = _camera_controller->_camera_wander_speed * _camera_controller->_camera_wander_speed;
                        target_rotation.y += (cur_mouse_pos.x - pre_mouse_pos.x) * angle_offset;
                        target_rotation.x += (cur_mouse_pos.y - pre_mouse_pos.y) * angle_offset;
                    }
                }
                pre_mouse_pos = cur_mouse_pos;
                _camera_controller->SetTargetRotation(target_rotation.x, target_rotation.y);
                _camera_controller->Accelerate(Input::IsKeyDown(EKey::kSHIFT));
                static const f32 move_distance = 1.0f;// 1 m
                f32 final_move_distance = move_distance * _camera_controller->_cur_move_speed * _camera_controller->_cur_move_speed;
                Vector3f move_dis{0, 0, 0};
                if (Input::IsKeyDown(EKey::kW))
                {
                    move_dis += Camera::sCurrent->Forward();
                }
                if (Input::IsKeyDown(EKey::kS))
                {
                    move_dis -= Camera::sCurrent->Forward();
                }
                if (Input::IsKeyDown(EKey::kD))
                {
                    move_dis += Camera::sCurrent->Right();
                }
                if (Input::IsKeyDown(EKey::kA))
                {
                    move_dis -= Camera::sCurrent->Right();
                }
                if (Input::IsKeyDown(EKey::kE))
                {
                    move_dis += Camera::sCurrent->Up();
                }
                if (Input::IsKeyDown(EKey::kQ))
                {
                    move_dis -= Camera::sCurrent->Up();
                }
                auto target_pos = _camera_controller->_target_pos;
                target_pos += move_dis * final_move_distance;
                //LOG_INFO("{}", target_pos.ToString());
                _camera_controller->SetTargetPosition(target_pos);
            }
            f32 lerp_factor = std::clamp(dt * _camera_controller->_lerp_speed_multifactor, 0.0f, 1.0f);
            lerp_factor = dt * _camera_controller->_lerp_speed_multifactor * _camera_controller->_lerp_speed_multifactor;
            lerp_factor = std::clamp(lerp_factor, 0.0f, 1.5f);
            _camera_controller->Interpolate(lerp_factor);
        }
        #pragma endregion

        #pragma region Texture3DView
        Texture3DView::Texture3DView() : DockWindow("Texture3DView")
        {
            _split_view = _content_root->AddChild<UI::SplitView>();
            _split_view->_is_horizontal = true;
            _left_preview = _split_view->AddChild<UI::Image>();
            _left_preview->SlotSizePolicy(UI::ESizePolicy::kFill);
            _left_preview->SlotAlignmentH(UI::EAlignment::kFill);
            _right_menu = _split_view->AddChild<UI::VerticalBox>();
            _right_menu->SlotSizePolicy(UI::ESizePolicy::kFixed);
            _right_menu->SlotPadding(_content_root->Thickness());
            _pass = new Render::VolumeTexturePreviewPass();
            _orbit_controller.Attach(_pass);
            auto pass_type = _pass->GetType();
            _right_menu->AddChild(UI::CompositeBuilder::BuildPropertyElement("CameraPos", pass_type->FindPropertyByName("_camera_pos"), _pass));
            UI::FloatFieldParams fparams;
            fparams._range = Vector2f{0.0f, 1.0f};
            _right_menu->AddChild(UI::CompositeBuilder::BuildPropertyElement("IsSliceMode", pass_type->FindPropertyByName("_is_slice_mode"), _pass));
            _right_menu->AddChild(UI::CompositeBuilder::BuildPropertyElement("SliceX", pass_type->FindPropertyByName("_slice_x"), _pass,&fparams));
            _right_menu->AddChild(UI::CompositeBuilder::BuildPropertyElement("SliceY", pass_type->FindPropertyByName("_slice_y"), _pass,&fparams));
            _right_menu->AddChild(UI::CompositeBuilder::BuildPropertyElement("SliceZ", pass_type->FindPropertyByName("_slice_z"), _pass,&fparams));
            auto z_slice_prop = pass_type->FindPropertyByName("_slice_z");
            z_slice_prop->AddObserver(_pass, [this, z_slice_prop](void* instance)
            {
                auto z = z_slice_prop->Get<f32>(instance);
                LOG_INFO("Slice Z changed: {}", round(z * 96 - 0.5));
            });
            _pass->_on_target_ready = [this](Render::RenderTexture* rt)
            {
                _left_preview->SetTexture(rt);
            };
            _left_preview->OnMouseDown() += [this](UI::UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    Vector4f rect = e._current_target->GetArrangeRect();
                    Vector2f local_pos = e._mouse_position - rect.xy;
                    _orbit_controller.BeginDrag(local_pos);
                }
            };
            _left_preview->OnMouseUp() += [this](UI::UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    _orbit_controller.EndDrag();
                }
            };
            _left_preview->OnMouseMove() += [this](UI::UIEvent &e)
            {
                Vector4f rect = e._current_target->GetArrangeRect();
                Vector2f local_pos = e._mouse_position - rect.xy;
                _orbit_controller.Drag(local_pos);
            };
            _left_preview->OnMouseScroll() += [this](UI::UIEvent &e)
            {
                _orbit_controller.Zoom(e._scroll_delta);
            };
        }

        Texture3DView::~Texture3DView()
        {
            DESTORY_PTR(_pass);
            //Render::RenderPipeline::Get().GetRenderer()->RemoveTaskPass(_pass);
        }
        void Texture3DView::Update(f32 dt)
        {
            _pass->_view_size.x = (i32)_left_preview->GetArrangeRect().z;
            _pass->_view_size.y = (i32)_left_preview->GetArrangeRect().w;
            Render::RenderPipeline::Get().GetRenderer()->SubmitTaskPass(_pass);
            DockWindow::Update(dt);
        }

        void Texture3DView::SetSource3D(Render::Texture *tex)
        {
            _pass->_slice_mat->SetTexture("_MainTex", tex);
        }
        
        #pragma endregion
    }// namespace Editor
}