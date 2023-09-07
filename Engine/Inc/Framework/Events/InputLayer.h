#pragma once
#ifndef __INPUT_LAYER_H__
#define __INPUT_LAYER_H__

#include "Layer.h"
#include "Render/Camera.h"

namespace Ailu
{
	class AILU_API InputLayer : public Layer
	{
	public:
		InputLayer();
		~InputLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event& e) override;
		void OnImguiRender() override;
		void OnUpdate(float delta_time) override;
	private:
		CameraState* _origin_cam_state;
		CameraState* _target_cam_state;
	};
}


#endif // !INPUT_LAYER_H__

