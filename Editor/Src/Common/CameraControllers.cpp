#include "Common/CameraControllers.h"

#include "Render/Camera.h"

#include <algorithm>
#include <cmath>

namespace Ailu::Editor
{
    namespace
    {
        Quaternion BuildRotationFromPitchYaw(f32 pitch, f32 yaw)
        {
            const Quaternion yaw_rotation = Quaternion::AngleAxis(yaw, Vector3f::kUp);
            const Vector3f right = yaw_rotation * Vector3f::kRight;
            const Quaternion pitch_rotation = Quaternion::AngleAxis(pitch, right);
            return yaw_rotation * pitch_rotation;
        }

        f32 ExtractRollDegrees(const Quaternion &rotation, f32 pitch, f32 yaw)
        {
            const Quaternion base_rotation = BuildRotationFromPitchYaw(pitch, yaw);
            const Quaternion local_rotation = rotation * Quaternion::Inverse(base_rotation);
            return 2.0f * std::atan2(local_rotation.z, local_rotation.w) * Math::kRadToDeg;
        }
    }

    FirstPersonCameraController::FirstPersonCameraController()
        : FirstPersonCameraController(nullptr)
    {
    }

    FirstPersonCameraController::FirstPersonCameraController(Render::Camera *camera)
    {
        Attach(camera);
    }

    void FirstPersonCameraController::Attach(Render::Camera *camera)
    {
        _p_camera = camera;
        _restore_projection_pending = false;
        if (_p_camera == nullptr)
            return;

        _current_rotation = _p_camera->Rotation();
        _target_rotation = _current_rotation;
        _target_pos = _p_camera->Position();
        UpdatePitchYaw();
        _roll_degrees = ExtractRollDegrees(_target_rotation, _rotation.x, _rotation.y);
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
        const Quaternion base_rotation = BuildRotationFromPitchYaw(x, y);
        _target_rotation = Quaternion::AngleAxis(_roll_degrees, Vector3f::kForward) * base_rotation;
    }

    void FirstPersonCameraController::SetTargetRotation(const Quaternion &rotation, bool is_force)
    {
        if (!_is_receive_input && !is_force)
            return;
        _target_rotation = rotation;
        UpdatePitchYaw();
        _roll_degrees = ExtractRollDegrees(_target_rotation, _rotation.x, _rotation.y);
    }

    void FirstPersonCameraController::UpdatePitchYaw()
    {
        const Vector3f forward = _target_rotation * Vector3f::kForward;
        _rotation.x = -std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * Math::kRadToDeg;
        _rotation.y = std::atan2(forward.x, forward.z) * Math::kRadToDeg;
    }

    // Teleport the camera and its interpolation state in one step.  Interpolate() eases the
    // camera from _rot_world_y/_rot_object_x toward _rotation every frame, so setting the
    // target alone still leaves the camera on its old attitude for dozens of frames.
    void FirstPersonCameraController::SnapTo(const Vector3f &position, f32 pitch, f32 yaw)
    {
        _roll_degrees = 0.0f;
        SnapTo(position, BuildRotationFromPitchYaw(pitch, yaw));
    }

    void FirstPersonCameraController::SnapTo(const Vector3f &position, const Quaternion &rotation)
    {
        _target_rotation = rotation;
        _current_rotation = rotation;
        _target_pos = position;
        UpdatePitchYaw();
        _roll_degrees = ExtractRollDegrees(_target_rotation, _rotation.x, _rotation.y);
        if (_p_camera != nullptr)
        {
            _p_camera->Position(position);
            _p_camera->Rotation(_current_rotation);
            _p_camera->RecalculateMatrix(true);
        }
    }

    void FirstPersonCameraController::SnapTo(const Render::Camera &camera)
    {
        if (_p_camera == nullptr)
            return;

        if (!_restore_projection_pending)
        {
            _projection_before_align = {_p_camera->Type(), _camera_fov_h, _camera_near, _camera_far,
                                        _p_camera->Size(), _p_camera->Aspect(), _p_camera->ProjectionOffset(),
                                        _p_camera->ReverseProjectionX(), _p_camera->LockAspect()};
            _restore_projection_pending = true;
        }
        const Vector3f forward = camera.Rotation() * camera.LocalForward();
        const Vector3f up = camera.Rotation() * camera.LocalUp();
        const Quaternion rotation = Quaternion::LookRotation(forward, up);
        _camera_fov_h = camera.FovH();
        _camera_near = camera.Near();
        _camera_far = camera.Far();
        _is_receive_input = camera.Type() == Render::ECameraType::kPerspective;
        _p_camera->SetLocalViewAxes(Vector3f::kForward, Vector3f::kUp);
        _p_camera->Type(camera.Type());
        _p_camera->FovH(_camera_fov_h);
        _p_camera->Near(_camera_near);
        _p_camera->Far(_camera_far);
        _p_camera->Size(camera.Size());
        _p_camera->Aspect(camera.Aspect());
        _p_camera->LockAspect(true);
        _p_camera->ProjectionOffset(camera.ProjectionOffset());
        _p_camera->ReverseProjectionX(camera.ReverseProjectionX());
        SnapTo(camera.Position(), rotation);
        _p_camera->MarkTemporalHistoryReset();
    }

    bool FirstPersonCameraController::RestoreProjection()
    {
        if (_p_camera == nullptr || !_restore_projection_pending)
            return false;

        const auto &state = _projection_before_align;
        _camera_fov_h = state._fov;
        _camera_near = state._near;
        _camera_far = state._far;
        _is_receive_input = state._type == Render::ECameraType::kPerspective;
        _p_camera->Type(state._type);
        _p_camera->FovH(_camera_fov_h);
        _p_camera->Near(_camera_near);
        _p_camera->Far(_camera_far);
        _p_camera->Size(state._size);
        _p_camera->Aspect(state._aspect);
        _p_camera->LockAspect(state._lock_aspect);
        _p_camera->ProjectionOffset(state._offset);
        _p_camera->ReverseProjectionX(state._reverse_x);
        const auto output_size = _p_camera->OutputSize();
        if (!state._lock_aspect && output_size.x > 0u && output_size.y > 0u)
            _p_camera->SetPixelSize(output_size.x, output_size.y);
        ResetRoll();
        _p_camera->MarkTemporalHistoryReset();
        _restore_projection_pending = false;
        return true;
    }

    void FirstPersonCameraController::ResetRoll()
    {
        if (_p_camera == nullptr)
            return;
        _roll_degrees = 0.0f;
        _target_rotation = BuildRotationFromPitchYaw(_rotation.x, _rotation.y);
        _current_rotation = _target_rotation;
        _p_camera->Rotation(_current_rotation);
        _p_camera->RecalculateMatrix(true);
    }

    void FirstPersonCameraController::Interpolate(float speed)
    {
        _fast_camera_move_speed = _base_camera_move_speed * 3.0f;
        Quaternion target_rotation = _target_rotation;
        if (Quaternion::Dot(_current_rotation, target_rotation) < 0.0f)
            target_rotation = Quaternion(-target_rotation.x, -target_rotation.y, -target_rotation.z, -target_rotation.w);
        _current_rotation = Quaternion::NLerp(_current_rotation, target_rotation, speed);
        _p_camera->Position(Lerp(_p_camera->Position(), _target_pos, speed));
        _p_camera->Rotation(_current_rotation);
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
