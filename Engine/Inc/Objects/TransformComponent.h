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
	public:
		TransformComponent();
		~TransformComponent();
		void Tick(const float& delta_time) final;
		void OnGizmo() final;
		Transform _transform;
		Vector3f GetEuler() const { return _rotation_data; };
		void Serialize(std::ofstream& file, String indent) final;
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

