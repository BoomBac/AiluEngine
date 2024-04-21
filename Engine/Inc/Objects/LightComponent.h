#pragma once
#ifndef __LIGHT_COMP_H__
#define __LIGHT_COMP_H__
#include "Component.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Camera.h"

namespace Ailu
{
	struct LightData
	{
		Vector4f _light_pos;
		Vector4f _light_dir;
		Color32 _light_color;
		Vector4f _light_param;
	};

	struct ShadowData
	{
		float _size;
		float _depth_bias;
		float _near;
		float _distance;
	};

	enum class ELightType : u8
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
		DECLARE_PRIVATE_PROPERTY(b_cast_shadow,CastShadow,bool)
	public:
		LightComponent();
		void Tick(const float& delta_time) final;
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) final;
		void OnGizmo() final;
		Camera* ShadowCamera() { return _p_shadow_camera.get(); }
	public:
		const inline static Vector3f kDefaultDirectionalLightDir = { 0.0f,-1.0f,0.0f };
		ELightType _light_type;
		LightData _light;
		ShadowData _shadow;
		float _intensity;
	private:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) final;
		void DrawLightGizmo();
	private:
		Scope<Camera> _p_shadow_camera;
	};
	REFLECT_FILED_BEGIN(LightComponent)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kColor, Color32, _light._light_color)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, Intensity, _intensity)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kBool, CastShadow, _b_cast_shadow)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, ShadowDistance, _shadow._distance)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, ShadowArea, _shadow._size)
	REFLECT_FILED_END
}

#endif // !LIGHT_COMP_H__

