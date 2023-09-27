#pragma once
#ifndef __TRANSFORM_COMP_H__
#define __TRANSFORM_COMP_H__
#include "Component.h"
#include "Framework/Common/Reflect.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
	class TransformComponent : public Component
	{
	public:
		TransformComponent();
		~TransformComponent();
		const Matrix4x4f& GetWorldMatrix();
		DECLARE_PRIVATE_PROPERTY(position,Position,Vector3f)
		DECLARE_PRIVATE_PROPERTY(scale,Scale,Vector3f)
		DECLARE_PRIVATE_PROPERTY(rotation,Rotation,Vector3f)
		COMPONENT_CLASS_TYPE(TransformComponent);

		DECLARE_REFLECT_FIELD(TransformComponent)
	private:
		void CalculateMatrix();
	private:
		Quaternion _inner_rot;
		Matrix4x4f _world_mat;
	};
}

#endif // !TRANSFORM_COMP_H__

