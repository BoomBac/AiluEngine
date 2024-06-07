#include "Widgets/InputLayer.h"
#include "Framework/Events/MouseEvent.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/KeyEvent.h"
#include "Render/Camera.h"
#include "Ext/imgui/imgui.h"


namespace Ailu
{
	namespace Editor
	{
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

		static Vector3f target_pos;
		InputLayer::InputLayer() : Layer("InputLayer")
		{
			_b_handle_input = true;
			if (Camera::sCurrent == nullptr)
			{
				LOG_WARNING("Current Camera is null");
				Camera::GetDefaultCamera();
			}
			_camera_near = Camera::sCurrent->Near();
			_camera_far = Camera::sCurrent->Far();
			FirstPersonCameraController::s_instance.Attach(Camera::sCurrent);
			target_pos = Camera::sCurrent->Position();
		}
		InputLayer::InputLayer(const String& name) : InputLayer()
		{
			_debug_name = name;
		}
		InputLayer::~InputLayer()
		{

		}
		void InputLayer::OnAttach()
		{
			_b_handle_input = true;
		}
		void InputLayer::OnDetach()
		{
			_b_handle_input = false;
		}
		void InputLayer::OnEvent(Event& e)
		{
			EventDispather dispater1(e);
			dispater1.Dispatch <MouseButtonReleasedEvent>([this](MouseButtonReleasedEvent& e)->bool {
				if (e.GetButton() == 0)
				{
					while (::ShowCursor(TRUE) < 0);
					return true;
				}
				return false;
				});
			EventDispather dispater(e);
			dispater.Dispatch<MouseScrollEvent>([this](MouseScrollEvent& e)->bool {
				_camera_move_speed += e.GetOffsetY() > 0 ? 0.01f : -0.01f;
				_camera_move_speed = std::ranges::max(0.0f, _camera_move_speed);
				return true;
				});
			EventDispather dispater0(e);
			dispater0.Dispatch <MouseButtonPressedEvent>([this](MouseButtonPressedEvent& e)->bool {
				if (e.GetButton() == AL_KEY_RBUTTON)
				{
					while (::ShowCursor(FALSE) >= 0); // 隐藏鼠标指针，直到它不再可见
					return true;
				}
				return true;
				});
			//dispater0.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& e)->bool {
			//	if (!e.Handled())
			//	{
			//		Input::s_key_pressed_map[e.GetKeyCode()] = true;
			//		return true;
			//	}
			//	});
			//dispater0.Dispatch<KeyReleasedEvent>([this](KeyReleasedEvent& e)->bool {
			//	if (!e.Handled())
			//	{
			//		Input::s_key_pressed_map[e.GetKeyCode()] = false;
			//		return true;
			//	}
			//	});
		}

		void InputLayer::OnImguiRender()
		{
			if (Camera::sCurrent)
			{
				auto camera_pos = Camera::sCurrent->Position();
				auto camera_rotation = FirstPersonCameraController::s_instance._rotation;
				_camera_near = Camera::sCurrent->Near();
				_camera_far = Camera::sCurrent->Far();
				ImGui::Begin("CameraDetail");
				ImGui::SliderFloat("MoveSpeed", &_camera_move_speed, 0.00f, 10.0f, "%.2f");
				ImGui::SliderFloat("WanderSpeed", &_camera_wander_speed, 0.00f, 2.0f, "%.2f");
				ImGui::SliderFloat("LerpSpeed", &_lerp_speed_multifactor, 0.0f, 100.0f, "%.2f");
				ImGui::SliderFloat("FovH", &_camera_fov_h, 0.0f, 120.0f, "%.2f");
				ImGui::SliderFloat("NearClip", &_camera_near, 0.00001f, 1000.0f, "%.2f");
				ImGui::SliderFloat("FarClip", &_camera_far, 5000.0f, 200000.0f, "%.2f");
				ImGui::Text("Position:");
				ImGui::SameLine();
				ImGui::Text(camera_pos.ToString().c_str());
				ImGui::Text("Rotation:");
				ImGui::SameLine();
				ImGui::Text(camera_rotation.ToString().c_str());
				ImGui::End();
			}
		}
		void InputLayer::OnUpdate(float delta_time)
		{
			Camera::sCurrent->FovH(_camera_fov_h);
			Camera::sCurrent->Near(_camera_near);
			Camera::sCurrent->Far(_camera_far);
			if (Input::s_block_input) return;
			if (!_b_handle_input) return;
			static bool init = false;
			static Vector2f pre_mouse_pos;
			static const f32 move_distance = 100.0f;// 1 m
			f32 final_move_distance = move_distance * _camera_move_speed * _camera_move_speed;
			if (!init)
			{
				pre_mouse_pos = Input::GetMousePos();
				init = true;
			}
			static Camera* pre_tick_camera = Camera::sCurrent;
			if (Camera::sCurrent)
			{
				Vector3f move_dis{ 0,0,0 };
				if (Input::IsKeyPressed(AL_KEY_W))
				{
					move_dis += Camera::sCurrent->Forward();
				}
				if (Input::IsKeyPressed(AL_KEY_S))
				{
					move_dis -= Camera::sCurrent->Forward();
				}
				if (Input::IsKeyPressed(AL_KEY_D))
				{
					move_dis += Camera::sCurrent->Right();
				}
				if (Input::IsKeyPressed(AL_KEY_A))
				{
					move_dis -= Camera::sCurrent->Right();
				}
				if (Input::IsKeyPressed(AL_KEY_E))
				{
					move_dis += Camera::sCurrent->Up();
				}
				if (Input::IsKeyPressed(AL_KEY_Q))
				{
					move_dis -= Camera::sCurrent->Up();
				}
				target_pos += move_dis * final_move_distance;
				static Vector2f target_rotation = { 0.f,0.f };
				target_rotation = FirstPersonCameraController::s_instance._rotation;
				auto cur_mouse_pos = Input::GetMousePos();
				if (Input::IsKeyPressed(AL_KEY_RBUTTON))
				{
					if (abs(cur_mouse_pos.x - pre_mouse_pos.x) < 100.0f && abs(cur_mouse_pos.y - pre_mouse_pos.y) < 100.0f)
					{
						float angle_offset = _camera_wander_speed * _camera_wander_speed;
						target_rotation.y += (cur_mouse_pos.x - pre_mouse_pos.x) * angle_offset;
						target_rotation.x += (cur_mouse_pos.y - pre_mouse_pos.y) * angle_offset;
					}
				}
				pre_mouse_pos = cur_mouse_pos;
				FirstPersonCameraController::s_instance.InterpolateTo(target_pos, target_rotation.x, target_rotation.y, delta_time * _lerp_speed_multifactor);
			}
			pre_tick_camera = Camera::sCurrent;
		}
	}
}
