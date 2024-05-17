#ifndef __CAMERA_H__
#define __CAMERA_H__
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Framework/Math/Geometry.h"

namespace Ailu
{
    DECLARE_ENUM(ECameraType, kOrthographic, kPerspective)
    class Camera : public Object
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
        static void DrawGizmo(const Camera* p_camera);
        static void CalcViewFrustumPlane(const Camera* cam, ViewFrustum& vf);
        //目前当eye_camera转动时，会重新计算aabb从而导致阴影抖动
        static Camera GetFitShaodwCamera(const Camera& eye_cam, float shadow_dis);
    public:
        Camera();
        Camera(float aspect, float near_clip = 10.0f, float far_clip = 10000.0f, ECameraType::ECameraType camera_type = ECameraType::kPerspective);
        void RecalculateMarix(bool force = false);
        void SetLens(float fovh, float aspect, float nz, float fz);
        const Matrix4x4f& GetProjection() const { return _proj_matrix; }
        const Matrix4x4f& GetView() const { return _view_matrix; }
        void LookTo(const Vector3f& direction, const Vector3f& up);
        bool IsInFrustum(const Vector3f& pos);
        void MarkDirty() { _is_dirty = true; }
        const ViewFrustum& GetViewFrustum() const { return _vf; }
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

    class FirstPersonCameraController
    {
    public:
        FirstPersonCameraController();
        FirstPersonCameraController(Camera* camera);
        void Attach(Camera* camera);
        void SetPosition(const Vector3f& position);
        void SetRotation(float x,float y);
        void Move(const Vector3f& d);
        void MoveForward(float distance);
        void MoveRight(float distance);
        void MoveUp(float distance);
        void TurnHorizontal(float angle);
        void TurnVertical(float angle);
        void InterpolateTo(const Vector3f target_pos,float rot_x,float rot_y,float speed);
        Vector2f _rotation;
        static FirstPersonCameraController s_instance;
    private:
        Camera* _p_camera;
        Quaternion _rot_object_x;
        Quaternion _rot_world_y;
    };
}

#endif // !__CAMERA_H__