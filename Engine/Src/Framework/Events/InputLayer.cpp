#include "pch.h"
#include "Framework/Events/InputLayer.h"
#include "Framework/Events/MouseEvent.h"
#include "Framework/Common/Input.h"

#include "imgui.h"

namespace Ailu
{
	InputLayer::InputLayer()
	{

	}
	InputLayer::~InputLayer()
	{

	}
	void InputLayer::OnAttach()
	{
		AL_ASSERT(Camera::sCurrent == nullptr, "Current Camera is null")
		_origin_cam_state = new CameraState(*Camera::sCurrent);
		_target_cam_state = new CameraState(*Camera::sCurrent);
	}
	void InputLayer::OnDetach()
	{
		DESTORY_PTR(_origin_cam_state)
		DESTORY_PTR(_target_cam_state)
	}
	void InputLayer::OnEvent(Event& e)
	{
		EventDispather dispater(e);
		dispater.Dispatch<MouseScrollEvent>([this](MouseScrollEvent& e)->bool {
			_camera_move_speed += e.GetOffsetY() > 0 ? 0.01f : -0.01f;
			_camera_move_speed = max(0.0f, _camera_move_speed);
			return true;
		});
	}

	void InputLayer::OnImguiRender()
	{
		auto camera_pos = Camera::sCurrent->GetPosition();
		auto camera_rotation = Camera::sCurrent->GetRotation();
		ImGui::Begin("CameraDetail");
		ImGui::SliderFloat("MoveSpeed", &_camera_move_speed, 0.00f, 1.0f,"%.2f");
		ImGui::SliderFloat("WanderSpeed", &_camera_wander_speed, 0.00f, 2.0f,"%.2f");
		ImGui::SliderFloat("LerpSpeed", &_lerp_speed_multifactor, 0.0f, 1.0f,"%.2f");
		ImGui::SliderFloat("FovH", &_camera_fov_h, 0.0f, 120.0f,"%.2f");
		ImGui::Text("Position:");
		ImGui::SameLine();
		ImGui::Text(camera_pos.ToString().c_str());
		ImGui::Text("Rotation:");
		ImGui::SameLine();
		ImGui::Text(camera_rotation.ToString().c_str());
		ImGui::End();
	}
	void InputLayer::OnUpdate(float delta_time)
	{
		float lerp_speed = delta_time / 1000.0f;
		static bool init = false;
		static Vector2f pre_mouse_pos;
		float move_speed = 0.1f;
		if (!init)
		{
			pre_mouse_pos = Input::GetMousePos();
			init = true;
		}
		if (Camera::sCurrent)
		{
			Vector3f move_dis{0,0,0};
			if (Input::IsKeyPressed(AL_KEY_W))
			{
				move_dis += Camera::sCurrent->GetForward()* _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_S))
			{
				move_dis -= Camera::sCurrent->GetForward() * _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_D))
			{
				move_dis += Camera::sCurrent->GetRight() * _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_A))
			{
				move_dis -= Camera::sCurrent->GetRight() * _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_E))
			{
				move_dis += Camera::sCurrent->GetUp() * _camera_move_speed * _camera_move_speed;
			}
			if (Input::IsKeyPressed(AL_KEY_Q))
			{
				move_dis -= Camera::sCurrent->GetUp() * _camera_move_speed * _camera_move_speed;
			}
			_target_cam_state->position += move_dis;
			auto cur_mouse_pos = Input::GetMousePos();
			if (Input::IsKeyPressed(AL_KEY_RBUTTON))
			{
				_target_cam_state->rotation.x += (cur_mouse_pos.x - pre_mouse_pos.x) * _camera_wander_speed * _camera_wander_speed;
				_target_cam_state->rotation.y += (cur_mouse_pos.y - pre_mouse_pos.y) * _camera_wander_speed * _camera_wander_speed;
			}
			pre_mouse_pos = cur_mouse_pos;
			_origin_cam_state->LerpTo(*_target_cam_state, lerp_speed * _lerp_speed_multifactor);
			_origin_cam_state->UpdateCamrea(*Camera::sCurrent);
			Camera::sCurrent->SetFovH(_camera_fov_h);
		}

	}
}
