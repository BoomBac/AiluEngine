#pragma once
#ifndef __COMPONENT_H__
#define __COMPONENT_H__
#include <string>
#include "GlobalMarco.h"
#include "Object.h"

namespace Ailu
{
	enum class EComponentType
	{
		kDefault = 0,
		kTransformComponent,
		kLightComponent,
		kStaticMeshComponent,
		kCameraComponent,
	};
	const static String& GetComponentTypeStr(EComponentType type)
	{
		static const String type_str[]{"TransformComponent","LightComponent","StaticMeshComponent","CameraComponent"};
		switch (type)
		{
		case Ailu::EComponentType::kTransformComponent: return type_str[0];
		case Ailu::EComponentType::kLightComponent:return type_str[1];
		case Ailu::EComponentType::kStaticMeshComponent:return type_str[2];
		case Ailu::EComponentType::kCameraComponent:return type_str[3];
		}
		return type_str[0];
	}
#define COMPONENT_CLASS_TYPE(type)\
public:\
	static EComponentType GetStaticType() {return EComponentType::k##type;};\
	inline EComponentType GetType() override {return GetStaticType();};

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
		virtual void OnGizmo();
		virtual void SetOwner(Actor* onwer);
		virtual Actor* GetOwner() {return _p_onwer; };
		virtual EComponentType GetType();
		void Serialize(std::ofstream& file, String indent) override;
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) override;
	protected:
		Actor* _p_onwer = nullptr;
	};
}


#endif // !__COMPONENT_H__

