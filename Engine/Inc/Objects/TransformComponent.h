#pragma once
#ifndef __TRANSFORM_COMP_H__
#define __TRANSFORM_COMP_H__
#include "Component.h"
#include "Framework/Common/Reflect.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Transform.h"

namespace Ailu
{
	class TransformComponent : public Component
	{
		COMPONENT_CLASS_TYPE(TransformComponent)
	public:
		TransformComponent();
		~TransformComponent();
		void Tick(const float& delta_time) final;
		Transform _transform;
	private:
		Vector3f _pos_data;
		Vector3f _rotation_data;
		Vector3f _scale_data;
	};
}

#endif // !TRANSFORM_COMP_H__

