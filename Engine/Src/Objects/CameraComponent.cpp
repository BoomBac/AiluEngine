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
		OnGizmo();
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
		float half_width{ 0.f }, half_height{0.f};
		if (_camera.Type() == ECameraType::kPerspective)
		{
			float tanHalfFov = tan(ToRadius(_camera.FovH()) * 0.5f);
			half_height = _camera.Near() * tanHalfFov;
			half_width = half_height * _camera.Aspect();
		}
		else
		{
			half_width = _camera.Size() * 0.5f;
			half_height = half_width / _camera.Aspect();
		}
		Vector3f near_top_left(-half_width, half_height, _camera.Near());
		Vector3f near_top_right(half_width, half_height, _camera.Near());
		Vector3f near_bottom_left(-half_width, -half_height, _camera.Near());
		Vector3f near_bottom_right(half_width, -half_height, _camera.Near());
		Vector3f far_top_left, far_top_right, far_bottom_left, far_bottom_right;
		if (_camera.Type() == ECameraType::kPerspective)
		{
			far_top_left = near_top_left * (_camera.Far() / _camera.Near());
			far_top_right = near_top_right * (_camera.Far() / _camera.Near());
			far_bottom_left = near_bottom_left * (_camera.Far() / _camera.Near());
			far_bottom_right = near_bottom_right * (_camera.Far() / _camera.Near());
		}
		else
		{
			far_top_left = near_top_left;
			far_top_right = near_top_right;
			far_bottom_left = near_bottom_left;
			far_bottom_right = near_bottom_right;
			float distance = _camera.Far() - _camera.Near();
			far_top_left.z += distance;
			far_top_right.z += distance;
			far_bottom_left.z += distance;
			far_bottom_right.z += distance;
		}
		
		Matrix4x4f camera_to_world = _camera.GetView();
		MatrixInverse(camera_to_world);

		TransformCoord(near_top_left,camera_to_world);
		TransformCoord(near_top_right,camera_to_world);
		TransformCoord(near_bottom_left,camera_to_world);
		TransformCoord(near_bottom_right,camera_to_world);
		TransformCoord(far_top_left,camera_to_world);
		TransformCoord(far_top_right,camera_to_world);
		TransformCoord(far_bottom_left,camera_to_world);
		TransformCoord(far_bottom_right,camera_to_world);

		// 绘制立方体的边框
		Gizmo::DrawLine(near_top_left, near_top_right, Colors::kWhite);
		Gizmo::DrawLine(near_top_right, near_bottom_right, Colors::kWhite);
		Gizmo::DrawLine(near_bottom_right, near_bottom_left, Colors::kWhite);
		Gizmo::DrawLine(near_bottom_left, near_top_left, Colors::kWhite);

		Gizmo::DrawLine(far_top_left, far_top_right, Colors::kWhite);
		Gizmo::DrawLine(far_top_right, far_bottom_right, Colors::kWhite);
		Gizmo::DrawLine(far_bottom_right, far_bottom_left, Colors::kWhite);
		Gizmo::DrawLine(far_bottom_left, far_top_left, Colors::kWhite);

		// 绘制连接立方体的线
		Gizmo::DrawLine(near_top_left, far_top_left, Colors::kWhite);
		Gizmo::DrawLine(near_top_right, far_top_right, Colors::kWhite);
		Gizmo::DrawLine(near_bottom_right, far_bottom_right, Colors::kWhite);
		Gizmo::DrawLine(near_bottom_left, far_bottom_left, Colors::kWhite);

		Gizmo::DrawLine(_camera.Position(), _camera.Position() + _camera.Forward() * _camera.Far(), Colors::kRed);
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