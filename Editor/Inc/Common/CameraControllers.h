#ifndef COMMON_CAMERA_CONTROLLERS_H
#define COMMON_CAMERA_CONTROLLERS_H
#pragma once

#include "Framework/Math/Transform.h"

namespace Ailu
{
    class PropertyInfo;
    namespace Render
    {
        class Camera;
        class VolumeTexturePreviewPass;
    }

    namespace Editor
    {
        class ICameraController
        {
        public:
            virtual ~ICameraController() = default;
            virtual void Update(f32 dt) { (void) dt; }
        };

        class FirstPersonCameraController : public ICameraController
        {
        public:
            FirstPersonCameraController();
            explicit FirstPersonCameraController(Render::Camera *camera);

            void Attach(Render::Camera *camera);
            void SetTargetPosition(const Vector3f &position, bool is_force = false);
            void SetTargetRotation(f32 x, f32 y, bool is_force = false);
            void Move(const Vector3f &d);
            void Interpolate(float speed);
            void Accelerate(bool faster) { _cur_move_speed = faster ? _fast_camera_move_speed : _base_camera_move_speed; };

        public:
            Vector2f _rotation;
            Vector3f _target_pos;
            bool _is_receive_input = true;
            f32 _base_camera_move_speed = 0.6f;
            f32 _fast_camera_move_speed = _base_camera_move_speed * 3.0f;
            f32 _cur_move_speed = _base_camera_move_speed;
            f32 _camera_wander_speed = 0.6f;
            f32 _lerp_speed_multifactor = 0.75f;
            f32 _camera_fov_h = 60.0f;
            f32 _camera_near = 0.01f;
            f32 _camera_far = 1000.0f;

        private:
            Render::Camera *_p_camera = nullptr;
            Quaternion _rot_object_x;
            Quaternion _rot_world_y;
        };

        class OrbitCameraController : public ICameraController
        {
        public:
            OrbitCameraController() = default;

            void Attach(Render::VolumeTexturePreviewPass *pass);
            void BeginDrag(const Vector2f &local_pos);
            void EndDrag();
            void Drag(const Vector2f &local_pos);
            void Zoom(f32 scroll_delta);

        private:
            void ApplyCameraPosition();

        private:
            Render::VolumeTexturePreviewPass *_pass = nullptr;
            PropertyInfo *_cam_pos_prop = nullptr;
            bool _is_dragging = false;
            Vector2f _last_mouse{0.f, 0.f};
            f32 _yaw = 0.f;
            f32 _pitch = 0.f;
            f32 _radius = 5.f;
            f32 _sensitivity = 0.01f;
            f32 _zoom_step = 0.5f;
            f32 _min_radius = 0.5f;
            f32 _max_radius = 100.0f;
        };
    }
}

#endif // COMMON_CAMERA_CONTROLLERS_H