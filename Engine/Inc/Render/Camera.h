#ifndef __CAMERA_H__
#define __CAMERA_H__
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Objects/Object.h"

namespace Ailu
{
    enum class ECameraType
    {
        kOrthographic,
        kPerspective
    };
    static String ECameraTypeStr(ECameraType type)
    {
        switch (type)
        {
        case Ailu::ECameraType::kOrthographic: return String{ "Orthographic" };
        case Ailu::ECameraType::kPerspective: return String{ "Perspective" };
        default: return "undefined";
        }
    };

    class Camera : public Object
    {
        DECLARE_PROTECTED_PROPERTY(camera_type,Type,ECameraType)
        DECLARE_PROTECTED_PROPERTY(aspect,Aspect,float)
        DECLARE_PROTECTED_PROPERTY(near_clip,Near,float)
        DECLARE_PROTECTED_PROPERTY(far_clip,Far,float)
    public:
        inline const static Vector3f kRight{ 1.f,0.f,0.f };
        inline const static Vector3f kUp{ 0.f,1.f,0.f };
        inline const static Vector3f kForward{ 0.f,0.f,1.f };
        inline static Camera* sCurrent = nullptr;
        static Camera* GetDefaultCamera();
    public:
        Camera() : _camera_type(ECameraType::kPerspective)
        {
            UpdateViewMatrix();
        };
        Camera(float aspect, float near_clip = 10.0f, float far_clip = 10000.0f, ECameraType camera_type = ECameraType::kPerspective) :
            _aspect(aspect), _near_clip(near_clip), _far_clip(far_clip), _camera_type(camera_type)
        {
            UpdateViewMatrix();
        };
    public:
        void SetLens(float fovy, float aspect, float nz, float fz);
        void SetFovH(float angle);
        void SetFovV(float angle);
        float GetFovH() const { return _hfov; };
        float GetFovV() const { return _vfov; };
        float GetNearPlaneWidth() const;
        float GetFarPlaneWidth() const;
        float GetNearPlaneHeight() const;
        float GetFarPlaneHeight() const;
        const Matrix4x4f& GetProjection() const { return _proj_matrix; }
        const Matrix4x4f& GetView() const { return _view_matrix; }
        void SetPosition(const float& x, const float& y, const float& z);
        void SetPosition(const Vector3f& new_pos);
        const Vector3f& GetPosition() const { return position_; };
        const Vector3f& GetRotation() const { return _rotation; };
        const Vector3f& GetForward() const { return forawrd_; };
        const Vector3f& GetRight() const { return right_; };
        const Vector3f& GetUp() const { return up_; };
        void MoveForward(float dis);
        void MoveRight(float dis);
        //上下俯仰
        void RotatePitch(float angle);
        //左右偏航
        void RotateYaw(float angle);
        void Rotate(float vertical, float horizontal);      
    protected:
        void UpdateViewMatrix();
        Vector3f position_{ 0.f, 0.0f, -1000.f };
        Vector3f _rotation{ 0.f,0.f,0.f };
        Vector3f forawrd_{ 0.0f, 0.0f, 1.f };
        Vector3f up_{ 0.0f, 1.0f, 0.0f };
        Vector3f right_{ 1.f,0.f,0.f };

        bool b_dirty_ = true;
        float _hfov;
        float _vfov;
        float _near_plane_height;
        float _far_plane_height;
        Matrix4x4f _view_matrix{};
        Matrix4x4f _proj_matrix{};
    };

    class CameraState
    {
    public:
        CameraState(const Camera& camera)
        {
            position = camera.GetPosition();
            rotation = camera.GetRotation();
        }
        void UpdateState(const Camera& camera)
        {
            position = camera.GetPosition();
            rotation = camera.GetRotation();
        }
        void LerpTo(const CameraState& target,float speed)
        {
            position = lerp(position, target.position, speed);
            rotation = lerp(rotation, target.rotation, speed * 2.0f);
        }
        void UpdateCamrea(Camera& camera) const
        {
            camera.SetPosition(position);
            camera.Rotate(rotation.y, rotation.x);
        }
        Vector3f position;
        Vector3f rotation;
    };
}

#endif // !__CAMERA_H__