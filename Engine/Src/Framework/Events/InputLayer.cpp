#include "pch.h"
#include "Framework/Events/InputLayer.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Common/Input.h"

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

	}
	void InputLayer::OnImguiRender()
	{
	}
	void InputLayer::OnUpdate(float delta_time)
	{
		static bool init = false;
		static Vector2f pre_mouse_pos;
		float move_speed = 0.5f;
		if (!init)
		{
			pre_mouse_pos = Input::GetMousePos();
			init = true;
		}
		if (Camera::sCurrent)
		{
			//Vector3f new_pos = Camera::sCurrent->GetPosition();
			//new_pos.x 
			if (Input::IsKeyPressed(AL_KEY_W)) _target_cam_state->z += 0.01f;
			if (Input::IsKeyPressed(AL_KEY_S)) _target_cam_state->z -= 0.01f;
			if (Input::IsKeyPressed(AL_KEY_A)) _target_cam_state->x -= 0.01f;
			if (Input::IsKeyPressed(AL_KEY_D)) _target_cam_state->x += 0.01f;
			auto cur_mouse_pos = Input::GetMousePos();
			if (Input::IsKeyPressed(AL_KEY_RBUTTON))
			{
				Camera::sCurrent->RotateYaw(cur_mouse_pos.x - pre_mouse_pos.x);
				Camera::sCurrent->RotatePitch(cur_mouse_pos.y - pre_mouse_pos.y);
				//Camera::sCurrent->Rotate(cur_mouse_pos.x - pre_mouse_pos.x, cur_mouse_pos.y - pre_mouse_pos.y);
			}
			pre_mouse_pos = cur_mouse_pos;
			_origin_cam_state->LerpTo(*_target_cam_state, delta_time / 1000.0f);
			_origin_cam_state->UpdateCamrea(*Camera::sCurrent);
		}

	}
}
