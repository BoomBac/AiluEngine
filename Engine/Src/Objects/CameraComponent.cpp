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
		if (!_b_enable) return;
		auto& parent_pos = static_cast<SceneActor*>(_p_onwer)->GetTransform()._position;
		auto& parent_rot = static_cast<SceneActor*>(_p_onwer)->GetTransform()._rotation;
		//auto parent_rot = Vector3f(0.f,0.f,0.f);
		_camera.Position(parent_pos);
		_camera.Rotation(parent_rot);
		_camera.RecalculateMarix(true);
	}

	static String SerializeProperty(const SerializableProperty& prop)
	{
		if (prop._type == ESerializablePropertyType::kFloat)
		{
			return std::format("{}: {}", prop._name, prop.GetProppertyValue<float>().value_or(0.f));
		}
		else if (prop._type == ESerializablePropertyType::kVector3f)
		{
			auto v = prop.GetProppertyValue<Vector3f>().value_or(Vector3f::kZero);
			return std::format("{}: {},{},{}", prop._name, v.x,v.y,v.z);
		}
		return "";
	}

	void CameraComponent::Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent)
	{
		using namespace std;
		os << indent << Component::GetTypeName(GetType()) << ": " <<_camera.GetAllProperties().size() + 1 << std::endl;
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
		Camera::DrawGizmo(&_camera,Gizmo::s_color);
		//float shadow_distance = _camera.Far() * 0.5f;
		//float len = shadow_distance / _camera.Far();
		//Vector<Vector3f> p(8);
		//p[0] = _camera._position + (_camera._far_bottom_left - _camera._position) * len;
		//p[1] = _camera._position + (_camera._far_bottom_right - _camera._position) * len;
		//p[2] = _camera._position + (_camera._far_top_left - _camera._position) * len;
		//p[3] = _camera._position + (_camera._far_top_right - _camera._position) * len;

		//p[4] = p[0] - _camera._forward * shadow_distance;
		//p[5] = p[1] - _camera._forward * shadow_distance;
		//p[6] = p[2] - _camera._forward * shadow_distance;
		//p[7] = p[3] - _camera._forward * shadow_distance;
		////for(int i = 0; i < 4; i++)
		////{
		////	Gizmo::DrawLine(p[i], p[i + 4], Colors::kRed);
		////}
		//auto vmax = AABB::MaxAABB(),vmin = AABB::MinAABB();
		//for (int i = 0; i < 8; i++)
		//{
		//	vmax = Max(vmax, p[i]);
		//	vmin = Min(vmin, p[i]);
		//}
		//Vector3f center = (vmax + vmin) * 0.5f;
		//float dx = vmax.x - vmin.x;
		//float dz = vmax.z - vmin.z;
		//float extent = std::max(dx, dz) * 0.5;
		////vmin = center - Vector3f(extent);
		////vmax = center + Vector3f(extent);
		//Gizmo::DrawAABB(AABB(vmin, vmax), Colors::kYellow);
		//Vector3f light_dir = { 0,-0.717,0.717 };
		//Gizmo::DrawLine(_camera._position, _camera._position + light_dir * shadow_distance, Colors::kRed);
		//Vector3f dir_min = vmin + light_dir * shadow_distance;
		//Vector3f dir_max = vmax + light_dir * shadow_distance;
		////dir_min.y = shadow_distance * 2;
		////dir_max.y = shadow_distance * 2;
		//Gizmo::DrawLine(vmin, vmin + light_dir * 1000, Colors::kGreen);
		//Gizmo::DrawLine(vmax, vmax + light_dir * 1000, Colors::kGreen);
		//
		//float height = Distance(vmin, vmax) * 2;
		//Camera cam;
		//cam.Type(ECameraType::kOrthographic);
		//cam.SetLens(90, 1, 10, height * 1.5);
		//cam.Size(extent * 2);
		//cam.Rotation(Quaternion::AngleAxis(45, Vector3f::kRight));
		//cam.Position(center + -light_dir * height);
		//cam.RecalculateMarix(true);
		//Camera::DrawGizmo(&cam);
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
		camera.Rotation(camera.GetProperty<Quaternion>("Rotation"));
		camera.RecalculateMarix();
		CameraComponent* cam = new CameraComponent(camera);
		return cam;
	}
}