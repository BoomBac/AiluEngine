#pragma once
#ifndef __INPUT_LAYER_H__
#define __INPUT_LAYER_H__

#include "Layer.h"

namespace Ailu
{
	class AILU_API InputLayer : public Layer
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
		bool _b_handle_input;
		float _camera_move_speed = 0.4f;
		float _camera_wander_speed = 0.6f;
		float _lerp_speed_multifactor = 0.1f;
		float _camera_fov_h = 60.0f;
		float _camera_near = 1.0f;
		float _camera_far = 10000.0f;
	};
}


#endif // !INPUT_LAYER_H__

