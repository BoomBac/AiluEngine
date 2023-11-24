#pragma once
#ifndef __COMPONENT_H__
#define __COMPONENT_H__
#include <string>
#include "GlobalMarco.h"
#include "Object.h"

namespace Ailu
{
#define COMPONENT_CLASS_TYPE(type)\
public:\
	static std::string GetStaticType() {return #type;};\
	inline std::string GetTypeName() override {return GetStaticType();};

	class Actor;
	class Component : public Object
	{
		DECLARE_PROTECTED_PROPERTY(b_enable, Active, bool)
	public:
		Component();
		virtual ~Component();
		virtual void BeginPlay();
		virtual void Tick(const float& delta_time);
		virtual void Destroy();
		virtual void SetOwner(Actor* onwer);
		virtual std::string GetTypeName();
		void Serialize(std::ofstream& file, String indent) override;
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) override;
	protected:
		Actor* _p_onwer = nullptr;
	};
}


#endif // !__COMPONENT_H__

