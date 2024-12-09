#include "Render/Camera.h"
#include "Render/Gizmo.h"
#include "pch.h"
//#include "Render/RenderQueue.h"
#include "Render/Renderer.h"
#include "Scene/Scene.h"

namespace Ailu
{
    Camera *Camera::GetDefaultCamera()
    {
        static Camera cam(16.0F / 9.0F);
        cam.Position(Vector3f::kZero);
        cam.Rotation(Quaternion());
        cam.SetLens(1.57f, 16.f / 9.f, 1.f, 5000.f);
        Camera::sCurrent = &cam;
        return &cam;
    }

    void Camera::DrawGizmo(const Camera *p_camera, Color c)
    {
        // 绘制立方体的边框
        Gizmo::DrawLine(p_camera->_near_top_left, p_camera->_near_top_right, c);
        Gizmo::DrawLine(p_camera->_near_top_right, p_camera->_near_bottom_right, c);
        Gizmo::DrawLine(p_camera->_near_bottom_right, p_camera->_near_bottom_left, c);
        Gizmo::DrawLine(p_camera->_near_bottom_left, p_camera->_near_top_left, c);

        Gizmo::DrawLine(p_camera->_far_top_left, p_camera->_far_top_right, c);
        Gizmo::DrawLine(p_camera->_far_top_right, p_camera->_far_bottom_right, c);
        Gizmo::DrawLine(p_camera->_far_bottom_right, p_camera->_far_bottom_left, c);
        Gizmo::DrawLine(p_camera->_far_bottom_left, p_camera->_far_top_left, c);

        // 绘制连接立方体的线
        Gizmo::DrawLine(p_camera->_near_top_left, p_camera->_far_top_left, c);
        Gizmo::DrawLine(p_camera->_near_top_right, p_camera->_far_top_right, c);
        Gizmo::DrawLine(p_camera->_near_bottom_right, p_camera->_far_bottom_right, c);
        Gizmo::DrawLine(p_camera->_near_bottom_left, p_camera->_far_bottom_left, c);
        Gizmo::DrawLine(p_camera->_near_bottom_left, p_camera->_far_bottom_left, c);

        // auto& vf = p_camera->_vf;
        // for (int i = 0; i < 6; i++)
        // {
        // 	auto& p = vf._planes[i];
        // 	float3 start = p_camera->Position();
        // 	Gizmo::DrawLine(p._point, p._point + p._normal * 100,Color(1.0f,0.0f,1.0f,1.0f));
        // }
    }

    static Vector3f calculateCenter(Vector3f p1, Vector3f p2, Vector3f p3, Vector3f p4)
    {
        return (p1 + p2 + p3 + p4) * 0.25f;
    }
    Matrix4x4f Camera::GetDefaultOrthogonalViewProj(f32 w, f32 h, f32 near, f32 far)
    {
        Matrix4x4f view, proj;
        f32 aspect = w / h;
        f32 half_width = w * 0.5f, half_height = h * 0.5f;
        BuildViewMatrixLookToLH(view,Vector3f(0.f,0.f,-50.f),Vector3f::kForward,Vector3f::kUp);
        BuildOrthographicMatrix(proj,-half_width,half_width,half_height,-half_height,1.f,200.f);
        return view * proj;
    }

    void Camera::CalcViewFrustumPlane(const Camera *cam, ViewFrustum &vf)
    {
        // 计算各个平面的中心位置
        Vector3f nearPlaneCenter = calculateCenter(cam->_near_top_left, cam->_near_top_right, cam->_near_bottom_left, cam->_near_bottom_right);
        Vector3f farPlaneCenter = calculateCenter(cam->_far_top_left, cam->_far_top_right, cam->_far_bottom_left, cam->_far_bottom_right);
        Vector3f leftPlaneCenter = calculateCenter(cam->_near_top_left, cam->_far_top_left, cam->_near_bottom_left, cam->_far_bottom_left);
        Vector3f rightPlaneCenter = calculateCenter(cam->_near_top_right, cam->_far_top_right, cam->_near_bottom_right, cam->_far_bottom_right);
        Vector3f topPlaneCenter = calculateCenter(cam->_near_top_left, cam->_far_top_left, cam->_near_top_right, cam->_far_top_right);
        Vector3f bottomPlaneCenter = calculateCenter(cam->_near_bottom_left, cam->_far_bottom_left, cam->_near_bottom_right, cam->_far_bottom_right);
        vf._planes[0]._normal = CrossProduct(cam->_near_top_left - cam->_near_bottom_left, cam->_near_bottom_right - cam->_near_bottom_left);
        vf._planes[1]._normal = -vf._planes[0]._normal;
        vf._planes[2]._normal = CrossProduct(cam->_far_top_left - cam->_near_top_left, cam->_near_top_right - cam->_near_top_left);             //top
        vf._planes[3]._normal = -CrossProduct(cam->_far_bottom_left - cam->_near_bottom_left, cam->_near_bottom_right - cam->_near_bottom_left);//bottom
        vf._planes[4]._normal = CrossProduct(cam->_far_bottom_left - cam->_near_bottom_left, cam->_near_top_left - cam->_near_bottom_left);     //left
        vf._planes[5]._normal = -CrossProduct(cam->_far_bottom_right - cam->_near_bottom_right, cam->_near_top_right - cam->_near_bottom_right);//right
        vf._planes[0]._point = nearPlaneCenter;
        vf._planes[1]._point = farPlaneCenter;
        vf._planes[2]._point = topPlaneCenter;
        vf._planes[3]._point = bottomPlaneCenter;
        vf._planes[4]._point = leftPlaneCenter;
        vf._planes[5]._point = rightPlaneCenter;
        for (int i = 0; i < 6; ++i)
        {
            vf._planes[i]._normal = Normalize(vf._planes[i]._normal);
        }
        vf._planes[0]._distance = -DotProduct(vf._planes[0]._normal, cam->_near_top_left);
        vf._planes[1]._distance = -DotProduct(vf._planes[1]._normal, cam->_far_top_left);
        vf._planes[2]._distance = -DotProduct(vf._planes[2]._normal, cam->_far_top_left);
        vf._planes[3]._distance = -DotProduct(vf._planes[3]._normal, cam->_near_bottom_left);
        vf._planes[4]._distance = -DotProduct(vf._planes[4]._normal, cam->_far_bottom_left);
        vf._planes[5]._distance = -DotProduct(vf._planes[5]._normal, cam->_far_bottom_right);
    }

    AABB Camera::GetBoundingAABB(const Camera &eye_cam, f32 start, f32 end)
    {
        end = end <= 0.0f? 1.0f : end;
        f32 end_len = end / eye_cam.Far(), start_len = start / eye_cam.Far();
        //end_len = std::min<f32>(end_len, 1.0);
        start_len = std::clamp(start_len, 0.0f, end_len);
        Vector3f vf_dir[4];
        vf_dir[0] = eye_cam._far_bottom_left - eye_cam._position;
        vf_dir[1] = eye_cam._far_bottom_right - eye_cam._position;
        vf_dir[2] = eye_cam._far_top_left - eye_cam._position;
        vf_dir[3] = eye_cam._far_top_right - eye_cam._position;
        Vector<Vector3f> p(8);
        p[0] = eye_cam._position + (vf_dir[0]) * end_len;
        p[1] = eye_cam._position + (vf_dir[1]) * end_len;
        p[2] = eye_cam._position + (vf_dir[2]) * end_len;
        p[3] = eye_cam._position + (vf_dir[3]) * end_len;
        f32 box_width = end - start;
        p[4] = p[0] - eye_cam._forward * box_width;
        p[5] = p[1] - eye_cam._forward * box_width;
        p[6] = p[2] - eye_cam._forward * box_width;
        p[7] = p[3] - eye_cam._forward * box_width;//根据阴影距离平行于摄像机屏幕裁切出一个立方体，摄像机局部轴对齐包围盒
        auto vmax = AABB::MaxAABB(), vmin = AABB::MinAABB();
        for (int i = 0; i < 8; i++)
        {
            vmax = Max(vmax, p[i]);
            vmin = Min(vmin, p[i]);
        }
        return AABB(vmin, vmax);
    }

    Sphere Camera::GetShadowCascadeSphere(const Camera &eye_cam, f32 start, f32 end)
    {
        if (end < 0.0f)
            end = QuailtySetting::s_main_light_shaodw_distance;
        f32 end_len = end / eye_cam.Far(), start_len = start / eye_cam.Far();
        end_len = std::min<f32>(end_len, 1.0);
        start_len = std::clamp(start_len, 0.0f, end_len);
        Vector3f vf_dir[4];
        vf_dir[0] = eye_cam._far_bottom_left - eye_cam._position;
        vf_dir[1] = eye_cam._far_bottom_right - eye_cam._position;
        vf_dir[2] = eye_cam._far_top_left - eye_cam._position;
        vf_dir[3] = eye_cam._far_top_right - eye_cam._position;
        for (u16 i = 0; i < 4; i++)
            Normalize(vf_dir[i]);
        Vector<Vector3f> p(8);
        p[0] = eye_cam._position + (vf_dir[0]) * end_len;
        p[1] = eye_cam._position + (vf_dir[1]) * end_len;
        p[2] = eye_cam._position + (vf_dir[2]) * end_len;
        p[3] = eye_cam._position + (vf_dir[3]) * end_len;
        p[4] = eye_cam._position + (vf_dir[0]) * start_len;
        p[5] = eye_cam._position + (vf_dir[1]) * start_len;
        p[6] = eye_cam._position + (vf_dir[2]) * start_len;
        p[7] = eye_cam._position + (vf_dir[3]) * start_len;
        Vector3f center = (p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + p[7]) * 0.125f;
        f32 r = Distance(center, p[0]);
        return Sphere(center, r);
    }

    Camera::Camera() : Camera(16.0F / 9.0F)
    {
    }

    Camera::Camera(float aspect, float near_clip, float far_clip, ECameraType::ECameraType camera_type) : _aspect(aspect), _near_clip(near_clip), _far_clip(far_clip), _camera_type(camera_type), _fov_h(60.0f),
                                                                                                          _forward(Vector3f::kForward), _right(Vector3f::kRight), _up(Vector3f::kUp)
    {
        MarkDirty();
        RecalculateMarix();
    }

    void Camera::RecalculateMarix(bool force)
    {
        if (!force && !_is_dirty) return;
        if (_camera_type == ECameraType::kPerspective)
            BuildPerspectiveFovLHMatrix(_proj_matrix, _fov_h * k2Radius, _aspect, _near_clip, _far_clip);
        else
        {
            float left = -_size * 0.5f;
            float right = -left;
            float top = right / _aspect;
            float bottom = -top;
            BuildOrthographicMatrix(_proj_matrix, left, right, top, bottom, _near_clip, _far_clip);
        }
        _forward = _rotation * Vector3f::kForward;
        _up = _rotation * Vector3f::kUp;
        _right = CrossProduct(_up, _forward);
        BuildViewMatrixLookToLH(_view_matrix, _position, _forward, _up);
        CalculateFrustum();
    }

    void Camera::SetLens(float fovy, float aspect, float nz, float fz)
    {
        _fov_h = fovy;
        _aspect = aspect;
        _near_clip = nz;
        _far_clip = fz;
    }

    void Camera::LookTo(const Vector3f &direction, const Vector3f &up)
    {
        _forward = direction;
        _right = CrossProduct(up, _forward);
        _up = CrossProduct(_forward, _right);
        _forward = Normalize(_forward);
        _right = Normalize(_right);
        _up = Normalize(_up);
        if (_camera_type == ECameraType::kPerspective)
            BuildPerspectiveFovLHMatrix(_proj_matrix, _fov_h * k2Radius, _aspect, _near_clip, _far_clip);
        else
        {
            float left = -_size * 0.5f;
            float right = -left;
            float top = right / _aspect;
            float bottom = -top;
            BuildOrthographicMatrix(_proj_matrix, left, right, top, bottom, _near_clip, _far_clip);
        }
        BuildViewMatrixLookToLH(_view_matrix, _position, _forward, _up);
        CalculateFrustum();
    }

    bool Camera::IsInFrustum(const Vector3f &pos)
    {
        return true;
    }

    void Camera::SetRenderer(Renderer *r)
    {
        _renderer = r;
        //_target_texture = g_pRenderTexturePool->Get(r->GetRenderingData()._camera_color_target_handle);
    }


    void Camera::SetViewAndProj(const Matrix4x4f &view, const Matrix4x4f &proj)
    {
        _view_matrix = view;
        _proj_matrix = proj;
        _is_custom_vp = true;
    }
    void Camera::CalculateFrustum()
    {
        float half_width{0.f}, half_height{0.f};
        if (_camera_type == ECameraType::kPerspective)
        {
            float tanHalfFov = tan(ToRadius(_fov_h) * 0.5f);
            half_height = _near_clip * tanHalfFov;
            half_width = half_height * _aspect;
        }
        else
        {
            half_width = _size * 0.5f;
            half_height = half_width / _aspect;
        }
        Vector3f near_top_left(-half_width, half_height, _near_clip);
        Vector3f near_top_right(half_width, half_height, _near_clip);
        Vector3f near_bottom_left(-half_width, -half_height, _near_clip);
        Vector3f near_bottom_right(half_width, -half_height, _near_clip);
        Vector3f far_top_left, far_top_right, far_bottom_left, far_bottom_right;
        if (_camera_type == ECameraType::kPerspective)
        {
            far_top_left = near_top_left * (_far_clip / _near_clip);
            far_top_right = near_top_right * (_far_clip / _near_clip);
            far_bottom_left = near_bottom_left * (_far_clip / _near_clip);
            far_bottom_right = near_bottom_right * (_far_clip / _near_clip);
        }
        else
        {
            far_top_left = near_top_left;
            far_top_right = near_top_right;
            far_bottom_left = near_bottom_left;
            far_bottom_right = near_bottom_right;
            float distance = _far_clip - _near_clip;
            far_top_left.z += distance;
            far_top_right.z += distance;
            far_bottom_left.z += distance;
            far_bottom_right.z += distance;
        }
        Matrix4x4f camera_to_world = _view_matrix;
        camera_to_world = Math::MatrixInverse(camera_to_world);
        Math::TransformCoord(near_top_left, camera_to_world);
        Math::TransformCoord(near_top_right, camera_to_world);
        Math::TransformCoord(near_bottom_left, camera_to_world);
        Math::TransformCoord(near_bottom_right, camera_to_world);
        Math::TransformCoord(far_top_left, camera_to_world);
        Math::TransformCoord(far_top_right, camera_to_world);
        Math::TransformCoord(far_bottom_left, camera_to_world);
        Math::TransformCoord(far_bottom_right, camera_to_world);
        _near_bottom_left = near_bottom_left;
        _near_bottom_right = near_bottom_right;
        _near_top_left = near_top_left;
        _near_top_right = near_top_right;
        _far_bottom_left = far_bottom_left;
        _far_bottom_right = far_bottom_right;
        _far_top_left = far_top_left;
        _far_top_right = far_top_right;
        CalcViewFrustumPlane(this, _vf);
    }
    Camera &Camera::GetCubemapGenCamera(const Camera &base_cam, ECubemapFace::ECubemapFace face)
    {
        static Camera cameras[6];
        Camera &out = cameras[face - 1];
        out._layer_mask = base_cam._layer_mask;
        float x = base_cam._position.x, y = base_cam._position.y, z = base_cam._position.z;
        Vector3f center = {x, y, z};
        Vector3f world_up = Vector3f::kUp;
        Matrix4x4f view, proj;
        BuildPerspectiveFovLHMatrix(proj, 90 * k2Radius, 1.0, base_cam._near_clip, base_cam._far_clip);
        const static Vector3f targets[] =
                {
                        {+1.f, 0.f, 0.0f},//+x
                        {-1.f, 0.f, 0.f},//-x
                        {0.f, +1.f, 0.f},//+y
                        {0.f, -1.f, 0.f},//-y
                        {0.f, 0.f,+1.f},//+z
                        {0.f, 0.f,-1.f} //-z
                };
        const static Vector3f ups[] =
                {
                        {0.f, 1.f, 0.f}, //+x
                        {0.f, 1.f, 0.f}, //-x
                        {0.f, 0.f, -1.f},//+y
                        {0.f, 0.f, 1.f}, //-y
                        {0.f, 1.f, 0.f}, //+z
                        {0.f, 1.f, 0.f}  //-z
                };
        //BuildViewMatrixLookToLH(view, center, targets[face+1], ups[face+1]);
        out.Position(base_cam._position);
        out.SetLens(90.0, 1.0, base_cam._near_clip, base_cam._far_clip);
        out.LookTo(Normalize(targets[face - 1]), Normalize(ups[face - 1]));
        return out;
    }
    Archive &operator<<(Archive &ar, const Camera &c)
    {
        ar.IncreaseIndent();
        ar << ar.GetIndent() << "_type:" << ECameraType::ToString(c._camera_type) << std::endl;
        ar << ar.GetIndent() << "_aspect:" << c.Aspect() << std::endl;
        ar << ar.GetIndent() << "_far:"    << c.Far() << std::endl;
        ar << ar.GetIndent() << "_near:"   << c.Near() << std::endl;
        ar << ar.GetIndent() << "_fovh:"   << c.FovH() << std::endl;
        ar << ar.GetIndent() << "_size:"   << c.Size() << std::endl;
        ar.DecreaseIndent();
        return ar;
    }
    Archive &operator>>(Archive &ar, Camera &c)
    {
        String buf;
        ar >> buf;
        AL_ASSERT(su::BeginWith(buf, "_type"));
        c._camera_type = ECameraType::FromString(su::Split(buf, ":")[1]);
        ar >> buf;
        c._aspect = std::stof(su::Split(buf, ":")[1]);
        ar >> buf;
        c._far_clip = std::stof(su::Split(buf, ":")[1]);
        ar >> buf;
        c._near_clip = std::stof(su::Split(buf, ":")[1]);
        ar >> buf;
        c._fov_h = std::stof(su::Split(buf, ":")[1]);
        ar >> buf;
        c._size = std::stof(su::Split(buf, ":")[1]);
        return ar;
    }
    void Camera::SetPixelSize(u16 w, u16 h)
    {
        _pixel_width = w;
        _pixel_height = h;
        f32 new_aspect = (f32)w / (f32)h;
        if (new_aspect != _aspect)
        {
            _aspect = new_aspect;
            MarkDirty();
            RecalculateMarix();
        }
    }
}// namespace Ailu
