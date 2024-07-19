#pragma warning(disable : 4251)
#pragma once
#ifndef __CAMERA_COMP_H__
#define __CAMERA_COMP_H__
#include "Component.h"
#include "Render/Camera.h"

namespace Ailu
{
	class AILU_API CameraComponent : public Component
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		COMPONENT_CLASS_TYPE(CameraComponent)
		//DECLARE_REFLECT_FIELD(CameraComponent)
	public:
		CameraComponent() = default;
		CameraComponent(const Camera& camera);
		~CameraComponent();
		void Tick(const float& delta_time) final;
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) final;
		void OnGizmo() final;
		Camera _camera;
	private:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) final;
	};
	//REFLECT_FILED_BEGIN(TransformComponent)
	//	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f, Position, _pos_data)
	//DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f, Scale, _scale_data)
	//DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kVector3f, Rotation, _rotation_data)
	//REFLECT_FILED_END
}

#endif // !__CAMERA_COMP_H__

