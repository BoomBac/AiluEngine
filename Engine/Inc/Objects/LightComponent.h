#pragma warning(disable : 4251)
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
		float _constant_bias;
		float _slope_bias;
		Vector2f _padding;
		//float _near;
		//float _distance;
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

	class AILU_API LightComponent : public Component
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
		Camera* ShadowCamera(u16 index = 0) { return &_shadow_cameras[index]; }
		ELightType LightType() const {return _light_type; };
		void LightType(ELightType type);
		const Vector4f& CascadeShadowMData(u16 index) const { return _cascade_shadow_data[index]; }
	public:
		const inline static Vector3f kDefaultDirectionalLightDir = { 0.0f,-1.0f,0.0f };
		LightData _light;
		ShadowData _shadow;
		float _intensity;
		Vector4f _cascade_shadow_data[RenderConstants::kMaxCascadeShadowMapSplitNum];
	private:
		ELightType _light_type;
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) final;
		void DrawLightGizmo();
		void UpdateShadowCamera();
	private:
		Vector<Camera> _shadow_cameras;
	};
	REFLECT_FILED_BEGIN(LightComponent)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kColor, Color32, _light._light_color)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, Intensity, _intensity)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kBool, CastShadow, _b_cast_shadow)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, ShadowConstantBias, _shadow._constant_bias)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kFloat, ShadowSlopeBias, _shadow._slope_bias)
	REFLECT_FILED_END
}

#endif // !LIGHT_COMP_H__

