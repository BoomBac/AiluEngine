#include "pch.h"
#include "Objects/TransformComponent.h"

namespace Ailu
{
	TransformComponent::TransformComponent()
	{
		_Scale = Vector3f::One;
		_Position = Vector3f::Zero;
		_world_mat = BuildIdentityMatrix();
		DECLARE_REFLECT_PROPERTY(Vector3f,Position,_Position)
		DECLARE_REFLECT_PROPERTY(Vector3f,Scale, _Scale)
		DECLARE_REFLECT_PROPERTY(Vector3f,Rotation, _Rotation)
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
		_Rotation.x = Clamp(_Rotation.x, 180.0f, -180.0f);
		_Rotation.y = Clamp(_Rotation.y, 180.0f, -180.0f);
		_Rotation.z = Clamp(_Rotation.z, 180.0f, -180.0f);
		_world_mat = MatrixScale(_Scale) * MatrixTranslation(_Position) * MatrixRotationYawPitchRoll(_Rotation);
	}
}
