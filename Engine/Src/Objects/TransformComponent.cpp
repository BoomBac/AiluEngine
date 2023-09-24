#include "pch.h"
#include "Objects/TransformComponent.h"

namespace Ailu
{
	TransformComponent::TransformComponent()
	{
		_Scale = Vector3f::One;
		_Position = Vector3f::Zero;
		_world_mat = BuildIdentityMatrix();
	}
	TransformComponent::~TransformComponent()
	{
	}
	const Matrix4x4f& TransformComponent::GetWorldMatrix()
	{
		CalculateMatrix();
		return _world_mat;
	}
	void TransformComponent::CalculateMatrix()
	{
		_world_mat = MatrixScale(_Scale) * MatrixTranslation(_Position);
	}
}
