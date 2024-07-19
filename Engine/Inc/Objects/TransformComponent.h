#pragma warning(disable : 4251)
#pragma once
#ifndef __TRANSFORM_COMP_H__
#define __TRANSFORM_COMP_H__
#include "Component.h"
#include "Framework/Common/Reflect.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Transform.h"

namespace Ailu
{
	class AILU_API TransformComponent : public Component
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
		const Matrix4x4f& GetMatrix() const { return _matrix; };
        void SetMatrix(const Matrix4x4f& new_mat);
		Vector3f GetEuler() const { return Quaternion::EulerAngles(_transform._rotation); };
		Vector3f GetScale() const { return _transform._scale; };
		void SetPosition(const Vector3f& pos);
		const Vector3f GetPosition() const { return _transform._position; };
		void SetRotation(const Quaternion& quat);
		void SetRotation(const Vector3f& euler);
        Quaternion GetRotation() const {return _transform._rotation;}
		void SetScale(const Vector3f& scale);
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) final;
		TransformComponent& operator=(const TransformComponent& other)
		{
			_transform = other._transform;
			return *this;
		}
		const Transform& SelfTransform() const {return _transform;}
	private:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) final;
	private:
		Transform _transform;
		Matrix4x4f _matrix;
        bool _is_need_update_mat = true;
	};
	REFLECT_FILED_BEGIN(TransformComponent)
	//DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f,Position,_pos_data)
	//DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f, Scale, _scale_data)
	//DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f, Rotation, _rotation_data)
	REFLECT_FILED_END
}

#endif // !TRANSFORM_COMP_H__

