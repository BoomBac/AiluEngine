#include "Widgets/RenderView.h"
#include "Framework/Common/Allocator.hpp"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "Render/Camera.h"
#include "Render/RenderPipeline.h"
#include "Render/Texture.h"
#include "EditorApp.h"
#include "Widgets/EditorLayer.h"
#include "Common/Selection.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Common/Undo.h"
#include "Common/TransformGizmo.h"
#include "Assets/PrefabAsset.h"
#include "Scene/PrefabSystem.h"
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
        namespace
        {
            constexpr u32 kSceneViewRTAlign = 64u;
            constexpr f32 kSceneViewResizeDebounceTime = 0.15f;
            constexpr f32 kSceneToolbarHeight = 32.0f;
            constexpr f32 kSceneToolbarTop = 8.0f;
            constexpr f32 kSceneToolbarLeft = 12.0f;
            constexpr f32 kSceneToolbarButtonWidth = 92.0f;
            constexpr f32 kSceneToolbarSmallButtonWidth = 68.0f;
            constexpr f32 kSceneToolbarDropdownWidth = 82.0f;
            constexpr f32 kSceneToolbarButtonGap = 4.0f;
            const Vector2f kPreviewRTSize = Vector2f(320.0f, 180.0f) * 0.8f;

            u32 AlignSceneViewSize(f32 value)
            {
                const u32 clamped = std::max<u32>(1u, static_cast<u32>(std::ceil(value)));
                return ((clamped + kSceneViewRTAlign - 1u) / kSceneViewRTAlign) * kSceneViewRTAlign;
            }

            Vector2UInt CalculateSceneViewOutputSize(const Vector2f &view_size)
            {
                return Vector2UInt(AlignSceneViewSize(view_size.x), AlignSceneViewSize(view_size.y));
            }

            UIBrush MakeSceneToolbarBrush(const Color &color)
            {
                UIBrush brush;
                brush._type = EUIBrushType::kColor;
                brush._tint = color;
                return brush;
            }

            UIControlVisual MakeSceneToolbarButtonVisual(const Color &background, const Color &border, const Color &text)
            {
                UIControlVisual visual;
                visual._background = MakeSceneToolbarBrush(background);
                visual._border_color = border;
                visual._border_width = Vector4f(1.0f);
                visual._content_color = text;
                visual._corner_radius = Vector4f(4.0f);
                return visual;
            }

            void ApplySceneToolbarButtonStyle(Button *button, bool active)
            {
                if (button == nullptr)
                    return;

                const Color normal_bg = active ? Color(0.20f, 0.48f, 0.86f, 0.82f) : Color(0.07f, 0.09f, 0.12f, 0.58f);
                const Color hovered_bg = active ? Color(0.25f, 0.55f, 0.95f, 0.90f) : Color(0.12f, 0.15f, 0.19f, 0.72f);
                const Color pressed_bg = active ? Color(0.15f, 0.38f, 0.72f, 0.92f) : Color(0.05f, 0.07f, 0.10f, 0.82f);
                const Color border = active ? Color(0.55f, 0.78f, 1.0f, 0.76f) : Color(1.0f, 1.0f, 1.0f, 0.18f);

                auto &style = button->GetStyleOverride();
                style._normal = MakeSceneToolbarButtonVisual(normal_bg, border, Colors::kWhite);
                style._hovered = MakeSceneToolbarButtonVisual(hovered_bg, border, Colors::kWhite);
                style._pressed = MakeSceneToolbarButtonVisual(pressed_bg, border, Colors::kWhite);
                style._focused = MakeSceneToolbarButtonVisual(normal_bg, border, Colors::kWhite);
                style._padding = Padding(8.0f, 0.0f, 8.0f, 0.0f);
                style._min_size = Vector2f(kSceneToolbarButtonWidth, 24.0f);
                style._font_size = 13.0f;
                style._override_mask = (u32)EUIButtonStyleOverride::kNormal | (u32)EUIButtonStyleOverride::kHovered |
                                       (u32)EUIButtonStyleOverride::kPressed | (u32)EUIButtonStyleOverride::kFocused |
                                       (u32)EUIButtonStyleOverride::kPadding | (u32)EUIButtonStyleOverride::kMinSize |
                                       (u32)EUIButtonStyleOverride::kFontSize;
                button->InvalidateStyle();
            }

            void ApplySceneToolbarSmallButtonStyle(Button *button, bool active)
            {
                if (button == nullptr)
                    return;
                ApplySceneToolbarButtonStyle(button, active);
                auto &style = button->GetStyleOverride();
                style._min_size = Vector2f(kSceneToolbarSmallButtonWidth, 24.0f);
                button->InvalidateStyle();
            }

            i32 SceneView2DOrientationToIndex(ESceneView2DOrientation orientation)
            {
                return static_cast<i32>(orientation);
            }

            ESceneView2DOrientation SceneView2DOrientationFromIndex(i32 index)
            {
                switch (index)
                {
                    case 1: return ESceneView2DOrientation::kNegX;
                    case 2: return ESceneView2DOrientation::kNegY;
                    case 3: return ESceneView2DOrientation::kNegZ;
                    case 0:
                    default: return ESceneView2DOrientation::kXY;
                }
            }

            void GetSceneView2DCameraBasis(ESceneView2DOrientation orientation, Vector3f &direction, Vector3f &up)
            {
                switch (orientation)
                {
                    case ESceneView2DOrientation::kNegX:
                        direction = -Vector3f::kRight;
                        up = Vector3f::kUp;
                        break;
                    case ESceneView2DOrientation::kNegY:
                        direction = -Vector3f::kUp;
                        up = Vector3f::kForward;
                        break;
                    case ESceneView2DOrientation::kNegZ:
                        direction = -Vector3f::kForward;
                        up = Vector3f::kUp;
                        break;
                    case ESceneView2DOrientation::kXY:
                    default:
                        direction = -Vector3f::kForward;
                        up = Vector3f::kUp;
                        break;
                }
            }

            f32 CalculateSceneView2DSize(const Camera *camera, const Vector3f &direction)
            {
                constexpr f32 kDefault2DCameraSize = 20.0f;
                if (camera == nullptr)
                    return kDefault2DCameraSize;

                f32 distance_to_origin_plane = std::abs(DotProduct(camera->Position(), direction));
                if (distance_to_origin_plane <= 0.01f)
                    return camera->Size() > 0.0f ? camera->Size() : kDefault2DCameraSize;
                return std::max(0.05f, distance_to_origin_plane * 2.0f * std::tan(camera->FovH() * k2Radius * 0.5f));
            }
        }
        #pragma region RenderView
        static class EditorLayer* s_editor_layer;
        RenderView::RenderView() : DockWindow("RenderView")
        {
            _view_canvas = _content_root->AddChild<UI::Canvas>();
            _view_canvas->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed);
            _view_canvas->SlotPadding() = UI::Padding(_content_root->Thickness());
            _view_canvas->InvalidateLayout();
            _source = _view_canvas->AddChild<UI::Image>();
            _source->GetSlotAs<UI::CanvasSlot>().Position(Vector2f::kZero).Size(_size);
            _on_size_change += [this](Vector2f new_size)
            {
                auto t = _content_root->Thickness();
                const Vector2f content_size = {
                        std::max(1.0f, new_size.x - t.x - t.z),
                        std::max(1.0f, new_size.y - kTitleBarHeight - t.y - t.w)};
                _view_canvas->GetSlot()->Size(content_size);
                _source->GetSlotAs<UI::CanvasSlot>().Position(Vector2f::kZero).Size(content_size);
            };
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
            constexpr f32 kPreviewBorderWidth = 2.0f;
            constexpr f32 kPreviewCornerRadius = 10.0f;
            _camera_preview_frame = _view_canvas->AddChild<UI::Border>();
            _camera_preview_frame->GetSlotAs<UI::CanvasSlot>().Position(Vector2f::kZero).Size(kPreviewRTSize);
            _camera_preview_frame->Thickness(kPreviewBorderWidth);
            _camera_preview_frame->CornerRadius(kPreviewCornerRadius);
            _camera_preview_frame->_bg_color = Color(0.03f, 0.04f, 0.06f, 0.92f);
            _camera_preview_frame->_border_color = Color(1.0f, 1.0f, 1.0f, 0.22f);
            UIBrush preview_background;
            preview_background._type = EUIBrushType::kColor;
            preview_background._tint = _camera_preview_frame->_bg_color;
            auto &preview_style = _camera_preview_frame->GetStyleOverride();
            preview_style.SetBackground(preview_background);
            preview_style.SetBorderColor(_camera_preview_frame->_border_color);
            preview_style.SetBorderWidth(kPreviewBorderWidth);
            preview_style.SetCornerRadius(kPreviewCornerRadius);
            _camera_preview = _camera_preview_frame->AddChild<UI::Image>();
            _camera_preview->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _camera_preview->GetStyleOverride().SetCornerRadius(kPreviewCornerRadius - kPreviewBorderWidth);
            _camera_preview_frame->_visibility = UI::EVisibility::kHide;
            _camera_preview_frame->SetVisible(false);
            _stored_camera_type = Render::ECameraType::kPerspective;
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
            _canvas_camera_controller.Attach(Camera::sCurrent);
            Camera::sCurrent->_is_scene_camera = true;
            Camera::sCurrent->_is_gen_voxel = true;
            Camera::sCurrent->_is_enable = true;
            BuildSceneToolbar();
            _on_size_change += [this](Vector2f new_size)
            {
                auto t = _content_root->Thickness();
                _view_size = {
                        std::max(1.0f, new_size.x - t.x - t.z),
                        std::max(1.0f, new_size.y - kTitleBarHeight - t.y - t.w)};
                _pending_output_size = CalculateSceneViewOutputSize(_view_size);
                _resize_stable_time = 0.0f;
                UpdateSceneToolbarLayout();
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
            };
            // 新增：按下开始拖拽
            _source->OnMouseDown() += [this](UI::UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    Vector4f rect = e._current_target->GetArrangeRect();
                    Vector2f local_pos = e._mouse_position - rect.xy;
                    Vector2f render_pos = ViewToRenderPosition(local_pos);
                    if (_transform_gizmo->IsHover(render_pos,Camera::sCurrent))
                        _transform_gizmo->BeginDrag(render_pos);
                    // 可视需要决定是否拦截
                    // e._is_handled = true;
                }
                else if (e._key_code == EKey::kRBUTTON)
                {
                    _is_camera_input_active = true;
                    Vector4f rect = e._current_target->GetArrangeRect();
                    Vector2f local_pos = e._mouse_position - rect.xy;
                    if (IsSceneCamera2D())
                    {
                        _canvas_camera_controller.BeginDrag(local_pos);
                    }
                    else
                    {
                        _camera_controller->_is_receive_input = true;
                        _camera_input_last_mouse_pos = Input::GetGlobalMousePosAccurate();
                        _has_camera_input_last_mouse_pos = true;
                        SetCursor(NULL);
                    }
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
                        Vector2f render_pos = ViewToRenderPosition(local_pos);
                        s_editor_layer->_pick.GetPickID((u16) render_pos.x, (u16) render_pos.y, [this, local_pos](ECS::Entity closest_entity,u32 submesh_index)
                        {
                            LOG_INFO("Pick entity: {},subidex: {} on pos {}", closest_entity, submesh_index, local_pos.ToString());
                            Selection::AddAndRemovePreSelection(closest_entity,submesh_index);
                            ray_trace->_debug_pos = local_pos;
                        });
                    }
                }
                else if (e._key_code == EKey::kRBUTTON)
                {
                    _is_camera_input_active = false;
                    if (IsSceneCamera2D())
                    {
                        _canvas_camera_controller.EndDrag();
                    }
                    else
                    {
                        _camera_controller->_is_receive_input = false;
                        _has_camera_input_last_mouse_pos = false;
                        while (::ShowCursor(TRUE) < 0);
                    }
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
                else if (e._key_code == EKey::kQ && !Input::IsKeyDown(EKey::kRBUTTON))
                {
                    _transform_gizmo->ToggleSpace();
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
                if (IsSceneCamera2D())
                {
                    _canvas_camera_controller.Zoom(e._scroll_delta);
                }
                else
                {
                    f32 d = _camera_controller->_base_camera_move_speed * 0.1f;
                    _camera_controller->_base_camera_move_speed += e._scroll_delta > 0.0f ? d : -d;
                    LOG_INFO("Camera move speed: {}", _camera_controller->_base_camera_move_speed);
                }
            };

            _transform_gizmo = MakeScope<TransformGizmo>();
            DropHandler handler;
            handler._can_drop = [](const DragPayload &payload) -> bool
            {
                return payload._type == EDragType::kMesh || payload._type == EDragType::kPrefab;
            };
            handler._on_drop = [this](const DragPayload &payload, f32 x, f32 y)
            {
                LOG_INFO("SceneView {} drop", StaticEnum<EDragType>()->GetNameByEnum(payload._type));
                if (payload._data == nullptr)
                    return;
                auto *asset = static_cast<Asset *>(payload._data);
                auto *scene = SceneMgr::Get().ActiveScene();
                if (scene == nullptr)
                    return;
                if (payload._type == EDragType::kPrefab)
                {
                    Ref<PrefabAssetDocument> prefab = asset->AsRef<PrefabAssetDocument>();
                    if (prefab == nullptr)
                        return;
                    const SceneManagement::PrefabInstantiateResult result = SceneManagement::PrefabSystem::Instantiate(*scene, *prefab);
                    if (result._root == ECS::kInvalidEntity)
                        return;
                    if (auto *transform = scene->GetRegister().GetComponent<ECS::TransformComponent>(result._root))
                        transform->SetLocalPosition(_drag_preview_pos);
                    Selection::SetSelection(result._root);
                    return;
                }
                if (payload._type != EDragType::kMesh)
                    return;
                auto mesh = asset->AsRef<Render::Mesh>();
                if (mesh == nullptr)
                    return;
                _drag_preview_mesh = mesh;
                Vector<Ref<Render::Material>> mats;
                for (u16 i = 0; i < mesh->SubmeshCount(); i++)
                {
                    auto mat = ResourceMgr::Get().GetEmbeddedMaterial(mesh.get(), i);
                    mats.push_back(mat? mat : Render::Material::s_checker.lock());
                }
                auto new_entity = scene->AddObject(mesh, mats);
                scene->GetRegister().GetComponent<ECS::TransformComponent>(new_entity)->_local_transform._position = _drag_preview_pos;
            };
            _source->SetDropHandler(handler);
        }
        void SceneView::Update(f32 dt)
        {
            RenderView::Update(dt);
            if (Application::Get()._is_playing_mode)
            {
                Render::RenderPipeline::Get().SetPreviewCamera(nullptr, nullptr);
                _camera_preview->SetTexture(nullptr);
                _camera_preview_frame->_visibility = UI::EVisibility::kHide;
                _camera_preview_frame->SetVisible(false);
                return;
            }
            UpdateCameraPreview();
            UpdateCameraOutputSize(dt);
            SetSource(Render::RenderPipeline::Get().GetTarget(0));
            UpdateDragPreview();
            UpdateSceneToolbarBackdrop();
            if (Render::Camera::sCurrent)
            {
                auto rect = _source->GetArrangeRect();
                s_editor_layer->_viewport_size = Vector2f{rect.z, rect.w};
                s_editor_layer->_scene_vp_rect = rect;
            }
            UpdateSceneToolbarState();
            _transform_gizmo->Update(dt, ViewToRenderPosition(_mouse_pos),Camera::sCurrent);
            _transform_gizmo->Draw();
            if (_drag_preview_mesh)
            {
                Render::Gizmo::DrawMesh(_drag_preview_mesh.get(), MatrixTranslation(_drag_preview_pos), Render::Material::s_standard_forward_lit.lock().get());
            }
            ProcessCameraInput(dt);
        }
        void SceneView::UpdateDragPreview()
        {
            if (!DragDropManager::Get().IsDragging(EDragType::kMesh))
            {
                _drag_preview_mesh = nullptr;
                return;
            }

            const auto &payload = DragDropManager::Get().GetPayload();
            auto *asset = payload && payload->_data != nullptr ? static_cast<Asset *>(payload->_data) : nullptr;
            _drag_preview_mesh = asset != nullptr ? asset->AsRef<Render::Mesh>() : nullptr;
            if (_drag_preview_mesh == nullptr || Camera::sCurrent == nullptr)
                return;

            const Vector4f rect = _source->GetArrangeRect();
            _mouse_pos = Input::GetMousePos(Application::FocusedWindow()) - rect.xy;
            const Ray ray{Camera::sCurrent->Position(), Camera::sCurrent->ScreenToWorld(ViewToRenderPosition(_mouse_pos))};
            Vector3f position = Vector3f::kZero;
            bool has_position = false;
            auto *scene = SceneMgr::Get().ActiveScene();
            if (scene != nullptr)
            {
                const auto hit = scene->Pick(ray);
                if (hit != ECS::kInvalidEntity)
                {
                    auto *static_mesh = scene->GetRegister().GetComponent<ECS::StaticMeshComponent>(hit);
                    if (static_mesh != nullptr && !static_mesh->_transformed_aabbs.empty())
                    {
                        const auto &box = static_mesh->_transformed_aabbs[0];
                        const Vector3f p = box.Center() + Vector3f(0.0f, box.GetHalfAxisLength().y, 0.0f);
                        has_position = Plane{p, Vector3f::kUp}.Intersect(ray._start, ray._dir, position);
                    }
                }
            }
            if (!has_position)
            {
                has_position = Plane{Vector3f::kZero, Vector3f::kUp}.Intersect(ray._start, ray._dir, position);
                if (!has_position)
                    position = ray._start + ray._dir * 10.0f;
            }
            position.y += _drag_preview_mesh->BoundBox()[0].GetHalfAxisLength().y;
            _drag_preview_pos = position;
        }
        void SceneView::BuildSceneToolbar()
        {
            if (_view_canvas == nullptr)
                return;

            _scene_toolbar_bg = _view_canvas->AddChild<UI::Border>();
            _scene_toolbar_bg->Thickness(1.0f);
            _scene_toolbar_bg->CornerRadius(5.0f);
            _scene_toolbar_bg->_bg_color = Color(0.06f, 0.07f, 0.09f, 0.72f);
            _scene_toolbar_bg->_border_color = Color(1.0f, 1.0f, 1.0f, 0.18f);
            _scene_toolbar_bg->GetSlotAs<UI::CanvasSlot>().Position({kSceneToolbarLeft, kSceneToolbarTop});

            _scene_toolbar = _scene_toolbar_bg->AddChild<UI::HorizontalBox>();
            _scene_toolbar->SlotPadding() = UI::Padding(6.0f, 3.0f, 6.0f, 3.0f);
            _scene_toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            _btn_2d = _scene_toolbar->AddChild<UI::Button>("2D");
            _btn_2d->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size({kSceneToolbarButtonWidth, 24.0f})
                    .Margin(UI::Padding(0.0f, 0.0f, kSceneToolbarButtonGap, 0.0f));
            _btn_2d->OnMouseClick() += [this](UI::UIEvent &e)
            {
                SetSceneCamera2DOrthographic(_scene_2d_orientation);
                e._is_handled = true;
            };
            _dropdown_2d_orientation = _scene_toolbar->AddChild<UI::Dropdown>(Vector<String>{"XY", "-X", "-Y", "-Z"});

            _dropdown_2d_orientation->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size({kSceneToolbarDropdownWidth, 24.0f})
                    .Margin(UI::Padding(0.0f, 0.0f, kSceneToolbarButtonGap, 0.0f));
            _dropdown_2d_orientation->SetSelectedIndex(SceneView2DOrientationToIndex(_scene_2d_orientation));
            _dropdown_2d_orientation->_on_selected_changed += [this](i32 index)
            {
                _scene_2d_orientation = SceneView2DOrientationFromIndex(index);
                if (IsSceneCamera2D())
                    SetSceneCamera2DOrthographic(_scene_2d_orientation);
            };

            _btn_perspective = _scene_toolbar->AddChild<UI::Button>("Persp");
            _btn_perspective->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size({kSceneToolbarButtonWidth, 24.0f})
                    .Margin(UI::Padding(0.0f, 0.0f, kSceneToolbarButtonGap, 0.0f));
            _btn_perspective->OnMouseClick() += [this](UI::UIEvent &e)
            {
                SetSceneCameraPerspective();
                e._is_handled = true;
            };

            _btn_snap = _scene_toolbar->AddChild<UI::Button>("Snap");
            _btn_snap->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size({kSceneToolbarSmallButtonWidth, 24.0f});
            _btn_snap->OnMouseClick() += [this](UI::UIEvent &e)
            {
                if (_transform_gizmo)
                {
                    _transform_gizmo->SetSnapEnabled(!_transform_gizmo->IsSnapEnabled());
                    _is_scene_toolbar_state_dirty = true;
                    UpdateSceneToolbarState();
                }
                e._is_handled = true;
            };

            UpdateSceneToolbarLayout();
            UpdateSceneToolbarState();
        }
        void SceneView::UpdateSceneToolbarLayout()
        {
            if (_scene_toolbar_bg == nullptr)
                return;

            const f32 min_width = kSceneToolbarButtonWidth * 2.0f + kSceneToolbarDropdownWidth + kSceneToolbarSmallButtonWidth +
                                  kSceneToolbarButtonGap * 3.0f + 14.0f;
            const f32 available_width = std::max(min_width, _view_size.x - kSceneToolbarLeft * 2.0f);
            const f32 toolbar_width = std::min(available_width, min_width);
            _scene_toolbar_bg->GetSlotAs<UI::CanvasSlot>()
                    .Position({kSceneToolbarLeft, kSceneToolbarTop})
                    .Size({toolbar_width, kSceneToolbarHeight});
        }
        void SceneView::UpdateSceneToolbarBackdrop()
        {
            auto *main_target = Render::RenderPipeline::Get().GetTarget(0);
            if (_scene_toolbar_bg == nullptr || main_target == nullptr)
                return;

            const Vector4f source_rect = _source->GetArrangeRect();
            if (_dropdown_2d_orientation != nullptr)
                _dropdown_2d_orientation->SetPopupBackdrop(main_target, source_rect);
            const Vector4f toolbar_rect = _scene_toolbar_bg->GetArrangeRect();
            if (source_rect.z <= 1.0f || source_rect.w <= 1.0f || toolbar_rect.z <= 1.0f || toolbar_rect.w <= 1.0f)
                return;

            UIBrush backdrop_brush;
            backdrop_brush._type = EUIBrushType::kBackdropBlur;
            backdrop_brush._texture = main_target;
            backdrop_brush._tint = Color(1.0f, 1.0f, 1.0f, 0.88f);
            backdrop_brush._uv_rect = {
                    (toolbar_rect.x - source_rect.x) / source_rect.z,
                    (toolbar_rect.y - source_rect.y) / source_rect.w,
                    toolbar_rect.z / source_rect.z,
                    toolbar_rect.w / source_rect.w};

            auto &style = _scene_toolbar_bg->GetStyleOverride();
            style._background = backdrop_brush;
            style._override_mask |= (u32)EUIControlVisualOverride::kBackground;
            _scene_toolbar_bg->InvalidateStyle(EStyleInvalidation::kPaintOnly);
        }
        void SceneView::UpdateSceneToolbarState()
        {
            const bool is_ortho = Camera::sCurrent && Camera::sCurrent->Type() == Render::ECameraType::kOrthographic;
            const bool snap_active = (_transform_gizmo && _transform_gizmo->IsSnapEnabled()) || Input::IsKeyDown(EKey::kLCONTROL) ||
                                     Input::IsKeyDown(EKey::kRCONTROL) || Input::IsKeyDown(EKey::kCONTROL);
            if (!_is_scene_toolbar_state_dirty && _is_scene_toolbar_ortho_cached == is_ortho &&
                _is_scene_toolbar_snap_cached == snap_active)
                return;
            _is_scene_toolbar_state_dirty = false;
            _is_scene_toolbar_ortho_cached = is_ortho;
            _is_scene_toolbar_snap_cached = snap_active;
            ApplySceneToolbarButtonStyle(_btn_2d, is_ortho);
            ApplySceneToolbarButtonStyle(_btn_perspective, !is_ortho);
            ApplySceneToolbarSmallButtonStyle(_btn_snap, snap_active);
        }
        void SceneView::SetSceneCamera2DOrthographic()
        {
            SetSceneCamera2DOrthographic(_scene_2d_orientation);
        }
        void SceneView::SetSceneCamera2DOrthographic(ESceneView2DOrientation orientation)
        {
            if (!Camera::sCurrent)
                return;

            if (Camera::sCurrent->Type() != Render::ECameraType::kOrthographic)
            {
                _stored_camera_type = Camera::sCurrent->Type();
                _stored_camera_position = Camera::sCurrent->Position();
                _stored_camera_rotation = _camera_controller->_rotation;
                _stored_camera_size = Camera::sCurrent->Size();
                _has_stored_perspective_camera = true;
            }

            Vector3f direction;
            Vector3f up;
            GetSceneView2DCameraBasis(orientation, direction, up);
            f32 camera_size = Camera::sCurrent->Type() == Render::ECameraType::kOrthographic
                                      ? Camera::sCurrent->Size()
                                      : CalculateSceneView2DSize(Camera::sCurrent, direction);
            _scene_2d_orientation = orientation;
            Camera::sCurrent->Type(Render::ECameraType::kOrthographic);
            Camera::sCurrent->Size(camera_size);
            _canvas_camera_controller.Attach(Camera::sCurrent);
            _canvas_camera_controller.SetViewBasis(direction, up);
            _camera_controller->_is_receive_input = false;
            _camera_controller->_camera_near = Camera::sCurrent->Near();
            _camera_controller->_camera_far = Camera::sCurrent->Far();
            if (_dropdown_2d_orientation != nullptr &&
                _dropdown_2d_orientation->GetSelectedIndex() != SceneView2DOrientationToIndex(_scene_2d_orientation))
                _dropdown_2d_orientation->SetSelectedIndex(SceneView2DOrientationToIndex(_scene_2d_orientation));
            _is_scene_toolbar_state_dirty = true;
            UpdateSceneToolbarState();
        }
        void SceneView::SetSceneCameraPerspective()
        {
            if (!Camera::sCurrent)
                return;

            Camera::sCurrent->Type(_has_stored_perspective_camera ? _stored_camera_type : Render::ECameraType::kPerspective);
            if (_has_stored_perspective_camera)
            {
                Camera::sCurrent->Position(_stored_camera_position);
                Camera::sCurrent->Size(_stored_camera_size);
                _camera_controller->_is_receive_input = true;
                _camera_controller->SetTargetPosition(_stored_camera_position, true);
                _camera_controller->SetTargetRotation(_stored_camera_rotation.x, _stored_camera_rotation.y, true);
            }
            else
            {
                Camera::sCurrent->Type(Render::ECameraType::kPerspective);
            }
            Camera::sCurrent->RecalculateMatrix(true);
            _is_scene_toolbar_state_dirty = true;
            UpdateSceneToolbarState();
        }
        bool SceneView::IsSceneCamera2D() const
        {
            return Camera::sCurrent && Camera::sCurrent->Type() == Render::ECameraType::kOrthographic;
        }

        GameView::GameView()
        {
            SetTitle("GameView");
            _no_main_camera_text = _view_canvas->AddChild<UI::Text>("no main camera");
            _no_main_camera_text->FontSize(18.0f);
            _no_main_camera_text->_color = Color(0.78f, 0.78f, 0.78f, 1.0f);
            _no_main_camera_text->GetSlotAs<UI::CanvasSlot>()
                    .Anchor(Vector2f(0.5f, 0.5f))
                    .Position(Vector2f::kZero)
                    .SizeToContent(true)
                    .Alignment(UI::EAlignment::kCenter, UI::EAlignment::kCenter);
        }

        void GameView::Update(f32 dt)
        {
            RenderView::Update(dt);
            const bool is_playing = Application::Get()._is_playing_mode;
            const bool has_main_camera = is_playing && Camera::sMain != nullptr;
            _source->_visibility = has_main_camera ? UI::EVisibility::kVisible : UI::EVisibility::kHide;
            _no_main_camera_text->_visibility = is_playing && !has_main_camera ? UI::EVisibility::kVisible : UI::EVisibility::kHide;
            if (!has_main_camera)
                return;

            const Vector4f rect = _source->GetArrangeRect();
            constexpr f32 kMaxGameViewOutputSize = 4095.0f;
            const u16 output_width = static_cast<u16>(std::clamp(rect.z, 1.0f, kMaxGameViewOutputSize));
            const u16 output_height = static_cast<u16>(std::clamp(rect.w, 1.0f, kMaxGameViewOutputSize));
            Camera::sMain->OutputSize(output_width, output_height);
            SetSource(Render::RenderPipeline::Get().GetTarget(0));
        }
        void SceneView::UpdateCameraPreview()
        {
            auto *scene = SceneMgr::Get().ActiveScene();
            const ECS::Entity selected_entity = Selection::FirstEntity();
            auto *camera_component = scene != nullptr && selected_entity != ECS::kInvalidEntity
                                             ? scene->GetRegister().GetComponent<ECS::CCamera>(selected_entity)
                                             : nullptr;
            if (camera_component == nullptr)
            {
                Render::RenderPipeline::Get().SetPreviewCamera(nullptr, nullptr);
                _camera_preview->SetTexture(nullptr);
                _camera_preview_frame->_visibility = UI::EVisibility::kHide;
                _camera_preview_frame->SetVisible(false);
                return;
            }

            const u16 kPreviewWidth = static_cast<u16>(kPreviewRTSize.x);
            const u16 kPreviewHeight = static_cast<u16>(kPreviewRTSize.y);
            const f32 kPreviewMargin = 12.0f;
            if (_camera_preview_texture == nullptr || _camera_preview_texture->Width() != kPreviewWidth ||
                _camera_preview_texture->Height() != kPreviewHeight)
                _camera_preview_texture = Render::RenderTexture::Create(kPreviewWidth, kPreviewHeight, "SceneCameraPreview");

            const Vector2f view_size = _source->GetSlotAs<UI::CanvasSlot>()._size;
            if (view_size.x <= 1.0f || view_size.y <= 1.0f)
            {
                _camera_preview_frame->_visibility = UI::EVisibility::kHide;
                _camera_preview_frame->SetVisible(false);
                return;
            }

            const f32 view_width = view_size.x;
            const f32 view_height = view_size.y;
            const f32 preview_x = std::max(0.0f, view_width - kPreviewWidth - kPreviewMargin);
            const f32 preview_y = std::max(0.0f, view_height - kPreviewHeight - kPreviewMargin);
            _camera_preview_frame->GetSlotAs<UI::CanvasSlot>()
                    .Position({preview_x, preview_y})
                    .Size({static_cast<f32>(kPreviewWidth), static_cast<f32>(kPreviewHeight)});
            camera_component->_camera.OutputSize(kPreviewWidth, kPreviewHeight);
            Render::RenderPipeline::Get().SetPreviewCamera(&camera_component->_camera, _camera_preview_texture.get());
            _camera_preview->SetTexture(_camera_preview_texture.get());
            _camera_preview_frame->_visibility = UI::EVisibility::kVisible;
            _camera_preview_frame->SetVisible(true);
        }

        void SceneView::UpdateCameraOutputSize(f32 dt)
        {
            if (!Render::Camera::sCurrent)
                return;

            if (_pending_output_size == Vector2UInt::kZero)
            {
                auto rect = _source->GetArrangeRect();
                _view_size = {std::max(1.0f, rect.z), std::max(1.0f, rect.w)};
                _pending_output_size = CalculateSceneViewOutputSize(_view_size);
            }

            _resize_stable_time += dt;
            Vector2UInt desired_output_size = _committed_output_size;
            const bool need_grow = _pending_output_size.x > _committed_output_size.x || _pending_output_size.y > _committed_output_size.y;
            const bool need_first_commit = _committed_output_size == Vector2UInt::kZero;
            const bool can_shrink = _pending_output_size.x < _committed_output_size.x || _pending_output_size.y < _committed_output_size.y;

            if (need_first_commit || need_grow)
            {
                desired_output_size = {
                        std::max(_pending_output_size.x, _committed_output_size.x),
                        std::max(_pending_output_size.y, _committed_output_size.y)};
            }
            else if (can_shrink && _resize_stable_time >= kSceneViewResizeDebounceTime)
            {
                desired_output_size = _pending_output_size;
            }

            if (desired_output_size != _committed_output_size)
            {
                _committed_output_size = desired_output_size;
                Render::Camera::sCurrent->OutputSize(
                        static_cast<u16>(std::min<u32>(_committed_output_size.x, UINT16_MAX)),
                        static_cast<u16>(std::min<u32>(_committed_output_size.y, UINT16_MAX)));
            }
        }
        Vector2f SceneView::ViewToRenderPosition(const Vector2f &view_pos) const
        {
            if (_view_size.x <= 0.0f || _view_size.y <= 0.0f || _committed_output_size == Vector2UInt::kZero)
                return view_pos;
            return {
                    view_pos.x * static_cast<f32>(_committed_output_size.x) / _view_size.x,
                    view_pos.y * static_cast<f32>(_committed_output_size.y) / _view_size.y};
        }
        void SceneView::ProcessCameraInput(f32 dt)
        {
            constexpr f32 kReferenceFrameRate = 60.0f;
            if (IsSceneCamera2D())
            {
                if (!Input::IsKeyDown(EKey::kRBUTTON))
                {
                    _is_camera_input_active = false;
                    _canvas_camera_controller.EndDrag();
                }
                if (Camera::sCurrent &&
                    !InputRouteState::Get().IsOwnedBy(InputChannel::kMouse, EInputOwner::kImGui) && _is_camera_input_active)
                    _canvas_camera_controller.Drag(_mouse_pos, _view_size);
                return;
            }
            Camera::sCurrent->FovH(_camera_controller->_camera_fov_h);
            Camera::sCurrent->Near(_camera_controller->_camera_near);
            Camera::sCurrent->Far(_camera_controller->_camera_far);
            if (!Input::IsKeyDown(EKey::kRBUTTON))
            {
                _is_camera_input_active = false;
                _has_camera_input_last_mouse_pos = false;
            }
            if (Camera::sCurrent &&
                !InputRouteState::Get().IsOwnedBy(InputChannel::kMouse, EInputOwner::kImGui) && _is_camera_input_active)
            {
                Vector2f target_rotation = _camera_controller->_rotation;
                auto cur_mouse_pos = Input::GetGlobalMousePosAccurate();
                if (Input::IsKeyDown(EKey::kRBUTTON) && _has_camera_input_last_mouse_pos)
                {
                    if (abs(cur_mouse_pos.x - _camera_input_last_mouse_pos.x) < 100.0f &&
                        abs(cur_mouse_pos.y - _camera_input_last_mouse_pos.y) < 100.0f)
                    {
                        float angle_offset = _camera_controller->_camera_wander_speed * _camera_controller->_camera_wander_speed;
                        target_rotation.y += (cur_mouse_pos.x - _camera_input_last_mouse_pos.x) * angle_offset;
                        target_rotation.x += (cur_mouse_pos.y - _camera_input_last_mouse_pos.y) * angle_offset;
                    }
                }
                _camera_input_last_mouse_pos = cur_mouse_pos;
                _has_camera_input_last_mouse_pos = true;
                _camera_controller->SetTargetRotation(target_rotation.x, target_rotation.y);
                _camera_controller->Accelerate(Input::IsKeyDown(EKey::kSHIFT));
                static const f32 move_distance = 1.0f;// 1 m
                f32 final_move_distance = move_distance * _camera_controller->_cur_move_speed * _camera_controller->_cur_move_speed *
                                          kReferenceFrameRate * dt;
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
            f32 lerp_factor = dt * _camera_controller->_lerp_speed_multifactor * _camera_controller->_lerp_speed_multifactor *
                              kReferenceFrameRate;
            lerp_factor = std::clamp(lerp_factor, 0.0f, 1.0f);
            _camera_controller->Interpolate(lerp_factor);
        }
        #pragma endregion

        #pragma region Texture3DView
        Texture3DView::Texture3DView() : DockWindow("Texture3DView")
        {
            _split_view = _content_root->AddChild<UI::SplitView>();
            _split_view->_is_horizontal = true;
            _left_preview = _split_view->AddChild<UI::Image>();
            _left_preview->GetSlot()->Size({0.0f, 0.0f});
            _right_menu = _split_view->AddChild<UI::VerticalBox>();
            _right_menu->GetSlot()->Size({100.0f, 100.0f});
            _right_menu->SlotPadding() = UI::Padding(_content_root->Thickness());
            _right_menu->InvalidateLayout();
            _pass = AL_NEW_TAG(EMemoryTag::kEditor, Render::VolumeTexturePreviewPass);
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
            AL_DELETE(_pass);
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
