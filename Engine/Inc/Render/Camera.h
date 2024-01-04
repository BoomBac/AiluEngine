#ifndef __CAMERA_H__
#define __CAMERA_H__
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Objects/Object.h"

namespace Ailu
{
    DECLARE_ENUM(ECameraType, kOrthographic, kPerspective)

    class Camera : public Object
    {
        DECLARE_PROTECTED_PROPERTY(camera_type,Type, ECameraType::ECameraType)
        DECLARE_PROTECTED_PROPERTY(aspect,Aspect,float)
        DECLARE_PROTECTED_PROPERTY(near_clip,Near,float)
        DECLARE_PROTECTED_PROPERTY(far_clip,Far,float)
        //DECLARE_PROTECTED_PROPERTY(position,Position,Vector3f)
        DECLARE_PROTECTED_PROPERTY(rotation,Rotation,Vector3f)
        DECLARE_PROTECTED_PROPERTY(forward,Forward,Vector3f)
        DECLARE_PROTECTED_PROPERTY(right,Right,Vector3f)
        DECLARE_PROTECTED_PROPERTY(up,Up,Vector3f)
        DECLARE_PROTECTED_PROPERTY(fov_h,FovH,float)
        //Orthographic only
        DECLARE_PROTECTED_PROPERTY(size,Size,float)
        DECLARE_REFLECT_FIELD(Camera)
    public:
        inline const static Vector3f kRight{ 1.f,0.f,0.f };
        inline const static Vector3f kUp{ 0.f,1.f,0.f };
        inline const static Vector3f kForward{ 0.f,0.f,1.f };
        inline static Camera* sCurrent = nullptr;
        static Camera* GetDefaultCamera();
    public:
        Camera() : _camera_type(ECameraType::kPerspective)
        {
            IMPLEMENT_REFLECT_FIELD(Camera);
            UpdateViewMatrix();
        };
        Camera(float aspect, float near_clip = 10.0f, float far_clip = 10000.0f, ECameraType::ECameraType camera_type = ECameraType::kPerspective) :
            _aspect(aspect), _near_clip(near_clip), _far_clip(far_clip), _camera_type(camera_type)
        {
            IMPLEMENT_REFLECT_FIELD(Camera);
            UpdateViewMatrix();
        };
        //Camera(const Camera& other);
    public:
        void Update();
        void SetLens(float fovy, float aspect, float nz, float fz);
        void FovV(float angle);
        float FovV() const { return _fov_v; };
        //float GetNearPlaneWidth() const;
        //float GetFarPlaneWidth() const;
        //float GetNearPlaneHeight() const;
        //float GetFarPlaneHeight() const;
        void Position(const Vector3f& new_pos);
        void Position(const float& x, const float& y, const float& z);
        const Vector3f& Position() const { return _position; };
        const Matrix4x4f& GetProjection() const { return _proj_matrix; }
        const Matrix4x4f& GetView() const { return _view_matrix; }
        void MoveForward(float dis);
        void MoveRight(float dis);
    private:
        void RotatePitch(float angle);
        void RotateYaw(float angle);
        void Rotate(float vertical, float horizontal);
        void UpdateViewMatrix();
        bool b_dirty_ = true;
        Vector3f _position;
        float _fov_v;
        Matrix4x4f _view_matrix{};
        Matrix4x4f _proj_matrix{};
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

    class CameraState
    {
    public:
        CameraState(const Camera& camera)
        {
            position = camera.Position();
            rotation = camera.Rotation();
        }
        void UpdateState(const Camera& camera)
        {
            position = camera.Position();
            rotation = camera.Rotation();
        }
        void LerpTo(const CameraState& target,float speed)
        {
            position = lerp(position, target.position, speed);
            rotation = lerp(rotation, target.rotation, speed * 2.0f);
        }
        void UpdateCamrea(Camera& camera) const
        {
            camera.Position(position);
            camera.Rotation(rotation);
        }
        Vector3f position;
        Vector3f rotation;
    };
}

#endif // !__CAMERA_H__