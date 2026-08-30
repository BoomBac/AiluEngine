#include "Common/CameraControllers.h"

#include "Render/Camera.h"

#include <algorithm>
#include <cmath>

namespace Ailu::Editor
{
    FirstPersonCameraController::FirstPersonCameraController()
        : FirstPersonCameraController(nullptr)
    {
    }

    FirstPersonCameraController::FirstPersonCameraController(Render::Camera *camera)
        : _p_camera(camera)
    {
    }

    void FirstPersonCameraController::Attach(Render::Camera *camera)
    {
        _p_camera = camera;
        _rot_world_y = Quaternion::AngleAxis(_rotation.y, Vector3f::kUp);
        Vector3f new_camera_right = _rot_world_y * Vector3f::kRight;
        _rot_object_x = Quaternion::AngleAxis(_rotation.x, new_camera_right);
        _target_pos = camera->Position();
    }

    void FirstPersonCameraController::Move(const Vector3f &d)
    {
        if (!_is_receive_input)
            return;
        _p_camera->Position(d);
    }

    void FirstPersonCameraController::SetTargetRotation(f32 x, f32 y, bool is_force)
    {
        if (!_is_receive_input && !is_force)
            return;
        _rotation.x = x;
        _rotation.y = y;
    }

    void FirstPersonCameraController::Interpolate(float speed)
    {
        _fast_camera_move_speed = _base_camera_move_speed * 3.0f;
        Quaternion new_quat_y = Quaternion::AngleAxis(_rotation.y, Vector3f::kUp);
        Vector3f new_camera_right = new_quat_y * Vector3f::kRight;
        auto new_quat_x = Quaternion::AngleAxis(_rotation.x, new_camera_right);
        _rot_object_x = Quaternion::NLerp(_rot_object_x, new_quat_x, speed);
        _rot_world_y = Quaternion::NLerp(_rot_world_y, new_quat_y, speed);
        _p_camera->Position(Lerp(_p_camera->Position(), _target_pos, speed));
        auto r = _rot_world_y * _rot_object_x;
        _p_camera->Rotation(r);
        _p_camera->RecalculateMatrix(true);
    }

    void FirstPersonCameraController::SetTargetPosition(const Vector3f &position, bool is_force)
    {
        if (!_is_receive_input && !is_force)
            return;
        _target_pos = position;
    }

    void OrbitCameraController::SetOrbit(const Vector3f &target, f32 yaw, f32 pitch, f32 distance)
    {
        _target = target;
        _yaw = yaw;
        _pitch = std::clamp(pitch, -1.5f, 1.5f);
        _radius = distance;
        ClampDistance();
    }

    void OrbitCameraController::SetCameraPosition(const Vector3f &position)
    {
        const Vector3f offset = position - _target;
        const f32 distance = Magnitude(offset);
        if (distance <= Math::kFloatEpsilon)
            return;
        _radius = distance;
        _yaw = std::atan2(offset.x, offset.z);
        _pitch = std::clamp(std::asin(std::clamp(offset.y / distance, -1.0f, 1.0f)), -1.5f, 1.5f);
        ClampDistance();
    }

    void OrbitCameraController::SetDistanceLimits(f32 min_distance, f32 max_distance)
    {
        _min_radius = std::max(min_distance, 0.001f);
        _max_radius = std::max(max_distance, _min_radius);
        ClampDistance();
    }

    Vector3f OrbitCameraController::GetCameraOffset() const
    {
        const f32 cos_pitch = std::cos(_pitch);
        return Vector3f{_radius * cos_pitch * std::sin(_yaw), _radius * std::sin(_pitch),
                        _radius * cos_pitch * std::cos(_yaw)};
    }

    Vector3f OrbitCameraController::GetCameraPosition() const
    {
        return _target + GetCameraOffset();
    }

    Vector3f OrbitCameraController::GetCameraForward() const
    {
        return Normalize(_target - GetCameraPosition());
    }

    Vector3f OrbitCameraController::GetCameraRight() const
    {
        Vector3f world_up = Vector3f::kUp;
        const Vector3f forward = GetCameraForward();
        if (std::abs(DotProduct(forward, world_up)) > 0.999f)
            world_up = Vector3f::kForward;
        return Normalize(CrossProduct(world_up, forward));
    }

    Vector3f OrbitCameraController::GetCameraUp() const
    {
        return CrossProduct(GetCameraForward(), GetCameraRight());
    }

    void OrbitCameraController::BeginOrbit(const Vector2f &local_pos)
    {
        _last_orbit_mouse = local_pos;
        _is_orbiting = true;
    }

    void OrbitCameraController::EndOrbit()
    {
        _is_orbiting = false;
    }

    void OrbitCameraController::Orbit(const Vector2f &local_pos)
    {
        if (!_is_orbiting)
            return;
        const Vector2f delta = local_pos - _last_orbit_mouse;
        _last_orbit_mouse = local_pos;
        _yaw += delta.x * _sensitivity;
        _pitch = std::clamp(_pitch + delta.y * _sensitivity, -1.5f, 1.5f);
    }

    void OrbitCameraController::BeginPan(const Vector2f &local_pos)
    {
        _last_pan_mouse = local_pos;
        _is_panning = true;
    }

    void OrbitCameraController::EndPan()
    {
        _is_panning = false;
    }

    void OrbitCameraController::Pan(const Vector2f &local_pos, const Vector2f &view_size, f32 vertical_fov)
    {
        if (!_is_panning || view_size.y <= 1.0f)
            return;
        const Vector2f delta = local_pos - _last_pan_mouse;
        _last_pan_mouse = local_pos;
        const f32 world_per_pixel = 2.0f * _radius * std::tan(vertical_fov * 0.5f) / view_size.y;
        _target -= GetCameraRight() * (delta.x * world_per_pixel);
        _target += GetCameraUp() * (delta.y * world_per_pixel);
    }

    void OrbitCameraController::Zoom(f32 scroll_delta)
    {
        if (std::abs(scroll_delta) <= Math::kFloatEpsilon)
            return;
        const f32 steps = scroll_delta / 120.0f;
        _radius *= std::pow(_zoom_factor, steps);
        ClampDistance();
    }

    void OrbitCameraController::ClampDistance()
    {
        _radius = std::clamp(_radius, _min_radius, _max_radius);
    }

    void CanvasCameraController::Attach(Render::Camera *camera)
    {
        _p_camera = camera;
        if (_p_camera != nullptr)
        {
            _direction = _p_camera->Forward();
            _up = _p_camera->Up();
        }
    }

    void CanvasCameraController::SetViewBasis(const Vector3f &direction, const Vector3f &up)
    {
        _direction = direction;
        _up = up;
        ApplyView();
    }

    void CanvasCameraController::BeginDrag(const Vector2f &local_pos)
    {
        _last_mouse = local_pos;
        _is_dragging = true;
    }

    void CanvasCameraController::EndDrag()
    {
        _is_dragging = false;
    }

    void CanvasCameraController::Drag(const Vector2f &local_pos, const Vector2f &view_size)
    {
        if (!_is_dragging || _p_camera == nullptr || view_size.y <= 1.0f)
            return;

        Vector2f delta = local_pos - _last_mouse;
        _last_mouse = local_pos;
        f32 world_per_pixel = _p_camera->Size() / view_size.y;
        Vector3f position = _p_camera->Position();
        position -= _p_camera->Right() * (delta.x * world_per_pixel);
        position += _p_camera->Up() * (delta.y * world_per_pixel);
        _p_camera->Position(position);
        ApplyView();
    }

    void CanvasCameraController::Zoom(f32 scroll_delta)
    {
        if (_p_camera == nullptr)
            return;

        f32 size = _p_camera->Size();
        size *= scroll_delta > 0.0f ? _zoom_factor : 1.0f / _zoom_factor;
        _p_camera->Size(std::clamp(size, _min_size, _max_size));
        ApplyView();
    }

    void CanvasCameraController::ApplyView()
    {
        if (_p_camera == nullptr)
            return;
        _p_camera->Rotation(Quaternion::LookRotation(_direction, _up));
        _p_camera->RecalculateMatrix(true);
    }
}
