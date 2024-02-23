#include "pch.h"
#include "Framework/Events/InputLayer.h"
#include "Framework/Events/MouseEvent.h"
#include "Framework/Common/Input.h"
#include "Render/Camera.h"

#include "imgui.h"

namespace Ailu
{
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
			_camera_move_speed = max(0.0f, _camera_move_speed);
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
			ImGui::SliderFloat("LerpSpeed", &_lerp_speed_multifactor, 0.0f, 1.0f, "%.2f");
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
		if (!_b_handle_input) return;
		float lerp_speed = delta_time / 1000.0f;
		static bool init = false;
		static Vector2f pre_mouse_pos;
		float move_speed = 0.1f;
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
				move_dis += Camera::sCurrent->Forward() * _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_S))
			{
				move_dis -= Camera::sCurrent->Forward() * _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_D))
			{
				move_dis += Camera::sCurrent->Right() * _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_A))
			{
				move_dis -= Camera::sCurrent->Right() * _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_E))
			{
				move_dis += Camera::sCurrent->Up() * _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_Q))
			{
				move_dis -= Camera::sCurrent->Up() * _camera_move_speed * _camera_move_speed;
			}
			target_pos += move_dis;
			static Vector2f target_rotation = { 0.f,0.f };
			target_rotation = FirstPersonCameraController::s_instance._rotation;
			auto cur_mouse_pos = Input::GetMousePos();
			if (Input::IsKeyPressed(AL_KEY_RBUTTON))
			{
				if (abs(cur_mouse_pos.x - pre_mouse_pos.x) < 100.0f && abs(cur_mouse_pos.y - pre_mouse_pos.y) < 100.0f)
				{
					target_rotation.y += (cur_mouse_pos.x - pre_mouse_pos.x) * _camera_wander_speed * _camera_wander_speed;
					target_rotation.x += (cur_mouse_pos.y - pre_mouse_pos.y) * _camera_wander_speed * _camera_wander_speed;
				}
			}
			pre_mouse_pos = cur_mouse_pos;
			FirstPersonCameraController::s_instance.InterpolateTo(target_pos,target_rotation.x, target_rotation.y, lerp_speed * _lerp_speed_multifactor);
			Camera::sCurrent->FovH(_camera_fov_h);
			Camera::sCurrent->Near(_camera_near);
			Camera::sCurrent->Far(_camera_far);
		}
		pre_tick_camera = Camera::sCurrent;
	}
}
