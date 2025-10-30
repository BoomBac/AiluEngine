#pragma once
#ifndef __INPUT_LAYER_H__
#define __INPUT_LAYER_H__

#include "Inc/Framework/Common/Application.h"
#include "Inc/Framework/Events/Layer.h"
#include "Render/Camera.h"

namespace Ailu
{
	namespace Editor
	{
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

