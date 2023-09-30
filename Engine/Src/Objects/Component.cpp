#include "pch.h"
#include "Objects/Component.h"

namespace Ailu
{
	Component::Component()
	{
		_b_enable = true;
	}
	Component::~Component()
	{
	}
	void Component::BeginPlay()
	{
	}
	void Component::Tick(const float& delta_time)
	{
	}

	void Component::Destroy()
	{
	}
	void Component::SetOwner(Actor* onwer)
	{
		_p_onwer = onwer;
	}
	std::string Component::GetTypeName()
	{
		return "Component";
	}
}
