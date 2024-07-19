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
			static FirstPersonCameraController s_inst;
			FirstPersonCameraController();
			explicit FirstPersonCameraController(Camera* camera);
			void Attach(Camera* camera);
            void SetTargetPosition(const Vector3f& position,bool is_force = false);
            void SetTargetRotation(f32 x,f32 y);
			void Move(const Vector3f& d);
			void Interpolate(float speed);
			Vector2f _rotation;
            Vector3f _target_pos;
            bool  _is_receive_input = true;
            float _camera_move_speed = 0.6f;
            float _camera_wander_speed = 0.6f;
            float _lerp_speed_multifactor = 0.25f;
            float _camera_fov_h = 60.0f;
            float _camera_near = 1.0f;
            float _camera_far = 10000.0f;
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
		private:
		};
	}
}


#endif // !INPUT_LAYER_H__

