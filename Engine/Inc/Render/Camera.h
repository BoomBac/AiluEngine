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
        static Camera* GetDefaultCamera();
        static void DrawGizmo(const Camera* p_camera);
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
    private:
        void CalculateFrustum();
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

    //class CameraState
    //{
    //public:
    //    CameraState(const Camera& camera)
    //    {
    //        position = camera.Position();
    //        rotation = camera.Rotation();
    //        _world_y = Quaternion::AngleAxis(rotation.x, Camera::kUp);
    //        _object_x = Quaternion::AngleAxis(rotation.y, camera.Right());
    //    }
    //    void UpdateState(const Camera& camera)
    //    {
    //        position = camera.Position();
    //        rotation = camera.Rotation();
    //        _world_y = Quaternion::AngleAxis(rotation.x,Camera::kUp);
    //        Vector3f rotated_right = TransformCoord(_world_y.ToMat4f(), Camera::kRight);
    //        _object_x = Quaternion::AngleAxis(rotation.y, rotated_right);
    //    }
    //    void UpdateState(const Camera& camera,bool b)
    //    {
    //        _world_y = Quaternion::AngleAxis(rotation.x, Camera::kUp);
    //        Vector3f rotated_right = TransformCoord(_world_y.ToMat4f(), Camera::kRight);
    //        _object_x = Quaternion::AngleAxis(rotation.y, rotated_right);
    //    }

    //    void LerpTo(const CameraState& target,float speed)
    //    {
    //        position = lerp(position, target.position, speed);
    //        rotation = lerp(rotation, target.rotation, speed * 2.0f);
    //        //auto test = target._world_y - _world_y;
    //        //LOG_INFO("x:{},y:{},z:{}", test.x, test.y, test.z);
    //        _world_y = Quaternion::NLerp(_world_y, target._world_y, speed * 2.0f);
    //        _object_x = Quaternion::NLerp(_object_x, target._object_x, speed * 2.0f);
    //    }
    //    void UpdateCamrea(Camera& camera) const
    //    {
    //        camera.Position(position);
    //        camera.Rotation(rotation);
    //        camera._object_x = _object_x;
    //        camera._world_y = _world_y;
    //    }
    //    Vector3f position;
    //    Vector3f rotation;
    //    Quaternion _world_y;
    //    Quaternion _object_x;
    //};
}

#endif // !__CAMERA_H__