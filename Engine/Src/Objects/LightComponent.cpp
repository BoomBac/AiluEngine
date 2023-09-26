#include "pch.h"
#include "Objects/LightComponent.h"

namespace Ailu
{
	LightComponent::LightComponent()
	{
		_light;
		_intensity = 1.0f;
		_shadow = false;
	}
}
