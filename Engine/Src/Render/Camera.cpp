#include "Render/Camera.h"
#include "Render/Gizmo.h"
#include "Render/Renderer.h"
#include "Scene/Scene.h"
#include "Framework/Common/SystemInfo.h"
#include "pch.h"

namespace Ailu::Render
{
    struct HaltonSequence
    {
        int count;
        int index;
        std::vector<float> arrX;
        std::vector<float> arrY;

        u64 frameCount;
        HaltonSequence() = default;
        HaltonSequence(int count) : count(count), index(0), arrX(count), arrY(count), frameCount(0)
        {
            for (int i = 0; i < count; ++i)
            {
                arrX[i] = get(i, 2);
            }

            for (int i = 0; i < count; ++i)
            {
                arrY[i] = get(i, 3);
            }
        }

        float get(int index, int base_)
        {
            float fraction = 1.0f;
            float result = 0.0f;

            while (index > 0)
            {
                fraction /= base_;
                result += fraction * (index % base_);
                index /= base_;
            }

            return result;
        }

        void Get(float &x, float &y)
        {
            if (++index == count) index = 1;
            x = arrX[index];
            y = arrY[index];
        }
        void Get(f32 &x, f32 &y,u64 seed)
        {
            seed = seed % count;
            x = arrX[seed];
            y = arrY[seed];
        }
    };

    HaltonSequence g_halton_seq(1024);

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

        auto& vf = p_camera->_vf;
        for (int i = 0; i < 6; i++)
        {
        	auto& p = vf._planes[i];
        	float3 start = p_camera->Position();
        	Gizmo::DrawLine(p._point, p._point + p._normal,Color(1.0f,0.0f,1.0f,1.0f));
        }
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
        BuildViewMatrixLookToLH(view, Vector3f(0.f, 0.f, -50.f), Vector3f::kForward, Vector3f::kUp);
        BuildOrthographicMatrix(proj, -half_width, half_width, half_height, -half_height, 1.f, 200.f);
        return view * proj;
    }

    AABB Camera::GetBoundingAABB(const Camera &eye_cam, f32 start, f32 end)
    {
        end = end <= 0.0f ? 1.0f : end;
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
        RecalculateMatrix();
    }

    void Camera::RecalculateMatrix(bool force)
    {
        if (!force && !_is_dirty)
            return;
        if (g_pGfxContext)
        {
            u64 cur_frame = g_pGfxContext->GetFrameCount();
            if (cur_frame != _prev_vp_matrix_update_frame)
            {
                _prev_view_proj_matrix = _no_jitter_view_proj_matrix;
                _prev_vp_matrix_update_frame = cur_frame;
            }
        }
        if (_camera_type == ECameraType::kPerspective)
            BuildPerspectiveFovLHMatrix(_proj_matrix, _fov_h * k2Radius, _aspect, _near_clip, _far_clip);
        else
        {
            float left = -_size * 0.5f;
            float right = _size * 0.5f;
            float top = (_size * 0.5f) / _aspect;
            float bottom = -(_size * 0.5f) / _aspect;
            BuildOrthographicMatrix(_proj_matrix, left, right, top, bottom,_near_clip, _far_clip);
        }
        _no_jitter_view_proj_matrix = _view_matrix * _proj_matrix;
        _forward = _rotation * Vector3f::kForward;
        _up = _rotation * Vector3f::kUp;
        _right = CrossProduct(_up, _forward);
        BuildViewMatrixLookToLH(_view_matrix, _position, _forward, _up);
        if (_anti_aliasing == EAntiAliasing::kTAA)
            ProcessJitter();
        CalculateFrustum();
    }

    void Camera::SetLens(float fovy, float aspect, float nz, float fz)
    {
        _fov_h = fovy;
        _aspect = aspect;
        _near_clip = nz;
        _far_clip = fz;
    }

    Vector3f Camera::ScreenToWorld(const Vector2f &screen_pos, f32 depth) const
    {
        Vector2f vp_size((f32) _pixel_width, (f32) _pixel_height);
        // NDC 坐标 [-1,1]
        float ndc_x = 2.0f * screen_pos.x / vp_size.x - 1.0f;
        float ndc_y = 1.0f - 2.0f * screen_pos.y / vp_size.y;
        Matrix4x4f inv_view = MatrixInverse(_view_matrix);

        if (_camera_type == ECameraType::kOrthographic)
        {
            //--------------------------------------------
            // 正交相机
            //--------------------------------------------
            // 计算视口范围（由相机参数定义）
            f32 ortho_width = _size;
            f32 ortho_height = _size / _aspect;
            float left = -ortho_width * 0.5f;
            float right = ortho_width * 0.5f;
            float bottom = -ortho_height * 0.5f;
            float top = ortho_height * 0.5f;

            // 映射到相机局部空间
            float x = left + (right - left) * (ndc_x * 0.5f + 0.5f);
            float y = bottom + (top - bottom) * (ndc_y * 0.5f + 0.5f);
            float z = -depth;

            // 相机空间 → 世界空间
            Vector3f local_point(x, y, z);
            Vector3f world_point = TransformCoord(inv_view, local_point);

            if (depth == 0.0f)
            {
                // depth=0 → 返回朝向世界的射线方向（即相机 -Z 方向）
                Vector3f dir = TransformNormal(inv_view, Vector3f(0, 0, -1));
                return Normalize(dir);
            }
            else
            {
                return world_point;
            }
        }
        else
        {
            //--------------------------------------------
            // 透视相机
            //--------------------------------------------
            Matrix4x4f inv_proj = MatrixInverse(_proj_matrix);

            // 在投影空间中从 NDC 转到相机空间
            Vector4f clip_pos(ndc_x, ndc_y, 1.0f, 1.0f);
            Vector4f cam_pos = TransformVector(inv_proj, clip_pos);
            cam_pos /= cam_pos.w;

            // 相机空间方向
            Vector3f ray_dir(cam_pos.x, cam_pos.y, cam_pos.z);
            ray_dir = Normalize(ray_dir);

            // 转换到世界空间
            ray_dir = TransformNormal(inv_view, ray_dir);
            ray_dir = Normalize(ray_dir);
            if (depth == 0.0f)
            {
                return ray_dir;// 单位长度方向
            }
            else
            {
                // 从相机位置出发，沿着 ray_dir 方向前进 depth
                Vector3f cam_world_pos = TransformCoord(inv_view,Vector3f(0, 0, 0));
                return cam_world_pos + ray_dir * (_near_clip + Lerp(_near_clip, _far_clip, depth));
            }
        }
    }

    Vector2f Camera::WorldToScreen(const Vector3f &world_pos) const
    {
        // 屏幕像素大小
        Vector2f vp_size((f32) _pixel_width, (f32) _pixel_height);

        // 世界空间 → 视图空间
        Vector4f view_pos = TransformVector(_view_matrix, Vector4f(world_pos, 1.0f));

        // -----------------------------------------------------
        // 正交相机
        // -----------------------------------------------------
        if (_camera_type == ECameraType::kOrthographic)
        {
            // 构造正交投影参数
            f32 ortho_width = _size;
            f32 ortho_height = _size / _aspect;
            float left = -ortho_width * 0.5f;
            float right = ortho_width * 0.5f;
            float bottom = -ortho_height * 0.5f;
            float top = ortho_height * 0.5f;

            // 相机空间 → NDC
            float ndc_x = (view_pos.x - left) / (right - left) * 2.0f - 1.0f;
            float ndc_y = (view_pos.y - bottom) / (top - bottom) * 2.0f - 1.0f;
            float ndc_z = -view_pos.z;// 深度符号方向保持一致

            // NDC → 屏幕坐标
            float screen_x = (ndc_x * 0.5f + 0.5f) * vp_size.x;
            float screen_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * vp_size.y;

            return Vector2f(screen_x, screen_y);
        }
        // -----------------------------------------------------
        // 透视相机
        // -----------------------------------------------------
        else
        {
            // 相机空间 → 裁剪空间
            Vector4f clip_pos = TransformVector(_proj_matrix, view_pos);

            // 透视除法 → NDC
            if (clip_pos.w != 0.0f)
                clip_pos /= clip_pos.w;

            float ndc_x = clip_pos.x;
            float ndc_y = clip_pos.y;
            float ndc_z = clip_pos.z;// 可用于 depth 输出

            // NDC → 屏幕像素坐标
            float screen_x = (ndc_x * 0.5f + 0.5f) * vp_size.x;
            float screen_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * vp_size.y;

            return Vector2f(screen_x, screen_y);
        }
    }


    void Camera::LookTo(const Vector3f &direction, const Vector3f &up)
    {
        if (g_pGfxContext)
        {
            u64 cur_frame = g_pGfxContext->GetFrameCount();
            if (cur_frame != _prev_vp_matrix_update_frame)
            {
                _prev_view_proj_matrix = _no_jitter_view_proj_matrix;
                _prev_vp_matrix_update_frame = cur_frame;
            }
        }
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
            float right = _size * 0.5f;
            float top = (_size * 0.5f) / _aspect;
            float bottom = -(_size * 0.5f) / _aspect;
            BuildOrthographicMatrix(_proj_matrix, left, right, top, bottom,_near_clip,_far_clip);
        }
        _no_jitter_view_proj_matrix = _view_matrix * _proj_matrix;
        BuildViewMatrixLookToLH(_view_matrix, _position, _forward, _up);
        if (_anti_aliasing == EAntiAliasing::kTAA)
            ProcessJitter();
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
        CalculateFrustum();
    }
    void Camera::CalculateFrustum()
    {
        Matrix4x4f ivp = _view_matrix * _proj_matrix;
        ivp = MatrixInverse(ivp);
        Vector4f temp = TransformVector(ivp,Vector4f(-1.0,-1.0,kZNear,1.0));
        _near_bottom_left = temp.xyz / temp.w;
        temp = TransformVector(ivp, Vector4f(1.0, -1.0, kZNear, 1.0));
        _near_bottom_right = temp.xyz / temp.w;
        temp = TransformVector(ivp, Vector4f(-1.0, 1.0, kZNear, 1.0));
        _near_top_left = temp.xyz / temp.w;
        temp = TransformVector(ivp, Vector4f(1.0, 1.0, kZNear, 1.0));
        _near_top_right = temp.xyz / temp.w;
        temp = TransformVector(ivp, Vector4f(-1.0, -1.0, kZFar, 1.0));
        _far_bottom_left = temp.xyz / temp.w;
        temp = TransformVector(ivp, Vector4f(1.0, -1.0, kZFar, 1.0));
        _far_bottom_right = temp.xyz / temp.w;
        temp = TransformVector(ivp, Vector4f(-1.0, 1.0, kZFar, 1.0));
        _far_top_left = temp.xyz / temp.w;
        temp = TransformVector(ivp, Vector4f(1.0, 1.0, kZFar, 1.0));
        _far_top_right = temp.xyz / temp.w;

        // 计算各个平面的中心位置
        Vector3f nearPlaneCenter = calculateCenter(_near_top_left, _near_top_right, _near_bottom_left, _near_bottom_right);
        Vector3f farPlaneCenter = calculateCenter(_far_top_left, _far_top_right, _far_bottom_left, _far_bottom_right);
        Vector3f leftPlaneCenter = calculateCenter(_near_top_left, _far_top_left, _near_bottom_left, _far_bottom_left);
        Vector3f rightPlaneCenter = calculateCenter(_near_top_right, _far_top_right, _near_bottom_right, _far_bottom_right);
        Vector3f topPlaneCenter = calculateCenter(_near_top_left, _far_top_left, _near_top_right, _far_top_right);
        Vector3f bottomPlaneCenter = calculateCenter(_near_bottom_left, _far_bottom_left, _near_bottom_right, _far_bottom_right);
        _vf._planes[0]._normal = CrossProduct(_near_top_left - _near_bottom_left, _near_bottom_right - _near_bottom_left);
        _vf._planes[1]._normal = -_vf._planes[0]._normal;
        _vf._planes[2]._normal = CrossProduct(_far_top_left - _near_top_left, _near_top_right - _near_top_left);             //top
        _vf._planes[3]._normal = -CrossProduct(_far_bottom_left - _near_bottom_left, _near_bottom_right - _near_bottom_left);//bottom
        _vf._planes[4]._normal = CrossProduct(_far_bottom_left - _near_bottom_left, _near_top_left - _near_bottom_left);     //left
        _vf._planes[5]._normal = -CrossProduct(_far_bottom_right - _near_bottom_right, _near_top_right - _near_bottom_right);//right
        _vf._planes[0]._point = nearPlaneCenter;
        _vf._planes[1]._point = farPlaneCenter;
        _vf._planes[2]._point = topPlaneCenter;
        _vf._planes[3]._point = bottomPlaneCenter;
        _vf._planes[4]._point = leftPlaneCenter;
        _vf._planes[5]._point = rightPlaneCenter;
        for (int i = 0; i < 6; ++i)
        {
            _vf._planes[i]._normal = Normalize(_vf._planes[i]._normal);
            _vf._planes[i]._distance = -DotProduct(_vf._planes[i]._normal, _vf._planes[i]._point);
        }
        // _vf._planes[0]._distance = -DotProduct(_vf._planes[0]._normal, _near_top_left);
        // _vf._planes[1]._distance = -DotProduct(_vf._planes[1]._normal, _far_top_left);
        // _vf._planes[2]._distance = -DotProduct(_vf._planes[2]._normal, _far_top_left);
        // _vf._planes[3]._distance = -DotProduct(_vf._planes[3]._normal, _near_bottom_left);
        // _vf._planes[4]._distance = -DotProduct(_vf._planes[4]._normal, _far_bottom_left);
        // _vf._planes[5]._distance = -DotProduct(_vf._planes[5]._normal, _far_bottom_right);
    }
    Camera &Camera::GetCubemapGenCamera(const Camera &base_cam, ECubemapFace::ECubemapFace face)
    {
        static bool s_is_init = false;
        static Camera cameras[6];
        if (!s_is_init)
        {
            cameras[0].Name("CubemapGenCamera+X");
            cameras[1].Name("CubemapGenCamera-X");
            cameras[2].Name("CubemapGenCamera+Y");
            cameras[3].Name("CubemapGenCamera-Y");
            cameras[4].Name("CubemapGenCamera+Z");
            cameras[5].Name("CubemapGenCamera-Z");
            s_is_init = true;
        }
        Camera &out = cameras[face - 1];
        out._layer_mask = base_cam._layer_mask;
        float x = base_cam._position.x, y = base_cam._position.y, z = base_cam._position.z;
        Vector3f center = {x, y, z};
        Vector3f world_up = Vector3f::kUp;
        Matrix4x4f  proj{};
        BuildPerspectiveFovLHMatrix(proj, 90 * k2Radius, 1.0, base_cam._near_clip, base_cam._far_clip);
        const static Vector3f targets[] =
                {
                        {+1.f, 0.f, 0.0f},//+x
                        {-1.f, 0.f, 0.f}, //-x
                        {0.f, +1.f, 0.f}, //+y
                        {0.f, -1.f, 0.f}, //-y
                        {0.f, 0.f, +1.f}, //+z
                        {0.f, 0.f, -1.f}  //-z
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
    
    void Camera::CalculateZBUfferAndProjParams(const Camera &cam, Vector4f &zb, Vector4f &proj_params)
    {
        f32 f = cam.Far(), n = cam.Near();
        u32 pixel_width = cam.OutputSize().x, pixel_height = cam.OutputSize().y;
        proj_params = Vector4f(1.0f,n,f,1.f / f);
        if (SystemInfo::kReverseZ)
        zb = Vector4f(-1.f + f/n,1.f,(-1.f + f/n) / f,1 / f);
        else
        {
            zb = Vector4f(1.f - f/n,f/n,0.f,0.f);
            zb.zw = {zb.x / f,zb.y / f};
        }
    }
    Archive &operator<<(Archive &ar, const Camera &c)
    {
        ar.IncreaseIndent();
        ar << ar.GetIndent() << "_type:" << ECameraType::ToString(c._camera_type) << std::endl;
        ar << ar.GetIndent() << "_aspect:" << c.Aspect() << std::endl;
        ar << ar.GetIndent() << "_far:" << c.Far() << std::endl;
        ar << ar.GetIndent() << "_near:" << c.Near() << std::endl;
        ar << ar.GetIndent() << "_fovh:" << c.FovH() << std::endl;
        ar << ar.GetIndent() << "_size:" << c.Size() << std::endl;
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
        f32 new_aspect = (f32) w / (f32) h;
        if (new_aspect != _aspect)
        {
            _aspect = new_aspect;
            MarkDirty();
            RecalculateMatrix();
        }
    }
    void Camera::GetCornerInWorld(Vector3f &lt, Vector3f &lb, Vector3f &rt, Vector3f &rb) const
    {
        // lt = Normalize(_far_top_left - _near_top_left);
        // lb = Normalize(_far_bottom_left - _near_bottom_left);
        // rt = Normalize(_far_top_right - _near_top_right);
        // rb = Normalize(_far_bottom_right - _near_bottom_right);
        lt = _far_top_left;
        lb = _far_bottom_left;
        rt = _far_top_right;
        rb = _far_bottom_right;
    }
    void Camera::ProcessJitter()
    {
        f32 offset_x, offset_y;
        g_halton_seq.Get(offset_x, offset_y,g_pGfxContext? g_pGfxContext->GetFrameCount():0u);
        if (_camera_type == ECameraType::kOrthographic)
        {
            _proj_matrix[0][3] -= (offset_x * 2 - 1) / (f32)_pixel_width;
            _proj_matrix[1][3] -= (offset_y * 2 - 1) / (f32)_pixel_height;
        }
        else
        {
            _jitter.x = (offset_x * 2 - 1) / (f32)_pixel_width  * _jitter_scale;
            _jitter.y = (offset_y * 2 - 1) / (f32)_pixel_height * _jitter_scale;
            _proj_matrix[2][0] += _jitter.x;
            _proj_matrix[2][1] += _jitter.y;
        }
        _jitter.z = (offset_x - 0.5f) / (f32)_pixel_width  * _jitter_scale;
        _jitter.w = (offset_y - 0.5f) / (f32)_pixel_height * _jitter_scale;
    }
}// namespace Ailu
