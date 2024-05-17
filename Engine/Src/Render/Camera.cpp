#include "pch.h"
#include "Render/Camera.h"
#include "Render/Gizmo.h"

namespace Ailu
{
	Camera* Camera::GetDefaultCamera()
	{
		static Camera cam(16.0F / 9.0F);
		cam.Position(Vector3f::kZero);
		cam.Rotation(Quaternion());
		cam.SetLens(1.57f, 16.f / 9.f, 1.f, 5000.f);
		Camera::sCurrent = &cam;
		return &cam;
	}

	void Camera::DrawGizmo(const Camera* p_camera)
	{
		// 绘制立方体的边框
		Gizmo::DrawLine(p_camera->_near_top_left, p_camera->_near_top_right, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_near_top_right, p_camera->_near_bottom_right, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_near_bottom_right, p_camera->_near_bottom_left, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_near_bottom_left, p_camera->_near_top_left, Colors::kWhite);

		Gizmo::DrawLine(p_camera->_far_top_left, p_camera->_far_top_right, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_far_top_right, p_camera->_far_bottom_right, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_far_bottom_right, p_camera->_far_bottom_left, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_far_bottom_left, p_camera->_far_top_left, Colors::kWhite);

		// 绘制连接立方体的线
		Gizmo::DrawLine(p_camera->_near_top_left, p_camera->_far_top_left, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_near_top_right, p_camera->_far_top_right, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_near_bottom_right, p_camera->_far_bottom_right, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_near_bottom_left, p_camera->_far_bottom_left, Colors::kWhite);
		Gizmo::DrawLine(p_camera->_near_bottom_left, p_camera->_far_bottom_left, Colors::kWhite);

		auto& vf = p_camera->_vf;
		for (int i = 0; i < 6; i++)
		{
			auto& p = vf._planes[i];
			float3 start = p_camera->Position();
			Gizmo::DrawLine(p._point, p._point + p._normal * 100,Color32(1.0f,0.0f,1.0f,1.0f));
		}
		//Gizmo::DrawLine(p_camera->Position(), p_camera->Position() + p_camera->_vf._planes[3]._normal * 200.0f, Colors::kWhite);
	}

	static Vector3f calculateCenter(Vector3f p1, Vector3f p2, Vector3f p3, Vector3f p4)
	{
		return (p1 + p2 + p3 + p4) * 0.25f;
	}

	void Camera::CalcViewFrustumPlane(const Camera* cam, ViewFrustum& vf)
	{
		// 计算各个平面的中心位置
		Vector3f nearPlaneCenter = calculateCenter(cam->_near_top_left, cam->_near_top_right, cam->_near_bottom_left, cam->_near_bottom_right);
		Vector3f farPlaneCenter = calculateCenter(cam->_far_top_left, cam->_far_top_right, cam->_far_bottom_left, cam->_far_bottom_right);
		Vector3f leftPlaneCenter = calculateCenter(cam->_near_top_left, cam->_far_top_left, cam->_near_bottom_left, cam->_far_bottom_left);
		Vector3f rightPlaneCenter = calculateCenter(cam->_near_top_right, cam->_far_top_right, cam->_near_bottom_right, cam->_far_bottom_right);
		Vector3f topPlaneCenter = calculateCenter(cam->_near_top_left, cam->_far_top_left, cam->_near_top_right, cam->_far_top_right);
		Vector3f bottomPlaneCenter = calculateCenter(cam->_near_bottom_left, cam->_far_bottom_left, cam->_near_bottom_right, cam->_far_bottom_right);
		vf._planes[0]._normal = CrossProduct(cam->_near_top_left - cam->_near_bottom_left, cam->_near_bottom_right - cam->_near_bottom_left);
		vf._planes[1]._normal = -vf._planes[0]._normal;
		vf._planes[2]._normal = CrossProduct(cam->_far_top_left - cam->_near_top_left, cam->_near_top_right - cam->_near_top_left);//top
		vf._planes[3]._normal = -CrossProduct(cam->_far_bottom_left - cam->_near_bottom_left, cam->_near_bottom_right - cam->_near_bottom_left);//bottom
		vf._planes[4]._normal = CrossProduct(cam->_far_bottom_left - cam->_near_bottom_left, cam->_near_top_left - cam->_near_bottom_left);//left
		vf._planes[5]._normal = -CrossProduct(cam->_far_bottom_right - cam->_near_bottom_right, cam->_near_top_right - cam->_near_bottom_right);//right
		vf._planes[0]._point = nearPlaneCenter;
		vf._planes[1]._point = farPlaneCenter;
		vf._planes[2]._point = topPlaneCenter;
		vf._planes[3]._point = bottomPlaneCenter;
		vf._planes[4]._point = leftPlaneCenter;
		vf._planes[5]._point = rightPlaneCenter;
		for (int i = 0; i < 6; ++i)
		{
			vf._planes[i]._normal = Normalize(vf._planes[i]._normal);
		}
		vf._planes[0]._distance = -DotProduct(vf._planes[0]._normal, cam->_near_top_left);
		vf._planes[1]._distance = -DotProduct(vf._planes[1]._normal, cam->_far_top_left);
		vf._planes[2]._distance = -DotProduct(vf._planes[2]._normal, cam->_far_top_left);
		vf._planes[3]._distance = -DotProduct(vf._planes[3]._normal, cam->_near_bottom_left);
		vf._planes[4]._distance = -DotProduct(vf._planes[4]._normal, cam->_far_bottom_left);
		vf._planes[5]._distance = -DotProduct(vf._planes[5]._normal, cam->_far_bottom_right);
	}

	Camera Camera::GetFitShaodwCamera(const Camera& eye_cam, float shadow_dis)
	{
		float len = shadow_dis / eye_cam.Far();
		Vector<Vector3f> p(8);
		p[0] = eye_cam._position + (eye_cam._far_bottom_left - eye_cam._position) * len;
		p[1] = eye_cam._position + (eye_cam._far_bottom_right - eye_cam._position) * len;
		p[2] = eye_cam._position + (eye_cam._far_top_left - eye_cam._position) * len;
		p[3] = eye_cam._position + (eye_cam._far_top_right - eye_cam._position) * len;
		p[4] = p[0] - eye_cam._forward * shadow_dis;
		p[5] = p[1] - eye_cam._forward * shadow_dis;
		p[6] = p[2] - eye_cam._forward * shadow_dis;
		p[7] = p[3] - eye_cam._forward * shadow_dis;
		auto vmax = AABB::MaxAABB(), vmin = AABB::MinAABB();
		for (int i = 0; i < 8; i++)
		{
			vmax = Max(vmax, p[i]);
			vmin = Min(vmin, p[i]);
		}
		Vector3f center = (vmax + vmin) * 0.5f;
		float dx = vmax.x - vmin.x;
		float dz = vmax.z - vmin.z;
		float extent = std::max(dx, dz) * 0.5;
		Gizmo::DrawAABB(AABB(vmin, vmax), Colors::kYellow);
		Vector3f light_dir = { 0,-0.717,0.717 };
		Gizmo::DrawLine(eye_cam._position, eye_cam._position + light_dir * shadow_dis, Colors::kRed);
		Vector3f dir_min = vmin + light_dir * shadow_dis;
		Vector3f dir_max = vmax + light_dir * shadow_dis;
		//dir_min.y = shadow_distance * 2;
		//dir_max.y = shadow_distance * 2;
		Gizmo::DrawLine(vmin, vmin + light_dir * 1000, Colors::kGreen);
		Gizmo::DrawLine(vmax, vmax + light_dir * 1000, Colors::kGreen);

		float height = Distance(vmin, vmax) * 2;
		Camera cam;
		cam.Type(ECameraType::kOrthographic);
		cam.SetLens(90, 1, 10, height * 1.5);
		cam.Size(extent * 2);
		cam.Rotation(Quaternion::AngleAxis(45, Vector3f::kRight));
		cam.Position(center + -light_dir * height);
		cam.RecalculateMarix(true);
		Camera::DrawGizmo(&cam);
		return cam;
	}

	Camera::Camera() : Camera(16.0F / 9.0F)
	{
	}

	Camera::Camera(float aspect, float near_clip, float far_clip, ECameraType::ECameraType camera_type) :
		_aspect(aspect), _near_clip(near_clip), _far_clip(far_clip), _camera_type(camera_type), _fov_h(60.0f),
		_forward(Vector3f::kForward), _right(Vector3f::kRight), _up(Vector3f::kUp)
	{
		IMPLEMENT_REFLECT_FIELD(Camera);
		MarkDirty();
		RecalculateMarix();
	}

	void Camera::RecalculateMarix(bool force)
	{
		if (!force && !_is_dirty) return;
		if (_camera_type == ECameraType::kPerspective)
			BuildPerspectiveFovLHMatrix(_proj_matrix, _fov_h * k2Radius, _aspect, _near_clip, _far_clip);
		else
		{
			float left = -_size * 0.5f;
			float right = -left;
			float top = right / _aspect;
			float bottom = -top;
			BuildOrthographicMatrix(_proj_matrix, left, right, top, bottom, _near_clip, _far_clip);
		}
		_forward = _rotation * Vector3f::kForward;
		_up = _rotation * Vector3f::kUp;
		_right = CrossProduct(_up, _forward);
		BuildViewMatrixLookToLH(_view_matrix, _position, _forward, _up);
		CalculateFrustum();
	}

	void Camera::SetLens(float fovy, float aspect, float nz, float fz)
	{
		_fov_h = fovy;
		_aspect = aspect;
		_near_clip = nz;
		_far_clip = fz;
	}

	void Camera::LookTo(const Vector3f& direction, const Vector3f& up)
	{
		_forward = direction;
		_right = CrossProduct(up, _forward);
		_up = CrossProduct(_forward, _right);
		_forward = Normalize(_forward);
		_right = Normalize(_right);
		_up = Normalize(_up);
		if (_camera_type == ECameraType::kPerspective)
			BuildPerspectiveFovLHMatrix(_proj_matrix, _fov_h * k2Radius, _aspect, _near_clip, _far_clip);
		else
		{
			float left = -_size * 0.5f;
			float right = -left;
			float top = right / _aspect;
			float bottom = -top;
			BuildOrthographicMatrix(_proj_matrix, left, right, top, bottom, _near_clip, _far_clip);
		}
		BuildViewMatrixLookToLH(_view_matrix, _position, _forward, _up);
		CalculateFrustum();
	}

	bool Camera::IsInFrustum(const Vector3f& pos)
	{
		return true;
	}

	void Camera::CalculateFrustum()
	{
		float half_width{ 0.f }, half_height{ 0.f };
		if (_camera_type == ECameraType::kPerspective)
		{
			float tanHalfFov = tan(ToRadius(_fov_h) * 0.5f);
			half_height = _near_clip * tanHalfFov;
			half_width = half_height * _aspect;
		}
		else
		{
			half_width = _size * 0.5f;
			half_height = half_width / _aspect;
		}
		Vector3f near_top_left(-half_width, half_height, _near_clip);
		Vector3f near_top_right(half_width, half_height, _near_clip);
		Vector3f near_bottom_left(-half_width, -half_height, _near_clip);
		Vector3f near_bottom_right(half_width, -half_height, _near_clip);
		Vector3f far_top_left, far_top_right, far_bottom_left, far_bottom_right;
		if (_camera_type == ECameraType::kPerspective)
		{
			far_top_left = near_top_left * (_far_clip / _near_clip);
			far_top_right = near_top_right * (_far_clip / _near_clip);
			far_bottom_left = near_bottom_left * (_far_clip / _near_clip);
			far_bottom_right = near_bottom_right * (_far_clip / _near_clip);
		}
		else
		{
			far_top_left = near_top_left;
			far_top_right = near_top_right;
			far_bottom_left = near_bottom_left;
			far_bottom_right = near_bottom_right;
			float distance = _far_clip - _near_clip;
			far_top_left.z += distance;
			far_top_right.z += distance;
			far_bottom_left.z += distance;
			far_bottom_right.z += distance;
		}
		Matrix4x4f camera_to_world = _view_matrix;
		MatrixInverse(camera_to_world);
		TransformCoord(near_top_left, camera_to_world);
		TransformCoord(near_top_right, camera_to_world);
		TransformCoord(near_bottom_left, camera_to_world);
		TransformCoord(near_bottom_right, camera_to_world);
		TransformCoord(far_top_left, camera_to_world);
		TransformCoord(far_top_right, camera_to_world);
		TransformCoord(far_bottom_left, camera_to_world);
		TransformCoord(far_bottom_right, camera_to_world);
		_near_bottom_left = near_bottom_left;
		_near_bottom_right = near_bottom_right;
		_near_top_left = near_top_left;
		_near_top_right = near_top_right;
		_far_bottom_left = far_bottom_left;
		_far_bottom_right = far_bottom_right;
		_far_top_left = far_top_left;
		_far_top_right = far_top_right;
		CalcViewFrustumPlane(this, _vf);
	}

	//---------------------------------------------------------FirstPersonCameraController--------------------------------------------------------
	FirstPersonCameraController FirstPersonCameraController::s_instance;

	FirstPersonCameraController::FirstPersonCameraController() : FirstPersonCameraController(nullptr)
	{

	}

	FirstPersonCameraController::FirstPersonCameraController(Camera* camera) : _p_camera(camera)
	{

	}
	void FirstPersonCameraController::Attach(Camera* camera)
	{
		_p_camera = camera;
		_rot_world_y = Quaternion::AngleAxis(_rotation.y, Vector3f::kUp);
		Vector3f new_camera_right = _rot_world_y * Vector3f::kRight;
		_rot_object_x = Quaternion::AngleAxis(_rotation.x, new_camera_right);
	}
	void FirstPersonCameraController::SetPosition(const Vector3f& position)
	{
		_p_camera->Position(position);
	}
	void FirstPersonCameraController::SetRotation(float x, float y)
	{
		_rotation.x = x;
		_rotation.y = y;
	}
	void FirstPersonCameraController::MoveForward(float distance)
	{
		_p_camera->Position(_p_camera->Position() + _p_camera->Forward() * distance);
	}
	void FirstPersonCameraController::MoveRight(float distance)
	{
		_p_camera->Position(_p_camera->Position() + _p_camera->Right() * distance);
	}
	void FirstPersonCameraController::MoveUp(float distance)
	{
		_p_camera->Position(_p_camera->Position() + _p_camera->Up() * distance);
	}
	void FirstPersonCameraController::TurnHorizontal(float angle)
	{
		_rotation.y = angle;
		_rot_world_y = Quaternion::AngleAxis(angle, Vector3f::kUp);
	}
	void FirstPersonCameraController::TurnVertical(float angle)
	{
		_rotation.x = angle;
		Vector3f camera_right = _rot_world_y * Vector3f::kRight;
		_rot_object_x = Quaternion::AngleAxis(angle, camera_right);
	}
	void FirstPersonCameraController::InterpolateTo(const Vector3f target_pos, float rot_x, float rot_y, float speed)
	{
		_rotation.x = rot_x;
		_rotation.y = rot_y;
		Quaternion new_quat_y = Quaternion::AngleAxis(rot_y, Vector3f::kUp);
		Vector3f new_camera_right = new_quat_y * Vector3f::kRight;
		auto new_quat_x = Quaternion::AngleAxis(rot_x, new_camera_right);
		_rot_object_x = Quaternion::NLerp(_rot_object_x, new_quat_x, speed);
		_rot_world_y = Quaternion::NLerp(_rot_world_y, new_quat_y, speed);
		_p_camera->Position(lerp(_p_camera->Position(), target_pos, speed));
		auto r = _rot_world_y * _rot_object_x;
		_p_camera->Rotation(r);
		_p_camera->RecalculateMarix(true);
	}

	void FirstPersonCameraController::Move(const Vector3f& d)
	{
		_p_camera->Position(d);
	}
	//---------------------------------------------------------FirstPersonCameraController--------------------------------------------------------
}
