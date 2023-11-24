#pragma once
#ifndef __LIGHT_COMP_H__
#define __LIGHT_COMP_H__
#include "Component.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
	struct LightData
	{
		Vector4f _light_pos;
		Vector4f _light_dir;
		Color _light_color;
		Vector4f _light_param;
	};

	enum class ELightType : uint8_t
	{
		kDirectional,kPoint,kSpot
	};

	static String ELightTypeStr(ELightType type)
	{
		switch (type)
		{
		case Ailu::ELightType::kDirectional: return "Directional";
		case Ailu::ELightType::kPoint: return "Point";
		case Ailu::ELightType::kSpot: return "Spot";
		default: return "undefined";
		}
	};

	class LightComponent : public Component
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		COMPONENT_CLASS_TYPE(LightComponent)
		DECLARE_REFLECT_FIELD(LightComponent)
	public:
		LightComponent();
		void Tick(const float& delta_time) final;
		void Serialize(std::ofstream& file, String indent) final;
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) final;
	public:
		const inline static Vector3f kDefaultDirectionalLightDir = { 0.0f,-1.0f,0.0f };
		ELightType _light_type;
		LightData _light;
		float _intensity;
		bool _b_shadow;
	private:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) final;
		void DrawLightGizmo();
	};
	REFLECT_FILED_BEGIN(LightComponent)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kColor, Color, _light._light_color)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, Intensity, _intensity)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kBool, CastShadow, _b_shadow)
	REFLECT_FILED_END
}

#endif // !LIGHT_COMP_H__

