#include "Common/TransformGizmo.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Inc/Physics/Collision.h"
#include "Render/Gizmo.h"

namespace Ailu
{
    namespace Editor
    {
        TransformGizmo::TransformGizmo()
        {
            auto shader = g_pResourceMgr->Load<Shader>(L"Shaders/transform_gizmo.alasset");
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
        }
        static Matrix4x4f MakeGizmoCylinder(const Vector3f &axis, const Vector3f &origin, f32 axis_length, f32 axis_radius)
        {
            Vector3f dir = Normalize(axis);
            Matrix4x4f orient;

            // 基础方向为 +Y，如果不是 +Y，需要旋转到目标方向
            Vector3f base = Vector3f::kUp;
            f32 dot = DotProduct(base, dir);

            if (fabs(dot - 1.0f) < 1e-6f)
            {
                orient = BuildIdentityMatrix();
            }
            else if (fabs(dot + 1.0f) < 1e-6f)
            {
                orient = MatrixRotationX(Math::kPi);// 反向朝下
            }
            else
            {
                Vector3f axis_rot = Normalize(CrossProduct(base, dir));
                f32 angle = acosf(dot);
                MatrixRotationAxis(orient,axis_rot, angle);
            }

            // 缩放与平移
            Matrix4x4f scale = MatrixScale(axis_radius, axis_length * 0.5f, axis_radius);
            Matrix4x4f translate = MatrixTranslation(0, axis_length * 0.5f, 0);

            return scale * translate * orient * MatrixTranslation(origin);
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

            // 缩放与平移
            Matrix4x4f scale = MatrixScale(axis_radius, axis_radius, axis_radius);
            Matrix4x4f translate = MatrixTranslation(0, height, 0);

            return scale * translate * orient * MatrixTranslation(origin);
        }

        static Matrix4x4f MakeGizmoPlane(Vector3f axis, Vector3f sub_axisa,Vector3f sub_axisb,const Vector3f &origin,f32 size)
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
            size *= 0.5f;
            // 缩放与平移
            Matrix4x4f scale = MatrixScale(size, 0.1f, size);
            return scale * orient * MatrixTranslation(origin + sub_axisa * size + sub_axisb * size);
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
                return camera.Size() * (desired_pixels / (f32) camera.OutputSize().y);
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
            Ray ray(start, dir);
            Vector3f origin = Transform::GetWorldPosition(*_target);
            f32 min_d = std::numeric_limits<f32>::max();
            u32 result = 0u;
            for (auto &a: _translate_axis)
            {
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

            // 本地空间：把本地轴旋到世界
            // 假设存在 Transform::GetWorldRotation
            Quaternion worldRot = Transform::GetWorldRotation(*_target);
            return Normalize(worldRot * localAxis);
        }
        // 让轴线段足够长，避免s被夹在[0, axis_length]
        static const f32 kVirualRayLen = 10000.0f;

        f32 TransformGizmo::ComputeAxisParamS(Vector2f mouse_pos, const Vector3f &origin, const Vector3f &axisDir) const
        {
            // 鼠标射线
            Vector3f camPos = _cam->Position();
            Vector3f rayDir = _cam->ScreenToWorld(mouse_pos, 0.0f);

            f32 s = 0.0f, t = 0.0f;
            Vector3f c1, c2;
            CollisionDetection::ClosestPtSegmentSegment(
                    origin - axisDir * (kVirualRayLen * 0.5f),
                    origin + axisDir * (kVirualRayLen * 0.5f),
                    camPos,
                    camPos + rayDir * kVirualRayLen,
                    s, t, c1, c2);
            // 此处的s通常是“从第一个端点起”的线性参数，配合上面的中心对称定义，可直接用于差分
            return s * kVirualRayLen;// _axis_length;
        }

        void TransformGizmo::BeginDrag(Vector2f mouse_pos)
        {
            if (!_target) return;

            // 锁定当前轴
            _drag_axis = _hover_axis;
            if (_drag_axis < 0) return;

            _is_dragging = true;
            _mouse_pos = mouse_pos;
            _drag_start_mouse_pos = mouse_pos;
            _drag_origin = Transform::GetWorldPosition(*_target);
            _drag_axis_num = 0u;
            if (_mode == EGizmoMode::kRotate)
            {
                _drag_start_hit = CollisionDetection::Intersect(Ray{_cam->Position(), _cam->ScreenToWorld(_mouse_pos)}, s_rotate_plane[_drag_axis>>1])._point;
                _drag_start_hit = _cur_target_pos + Normalize(_drag_start_hit - _cur_target_pos) * _scaled_axis_length;
                _drag_start_rot = _target->_rotation;
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
                _drag_start_pos = _drag_origin;// 简化：无父层或以世界位移为准
                if (_drag_axis_num == 2)
                {
                    s_drag_plane = Plane(_drag_start_pos, CrossProduct(_drag_axis_ctx[0]._drag_axis_dir, _drag_axis_ctx[1]._drag_axis_dir));
                    Ray ray{_cam->Position(), _cam->ScreenToWorld(_mouse_pos)};
                    _drag_start_hit = CollisionDetection::Intersect(ray, s_drag_plane)._point;
                }
                else if (_drag_axis_num == 3)//scale all
                {
                    s_drag_plane = Plane(_drag_start_pos, Normalize(_cam->Position() - _drag_origin));
                    Ray ray{_cam->Position(), _cam->ScreenToWorld(_mouse_pos)};
                    _drag_start_hit = CollisionDetection::Intersect(ray, s_drag_plane)._point;
                    _drag_start_target_delta = _drag_start_mouse_pos - _cam->WorldToScreen(_drag_start_pos);
                }
                _drag_start_scale = _target->_scale;
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
            _is_dragging = false;
            _drag_axis = -1;
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
            if (_target)
            {
                _cur_target_pos = Transform::GetWorldPosition(*_target);
                _dis_scale = ComputeGizmoScale(*_cam, _is_dragging ? _drag_start_pos : _cur_target_pos, 100.0f);
                if (!_is_dragging)
                {
                    _scaled_axis_length = _axis_length * _dis_scale;
                    _scaled_axis_radius = _axis_radius * _dis_scale;
                    IsHover(_mouse_pos, _cam, &_hover_axis);
                    if (_mode == EGizmoMode::kTranslate)
                    {
                        //s_p_plane 为2x2
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
                    if (_mode == EGizmoMode::kTranslate || _mode == EGizmoMode::kScale)
                    {
                        Vector3f view_dir = Normalize(_cam->Position() - _cur_target_pos);
                        for (auto &a: _translate_axis)
                        {
                            f32 dot = DotProduct(a._default_dir, view_dir);
                            a._dir = (dot >= 0.0f) ? a._default_dir : -a._default_dir;// 朝向相机的方向
                            if (a._axis & _hover_axis)
                                a._mat->SetVector("_color", kHoverColor);
                            else
                                a._mat->SetVector("_color", kNormalColors[a._index]);
                        }
                    }
                    else
                    {
                        for (u32 i = 0; i < 3; i++)
                        {
                            if (_hover_axis & (1 << i))
                                _rotate_rings[i]->SetVector("_color", kHoverColor);
                            else
                                _rotate_rings[i]->SetVector("_color", kNormalColors[i]);
                        }
                    }
                }
                // 正在拖拽：计算位移并应用
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
                        else if (_drag_axis_num == 2)//多轴不能简单叠加，因为两轴的最近点可能不在同一平面
                        {
                            Ray ray{_cam->Position(), _cam->ScreenToWorld(mouse_pos)};
                            Vector3f hit = CollisionDetection::Intersect(ray, s_drag_plane)._point;
                            world_delta = hit - _drag_start_hit;
                            Render::Gizmo::DrawLine(_drag_start_hit, hit, Colors::kYellow);
                            Vector2f p = mouse_pos + Vector2f{20, 20};
                            Render::Gizmo::DrawText(std::format("delta: {}", world_delta.ToString()), p, 10u, Colors::kCyan);
                            Render::Gizmo::DrawText(std::format("hit: {}", hit.ToString()), p + Vector2f(0.0f, 118 * 10 / 65.0f), 10u, Colors::kCyan);
                            Render::Gizmo::DrawText(std::format("origin: {}", _drag_start_pos.ToString()), p + Vector2f(0.0f, 118 * 10 / 65.0f) * 2.0f, 10u, Colors::kCyan);
                        }
                        else {}
                        // 注意：有父节点层级时，需要把world_delta转换到local空间再叠加到local position
                        _target->_position = _drag_start_pos + world_delta;
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
                            _target->_scale = _drag_start_scale * world_scale;
                        }
                        else if (_drag_axis_num == 3)//多轴不能简单叠加，因为两轴的最近点可能不在同一平面
                        {
                            // 屏幕空间拖拽距离
                            Vector2f v = _drag_start_mouse_pos - _mouse_pos;
                            float pixel_distance = Magnitude(v);

                            // 拖拽方向与相机-物体方向在屏幕的投影点积，稳定符号
                            Vector3f gizmo_dir_ws = Normalize(_drag_start_pos - _cam->Position());
                            Vector2f gizmo_dir_ss = Normalize(Vector2f(DotProduct(gizmo_dir_ws, _cam->Right()), -DotProduct(gizmo_dir_ws, _cam->Up())));
                            Render::Gizmo::DrawLine(Vector2f{500.0f, 500.0f}, Vector2f{500.0f, 500.0f}  + gizmo_dir_ss);
                            Vector2f drag_dir = Normalize(v);
                            float proj = DotProduct(gizmo_dir_ss, drag_dir);

                            // 防止微小跳变
                            if (fabs(proj) < 0.1f)
                                proj = 0.0f;

                            // 将屏幕距离映射为世界空间距离
                            // 计算屏幕上移动 1 像素对应的世界单位长度
                            Ray ray0 = Ray{_cam->Position(), _cam->ScreenToWorld(_drag_start_mouse_pos)};
                            Ray ray1 = Ray{_cam->Position(), _cam->ScreenToWorld(_drag_start_mouse_pos + Vector2f(1, 1))};

                            // 在 gizmo 所在平面上求交点
                            Vector3f hit0 = CollisionDetection::Intersect(ray0, s_drag_plane)._point;
                            Vector3f hit1 = CollisionDetection::Intersect(ray1, s_drag_plane)._point;

                            // 每像素对应的世界单位
                            float world_per_pixel = Magnitude(hit1 - hit0);

                            // 得到当前世界空间“缩放距离”
                            float world_distance = pixel_distance * world_per_pixel * proj;

                            // 根据 gizmo 尺寸或场景比例做调节
                            float s = world_distance * 0.5f;// scale 系数调节

                            // 三轴统一缩放
                            world_scale = Vector3f(s);

                            // 应用到目标
                            _target->_scale = _drag_start_scale + world_scale;
                            _drag_scale_factor = Vector3f::kOne + world_scale;
                            Vector2f p = mouse_pos + Vector2f{20, 20};
                            Render::Gizmo::DrawText(std::format("now s: {}", s), p, 10u, Colors::kCyan);
                        }
                        else {}
                        // 注意：有父节点层级时，需要把world_delta转换到local空间再叠加到local position

                        Render::Gizmo::DrawLine(Vector3f::kZero, _target->_position);
                    }
                    else//if (_mode == EGizmoMode::kRotate)
                    {
                        Vector3f axis = GetAxisDirWorld(_drag_axis >> 1);// 例如 X: (1,0,0)
                        _drag_current_hit = CollisionDetection::Intersect(
                                               Ray{_cam->Position(), _cam->ScreenToWorld(_mouse_pos)},
                                               s_rotate_plane[_drag_axis >> 1])
                                               ._point;
                        _drag_current_hit = _cur_target_pos + Normalize(_drag_current_hit - _cur_target_pos) * _scaled_axis_length;
                        Vector3f v0 = Normalize(_drag_start_hit - _cur_target_pos);
                        Vector3f v1 = Normalize(_drag_current_hit - _cur_target_pos);
                        f32 cos_theta = std::clamp(DotProduct(v0, v1), -1.0f, 1.0f);
                        f32 sin_theta = DotProduct(CrossProduct(v0, v1), axis);
                        f32 angle = atan2(sin_theta, cos_theta);
                        Vector2f p = mouse_pos + Vector2f{20, 20};
                        Render::Gizmo::DrawText(std::format("angle: {}", angle * k2Angle), p, 10u, Colors::kCyan);
                        _drag_rot = Quaternion::AngleAxis(angle * k2Angle, axis);
                        _target->_rotation = _drag_start_rot * _drag_rot;
                    }
                }
            }
        }

        void TransformGizmo::Draw()
        {
            if (!_target) 
                return;
            if (_mode == EGizmoMode::kTranslate)
            {
                for (auto &axis: _translate_axis)
                {
                    f32 scale_factor = (_hover_axis & axis._axis) ? 1.2f : 1.0f;
                    Render::Gizmo::DrawMesh(Render::Mesh::s_cylinder.lock().get(),
                                            MakeGizmoCylinder(axis._dir, _cur_target_pos, _scaled_axis_length, _scaled_axis_radius * scale_factor),
                                            axis._mat.get());

                    Render::Gizmo::DrawMesh(Render::Mesh::s_cone.lock().get(),
                                            MakeGizmoCone(axis._dir, _cur_target_pos, _scaled_axis_length, _scaled_axis_radius * 4.0f * scale_factor),
                                            axis._mat.get());
                }
                //s_p_plane 为2x2
                const f32 quad_w = _scaled_axis_length * _axis_quad_width_scale;
                //X->YZ Plane
                {
                    Render::Gizmo::DrawMesh(Render::Mesh::s_plane.lock().get(),
                                            MakeGizmoPlane(_translate_axis[0]._dir, _translate_axis[1]._dir, _translate_axis[2]._dir, _cur_target_pos, quad_w),
                                            _translate_axis[0]._mat.get());
                }
                //Y->XZ Plane
                {
                    Render::Gizmo::DrawMesh(Render::Mesh::s_plane.lock().get(),
                                            MakeGizmoPlane(_translate_axis[1]._dir, _translate_axis[0]._dir, _translate_axis[2]._dir, _cur_target_pos, quad_w),
                                            _translate_axis[1]._mat.get());
                }
                //Z->XY Plane
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
                Render::Gizmo::DrawMesh(Render::Mesh::s_cube.lock().get(), MatrixScale(s,s,s)* MatrixTranslation(_cur_target_pos),
                                        _translate_axis[0]._mat.get());
            }
            else//if (_mode == EGizmoMode::kRotate)
            {
                for (auto &axis: _translate_axis)
                {
                    f32 scale_factor = _scaled_axis_length * (_hover_axis & axis._axis ? 1.2f : 1.05f);
                    //Render::Gizmo::DrawCircle(_cur_target_pos, _scaled_axis_length, 36u, _hover_axis & axis._axis ? kHoverColor : kNormalColors[axis._index],MakeCircle(axis._dir));
                    auto rmat = Quaternion::ToMat4f(_drag_rot);
                    rmat = MakeCircle(axis._dir) * rmat;
                    Render::Gizmo::DrawMesh(Render::Mesh::s_plane.lock().get(),
                                            MatrixScale(scale_factor, scale_factor, scale_factor) * rmat * MatrixTranslation(_cur_target_pos),
                                            _rotate_rings[axis._index].get());
                }
                if (_is_dragging)
                {
                    Vector3f cur_hit = CollisionDetection::Intersect(Ray{_cam->Position(), _cam->ScreenToWorld(_mouse_pos)}, s_rotate_plane[_drag_axis >> 1])._point;
                    cur_hit = _cur_target_pos + Normalize(cur_hit - _cur_target_pos) * _scaled_axis_length;
                    Render::Gizmo::DrawLine(_cur_target_pos, _drag_start_hit, kDragingColor);
                    Render::Gizmo::DrawLine(_cur_target_pos, cur_hit, kDragingColor);
                }
            }
        }

        bool TransformGizmo::IsHover(Vector2f pos, Render::Camera *cam, u32 *hover_axis) const
        {
            if (!_target)
                return false;
            u32 hover = 0u;
            if (_mode == EGizmoMode::kTranslate)
            {
                auto &cam_pos = cam->Position();
                hover = PickAxis(cam_pos, cam->ScreenToWorld(pos, 0.0f));
                Ray r{cam_pos, cam->ScreenToWorld(pos, 0.0f)};
                if (CollisionDetection::Intersect(r, *_plane_obbs[0])._is_collision)
                {
                    hover |= kAxisYZ;
                }
                if (CollisionDetection::Intersect(r, *_plane_obbs[1])._is_collision)
                {
                    hover |= kAxisXZ;
                }
                if (CollisionDetection::Intersect(r, *_plane_obbs[2])._is_collision)
                {
                    hover |= kAxisXY;
                }
                if (hover_axis)
                    *hover_axis = hover;
                return hover;
            }
            else if (_mode == EGizmoMode::kScale)
            {
                auto &cam_pos = cam->Position();
                hover = PickAxis(cam_pos, cam->ScreenToWorld(pos, 0.0f));
                Ray r{cam_pos, cam->ScreenToWorld(pos, 0.0f)};
                if (CollisionDetection::Intersect(r, *_scale_center_obb)._is_collision)
                {
                    hover |= kAxisXYZ;
                }
                if (hover_axis)
                    *hover_axis = hover;
                return hover;
            }
            else
            {
                auto &cam_pos = cam->Position();
                Ray r{cam_pos, cam->ScreenToWorld(pos, 0.0f)};
                const f32 radius_threshold = _scaled_axis_length * 0.1f;
                for (u16 i = 0; i < 3; i++)
                {
                    if (auto hit_res = CollisionDetection::Intersect(r, s_rotate_plane[i]); hit_res._is_collision == true)
                    {
                        if (abs(Magnitude(hit_res._point - s_rotate_plane[i]._point) - _scaled_axis_length) < radius_threshold)
                            hover |= 1 << i;
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