#ifndef __CAMERA_H__
#define __CAMERA_H__
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
    class Camera
    {
    public:
        inline const static Vector3f kRight{ 1.f,0.f,0.f };
        inline const static Vector3f kUp{ 0.f,1.f,0.f };
        inline const static Vector3f kForward{ 0.f,0.f,1.f };
        inline static Camera* sCurrent = nullptr;
    public:
        Camera()
        {
            UpdateViewMatrix();
        };
        Camera(float aspect, float near_clip = 10.0f, float far_clip = 10000.0f) : 
            _aspect(aspect), _near_clip(near_clip), _far_clip(far_clip)
        {
            UpdateViewMatrix();
        };

        void SetLens(float fovy, float aspect, float nz, float fz);
        float GetNearZ() const { return _near_clip; };
        float GetFarZ() const { return _far_clip; };
        float GetAspect() const { return _aspect; };
        float GetFovX() const;
        float GetFovY() const;
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
        float _aspect;
        float _near_clip;
        float _far_clip;
        float _fov;
        float _near_plane_height;
        float _far_plane_height;
        Matrix4x4f _view_matrix{};
        Matrix4x4f _proj_matrix{};
    };

    class CameraState
    {
    public:
        CameraState(Camera camera)
        {
            position = camera.GetPosition();
            rotation = camera.GetRotation();
        }
        void LerpTo(const CameraState& target,float speed)
        {
            position = lerp(position, target.position, speed);
            rotation = lerp(rotation, target.rotation, speed);
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