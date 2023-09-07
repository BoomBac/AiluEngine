#include "pch.h"
#include "Render/Camera.h"

namespace Ailu
{
	void Camera::SetLens(float fovy, float aspect, float nz, float fz)
	{
		_fov = fovy;
		_aspect = aspect;
		_near_clip = nz;
		_far_clip = fz;
		_near_plane_height = 2.f * nz * tanf(0.5f * _fov);
		_near_plane_height = 2.f * fz * tanf(0.5f * _fov);
		BuildPerspectiveFovLHMatrix(_proj_matrix, _fov, _aspect, nz, fz);
	}
	float Camera::GetFovX() const
	{
		float half = 0.5f * GetNearPlaneWidth();
		return 2.f * atan(half / _near_clip);
	}
	float Camera::GetFovY() const
	{
		return 0.0f;
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
		Matrix4x4f m{};
		MatrixRotationAxis(m, right_, ToRadius(vertical));
		MatrixRotationY(m, ToRadius(horizontal));
		TransformCoord(right_, m);
		TransformCoord(up_, m);
		TransformCoord(forawrd_, m);
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
