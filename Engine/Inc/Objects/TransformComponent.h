#pragma once
#ifndef __TRANSFORM_COMP_H__
#define __TRANSFORM_COMP_H__
#include "Component.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
	class TransformComponent : public Component
	{
	public:
		TransformComponent();
		~TransformComponent();
		const Matrix4x4f& GetWorldMatrix();
		DECLARE__PRIVATE_PROPERTY(Position,Vector3f)
		DECLARE__PRIVATE_PROPERTY(Scale,Vector3f)
		DECLARE__PRIVATE_PROPERTY(Rotation,Quaternion)
		COMPONENT_CLASS_TYPE(TransformComponent);
	private:
		void CalculateMatrix();
	private:
		Matrix4x4f _world_mat;
	};
}

#endif // !TRANSFORM_COMP_H__

