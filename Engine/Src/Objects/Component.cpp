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
	void Component::Serialize(std::ofstream& file, String indent)
	{
		file << indent << GetTypeName() << ": " << std::endl;
	}
	void Component::Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent)
	{
		os << indent << GetTypeName() << ": " << std::endl;
	}
}
