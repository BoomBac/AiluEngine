#include "Common/CameraControllers.h"

#include "Objects/Type.h"
#include "Render/Camera.h"
#include "Render/Features/MiscPasses.h"

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

    void OrbitCameraController::Attach(Render::VolumeTexturePreviewPass *pass)
    {
        _pass = pass;
        _cam_pos_prop = nullptr;
        if (_pass)
        {
            _cam_pos_prop = _pass->GetType()->FindPropertyByName("_camera_pos");
            _radius = std::max(_min_radius, Magnitude(_pass->_camera_pos));
        }
    }

    void OrbitCameraController::BeginDrag(const Vector2f &local_pos)
    {
        _last_mouse = local_pos;
        _is_dragging = true;
        if (_pass)
            _radius = std::max(_min_radius, Magnitude(_pass->_camera_pos));
    }

    void OrbitCameraController::EndDrag()
    {
        _is_dragging = false;
    }

    void OrbitCameraController::Drag(const Vector2f &local_pos)
    {
        if (!_is_dragging || !_pass || !_cam_pos_prop)
            return;
        Vector2f delta = local_pos - _last_mouse;
        _last_mouse = local_pos;

        _yaw += delta.x * _sensitivity;
        _pitch += delta.y * _sensitivity;
        _pitch = std::clamp(_pitch, -1.5f, 1.5f);
        ApplyCameraPosition();
    }

    void OrbitCameraController::Zoom(f32 scroll_delta)
    {
        if (!_pass || !_cam_pos_prop)
            return;
        f32 delta = scroll_delta > 0.0f ? -_zoom_step : _zoom_step;
        _radius = std::clamp(_radius + delta, _min_radius, _max_radius);

        f32 len = Magnitude(_pass->_camera_pos);
        if (len > 1e-6f)
        {
            auto pos = _pass->_camera_pos * (_radius / len);
            _cam_pos_prop->Set(_pass, pos, PropertyInfo::EPropertyChangeSource::kUI);
        }
    }

    void OrbitCameraController::ApplyCameraPosition()
    {
        Vector3f pos;
        f32 cp = std::cos(_pitch);
        pos.x = _radius * cp * std::sin(_yaw);
        pos.y = _radius * std::sin(_pitch);
        pos.z = _radius * cp * std::cos(_yaw);

        f32 len = Magnitude(pos);
        LOG_INFO("Orbit Camera Pos: {}, len={}", pos.ToString(), len);
        if (len > 1e-6f)
            pos *= (_radius / len);
        _cam_pos_prop->Set(_pass, pos, PropertyInfo::EPropertyChangeSource::kUI);
    }
}
