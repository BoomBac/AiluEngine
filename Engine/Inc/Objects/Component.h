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
		kSkinedMeshComponent
	};

#define COMPONENT_CLASS_TYPE(type)\
public:\
	static EComponentType GetStaticType() {return EComponentType::k##type;};\
	inline EComponentType GetType() override {return GetStaticType();};

	class Actor;
	class AILU_API Component : public Object
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
		void Serialize(std::ostream& os, String indent) override;
		const static String& GetTypeName(EComponentType type)
		{
			switch (type)
			{
			case Ailu::EComponentType::kTransformComponent: return s_type_str[0];
			case Ailu::EComponentType::kLightComponent:return s_type_str[1];
			case Ailu::EComponentType::kStaticMeshComponent:return s_type_str[2];
			case Ailu::EComponentType::kCameraComponent:return s_type_str[3];
			case Ailu::EComponentType::kSkinedMeshComponent:return s_type_str[4];
			}
			return s_type_str[0];
		}
		const static Vector<String>& GetAllComponentTypeStr() { return s_type_str; }
		static Component* Create(String& type_name);
	private:
		inline static const Vector<String> s_type_str{ "TransformComponent","LightComponent","StaticMeshComponent","CameraComponent","SkinedMeshComponent" };
	protected:
		Actor* _p_onwer = nullptr;
	};
}


#endif // !__COMPONENT_H__

