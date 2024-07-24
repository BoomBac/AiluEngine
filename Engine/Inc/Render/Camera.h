#pragma warning(disable: 4275) //dll interface warning
#ifndef __CAMERA_H__
#define __CAMERA_H__
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Framework/Math/Geometry.h"
#include "Render/RenderingData.h"

namespace Ailu
{
    class Scene;
    class Renderer;
    DECLARE_ENUM(ECameraType, kOrthographic, kPerspective)
    class AILU_API Camera : public Object
    {
        friend class CameraComponent;

        DECLARE_PROTECTED_PROPERTY(camera_type,Type, ECameraType::ECameraType)
        DECLARE_PROTECTED_PROPERTY(aspect,Aspect,float)
        DECLARE_PROTECTED_PROPERTY(near_clip,Near,float)
        DECLARE_PROTECTED_PROPERTY(far_clip,Far,float)
        DECLARE_PROTECTED_PROPERTY(position,Position,Vector3f)
        DECLARE_PROTECTED_PROPERTY(rotation,Rotation,Quaternion)
        DECLARE_PROTECTED_PROPERTY(forward,Forward,Vector3f)
        DECLARE_PROTECTED_PROPERTY(right,Right,Vector3f)
        DECLARE_PROTECTED_PROPERTY(up,Up,Vector3f)
        DECLARE_PROTECTED_PROPERTY(fov_h,FovH,float)
        //Orthographic only
        DECLARE_PROTECTED_PROPERTY(size,Size,float)
        DECLARE_REFLECT_FIELD(Camera)
    public:
        inline static Camera* sCurrent = nullptr;
        inline static Camera* sSelected = nullptr;
        static Camera* GetDefaultCamera();
        static void DrawGizmo(const Camera* p_camera,Color32 c);
        static void CalcViewFrustumPlane(const Camera* cam, ViewFrustum& vf);
        //when the eye_camera turns, the AABB is recalculated, resulting in shadow jitter
        static AABB GetShadowCascadeAABB(const Camera& eye_cam, f32 start = 0.0f,f32 end = -1.0f);
        static Sphere GetShadowCascadeSphere(const Camera& eye_cam, f32 start = 0.0f, f32 end = -1.0f);
    public:
        Camera();
        Camera(float aspect, float near_clip = 10.0f, float far_clip = 10000.0f, ECameraType::ECameraType camera_type = ECameraType::kPerspective);
        void Cull(Scene* s);
        void RecalculateMarix(bool force = false);
        void SetLens(float fovh, float aspect, float nz, float fz);
        void Rect(u16 w, u16 h) 
        {
            _pixel_width = w;
            _pixel_height = h;
            _aspect = (f32)_pixel_width / (f32)_pixel_height;
        };
        Vector2D<u32> Rect() const {return Vector2D<u32>{_pixel_width, _pixel_height};}
        const Matrix4x4f& GetProjection() const { return _proj_matrix; }
        const Matrix4x4f& GetView() const { return _view_matrix; }
        void LookTo(const Vector3f& direction, const Vector3f& up);
        bool IsInFrustum(const Vector3f& pos);
        void MarkDirty() { _is_dirty = true; }
        const ViewFrustum& GetViewFrustum() const { return _vf; }
        const CullResult& CullResults() const { return _cull_results; };
        void SetRenderer(Renderer* r);
        RenderTexture* TargetTexture() { return _target_texture; }
    private:
        void CalculateFrustum();
    private:
        ViewFrustum _vf;
        bool _is_dirty = true;
        Matrix4x4f _view_matrix{};
        Matrix4x4f _proj_matrix{};

        Vector3f _near_top_left;
        Vector3f _near_top_right;
        Vector3f _near_bottom_left;
        Vector3f _near_bottom_right;
        Vector3f _far_top_left;
        Vector3f _far_top_right;
        Vector3f _far_bottom_left;
        Vector3f _far_bottom_right;
        CullResult _cull_results;
        u16 _pixel_width = 1600u, _pixel_height = 900u;
        Renderer* _renderer = nullptr;
        RenderTexture* _target_texture = nullptr;
    };
    REFLECT_FILED_BEGIN(Camera)
    DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f,Position,_position)
    DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f,Rotation,_rotation)
    DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, Aspect,_aspect)
    DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, Near,_near_clip)
    DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, Far,_far_clip)
    DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, Size,_size)
    DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, FovH,_fov_h)
    REFLECT_FILED_END
}

#endif // !__CAMERA_H__