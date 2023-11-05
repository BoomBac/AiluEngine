#include "pch.h"
#include "Render/Camera.h"

namespace Ailu
{
	Camera* Camera::GetDefaultCamera()
	{
		static Camera cam(16.0F / 9.0F);
		cam.SetPosition(1356.43f, 604.0f, -613.45f);
		cam.Rotate(11.80f, -59.76f);
		cam.SetLens(1.57f, 16.f / 9.f, 1.f, 5000.f);
		Camera::sCurrent = &cam;
		return &cam;
	}
	void Camera::SetLens(float fovy, float aspect, float nz, float fz)
	{
		_hfov = fovy;
		_aspect = aspect;
		_near_clip = nz;
		_far_clip = fz;
		_near_plane_height = 2.f * nz * tanf(0.5f * _hfov);
		_far_plane_height = 2.f * fz * tanf(0.5f * _hfov);
		BuildPerspectiveFovLHMatrix(_proj_matrix, _hfov, _aspect, nz, fz);
	}
	void Camera::SetFovV(float angle)
	{
		_vfov = angle;
		_hfov = ToAngle(2.0f * atanf(tanf(ToRadius(_vfov) / 2.0f) * _aspect));
		BuildPerspectiveFovLHMatrix(_proj_matrix, ToRadius(_hfov), _aspect, _near_clip, _far_clip);
	}
	void Camera::SetFovH(float angle)
	{
		_hfov = angle;
		_vfov = ToAngle(2.0f * atanf(tanf(ToRadius(_hfov) / 2.0f) / _aspect));
		BuildPerspectiveFovLHMatrix(_proj_matrix, ToRadius(_hfov), _aspect, _near_clip, _far_clip);
	}

	float Camera::GetNearPlaneWidth() const
	{
		return _aspect * _near_plane_height;
	}
	float Camera::GetFarPlaneWidth() const
	{
		return _aspect * _far_plane_height;
	}
	float Camera::GetNearPlaneHeight() const
	{
		return _near_plane_height;
	}
	float Camera::GetFarPlaneHeight() const
	{
		return _far_plane_height;
	}
	void Camera::SetPosition(const float& x, const float& y, const float& z)
	{
		position_.x = x;
		position_.y = y;
		position_.z = z;
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::SetPosition(const Vector3f& new_pos)
	{
		position_ = new_pos;
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::MoveForward(float dis)
	{
		Vector3f dx = DotProduct(forawrd_, Vector3f{ dis,0.f,0.f });
		Vector3f dy = DotProduct(forawrd_, Vector3f{ 0.f,dis,0.f });
		Vector3f dz = DotProduct(forawrd_, Vector3f{ 0.f,0.f,dis });
		Vector3f len = { dx[0],dy[0],dz[0] };
		position_ = len + position_;
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::MoveRight(float dis)
	{
		Vector3f dx = DotProduct(right_, Vector3f{ dis,0.f,0.f });
		Vector3f dy = DotProduct(right_, Vector3f{ 0.f,dis,0.f });
		Vector3f dz = DotProduct(right_, Vector3f{ 0.f,0.f,dis });
		Vector3f len = { dx[0],dy[0],dz[0] };
		position_ = len + position_;
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::RotatePitch(float angle)
	{
		Matrix4x4f m{};
		MatrixRotationAxis(m, right_, ToRadius(angle));
		TransformCoord(up_, m);
		TransformCoord(forawrd_, m);
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::RotateYaw(float angle)
	{
		Matrix4x4f m{};
		MatrixRotationY(m, ToRadius(angle));
		TransformCoord(right_, m);
		TransformCoord(up_, m);
		TransformCoord(forawrd_, m);
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::Rotate(float vertical, float horizontal)
	{
		_rotation.x = NormalizeAngle(horizontal);
		_rotation.y = NormalizeAngle(vertical);
		Matrix4x4f m{};
		MatrixRotationY(m, ToRadius(_rotation.x));
		right_ = TransformCoord(kRight, m);
		up_ = TransformCoord(kUp, m);
		forawrd_= TransformCoord(kForward, m);

		Matrix4x4f m0{};
		MatrixRotationAxis(m0, right_, ToRadius(_rotation.y));
		TransformCoord(up_, m0);
		TransformCoord(forawrd_, m0);
		b_dirty_ = true;
		UpdateViewMatrix();
	}
	void Camera::UpdateViewMatrix()
	{
		if (b_dirty_)
		{
			BuildViewMatrixLookToLH(_view_matrix, position_, forawrd_, up_);
			b_dirty_ = false;
		}
	}
}
