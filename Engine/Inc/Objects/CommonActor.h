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
	private:
		LightComponent* _p_light_comp;
	};
}


#endif // !COMMON_ACTOR_H__
