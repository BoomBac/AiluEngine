#pragma once
#ifndef __COMPONENT_H__
#define __COMPONENT_H__
#include <string>
#include "GlobalMarco.h"
namespace Ailu
{
#define COMPONENT_CLASS_TYPE(type)\
	static std::string GetType() {return #type;};\
	inline std::string GetTypeName() override {return GetType();};

	class Component
	{
	public:
		virtual ~Component();
		virtual void BeginPlay();
		virtual void Tick();
		virtual void Destroy();
		virtual std::string GetTypeName();
	protected:
	};
}


#endif // !__COMPONENT_H__

