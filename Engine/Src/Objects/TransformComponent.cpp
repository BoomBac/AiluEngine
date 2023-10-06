#include "pch.h"
#include "Objects/TransformComponent.h"

namespace Ailu
{
	TransformComponent::TransformComponent()
	{
		DECLARE_REFLECT_PROPERTY(Vector3f,Position, _pos_data)
		DECLARE_REFLECT_PROPERTY(Vector3f,Scale, _scale_data)
		DECLARE_REFLECT_PROPERTY(Vector3f,Rotation, _rotation_data)
		_pos_data = _transform.Position();
		_scale_data = _transform.Scale();
		_rotation_data = _transform.Rotation();
	}
	TransformComponent::~TransformComponent()
	{
	}
	void TransformComponent::Tick(const float& delta_time)
	{
		_transform.Position(_pos_data);
		_transform.Scale(_scale_data);
		_transform.Rotation(_rotation_data);
		_transform.UpdateMatrix();
	}
}
