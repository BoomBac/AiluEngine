#include "pch.h"
#include "Objects/Component.h"
#include "Objects/StaticMeshComponent.h"

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
	void Component::OnGizmo()
	{
		
	}
	void Component::Destroy()
	{
	}
	void Component::SetOwner(Actor* onwer)
	{
		_p_onwer = onwer;
	}
	EComponentType Component::GetType()
	{
		return EComponentType::kDefault;
	}
	void Component::Serialize(std::ostream& os, String indent)
	{
		os << indent << GetTypeName(GetType()) << ": " << std::endl;
	}
	Component* Component::Create(String& type_name)
	{
		if(type_name == GetTypeName(StaticMeshComponent::GetStaticType())) return new StaticMeshComponent();
		else if(type_name == GetTypeName(SkinedMeshComponent::GetStaticType())) return new SkinedMeshComponent();
		return nullptr;
	}
}
