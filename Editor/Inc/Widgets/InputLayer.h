#pragma once
#ifndef __INPUT_LAYER_H__
#define __INPUT_LAYER_H__

#include "Inc/Framework/Events/Layer.h"
#include "Render/Camera.h"

namespace Ailu
{
	namespace Editor
	{
		class FirstPersonCameraController
		{
		public:
			FirstPersonCameraController();
			FirstPersonCameraController(Camera* camera);
			void Attach(Camera* camera);
			void SetPosition(const Vector3f& position);
			void SetRotation(float x, float y);
			void Move(const Vector3f& d);
			void MoveForward(float distance);
			void MoveRight(float distance);
			void MoveUp(float distance);
			void TurnHorizontal(float angle);
			void TurnVertical(float angle);
			void InterpolateTo(const Vector3f target_pos, float rot_x, float rot_y, float speed);
			Vector2f _rotation;
			static FirstPersonCameraController s_instance;
		private:
			Camera* _p_camera;
			Quaternion _rot_object_x;
			Quaternion _rot_world_y;
		};

		class InputLayer : public Layer
		{
		public:
			InputLayer();
			InputLayer(const String& name);
			~InputLayer();

			void OnAttach() override;
			void OnDetach() override;
			void OnEvent(Event& e) override;
			void OnImguiRender() override;
			void OnUpdate(float delta_time) override;
			void HandleInput(bool is_handle) { _b_handle_input = is_handle; }
		private:
			bool _b_handle_input;
			float _camera_move_speed = 0.9f;
			float _camera_wander_speed = 0.6f;
			float _lerp_speed_multifactor = 2.0f;
			float _camera_fov_h = 60.0f;
			float _camera_near = 1.0f;
			float _camera_far = 10000.0f;
		};
	}
}


#endif // !INPUT_LAYER_H__

