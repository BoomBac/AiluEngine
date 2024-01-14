#ifndef __CAMERA_H__
#define __CAMERA_H__
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Objects/Object.h"

namespace Ailu
{
    DECLARE_ENUM(ECameraType, kOrthographic, kPerspective)

    class Frustum 
    {
    public:
        Frustum(float fov, float aspectRatio, float nearDist, float farDist, Vector3f position, Vector3f forward, Vector3f up) 
        {
            BuildPerspectiveFovLHMatrix(_proj, fov, aspectRatio, nearDist, farDist);
            BuildViewMatrixLookToLH(_view,position,forward,up);
            _view_proj = _proj * _view;
            ExtractPlanes();
        }

        // Check if a point is inside the frustum
        bool pointInside(const Vector3f&  point) const 
        {
            for (const auto& plane : _planes) 
            {
                if (DotProduct(plane, point) < 0.0)
                {
                    return false;
                }
            }
            return true;
        }
    private:
        void ExtractPlanes()
        {
            //// Extract planes using the rows of the combined frustum matrix
            //planes[0] = glm::row(frustumMatrix, 3) + glm::row(frustumMatrix, 0); // Left
            //planes[1] = glm::row(frustumMatrix, 3) - glm::row(frustumMatrix, 0); // Right
            //planes[2] = glm::row(frustumMatrix, 3) - glm::row(frustumMatrix, 1); // Top
            //planes[3] = glm::row(frustumMatrix, 3) + glm::row(frustumMatrix, 1); // Bottom
            //planes[4] = glm::row(frustumMatrix, 2); // Near
            //planes[5] = glm::row(frustumMatrix, 3) - glm::row(frustumMatrix, 2); // Far
            for (auto& plane : _planes)
            {
                plane = Normalize(plane);
            }
        }
    private:
        Matrix4x4f _proj;
        Matrix4x4f _view;
        Matrix4x4f _view_proj;
        std::array<Vector3f, 6> _planes;
    };

    class Camera : public Object
    {
        DECLARE_PROTECTED_PROPERTY(camera_type,Type, ECameraType::ECameraType)
        DECLARE_PROTECTED_PROPERTY(aspect,Aspect,float)
        DECLARE_PROTECTED_PROPERTY(near_clip,Near,float)
        DECLARE_PROTECTED_PROPERTY(far_clip,Far,float)
        //DECLARE_PROTECTED_PROPERTY(position,Position,Vector3f)
        //DECLARE_PROTECTED_PROPERTY(rotation,Rotation,Vector3f)
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
        static void DrawGizmo(const Camera* p_camera);
    public:
        void Update();
        void SetLens(float fovy, float aspect, float nz, float fz);
        void FovV(float angle);
        float FovV() const { return _fov_v; };
        void Position(const Vector3f& new_pos);
        void Rotation(const Vector3f& new_rot);
        void Position(const float& x, const float& y, const float& z);
        const Vector3f& Position() const { return _position; };
        const Vector3f& Rotation() const { return _rotation; };
        const Matrix4x4f& GetProjection() const { return _proj_matrix; }
        const Matrix4x4f& GetView() const { return _view_matrix; }
        void MoveForward(float dis);
        void MoveRight(float dis);
        void LookTo(const Vector3f& target, const Vector3f& up);
        bool IsInFrustum(const Vector3f& pos);
    private:
        void CalculateFrustum();
        void RotatePitch(float angle);
        void RotateYaw(float angle);
        void Rotate(float vertical, float horizontal);
        void UpdateViewMatrix();
        bool b_dirty_ = true;
        Vector3f _position;
        Vector3f _rotation;
        float _fov_v;
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