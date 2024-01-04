#include "pch.h"
#include "Render/Camera.h"

namespace Ailu
{
	Camera* Camera::GetDefaultCamera()
	{
		static Camera cam(16.0F / 9.0F);
		cam.Position(1356.43f, 604.0f, -613.45f);
		cam.Rotate(11.80f, -59.76f);
		cam.SetLens(1.57f, 16.f / 9.f, 1.f, 5000.f);
		Camera::sCurrent = &cam;
		return &cam;
	}
	//Camera::Camera(const Camera& other)
	//{
	//	_camera_type = other._camera_type;
	//	_aspect = other._aspect;
	//	_near_clip = other._near_clip;
	//	_far_clip = other._far_clip;
	//	_rotation = other._rotation;
	//	_forward = other._forward;
	//	_right = other._right;
	//	_up = other._up;
	//	_size = other._size;
	//	b_dirty_ = other.b_dirty_;
	//	_position = other._position;
	//	_view_matrix = other._view_matrix;
	//	_proj_matrix = other._proj_matrix;
	//}

	void Camera::Update()
	{
		b_dirty_ = true;
		UpdateViewMatrix();
		if (_camera_type == ECameraType::kPerspective)
			BuildPerspectiveFovLHMatrix(_proj_matrix, ToRadius(_fov_h), _aspect, _near_clip, _far_clip);
		else
		{
			float left = -_size * 0.5f;
			float right = -left;
			float top = right / _aspect;
			float bottom = -top;
			BuildOrthographicMatrix(_proj_matrix,left,right,top,bottom,_near_clip,_far_clip);
		}
	}
	void Camera::SetLens(float fovy, float aspect, float nz, float fz)
	{
		_fov_h = fovy;
		_aspect = aspect;
		_near_clip = nz;
		_far_clip = fz;
		//_near_plane_height = 2.f * nz * tanf(0.5f * _hfov);
		//_far_plane_height = 2.f * fz * tanf(0.5f * _hfov);
		BuildPerspectiveFovLHMatrix(_proj_matrix, _fov_h, _aspect, nz, fz);
	}
	void Camera::FovV(float angle)
	{
		_fov_v = angle;
		_fov_h = ToAngle(2.0f * atanf(tanf(ToRadius(_fov_v) / 2.0f) * _aspect));
		BuildPerspectiveFovLHMatrix(_proj_matrix, ToRadius(_fov_v), _aspect, _near_clip, _far_clip);
	}
	//void Camera::FovH(float angle)
	//{
	//	_fov_h = angle;
	//	_fov_v = ToAngle(2.0f * atanf(tanf(ToRadius(_fov_h) / 2.0f) / _aspect));
	//	BuildPerspectiveFovLHMatrix(_proj_matrix, ToRadius(_fov_h), _aspect, _near_clip, _far_clip);
	//}

	//float Camera::GetNearPlaneWidth() const
	//{
	//	return _aspect * _near_plane_height;
	//}
	//float Camera::GetFarPlaneWidth() const
	//{
	//	return _aspect * _far_plane_height;
	//}
	//float Camera::GetNearPlaneHeight() const
	//{
	//	return _near_plane_height;
	//}
	//float Camera::GetFarPlaneHeight() const
	//{
	//	return _far_plane_height;
	//}
	void Camera::Position(const float& x, const float& y, const float& z)
	{
		_position.x = x;
		_position.y = y;
		_position.z = z;
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::Position(const Vector3f& new_pos)
	{
		_position = new_pos;
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::MoveForward(float dis)
	{
		Vector3f dx = DotProduct(_forward, Vector3f{ dis,0.f,0.f });
		Vector3f dy = DotProduct(_forward, Vector3f{ 0.f,dis,0.f });
		Vector3f dz = DotProduct(_forward, Vector3f{ 0.f,0.f,dis });
		Vector3f len = { dx[0],dy[0],dz[0] };
		_position = len + _position;
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::MoveRight(float dis)
	{
		Vector3f dx = DotProduct(_right, Vector3f{ dis,0.f,0.f });
		Vector3f dy = DotProduct(_right, Vector3f{ 0.f,dis,0.f });
		Vector3f dz = DotProduct(_right, Vector3f{ 0.f,0.f,dis });
		Vector3f len = { dx[0],dy[0],dz[0] };
		_position = len + _position;
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::RotatePitch(float angle)
	{
		Matrix4x4f m{};
		MatrixRotationAxis(m, _right, ToRadius(angle));
		TransformCoord(_up, m);
		TransformCoord(_forward, m);
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::RotateYaw(float angle)
	{
		Matrix4x4f m{};
		MatrixRotationY(m, ToRadius(angle));
		TransformCoord(_right, m);
		TransformCoord(_up, m);
		TransformCoord(_forward, m);
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::Rotate(float vertical, float horizontal)
	{
		_rotation.x = NormalizeAngle(horizontal);
		_rotation.y = NormalizeAngle(vertical);
		Matrix4x4f m{};
		MatrixRotationY(m, ToRadius(_rotation.x));
		_right = TransformCoord(m, kRight);
		_up = TransformCoord(m, kUp);
		_forward = TransformCoord(m, kForward);

		Matrix4x4f m0{};
		MatrixRotationAxis(m0, _right, ToRadius(_rotation.y));
		TransformCoord(_up, m0);
		TransformCoord(_forward, m0);
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::UpdateViewMatrix()
	{
		if (b_dirty_)
		{
			_rotation.x = NormalizeAngle(_rotation.x);
			_rotation.y = NormalizeAngle(_rotation.y);
			Matrix4x4f m{};
			MatrixRotationY(m, ToRadius(_rotation.x));
			_right = TransformCoord(m, kRight);
			_up = TransformCoord(m, kUp);
			_forward = TransformCoord(m, kForward);

			Matrix4x4f m0{};
			MatrixRotationAxis(m0, _right, ToRadius(_rotation.y));
			TransformCoord(_up, m0);
			TransformCoord(_forward, m0);

			BuildViewMatrixLookToLH(_view_matrix, _position, _forward, _up);
			b_dirty_ = false;
		}
	}
}
