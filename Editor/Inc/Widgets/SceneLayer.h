#pragma once
#ifndef __SCENE_LAYER_H__
#define __SCENE_LAYER_H__

#include "Inc/Framework/Events/Layer.h"
#include "Inc/Framework/Events/Event.h"
#include "Render/Camera.h"

namespace Ailu
{
	namespace Editor
	{
		class SceneLayer : public Layer
		{
		public:
			SceneLayer();
			SceneLayer(const String& name);
			~SceneLayer();

			//	void OnAttach() override;
				//void OnDetach() override;
			void OnEvent(Event& e) final;
			//		void OnImguiRender() override;
					//void OnUpdate(float delta_time) override;
		private:
		};
	}
}


#endif // !__SCENE_LAYER_H__

