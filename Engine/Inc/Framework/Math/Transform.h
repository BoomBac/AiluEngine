#pragma once
#ifndef __TRANSFORM_H__
#define __TRANSFORM_H__
#include "ALMath.hpp"
#include "GlobalMarco.h"

namespace Ailu
{
	class Transform
	{
		DECLARE_PRIVATE_PROPERTY(position, Position, Vector3f)
		DECLARE_PRIVATE_PROPERTY(scale, Scale, Vector3f)
		DECLARE_PRIVATE_PROPERTY(rotation, Rotation, Vector3f)
	public:
		Transform() 
		{
			_position = { 0.f,0.f,0.f };
			_scale = { 1.f,1.f,1.f };
			_rotation = { 0.f,0.f,0.f };
			_world_mat = BuildIdentityMatrix();
		}
		const Matrix4x4f& GetTransformMat()
		{
			UpdateMatrix();
			return _world_mat;
		}
		void UpdateMatrix()
		{
			Clamp(_rotation.x, -180.0f, 180.0f);
			Clamp(_rotation.y, -180.0f, 180.0f);
			Clamp(_rotation.z, -180.0f, 180.0f);
			_world_mat = MatrixScale(_scale) * MatrixRotationYawPitchRoll(_rotation) * MatrixTranslation(_position);
		}
		Transform& operator=(const Transform& other)
		{
			this->_position = other._position;
			this->_rotation = other._rotation;
			this->_scale = other._scale;
			this->_world_mat = other._world_mat;
			return *this;
		}
	private:
		Matrix4x4f _world_mat;
	};
}


#endif // !TRANSFORM_H__

