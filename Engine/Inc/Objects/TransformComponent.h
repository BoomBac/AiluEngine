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
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		COMPONENT_CLASS_TYPE(TransformComponent)
		DECLARE_REFLECT_FIELD(TransformComponent)
		DECLARE_PRIVATE_PROPERTY_RO(transform,SelfTransform,Transform)
	public:
		TransformComponent();
		~TransformComponent();
		void Tick(const float& delta_time) final;
		void OnGizmo() final;
		const Matrix4x4f& GetMatrix() const { return _matrix; };
		Vector3f GetEuler() const { return _rotation_data; };
		void SetPosition(const Vector3f& pos);
		const Vector3f GetPosition() const { return _transform._position; };
		void SetRotation(const Quaternion& quat);
		void SetScale(const Vector3f& scale);
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) final;
		TransformComponent& operator=(const TransformComponent& other)
		{
			this->_pos_data = other._pos_data;
			this->_rotation_data = other._rotation_data;
			this->_scale_data = other._scale_data;
		}
	private:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) final;
	private:
		Matrix4x4f _matrix;
		Vector3f _pos_data;
		Vector3f _rotation_data;
		Vector3f _scale_data;
		Vector3f _pre_rotation_data;
	};
	REFLECT_FILED_BEGIN(TransformComponent)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f,Position,_pos_data)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f, Scale, _scale_data)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f, Rotation, _rotation_data)
	REFLECT_FILED_END
}

#endif // !TRANSFORM_COMP_H__

