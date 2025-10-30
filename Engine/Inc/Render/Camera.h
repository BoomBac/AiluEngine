#pragma warning(disable : 4275)//dll interface warning
#ifndef __CAMERA_H__
#define __CAMERA_H__
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Geometry.h"
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Objects/Serialize.h"
#include "Render/RenderingData.h"
#include "generated/Camera.gen.h"

namespace Ailu
{
    namespace Render
    {
        AENUM()
        enum class EAntiAliasing
        {
            kNone,
            kFXAA,
            kTAA
        };
        class Renderer;
        DECLARE_ENUM(ECameraType, kOrthographic, kPerspective)

        class AILU_API Camera : public Object
        {
            friend struct CCamera;

            DECLARE_PROTECTED_PROPERTY(camera_type, Type, ECameraType::ECameraType)
            DECLARE_PROTECTED_PROPERTY(aspect, Aspect, float)
            DECLARE_PROTECTED_PROPERTY(near_clip, Near, float)
            DECLARE_PROTECTED_PROPERTY(far_clip, Far, float)
            DECLARE_PROTECTED_PROPERTY(position, Position, Vector3f)
            DECLARE_PROTECTED_PROPERTY(rotation, Rotation, Quaternion)
            DECLARE_PROTECTED_PROPERTY(forward, Forward, Vector3f)
            DECLARE_PROTECTED_PROPERTY(right, Right, Vector3f)
            DECLARE_PROTECTED_PROPERTY(up, Up, Vector3f)
            DECLARE_PROTECTED_PROPERTY(fov_h, FovH, float)
            //Orthographic only
            DECLARE_PROTECTED_PROPERTY(size, Size, float)

        public:
            inline static Camera *sCurrent = nullptr;
            inline static Camera *sSelected = nullptr;
            static Camera *GetDefaultCamera();
            static void DrawGizmo(const Camera *p_camera, Color c);
            static Matrix4x4f GetDefaultOrthogonalViewProj(f32 w, f32 h, f32 near  = 1.f, f32 far  = 200.f);
            //when the eye_camera turns, the AABB is recalculated, resulting in shadow jitter
            static AABB GetBoundingAABB(const Camera &eye_cam, f32 start = 0.0f, f32 end = -1.0f);
            static Sphere GetShadowCascadeSphere(const Camera &eye_cam, f32 start = 0.0f, f32 end = -1.0f);
            static Camera &GetCubemapGenCamera(const Camera &base_cam, ECubemapFace::ECubemapFace face);

        public:
            static void CalculateZBUfferAndProjParams(const Camera &cam, Vector4f &zb, Vector4f &proj_params);
            Camera();
            Camera(float aspect, float near_clip = 10.0f, float far_clip = 10000.0f, ECameraType::ECameraType camera_type = ECameraType::kPerspective);
            void RecalculateMatrix(bool force = false);
            void SetLens(float fovh, float aspect, float nz, float fz);

            void OutputSize(u16 w, u16 h)
            {
                _pixel_width = w;
                _pixel_height = h;
                _aspect = (f32) _pixel_width / (f32) _pixel_height;
            };
            Vector2D<u32> OutputSize() const { return Vector2D<u32>{_pixel_width, _pixel_height}; }
            //包括抖动的透视矩阵
            const Matrix4x4f &GetProj() const { return _proj_matrix; }

            const Matrix4x4f GetProjNoJitter() const
            {
                Matrix4x4f p = _proj_matrix;
                p[2][0] -= _jitter.x;
                p[2][1] -= _jitter.y;
                return p;
            }
            Vector3f ScreenToWorld(const Vector2f &screen_pos, f32 depth = 0.0f) const;
            Vector2f WorldToScreen(const Vector3f& world_pos) const;

            const Matrix4x4f &GetView() const { return _view_matrix; }
            /// @brief 返回当前帧的透视投影矩阵（不包括抖动）
            const Matrix4x4f &GetViewProj() const { return _no_jitter_view_proj_matrix; }
            /// @brief 返回上一帧的透视投影矩阵（不包括抖动）
            const Matrix4x4f &GetPrevViewProj() const { return _prev_view_proj_matrix; }
            /// @brief 返回当前帧抖动
            /// @return xy: jitter(-1~+1) matrix,zw: jitter(-0.5~0.5) uv
            const Vector4f &GetJitter() const { return _jitter; }
            void LookTo(const Vector3f &direction, const Vector3f &up);
            bool IsInFrustum(const Vector3f &pos);
            void MarkDirty() { _is_dirty = true; }
            const ViewFrustum &GetViewFrustum() const { return _vf; }
            void SetRenderer(Renderer *r);
            RenderTexture *TargetTexture() const { return _target_texture; }
            void TargetTexture(RenderTexture *output) { _target_texture = output; }
            void SetViewAndProj(const Matrix4x4f &view, const Matrix4x4f &proj);
            void SetPixelSize(u16 w, u16 h);
            /// @brief 获取视椎体世界坐标系的四个边指向远平面方向（归一化）
            /// @param lt 左上
            /// @param lb 左下
            /// @param rt 右上
            /// @param rb 右下
            void GetCornerInWorld(Vector3f &lt, Vector3f &lb, Vector3f &rt, Vector3f &rb) const;
            //temp
            bool IsCustomVP() const { return _is_custom_vp; }
            friend Archive &operator<<(Archive &ar, const Camera &c);
            friend Archive &operator>>(Archive &ar, Camera &c);
            Vector4f GetZBufferParams() const { return _zbuffer_params; }
            Vector4f GetProjParams() const { return _proj_params; }

        public:
            u32 _layer_mask = (u32) -1;
            bool _is_scene_camera = false;
            bool _is_render_shadow = true;
            bool _is_render_sky_box = true;
            bool _is_enable_postprocess = true;
            bool _is_gen_voxel = false;
            bool _is_enable = false;
            EAntiAliasing _anti_aliasing = EAntiAliasing::kNone;
            f32 _jitter_scale = 1.0f;

        private:
            void CalculateFrustum();
            void ProcessJitter();

        private:
            bool _is_custom_vp = false;
            ViewFrustum _vf;
            bool _is_dirty = true;
            Matrix4x4f _view_matrix{};
            Matrix4x4f _proj_matrix{};
            Matrix4x4f _no_jitter_view_proj_matrix{};
            Matrix4x4f _prev_view_proj_matrix{};
            Vector4f _zbuffer_params;
            Vector4f _proj_params;
            Vector3f _near_top_left;
            Vector3f _near_top_right;
            Vector3f _near_bottom_left;
            Vector3f _near_bottom_right;
            Vector3f _far_top_left;
            Vector3f _far_top_right;
            Vector3f _far_bottom_left;
            Vector3f _far_bottom_right;
            Vector4f _jitter;
            u16 _pixel_width = 800u, _pixel_height = 450u;
            Renderer *_renderer = nullptr;
            RenderTexture *_target_texture = nullptr;
            u64 _prev_vp_matrix_update_frame = 0u;
        };

        Archive &operator<<(Archive &ar, const Camera &c);
        Archive &operator>>(Archive &ar, Camera &c);
    }
}// namespace Ailu

#endif// !__CAMERA_H__