#pragma once
#ifndef __SCENE_LAYER_H__
#define __SCENE_LAYER_H__

#include "Layer.h"
#include "Render/Camera.h"
#include "Event.h"

namespace Ailu
{
	class AILU_API SceneLayer : public Layer
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


#endif // !__SCENE_LAYER_H__

