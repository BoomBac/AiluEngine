#pragma once
#ifndef __COMMON_ACTOR_H__
#define __COMMON_ACTOR_H__
#include "SceneActor.h"
#include "LightComponent.h"

namespace Ailu
{
	class LightActor : public SceneActor
	{
	public:
		LightActor()
		{		
			_p_light_comp = AddComponent<LightComponent>();
		}
		void Tick() final
		{
			_p_light_comp->_light._light_pos = _p_transform->Position();
			auto rot = _p_transform->Rotation();
			//MatrixRotationX(rot.x) * MatrixRotationY(rot.y)
		}
	private:
		LightComponent* _p_light_comp;
	};
}


#endif // !COMMON_ACTOR_H__
