#include "pch.h"
#include "Objects/CameraComponent.h"

#include "Render/Gizmo.h"
#include <Objects/CommonActor.h>

namespace Ailu 
{
	CameraComponent::CameraComponent(const Camera& camera) : _camera(camera)
	{
	}
	CameraComponent::~CameraComponent()
	{
	}
	void CameraComponent::Tick(const float& delta_time)
	{
		auto& parent_pos = static_cast<SceneActor*>(_p_onwer)->GetTransform().Position();
		auto& parent_rot = static_cast<SceneActor*>(_p_onwer)->GetTransform().Rotation();
		_camera.Position(parent_pos);
		_camera.Rotation(parent_rot);
		_camera.Update();
	}
	void CameraComponent::Serialize(std::ofstream& file, String indent)
	{
		Component::Serialize(file, indent);
		using namespace std;
		String prop_indent = indent.append("  ");
		file << prop_indent << "Type: " << ECameraType::ToString(_camera.Type()) << endl;
		file << prop_indent << "Position: " << _camera.Position() << endl;
		file << prop_indent << "Rotation: " << _camera.Rotation() << endl;
		file << prop_indent << "Aspect: " << _camera.Aspect() << endl;
		file << prop_indent << "Far: " << _camera.Far() << endl;
		file << prop_indent << "Near: " << _camera.Near() << endl;
		file << prop_indent << "FovH: " << _camera.FovH() << endl;
		file << prop_indent << "Size: " << _camera.Size() << endl;
	}

	static String SerializeProperty(const SerializableProperty& prop)
	{
		if (prop._type == ESerializablePropertyType::kFloat)
		{
			return std::format("{}: {}", prop._name, prop.GetProppertyValue<float>().value_or(0.f));
		}
		else if (prop._type == ESerializablePropertyType::kVector3f)
		{
			auto v = prop.GetProppertyValue<Vector3f>().value_or(Vector3f::Zero);
			return std::format("{}: {},{},{}", prop._name, v.x,v.y,v.z);
		}
		return "";
	}

	void CameraComponent::Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent)
	{
		using namespace std;
		os << indent << GetComponentTypeStr(GetType()) << ": " <<_camera.GetAllProperties().size() + 1 << std::endl;
		String prop_indent = indent.append("  ");
		os << prop_indent << "Type: " << ECameraType::ToString(_camera.Type()) << endl;
		for (auto it = _camera.PropertyBegin(); it != _camera.PropertyEnd(); it++)
		{
			os << prop_indent << SerializeProperty(it->second) << endl;
		}
		//os << prop_indent << "Position: " << _camera.Position() << endl;
		//os << prop_indent << "Rotation: " << _camera.Rotation() << endl;
		//os << prop_indent << "Aspect: " << _camera.Aspect() << endl;
		//os << prop_indent << "Far: " << _camera.Far() << endl;
		//os << prop_indent << "Near: " << _camera.Near() << endl;
		//os << prop_indent << "FovH: " << _camera.FovH() << endl;
		//os << prop_indent << "Size: " << _camera.Size() << endl;
	}
	void CameraComponent::OnGizmo()
	{
		Camera::DrawGizmo(&_camera);
	}

	void* CameraComponent::DeserializeImpl(Queue<std::tuple<String, String>>& formated_str)
	{
		Camera camera;
		u16 prop_num = static_cast<u16>(LoadFloat(std::get<1>(formated_str.front()).c_str()));
		formated_str.pop();
		if (std::get<1>(formated_str.front()) == ECameraType::ToString(ECameraType::kPerspective)) 
			camera.Type(ECameraType::kPerspective);
		else 
			camera.Type(ECameraType::kOrthographic);
		formated_str.pop();
		prop_num--;
		while (prop_num--)
		{
			auto& prop_name = TP_ZERO(formated_str.front());
			auto& prop_value = TP_ONE(formated_str.front());
			auto& prop = camera.GetProperty(prop_name);
			if (prop._type == ESerializablePropertyType::kFloat)
			{
				prop.SetProppertyValue(LoadFloat(prop_value.c_str()));
			}
			else if (prop._type == ESerializablePropertyType::kVector3f)
			{
				prop.SetProppertyValue(LoadVector<Vector3D,float>(prop_value.c_str()));
			}
			formated_str.pop();
		}
		camera.Position(camera.GetProperty<Vector3f>("Position"));
		camera.Rotation(camera.GetProperty<Vector3f>("Rotation"));
		camera.Update();
		CameraComponent* cam = new CameraComponent(camera);
		return cam;
	}
}