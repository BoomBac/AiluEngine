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
	class LightComponent : public Component
	{
		COMPONENT_CLASS_TYPE(LightComponent)
	public:
		LightComponent();
		void Tick(const float& delta_time) final;
	public:
		const inline static Vector3f kDefaultDirectionalLightDir = { 0.0f,-1.0f,0.0f };
		ELightType _light_type;
		LightData _light;
		float _intensity;
		bool _b_shadow;
	private:
		void DrawLightGizmo();
		
	};
}

#endif // !LIGHT_COMP_H__
