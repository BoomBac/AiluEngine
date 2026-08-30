#include "Common/TransformGizmo.h"
#include "Common/Undo.h"
#include "Framework/Common/Input.h"
#include "Common/Selection.h"
#include "Framework/Common/ResourceMgr.h"
#include "Inc/Physics/Collision.h"
#include "Render/Gizmo.h"
#include "Render/Material.h"
#include "Scene/Scene.h"
#include "Scene/Component.h"

namespace Ailu
{
    namespace Editor
    {
        static bool IsTransformChanged(const Transform &lhs, const Transform &rhs)
        {
            return lhs._position != rhs._position || lhs._rotation != rhs._rotation || lhs._scale != rhs._scale;
        }

        void TransformGizmo::ClearTarget()
        {
            if (_is_dragging)
                EndDrag();
            _hover_axis = 0u;
            _target_scene = nullptr;
            _target_entity = ECS::kInvalidEntity;
        }

        ECS::TransformComponent *TransformGizmo::Target() const
        {
            if (_target_scene == nullptr || _target_entity == ECS::kInvalidEntity ||
                !_target_scene->IsValidEntity(_target_entity))
                return nullptr;
            auto *comp = _target_scene->GetRegister().GetComponent<ECS::TransformComponent>(_target_entity);
            return comp ? comp : nullptr;
        }

        ECS::TransformComponent *TransformGizmo::ParentTarget() const
        {
            if (_target_scene == nullptr || _target_entity == ECS::kInvalidEntity ||
                !_target_scene->IsValidEntity(_target_entity))
                return nullptr;

            auto &reg = _target_scene->GetRegister();
            auto *hier = reg.GetComponent<ECS::CHierarchy>(_target_entity);
            if (hier == nullptr || hier->_parent == ECS::kInvalidEntity || !reg.IsAlive(hier->_parent))
                return nullptr;

            return reg.GetComponent<ECS::TransformComponent>(hier->_parent);
        }

        Vector3f TransformGizmo::WorldPositionToTargetLocal(const Vector3f &world_position) const
        {
            auto *parent = ParentTarget();
            if (parent == nullptr)
                return world_position;

            const Matrix4x4f local_matrix = MatrixTranslation(world_position) * Math::MatrixInverse(parent->GetWorldMatrix());
            return Vector3f(local_matrix[3][0], local_matrix[3][1], local_matrix[3][2]);
        }

        Quaternion TransformGizmo::WorldRotationToTargetLocal(const Quaternion &world_rotation) const
        {
            auto *parent = ParentTarget();
            if (parent == nullptr)
                return world_rotation;

            return world_rotation * Quaternion::Inverse(parent->GetRotation());
        }

        void TransformGizmo::SetTargetWorldPosition(ECS::TransformComponent *target, const Vector3f &world_position) const
        {
            if (target == nullptr)
                return;

            target->SetLocalPosition(WorldPositionToTargetLocal(world_position));
        }

        void TransformGizmo::SetTargetWorldRotation(ECS::TransformComponent *target, const Quaternion &world_rotation) const
        {
            if (target == nullptr)
                return;

            target->SetLocalRotation(WorldRotationToTargetLocal(world_rotation));
        }
        void TransformGizmo::ToggleSpace()
        {
            if (_is_dragging)
                return;
            _space = _space == EGizmoSpace::kWorld ? EGizmoSpace::kLocal : EGizmoSpace::kWorld;
            _hover_axis = 0u;
        }

        void TransformGizmo::RefreshAxisDirections()
        {
            Vector3f view_dir = Vector3f::kForward;
            if (_cam != nullptr)
                view_dir = Normalize(_cam->Position() - _cur_target_pos);

            for (auto &a: _translate_axis)
            {
                Vector3f axis_dir = GetAxisDirWorld(a._index);
                f32 dot = DotProduct(axis_dir, view_dir);
                a._dir = (dot >= 0.0f) ? axis_dir : -axis_dir;
            }
        }

        bool TransformGizmo::Is2DMode() const
        {
            return _cam != nullptr && _cam->Type() == Render::ECameraType::kOrthographic;
        }

        u32 TransformGizmo::Get2DHiddenAxisMask() const
        {
            if (!Is2DMode())
                return 0u;

            Vector3f forward = _cam->Forward();
            Vector3f abs_forward(std::abs(forward.x), std::abs(forward.y), std::abs(forward.z));
            if (abs_forward.x >= abs_forward.y && abs_forward.x >= abs_forward.z)
                return kAxisX;
            if (abs_forward.y >= abs_forward.x && abs_forward.y >= abs_forward.z)
                return kAxisY;
            return kAxisZ;
        }

        u32 TransformGizmo::Get2DVisibleAxisMask() const
        {
            if (!Is2DMode())
                return kAxisXYZ;
            return kAxisXYZ & ~Get2DHiddenAxisMask();
        }

        bool TransformGizmo::IsAxisMaskAvailable(u32 axis_mask) const
        {
            if (!Is2DMode())
                return true;
            return (axis_mask & Get2DHiddenAxisMask()) == 0u;
        }

        bool TransformGizmo::IsSnapActive() const
        {
            return _snap_enabled || Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL) ||
                   Input::IsKeyDown(EKey::kCONTROL);
        }

        f32 TransformGizmo::SnapFloat(f32 value, f32 step) const
        {
            if (step <= 0.0f)
                return value;
            return std::round(value / step) * step;
        }

        Vector3f TransformGizmo::SnapVector(const Vector3f &value, f32 step) const
        {
            return Vector3f(SnapFloat(value.x, step), SnapFloat(value.y, step), SnapFloat(value.z, step));
        }

        static Ray MakeGizmoMouseRay(Render::Camera *cam, Vector2f mouse_pos)
        {
            if (cam != nullptr && cam->Type() == Render::ECameraType::kOrthographic)
                return Ray{cam->ScreenToWorld(mouse_pos, -cam->Near()), cam->Forward()};
            return Ray{cam->Position(), cam->ScreenToWorld(mouse_pos, 0.0f)};
        }

        TransformGizmo::TransformGizmo()
        {
            auto shader = ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/transform_gizmo.alasset");
            auto mat_x = MakeRef<Render::Material>(shader.get(), "TransformGizmoMat");
            mat_x->SetVector("_color", Colors::kRed);
            auto mat_y = MakeRef<Render::Material>(shader.get(), "TransformGizmoMat");
            mat_y->SetVector("_color", Colors::kGreen);
            auto mat_z = MakeRef<Render::Material>(shader.get(), "TransformGizmoMat");
            mat_z->SetVector("_color", Colors::kBlue);
            _translate_axis[0] = {Vector3f::kRight, BuildIdentityMatrix(), mat_x, 0u,kAxisX, Vector3f::kRight};
            _translate_axis[1] = {Vector3f::kUp, BuildIdentityMatrix(), mat_y, 1u,kAxisY, Vector3f::kUp};
            _translate_axis[2] = {Vector3f::kForward, BuildIdentityMatrix(), mat_z, 2u,kAxisZ, Vector3f::kForward};
            _plane_obbs[0] = MakeScope<OBB>();
            _plane_obbs[1] = MakeScope<OBB>();
            _plane_obbs[2] = MakeScope<OBB>();
            _scale_center_obb = MakeScope<OBB>();
            _scale_center_obb->_local_axis[0] = Vector3f::kRight;
            _scale_center_obb->_local_axis[1] = Vector3f::kUp;
            _scale_center_obb->_local_axis[2] = Vector3f::kForward;
            for (u16 i = 0; i < 3; i++)
            {
                _rotate_rings[i] = MakeRef<Render::Material>(shader.get(), "TransformGizmoMat");
                _rotate_rings[i]->SetVector("_color", kNormalColors[i]);
                _rotate_rings[i]->EnableKeyword("_CIRCLE");
            }

            _selection_changed_handle = Selection::on_selection_changed += [this]()
            {
                SyncTargetFromSelection();
            };
            SyncTargetFromSelection();
        }

        TransformGizmo::~TransformGizmo()
        {
            if (_selection_changed_handle.has_value())
            {
                Selection::on_selection_changed -= _selection_changed_handle.value();
                _selection_changed_handle.reset();
            }
        }

        void TransformGizmo::SyncTargetFromSelection()
        {
            auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
            const ECS::Entity entity = Selection::FirstEntity();
            if (scene == nullptr || entity == ECS::kInvalidEntity ||
                !scene->IsValidEntity(entity) ||
                scene->GetRegister().GetComponent<ECS::TransformComponent>(entity) == nullptr)
            {
                ClearTarget();
                return;
            }

            if (_target_scene != scene || _target_entity != entity)
                SetTarget(scene, entity);
        }
        static Matrix4x4f MakeGizmoCylinder(const Vector3f &axis, const Vector3f &origin, f32 axis_length, f32 axis_radius)
        {
            Vector3f dir = Normalize(axis);
            Matrix4x4f orient;

            // Base direction is +Y; rotate it to the target axis when needed.
            Vector3f base = Vector3f::kUp;
            f32 dot = DotProduct(base, dir);

            if (fabs(dot - 1.0f) < 1e-6f)
            {
                orient = BuildIdentityMatrix();
            }
            else if (fabs(dot + 1.0f) < 1e-6f)
            {
                orient = MatrixRotationX(Math::kPi);// Reverse to -Y.
            }
            else
            {
                Vector3f axis_rot = Normalize(CrossProduct(base, dir));
                f32 angle = acosf(dot);
                MatrixRotationAxis(orient,axis_rot, angle);
            }

            // Scale and translate.
            Matrix4x4f scl = MatrixScale(axis_radius, axis_length * 0.5f, axis_radius);
            Matrix4x4f translate = MatrixTranslation(0, axis_length * 0.5f, 0);
            return scl * translate * orient * MatrixTranslation(origin);
        }

        static Matrix4x4f MakeGizmoCone(const Vector3f &axis, const Vector3f &origin, f32 height, f32 axis_radius)
        {
            Vector3f dir = Normalize(axis);
            Matrix4x4f orient;

            Vector3f base = Vector3f::kUp;
            f32 dot = DotProduct(base, dir);

            if (fabs(dot - 1.0f) < 1e-6f)
            {
                orient = BuildIdentityMatrix();
            }
            else if (fabs(dot + 1.0f) < 1e-6f)
            {
                orient = MatrixRotationX(Math::kPi);
            }
            else
            {
                Vector3f axis_rot = Normalize(CrossProduct(base, dir));
                f32 angle = acosf(dot);
                MatrixRotationAxis(orient, axis_rot, angle);
            }

            // Scale and translate.
            Matrix4x4f scl = MatrixScale(axis_radius, axis_radius, axis_radius);
            Matrix4x4f translate = MatrixTranslation(0, height, 0);

            return scl * translate * orient * MatrixTranslation(origin);
        }

        static Matrix4x4f MakeGizmoPlane(Vector3f axis, Vector3f sub_axisa, Vector3f sub_axisb, const Vector3f &origin, f32 size)
        {
            sub_axisa = Normalize(sub_axisa);
            sub_axisb = Normalize(sub_axisb);
            Vector3f normal = Normalize(CrossProduct(sub_axisa, sub_axisb));
            if (DotProduct(normal, axis) < 0.0f)
                normal = -normal;

            Matrix4x4f orient = BuildIdentityMatrix();
            orient[0][0] = sub_axisa.x;
            orient[0][1] = sub_axisa.y;
            orient[0][2] = sub_axisa.z;
            orient[1][0] = normal.x;
            orient[1][1] = normal.y;
            orient[1][2] = normal.z;
            orient[2][0] = sub_axisb.x;
            orient[2][1] = sub_axisb.y;
            orient[2][2] = sub_axisb.z;

            size *= 0.5f;
            Matrix4x4f scl = MatrixScale(size, 0.1f, size);
            return scl * orient * MatrixTranslation(origin + sub_axisa * size + sub_axisb * size);
        }

        static Matrix4x4f MakeCircle(Vector3f axis)
        {
            Vector3f dir = Normalize(axis);
            Matrix4x4f orient;

            Vector3f base = Vector3f::kUp;
            f32 dot = DotProduct(base, dir);

            if (fabs(dot - 1.0f) < 1e-6f)
            {
                orient = BuildIdentityMatrix();
            }
            else if (fabs(dot + 1.0f) < 1e-6f)
            {
                orient = MatrixRotationX(Math::kPi);
            }
            else
            {
                Vector3f axis_rot = Normalize(CrossProduct(base, dir));
                f32 angle = acosf(dot);
                MatrixRotationAxis(orient, axis_rot, angle);
            }
            return orient;
        }



        static f32 ComputeGizmoScale(const Camera &camera, Vector3f target_pos, f32 desired_pixels = 100.0f)
        {
            if (camera.Type() == Render::ECameraType::kOrthographic)
            {
                f32 ortho_height = camera.Size() / camera.Aspect();
                return ortho_height * (desired_pixels / (f32) camera.OutputSize().y);
            }
            else
            {
                f32 dist = Magnitude(camera.Position() - target_pos);
                f32 fov_y = camera.FovH() / camera.Aspect();
                f32 screen_h = (f32) camera.OutputSize().y;
                return 2.0f * dist * tanf(fov_y * 0.5f * k2Radius) * (desired_pixels / screen_h);
            }
        }

        static Plane s_drag_plane;
        static Plane s_rotate_plane[3];

        u32 TransformGizmo::PickAxis(Vector3f start, Vector3f dir) const
        {
            auto *target = Target();
            if (!target)
                return 0u;
            Ray ray(start, dir);
            Vector3f origin = target->GetPosition();
            f32 min_d = std::numeric_limits<f32>::max();
            u32 result = 0u;
            for (auto &a: _translate_axis)
            {
                if (!IsAxisMaskAvailable(a._axis))
                    continue;
                f32 s, t;
                Vector3f c1, c2;
                f32 d = CollisionDetection::ClosestPtSegmentSegment(origin, origin + a._dir * _axis_length * _dis_scale, start, start + dir * Distance(start, origin) * 1.5f, s, t, c1, c2);
                if (d < min_d && d <= _dis_scale * _axis_radius * 2.0f)
                {
                    min_d = d;
                    result = a._axis;
                }
            }
            //_hover_axis_plane = flag;
            //UI::UIRenderer::Get()->DrawText(std::format("dx: {},dy: {},dz: {},w: {}", axis_d[0], axis_d[1], axis_d[2], quad_w), Input::GetMousePos(Application::FocusedWindow()));
            return result;
        }


        Vector3f TransformGizmo::GetAxisDirWorld(int axisId) const
        {
            Vector3f localAxis = Vector3f::kRight;
            if (axisId == 1) localAxis = Vector3f::kUp;
            else if (axisId == 2)
                localAxis = Vector3f::kForward;

            if (_space == EGizmoSpace::kWorld)
                return Normalize(localAxis);

            // Local space: rotate the local axis into world space.
            // This assumes the target transform exposes world rotation.
            auto *target = Target();
            if (!target)
                return Normalize(localAxis);
            Quaternion worldRot = target->GetRotation();
            return Normalize(worldRot * localAxis);
        }
        // Keep the axis segment long enough so s is not clamped to [0, axis_length].
        static const f32 kVirualRayLen = 10000.0f;

        f32 TransformGizmo::ComputeAxisParamS(Vector2f mouse_pos, const Vector3f &origin, const Vector3f &axisDir) const
        {
            // Mouse ray.
            Ray ray = MakeGizmoMouseRay(_cam, mouse_pos);

            f32 s = 0.0f, t = 0.0f;
            Vector3f c1, c2;
            CollisionDetection::ClosestPtSegmentSegment(
                    origin - axisDir * (kVirualRayLen * 0.5f),
                    origin + axisDir * (kVirualRayLen * 0.5f),
                    ray._start,
                    ray._start + ray._dir * kVirualRayLen,
                    s, t, c1, c2);
            // s is usually measured from the first endpoint; with the centered segment it can be diffed directly.
            return s * kVirualRayLen;// _axis_length;
        }

        void TransformGizmo::BeginDrag(Vector2f mouse_pos)
        {
            auto *target = Target();
            if (!target) return;

            // Lock the current axis.
            _drag_axis = _hover_axis;
            if (_drag_axis == 0u)
                return;
            if (_mode == EGizmoMode::kRotate)
            {
                if (Is2DMode() && _drag_axis != Get2DHiddenAxisMask())
                    return;
            }
            else if (!IsAxisMaskAvailable(_drag_axis))
            {
                return;
            }

            _drag_start_local_transform = target->_local_transform;
            _has_drag_start_transform = true;
            _is_dragging = true;
            _mouse_pos = mouse_pos;
            _drag_start_mouse_pos = mouse_pos;
            _drag_origin = target->GetPosition();
            _drag_axis_num = 0u;
            if (_mode == EGizmoMode::kRotate)
            {
                _drag_start_hit = CollisionDetection::Intersect(MakeGizmoMouseRay(_cam, _mouse_pos), s_rotate_plane[_drag_axis>>1])._point;
                _drag_start_hit = _cur_target_pos + Normalize(_drag_start_hit - _cur_target_pos) * _scaled_axis_length;
                _drag_start_rot = target->_rotation;
            }
            else
            {
                if (_drag_axis & EAxis::kAxisX)
                {
                    auto axis_dir = GetAxisDirWorld(0);
                    _drag_axis_ctx[_drag_axis_num++] = {axis_dir, ComputeAxisParamS(mouse_pos, _drag_origin, axis_dir)};
                }
                if (_drag_axis & EAxis::kAxisY)
                {
                    auto axis_dir = GetAxisDirWorld(1);
                    _drag_axis_ctx[_drag_axis_num++] = {axis_dir, ComputeAxisParamS(mouse_pos, _drag_origin, axis_dir)};
                }
                if (_drag_axis & EAxis::kAxisZ)
                {
                    auto axis_dir = GetAxisDirWorld(2);
                    _drag_axis_ctx[_drag_axis_num++] = {axis_dir, ComputeAxisParamS(mouse_pos, _drag_origin, axis_dir)};
                }
                _drag_start_pos = _drag_origin;// Simplified: use world movement as the drag basis.
                if (_drag_axis_num == 2)
                {
                    s_drag_plane = Plane(_drag_start_pos, CrossProduct(_drag_axis_ctx[0]._drag_axis_dir, _drag_axis_ctx[1]._drag_axis_dir));
                    Ray ray = MakeGizmoMouseRay(_cam, _mouse_pos);
                    _drag_start_hit = CollisionDetection::Intersect(ray, s_drag_plane)._point;
                }
                else if (_drag_axis_num == 3)//scale all
                {
                    s_drag_plane = Plane(_drag_start_pos, Normalize(_cam->Position() - _drag_origin));
                    Ray ray = MakeGizmoMouseRay(_cam, _mouse_pos);
                    _drag_start_hit = CollisionDetection::Intersect(ray, s_drag_plane)._point;
                    _drag_start_target_delta = _drag_start_mouse_pos - _cam->WorldToScreen(_drag_start_pos);
                }
                _drag_start_scale = target->GetLocalScale();
            }

            if (_mode == EGizmoMode::kTranslate)
            {
                for (auto &a: _translate_axis)
                {
                    if (a._axis & _drag_axis)
                        a._mat->SetVector("_color", kDragingColor);
                    else
                        a._mat->SetVector("_color", kInactiveColor);
                }
            }
        }

        void TransformGizmo::EndDrag()
        {
            auto *target = Target();
            if (_is_dragging && _has_drag_start_transform && target != nullptr && g_pCommandMgr != nullptr &&
                IsTransformChanged(_drag_start_local_transform, target->_local_transform))
            {
                g_pCommandMgr->ExecuteCommand(MakeScope<TransformCommand>("TransformGizmo", target, _drag_start_local_transform));
            }
            _is_dragging = false;
            _drag_axis = -1;
            _has_drag_start_transform = false;
            _drag_scale_factor = Vector3f::kOne;
            _drag_rot = Quaternion::Identity();
        }
        static Vector2f PixelWorldSize(f32 fovX, f32 aspect, f32 distance, int widthPx, int heightPx)
        {
            f32 tanHalfX = std::tan(fovX * 0.5f);
            f32 fovY = 2.0f * std::atan(std::tan(fovX * 0.5f) / aspect);

            f32 worldWidth = 2.0f * distance * tanHalfX;
            f32 worldHeight = 2.0f * distance * std::tan(fovY * 0.5f);

            f32 pixelWidth = worldWidth / f32(widthPx);
            f32 pixelHeight = worldHeight / f32(heightPx);

            return {pixelWidth, pixelHeight};
        }


        void TransformGizmo::Update(f32 dt, Vector2f mouse_pos,Camera* cam)
        {
            _mouse_pos = mouse_pos;
            _cam = cam;

            auto *active_scene = SceneManagement::SceneMgr::Get().ActiveScene();
            if (_target_scene != nullptr && _target_scene != active_scene)
                ClearTarget();

            auto *target = Target();
            if (target == nullptr)
            {
                if (_target_scene != nullptr || _target_entity != ECS::kInvalidEntity)
                    ClearTarget();
                return;
            }
            // 新创建或刚重挂父节点的实体会在下一次 TransformSystem 更新前保持 world dirty。
            // 此时 world-space accessor 会断言；延后一帧绘制 Gizmo，等待场景变换同步完成。
            if (target->_world_dirty)
                return;
            if (target)
            {
                _cur_target_pos = target->GetPosition();
                _dis_scale = ComputeGizmoScale(*_cam, _is_dragging ? _drag_start_pos : _cur_target_pos, 100.0f);
                if (!_is_dragging)
                {
                    _scaled_axis_length = _axis_length * _dis_scale;
                    _scaled_axis_radius = _axis_radius * _dis_scale;
                    RefreshAxisDirections();
                    if (_mode == EGizmoMode::kTranslate)
                    {
                        // s_p_plane is 2x2.
                        f32 quad_w = _scaled_axis_length * _axis_quad_width_scale;
                        //X->YZ Plane
                        {
                            OBB &box = *_plane_obbs[0];
                            box._center = _cur_target_pos + _translate_axis[1]._dir * quad_w * 0.5 + _translate_axis[2]._dir * quad_w * 0.5;
                            box._half_axis_length = Vector3f{0.1f, 0.5f * quad_w, 0.5f * quad_w};
                            box._local_axis[0] = _translate_axis[0]._dir;
                            box._local_axis[1] = _translate_axis[1]._dir;
                            box._local_axis[2] = _translate_axis[2]._dir;
                        }
                        //Y->XZ Plane
                        {
                            OBB &box = *_plane_obbs[1];
                            box._center = _cur_target_pos + _translate_axis[0]._dir * quad_w * 0.5 + _translate_axis[2]._dir * quad_w * 0.5;
                            box._half_axis_length = Vector3f{0.5f * quad_w, 0.1f, 0.5f * quad_w};
                            box._local_axis[0] = _translate_axis[0]._dir;
                            box._local_axis[1] = _translate_axis[1]._dir;
                            box._local_axis[2] = _translate_axis[2]._dir;
                        }
                        //Z->XY Plane
                        {
                            OBB &box = *_plane_obbs[2];
                            box._center = _cur_target_pos + _translate_axis[0]._dir * quad_w * 0.5 + _translate_axis[1]._dir * quad_w * 0.5;
                            box._half_axis_length = Vector3f{0.5f * quad_w, 0.5f * quad_w, 0.1f};
                            box._local_axis[0] = _translate_axis[0]._dir;
                            box._local_axis[1] = _translate_axis[1]._dir;
                            box._local_axis[2] = _translate_axis[2]._dir;
                        }
                    }
                    else if (_mode == EGizmoMode::kScale)
                    {
                        _scale_center_obb->_center = _cur_target_pos;
                        const f32 s = _scaled_axis_radius * 2.0f;
                        _scale_center_obb->_half_axis_length = Vector3f{s};
                    }
                    else 
                    {
                        s_rotate_plane[0] = Plane(_cur_target_pos, GetAxisDirWorld(0));
                        s_rotate_plane[1] = Plane(_cur_target_pos, GetAxisDirWorld(1));
                        s_rotate_plane[2] = Plane(_cur_target_pos, GetAxisDirWorld(2));
                        for (auto &m: _rotate_rings)
                            m->SetFloat("_radius", _scaled_axis_length);
                    }
                    IsHover(_mouse_pos, _cam, &_hover_axis);
                    if (_mode == EGizmoMode::kTranslate || _mode == EGizmoMode::kScale)
                    {
                        for (auto &a: _translate_axis)
                        {
                            if (!IsAxisMaskAvailable(a._axis))
                                a._mat->SetVector("_color", kInactiveColor);
                            else if (a._axis & _hover_axis)
                                a._mat->SetVector("_color", kHoverColor);
                            else
                                a._mat->SetVector("_color", kNormalColors[a._index]);
                        }
                    }
                    else
                    {
                        for (u32 i = 0; i < 3; i++)
                        {
                            const u32 axis_mask = 1u << i;
                            if (Is2DMode() && axis_mask != Get2DHiddenAxisMask())
                                _rotate_rings[i]->SetVector("_color", kInactiveColor);
                            else if (_hover_axis & axis_mask)
                                _rotate_rings[i]->SetVector("_color", kHoverColor);
                            else
                                _rotate_rings[i]->SetVector("_color", kNormalColors[i]);
                        }
                    }
                }
                // While dragging, compute and apply the transform delta.
                if (_is_dragging && _drag_axis >= 0)
                {
                    if (_mode == EGizmoMode::kTranslate)
                    {
                        Vector3f world_delta = Vector3f::kZero;
                        if (_drag_axis_num == 1)
                        {
                            const auto &ctx = _drag_axis_ctx[0];
                            f32 s_now = ComputeAxisParamS(mouse_pos, _drag_origin, ctx._drag_axis_dir);
                            f32 delta_s = s_now - ctx._drag_start_s;
                            world_delta = ctx._drag_axis_dir * delta_s;
                        }
                        else if (_drag_axis_num == 2)// Multi-axis movement uses the drag plane.
                        {
                            Ray ray = MakeGizmoMouseRay(_cam, mouse_pos);
                            Vector3f hit = CollisionDetection::Intersect(ray, s_drag_plane)._point;
                            world_delta = hit - _drag_start_hit;
                            Render::Gizmo::DrawLine(_drag_start_hit, hit, Colors::kYellow);
                            Vector2f p = mouse_pos + Vector2f{20, 20};
                            Render::Gizmo::DrawText(std::format("delta: {}", world_delta.ToString()), p, 10u, Colors::kCyan);
                            Render::Gizmo::DrawText(std::format("hit: {}", hit.ToString()), p + Vector2f(0.0f, 118 * 10 / 65.0f), 10u, Colors::kCyan);
                            Render::Gizmo::DrawText(std::format("origin: {}", _drag_start_pos.ToString()), p + Vector2f(0.0f, 118 * 10 / 65.0f) * 2.0f, 10u, Colors::kCyan);
                        }
                        else {}
                        if (IsSnapActive())
                            world_delta = SnapVector(world_delta, _translate_snap_step);
                        SetTargetWorldPosition(target, _drag_start_pos + world_delta);
                    }
                    else if (_mode == EGizmoMode::kScale)
                    {
                        Vector3f world_scale = Vector3f::kOne;
                        if (_drag_axis_num == 1)
                        {
                            const auto &ctx = _drag_axis_ctx[0];
                            f32 s_now = abs(ComputeAxisParamS(mouse_pos, _drag_origin, ctx._drag_axis_dir) - kVirualRayLen * 0.5f);
                            f32 s_start = abs(ctx._drag_start_s - kVirualRayLen * 0.5f);
                            f32 delta_s = s_now > s_start ? 1.0f + (s_now - s_start) / (_axis_length * _dis_scale)
                                                          : 1.0f - (s_start - s_now) / (_axis_length * _dis_scale);

                            world_scale[_drag_axis >> 1] = delta_s;
                            Vector2f p = mouse_pos + Vector2f{20, 20};
                            Render::Gizmo::DrawText(std::format("start_s: {},now s: {}", ctx._drag_start_s,s_now), p, 10u, Colors::kCyan);
                            _drag_scale_factor[_drag_axis >> 1] = delta_s;
                            Vector3f target_scale = _drag_start_scale * world_scale;
                            if (IsSnapActive())
                                target_scale = _drag_start_scale + SnapVector(target_scale - _drag_start_scale, _scale_snap_step);
                            target->SetLocalScale(target_scale);
                        }
                        else if (_drag_axis_num == 3)// Uniform scale uses screen-space movement.
                        {
                            // Screen-space drag distance.
                            Vector2f v = _drag_start_mouse_pos - _mouse_pos;
                            float pixel_distance = Magnitude(v);

                            // Project the camera-to-gizmo direction onto screen space for a stable sign.
                            Vector3f gizmo_dir_ws = Normalize(_drag_start_pos - _cam->Position());
                            Vector2f gizmo_dir_ss = Normalize(Vector2f(DotProduct(gizmo_dir_ws, _cam->Right()), -DotProduct(gizmo_dir_ws, _cam->Up())));
                            Render::Gizmo::DrawLine(Vector2f{500.0f, 500.0f}, Vector2f{500.0f, 500.0f}  + gizmo_dir_ss);
                            Vector2f drag_dir = Normalize(v);
                            float proj = DotProduct(gizmo_dir_ss, drag_dir);

                            // Avoid tiny jitter.
                            if (fabs(proj) < 0.1f)
                                proj = 0.0f;

                            // Map screen distance to world-space distance.
                            // Compute the world units represented by one screen pixel.
                            Ray ray0 = MakeGizmoMouseRay(_cam, _drag_start_mouse_pos);
                            Ray ray1 = MakeGizmoMouseRay(_cam, _drag_start_mouse_pos + Vector2f(1, 1));

                            // Intersect on the gizmo plane.
                            Vector3f hit0 = CollisionDetection::Intersect(ray0, s_drag_plane)._point;
                            Vector3f hit1 = CollisionDetection::Intersect(ray1, s_drag_plane)._point;

                            // World units per pixel.
                            float world_per_pixel = Magnitude(hit1 - hit0);

                            // Current scale distance in world space.
                            float world_distance = pixel_distance * world_per_pixel * proj;

                            // Adjust based on gizmo size or scene scale.
                            float s = world_distance * 0.5f;// Scale tuning.

                            // Uniform scale on all three axes.
                            world_scale = Vector3f(s);

                            // Apply to target.
                            Vector3f target_scale = _drag_start_scale + world_scale;
                            if (IsSnapActive())
                                target_scale = _drag_start_scale + SnapVector(target_scale - _drag_start_scale, _scale_snap_step);
                            target->SetLocalScale(target_scale);
                            _drag_scale_factor = Vector3f::kOne + world_scale;
                            Vector2f p = mouse_pos + Vector2f{20, 20};
                            Render::Gizmo::DrawText(std::format("now s: {}", s), p, 10u, Colors::kCyan);
                        }
                        else {}
                        // With hierarchy, world_delta should be converted to local space before adding to local position.

                        Render::Gizmo::DrawLine(Vector3f::kZero, target->_position);
                    }
                    else//if (_mode == EGizmoMode::kRotate)
                    {
                        Vector3f axis = GetAxisDirWorld(_drag_axis >> 1);// Example: X is (1, 0, 0).
                        _drag_current_hit = CollisionDetection::Intersect(
                                               MakeGizmoMouseRay(_cam, _mouse_pos),
                                               s_rotate_plane[_drag_axis >> 1])
                                               ._point;
                        _drag_current_hit = _cur_target_pos + Normalize(_drag_current_hit - _cur_target_pos) * _scaled_axis_length;
                        Vector3f v0 = Normalize(_drag_start_hit - _cur_target_pos);
                        Vector3f v1 = Normalize(_drag_current_hit - _cur_target_pos);
                        f32 cos_theta = std::clamp(DotProduct(v0, v1), -1.0f, 1.0f);
                        f32 sin_theta = DotProduct(CrossProduct(v0, v1), axis);
                        f32 angle = atan2(sin_theta, cos_theta);
                        if (IsSnapActive())
                            angle = SnapFloat(angle * k2Angle, _rotate_snap_degrees) * k2Radius;
                        Vector2f p = mouse_pos + Vector2f{20, 20};
                        Render::Gizmo::DrawText(std::format("angle: {}", angle * k2Angle), p, 10u, Colors::kCyan);
                        _drag_rot = Quaternion::AngleAxis(angle * k2Angle, axis);
                        SetTargetWorldRotation(target, _drag_start_rot * _drag_rot);
                    }
                }
            }
        }

        void TransformGizmo::Draw()
        {
            if (!Target()) 
                return;
            if (_mode == EGizmoMode::kTranslate)
            {
                for (auto &axis: _translate_axis)
                {
                    if (!IsAxisMaskAvailable(axis._axis))
                        continue;
                    f32 scale_factor = (_hover_axis & axis._axis) ? 1.2f : 1.0f;
                    Render::Gizmo::DrawMesh(Render::Mesh::s_cylinder.lock().get(),
                                            MakeGizmoCylinder(axis._dir, _cur_target_pos, _scaled_axis_length, _scaled_axis_radius * scale_factor),
                                            axis._mat.get());

                    Render::Gizmo::DrawMesh(Render::Mesh::s_cone.lock().get(),
                                            MakeGizmoCone(axis._dir, _cur_target_pos, _scaled_axis_length, _scaled_axis_radius * 4.0f * scale_factor),
                                            axis._mat.get());
                }
                // s_p_plane is 2x2.
                const f32 quad_w = _scaled_axis_length * _axis_quad_width_scale;
                //X->YZ Plane
                if (IsAxisMaskAvailable(kAxisYZ))
                {
                    Render::Gizmo::DrawMesh(Render::Mesh::s_plane.lock().get(),
                                            MakeGizmoPlane(_translate_axis[0]._dir, _translate_axis[1]._dir, _translate_axis[2]._dir, _cur_target_pos, quad_w),
                                            _translate_axis[0]._mat.get());
                }
                //Y->XZ Plane
                if (IsAxisMaskAvailable(kAxisXZ))
                {
                    Render::Gizmo::DrawMesh(Render::Mesh::s_plane.lock().get(),
                                            MakeGizmoPlane(_translate_axis[1]._dir, _translate_axis[0]._dir, _translate_axis[2]._dir, _cur_target_pos, quad_w),
                                            _translate_axis[1]._mat.get());
                }
                //Z->XY Plane
                if (IsAxisMaskAvailable(kAxisXY))
                {
                    Render::Gizmo::DrawMesh(Render::Mesh::s_plane.lock().get(),
                                            MakeGizmoPlane(_translate_axis[2]._dir, _translate_axis[0]._dir, _translate_axis[1]._dir, _cur_target_pos, quad_w),
                                            _translate_axis[2]._mat.get());
                }
            }
            else if (_mode == EGizmoMode::kScale)
            {
                for (auto &axis: _translate_axis)
                {
                    if (!IsAxisMaskAvailable(axis._axis))
                        continue;
                    f32 cur_axis_length = _scaled_axis_length * _drag_scale_factor[axis._index];
                    f32 scale_factor = (_hover_axis & axis._axis) ? 1.2f : 1.0f;
                    Render::Gizmo::DrawMesh(Render::Mesh::s_cylinder.lock().get(),
                                            MakeGizmoCylinder(axis._dir, _cur_target_pos, cur_axis_length, _scaled_axis_radius * scale_factor),
                                            axis._mat.get());

                    Render::Gizmo::DrawMesh(Render::Mesh::s_cube.lock().get(),
                                            MakeGizmoCone(axis._dir, _cur_target_pos, cur_axis_length, _scaled_axis_radius * 4.0f * scale_factor),
                                            axis._mat.get());
                }
                const f32 s = _scale_center_obb->_half_axis_length.x * 2.0f;
                if (!Is2DMode())
                {
                    Render::Gizmo::DrawMesh(Render::Mesh::s_cube.lock().get(), MatrixScale(s,s,s)* MatrixTranslation(_cur_target_pos),
                                            _translate_axis[0]._mat.get());
                }
            }
            else//if (_mode == EGizmoMode::kRotate)
            {
                for (auto &axis: _translate_axis)
                {
                    if (Is2DMode() && axis._axis != Get2DHiddenAxisMask())
                        continue;
                    f32 scale_factor = _scaled_axis_length * (_hover_axis & axis._axis ? 1.2f : 1.05f);
                    //Render::Gizmo::DrawCircle(_cur_target_pos, _scaled_axis_length, 36u, _hover_axis & axis._axis ? kHoverColor : kNormalColors[axis._index],MakeCircle(axis._dir));
                    Vector3f ring_axis = Is2DMode() ? -_cam->Forward() : axis._dir;
                    auto rmat = Quaternion::ToMat4f(_drag_rot);
                    rmat = MakeCircle(ring_axis) * rmat;
                    Render::Gizmo::DrawMesh(Render::Mesh::s_plane.lock().get(),
                                            MatrixScale(scale_factor, scale_factor, scale_factor) * rmat * MatrixTranslation(_cur_target_pos),
                                            _rotate_rings[axis._index].get());
                }
                if (_is_dragging)
                {
                    Vector3f cur_hit = CollisionDetection::Intersect(MakeGizmoMouseRay(_cam, _mouse_pos), s_rotate_plane[_drag_axis >> 1])._point;
                    cur_hit = _cur_target_pos + Normalize(cur_hit - _cur_target_pos) * _scaled_axis_length;
                    Render::Gizmo::DrawLine(_cur_target_pos, _drag_start_hit, kDragingColor);
                    Render::Gizmo::DrawLine(_cur_target_pos, cur_hit, kDragingColor);
                }
            }
        }

        bool TransformGizmo::IsHover(Vector2f pos, Render::Camera *cam, u32 *hover_axis) const
        {
            if (!Target())
                return false;
            u32 hover = 0u;
            if (_mode == EGizmoMode::kTranslate)
            {
                Ray r = MakeGizmoMouseRay(cam, pos);
                hover = PickAxis(r._start, r._dir);
                if (IsAxisMaskAvailable(kAxisYZ) && CollisionDetection::Intersect(r, *_plane_obbs[0])._is_collision)
                {
                    hover |= kAxisYZ;
                }
                if (IsAxisMaskAvailable(kAxisXZ) && CollisionDetection::Intersect(r, *_plane_obbs[1])._is_collision)
                {
                    hover |= kAxisXZ;
                }
                if (IsAxisMaskAvailable(kAxisXY) && CollisionDetection::Intersect(r, *_plane_obbs[2])._is_collision)
                {
                    hover |= kAxisXY;
                }
                if (hover_axis)
                    *hover_axis = hover;
                return hover;
            }
            else if (_mode == EGizmoMode::kScale)
            {
                Ray r = MakeGizmoMouseRay(cam, pos);
                hover = PickAxis(r._start, r._dir);
                if (!Is2DMode() && CollisionDetection::Intersect(r, *_scale_center_obb)._is_collision)
                {
                    hover |= kAxisXYZ;
                }
                if (hover_axis)
                    *hover_axis = hover;
                return hover;
            }
            else
            {
                Ray r = MakeGizmoMouseRay(cam, pos);
                const f32 radius_threshold = _scaled_axis_length * 0.1f;
                for (u16 i = 0; i < 3; i++)
                {
                    const u32 axis_mask = 1u << i;
                    if (Is2DMode() && axis_mask != Get2DHiddenAxisMask())
                        continue;
                    if (auto hit_res = CollisionDetection::Intersect(r, s_rotate_plane[i]); hit_res._is_collision == true)
                    {
                        if (abs(Magnitude(hit_res._point - s_rotate_plane[i]._point) - _scaled_axis_length) < radius_threshold)
                            hover |= axis_mask;
                        if (hover)
                            break;
                    }
                }
                if (hover_axis)
                    *hover_axis = hover;
                return hover;
            }
            return false;
        }

    }// namespace Editor
}// namespace Ailu
