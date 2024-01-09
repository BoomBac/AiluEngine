#include "pch.h"
#include "Render/Gizmo.h"
#include "Objects/LightComponent.h"
#include "Objects/SceneActor.h"

namespace Ailu
{
	LightComponent::LightComponent()
	{
		IMPLEMENT_REFLECT_FIELD(LightComponent)
		_light._light_color = Colors::kWhite;
		_light._light_pos = {0.0f,0.0f,0.0f,0.0f};
		_light._light_dir = kDefaultDirectionalLightDir;
		_intensity = 1.0f;
		_b_cast_shadow = false;
		_light_type = ELightType::kDirectional;
		_p_shadow_camera = MakeScope<Camera>(1.0f, 10.0f, 10000.0f,ECameraType::kOrthographic);
		_p_shadow_camera->Size(10000);
	}
	void Ailu::LightComponent::Tick(const float& delta_time)
	{
		if (_b_enable)
		{
			auto& transf = static_cast<SceneActor*>(_p_onwer)->GetTransform();
			Clamp(_intensity, 0.0, 4.0);
			_light._light_color.a = _intensity;
			_light._light_pos = transf.Position();
			Clamp(_light._light_param.z,0.0f, 180.0f);
			Clamp(_light._light_param.y, 0.0f,_light._light_param.z - 0.1f);
			_p_shadow_camera->Position(transf.Position());
			auto rot = transf.Rotation();
			rot.xy = transf.Rotation().yx;
			rot.y += 90.0f;
			_p_shadow_camera->Rotation(rot);
			_p_shadow_camera->Size(_shadow._size);
			_p_shadow_camera->Far(_shadow._distance);
			_p_shadow_camera->Update();
			if(_b_cast_shadow)
				Camera::DrawGizmo(_p_shadow_camera.get());
		}
	}
	void LightComponent::Serialize(std::ofstream& file, String indent)
	{
		Component::Serialize(file, indent);
		using namespace std;
		String prop_indent = indent.append("  ");
		file << prop_indent << "Type: " << ELightTypeStr(_light_type) << endl;
		Vector3f color = _light._light_color.xyz;
		file << prop_indent << "Color: " << color << endl;
		file << prop_indent << "Intensity: " << _light._light_color.a << endl;
		if (_light_type != ELightType::kDirectional)
			file << prop_indent << "Radius: " << _light._light_param.x << endl;
		if (_light_type == ELightType::kSpot)
		{
			file << prop_indent << "Inner: " << _light._light_param.y << endl;
			file << prop_indent << "Outer: " << _light._light_param.z << endl;
		}
		file << prop_indent << "CastShadow: " << (_b_cast_shadow ? "true" : "false") << endl;
		file << prop_indent << "ShadowArea: " << _shadow._size << endl;
		file << prop_indent << "ShadowDistance: " << _shadow._distance << endl;
	}
	void LightComponent::Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent)
	{
		Component::Serialize(os, indent);
		using namespace std;
		String prop_indent = indent.append("  ");
		os << prop_indent << "Type: " << ELightTypeStr(_light_type) << endl;
		Vector3f color = _light._light_color.xyz;
		os << prop_indent << "Color: " << color << endl;
		os << prop_indent << "Intensity: " << _light._light_color.a << endl;
		if (_light_type != ELightType::kDirectional)
			os << prop_indent << "Radius: " << _light._light_param.x << endl;
		if (_light_type == ELightType::kSpot)
		{
			os << prop_indent << "Inner: " << _light._light_param.y << endl;
			os << prop_indent << "Outer: " << _light._light_param.z << endl;
		}
		os << prop_indent << "CastShadow: " << (_b_cast_shadow ? "true" : "false") << endl;
		os << prop_indent << "ShadowArea: " << _shadow._size << endl;
		os << prop_indent << "ShadowDistance: " << _shadow._distance << endl;
	}
	void LightComponent::OnGizmo()
	{
		DrawLightGizmo();
	}
	void* LightComponent::DeserializeImpl(Queue<std::tuple<String, String>>& formated_str)
	{
		formated_str.pop();
		auto type = TP_ONE(formated_str.front());
		formated_str.pop();
		LightComponent* comp = new LightComponent();
		Vector3f vec{};
		float f{};
		LoadVector(TP_ONE(formated_str.front()).c_str(), vec);
		formated_str.pop();
		comp->_light._light_color = { vec,LoadFloat(TP_ONE(formated_str.front()).c_str()) };
		formated_str.pop();
		if(type == ELightTypeStr(ELightType::kPoint))
		{
			comp->_light_type = ELightType::kPoint;
			comp->_light._light_param.x = LoadFloat(TP_ONE(formated_str.front()).c_str());
			formated_str.pop();
		}
		else if (type == ELightTypeStr(ELightType::kSpot))
		{
			comp->_light_type = ELightType::kSpot;
			comp->_light._light_param.x = LoadFloat(TP_ONE(formated_str.front()).c_str());
			formated_str.pop();
			comp->_light._light_param.y = LoadFloat(TP_ONE(formated_str.front()).c_str());
			formated_str.pop();
			comp->_light._light_param.z = LoadFloat(TP_ONE(formated_str.front()).c_str());
			formated_str.pop();
		}
		if (String{ TP_ONE(formated_str.front()).c_str() } == "true")
			comp->_b_cast_shadow = true;
		else
			comp->_b_cast_shadow = false;
		formated_str.pop();
		comp->_shadow._size = LoadFloat(TP_ONE(formated_str.front()).c_str());
		formated_str.pop();
		comp->_shadow._distance = LoadFloat(TP_ONE(formated_str.front()).c_str());
		formated_str.pop();
		return comp;
	}

	void LightComponent::DrawLightGizmo()
	{
		auto& transf = static_cast<SceneActor*>(_p_onwer)->GetTransform();
		switch (_light_type)
		{
			case Ailu::ELightType::kDirectional:
			{
				auto rot = transf.Rotation();
				Vector4f light_forward = kDefaultDirectionalLightDir;
				auto rot_mat = MatrixRotationX(ToRadius(rot.x)) * MatrixRotationY(ToRadius(rot.y));
				TransformVector(light_forward, rot_mat);
				Normalize(light_forward);
				_light._light_dir = light_forward;
				Vector3f light_to = _light._light_dir.xyz;
				light_to.x *= 100;
				light_to.y *= 100;
				light_to.z *= 100;
				Gizmo::DrawLine(transf.Position(), transf.Position() + light_to, _light._light_color);
				Gizmo::DrawCircle(transf.Position(), 50.0f, 24, _light._light_color, rot_mat);
				return;
			}
			case Ailu::ELightType::kPoint:
			{
				Gizmo::DrawCircle(transf.Position(), _light._light_param.x, 24, _light._light_color, MatrixRotationX(ToRadius(90.0f)));
				Gizmo::DrawCircle(transf.Position(), _light._light_param.x, 24, _light._light_color, MatrixRotationZ(ToRadius(90.0f)));
				Gizmo::DrawCircle(transf.Position(), _light._light_param.x, 24, _light._light_color);
				return;
			}
			case Ailu::ELightType::kSpot:
			{
				auto rot = transf.Rotation();
				Vector4f light_forward = kDefaultDirectionalLightDir;
				auto rot_mat = MatrixRotationX(ToRadius(rot.x)) * MatrixRotationY(ToRadius(rot.y));
				TransformVector(light_forward, rot_mat);
				Normalize(light_forward);
				float angleIncrement = 90.0;
				_light._light_dir = light_forward;
				Vector3f light_to = _light._light_dir.xyz;
				light_to *= _light._light_param.x;
				{
					Vector3f inner_center = light_to + transf.Position();
					float inner_radius = tan(ToRadius(_light._light_param.y / 2.0f)) * _light._light_param.x;
					Gizmo::DrawCircle(inner_center, inner_radius, 24, _light._light_color, rot_mat);
					for (int i = 0; i < 4; ++i)
					{
						float angle1 = ToRadius(angleIncrement * static_cast<float>(i));
						float angle2 = ToRadius(angleIncrement * static_cast<float>(i + 1));
						Vector3f point1(inner_center.x + inner_radius * cos(angle1), inner_center.y, inner_center.z + inner_radius * sin(angle1));
						Vector3f point2(inner_center.x + inner_radius * cos(angle2), inner_center.y, inner_center.z + inner_radius * sin(angle2));
						point1 -= inner_center;
						point2 -= inner_center;
						TransformCoord(point1, rot_mat);
						TransformCoord(point2, rot_mat);
						point1 += inner_center;
						point2 += inner_center;
						Gizmo::DrawLine(transf.Position(), point2, _light._light_color);
					}
				}
				light_to *= 0.9f;
				{
					Vector3f outer_center = light_to + transf.Position();
					float outer_radius = tan(ToRadius(_light._light_param.z / 2.0f)) * _light._light_param.x;
					Gizmo::DrawCircle(outer_center, outer_radius, 24, _light._light_color, rot_mat);
					for (int i = 0; i < 4; ++i)
					{
						float angle1 = ToRadius(angleIncrement * static_cast<float>(i));
						float angle2 = ToRadius(angleIncrement * static_cast<float>(i + 1));
						Vector3f point1(outer_center.x + outer_radius * cos(angle1), outer_center.y, outer_center.z + outer_radius * sin(angle1));
						Vector3f point2(outer_center.x + outer_radius * cos(angle2), outer_center.y, outer_center.z + outer_radius * sin(angle2));
						point1 -= outer_center;
						point2 -= outer_center;
						TransformCoord(point1, rot_mat);
						TransformCoord(point2, rot_mat);
						point1 += outer_center;
						point2 += outer_center;
						Gizmo::DrawLine(transf.Position(), point2, _light._light_color);
					}
				}
			}
		}
	}
}
