#pragma once
#ifndef __COMPONENT_H__
#define __COMPONENT_H__
#include <string>
#include "GlobalMarco.h"
#include "Framework/Common/Reflect.h"

namespace Ailu
{
#define COMPONENT_CLASS_TYPE(type)\
public:\
	static std::string GetStaticType() {return #type;};\
	inline std::string GetTypeName() override {return GetStaticType();};

	class Actor;
	class Component
	{
	public:
		Component();
		virtual ~Component();
		virtual void BeginPlay();
		virtual void Tick(const float& delta_time);
		virtual void Destroy();
		virtual void SetOwner(Actor* onwer);
		virtual std::string GetTypeName();
		DECLARE_PROTECTED_PROPERTY(b_enable,Active,bool)
		DECLARE_REFLECT_FIELD(Component)
	protected:
		Actor* _p_onwer = nullptr;
		
	};
}


#endif // !__COMPONENT_H__

