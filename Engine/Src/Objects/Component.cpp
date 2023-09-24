#include "pch.h"
#include "Objects/Component.h"

namespace Ailu
{
	Component::~Component()
	{
	}
	void Component::BeginPlay()
	{
	}
	void Component::Tick()
	{
	}
	void Component::Destroy()
	{
	}
	std::string Component::GetTypeName()
	{
		return "Component";
	}
}
