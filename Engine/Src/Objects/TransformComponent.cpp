#include "pch.h"
#include "Objects/TransformComponent.h"

namespace Ailu
{
	TransformComponent::TransformComponent()
	{
		_scale = Vector3f::One;
		_position = Vector3f::Zero;
		_world_mat = BuildIdentityMatrix();
		DECLARE_REFLECT_PROPERTY(Vector3f,Position,_position)
		DECLARE_REFLECT_PROPERTY(Vector3f,Scale, _scale)
		DECLARE_REFLECT_PROPERTY(Vector3f,Rotation, _rotation)
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
		Clamp(_rotation.x, -180.0f, 180.0f);
		Clamp(_rotation.y, -180.0f, 180.0f);
		Clamp(_rotation.z, -180.0f, 180.0f);
		_world_mat = MatrixScale(_scale) * MatrixTranslation(_position) * MatrixRotationYawPitchRoll(_rotation);
	}
}
