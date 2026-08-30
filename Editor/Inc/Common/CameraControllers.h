#ifndef COMMON_CAMERA_CONTROLLERS_H
#define COMMON_CAMERA_CONTROLLERS_H
#pragma once

#include "Framework/Math/Transform.h"
#include "Framework/Math/Quaternion.h"

namespace Ailu
{
    using Math::Quaternion;
    using Math::Vector2f;
    using Math::Vector3f;

    namespace Render
    {
        class Camera;
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

            void SetOrbit(const Vector3f &target, f32 yaw, f32 pitch, f32 distance);
            void SetTarget(const Vector3f &target) { _target = target; }
            void SetCameraPosition(const Vector3f &position);
            void SetDistanceLimits(f32 min_distance, f32 max_distance);

            Vector3f GetTarget() const { return _target; }
            Vector3f GetCameraPosition() const;
            Vector3f GetCameraForward() const;
            Vector3f GetCameraRight() const;
            Vector3f GetCameraUp() const;

            void BeginOrbit(const Vector2f &local_pos);
            void EndOrbit();
            void Orbit(const Vector2f &local_pos);
            void BeginPan(const Vector2f &local_pos);
            void EndPan();
            void Pan(const Vector2f &local_pos, const Vector2f &view_size, f32 vertical_fov);
            void Zoom(f32 scroll_delta);

        private:
            Vector3f GetCameraOffset() const;
            void ClampDistance();

        private:
            Vector3f _target = Vector3f::kZero;
            bool _is_orbiting = false;
            bool _is_panning = false;
            Vector2f _last_orbit_mouse{0.f, 0.f};
            Vector2f _last_pan_mouse{0.f, 0.f};
            f32 _yaw = 0.f;
            f32 _pitch = 0.f;
            f32 _radius = 5.f;
            f32 _sensitivity = 0.01f;
            f32 _zoom_factor = 0.9f;
            f32 _min_radius = 0.01f;
            f32 _max_radius = 1000000.0f;
        };

        class CanvasCameraController : public ICameraController
        {
        public:
            CanvasCameraController() = default;

            void Attach(Render::Camera *camera);
            void SetViewBasis(const Vector3f &direction, const Vector3f &up);
            void BeginDrag(const Vector2f &local_pos);
            void EndDrag();
            void Drag(const Vector2f &local_pos, const Vector2f &view_size);
            void Zoom(f32 scroll_delta);

        private:
            void ApplyView();

        private:
            Render::Camera *_p_camera = nullptr;
            Vector3f _direction = Vector3f::kForward;
            Vector3f _up = Vector3f::kUp;
            bool _is_dragging = false;
            Vector2f _last_mouse = Vector2f::kZero;
            f32 _zoom_factor = 0.9f;
            f32 _min_size = 0.05f;
            f32 _max_size = 10000.0f;
        };
    }
}

#endif // COMMON_CAMERA_CONTROLLERS_H
