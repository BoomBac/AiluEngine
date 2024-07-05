#include "pch.h"
#include "Render/Gizmo.h"
#include "Objects/LightComponent.h"
#include "Objects/SceneActor.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Math/Random.h"

namespace Ailu
{
	LightComponent::LightComponent()
	{
		IMPLEMENT_REFLECT_FIELD(LightComponent)
			_light._light_color = Colors::kWhite;
		_light._light_pos = { 0.0f,0.0f,0.0f,0.0f };
		_light._light_dir = kDefaultDirectionalLightDir;
		_intensity = 1.0f;
		_b_cast_shadow = false;
		_light_type = ELightType::kDirectional;
		_shadow_cameras.resize(QuailtySetting::s_cascade_shadow_map_count);
	}
	void Ailu::LightComponent::Tick(const float& delta_time)
	{
		if (!_b_enable) return;
		auto& transf = static_cast<SceneActor*>(_p_onwer)->GetTransform();
		_light._light_color.a = _intensity;
		_light._light_pos = transf._position;
		Vector4f light_forward = kDefaultDirectionalLightDir;
		light_forward.xyz = transf._rotation * light_forward.xyz;
		_light._light_dir.xyz = Normalize(Vector3f{ light_forward.xyz });

		Clamp(_light._light_param.z, 0.0f, 180.0f);
		Clamp(_light._light_param.y, 0.0f, _light._light_param.z - 0.1f);
		if (_b_cast_shadow)
		{
			UpdateShadowCamera();
		}
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
		os << prop_indent << "ConstantBias: " << _shadow._constant_bias << endl;
		os << prop_indent << "SlopeBias: " << _shadow._slope_bias << endl;
	}
	void LightComponent::OnGizmo()
	{
		DrawLightGizmo();
	}

	void LightComponent::LightType(ELightType type)
	{
		if (type == _light_type)
			return;
		if (type == ELightType::kPoint && _shadow_cameras.size() != 6)
		{
			_shadow_cameras.resize(6);
		}
		_light_type = type;
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
		comp->_intensity = comp->_light._light_color.a;
		if (type == ELightTypeStr(ELightType::kPoint))
		{
			comp->_light_type = ELightType::kPoint;
			comp->_light._light_param.x = LoadFloat(TP_ONE(formated_str.front()).c_str());
			comp->_shadow_cameras.resize(6);
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
		comp->_shadow._constant_bias = LoadFloat(TP_ONE(formated_str.front()).c_str());
		formated_str.pop();
		comp->_shadow._slope_bias = LoadFloat(TP_ONE(formated_str.front()).c_str());
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
			Vector3f light_to = _light._light_dir.xyz;
			light_to.x *= 500;
			light_to.y *= 500;
			light_to.z *= 500;
			Gizmo::DrawLine(transf._position, transf._position + light_to, _light._light_color);
			Gizmo::DrawCircle(transf._position, 50.0f, 24, _light._light_color, Quaternion::ToMat4f(transf._rotation));
			if (_b_cast_shadow)
			{
				//Camera::DrawGizmo(&_shadow_cameras[0]);
			}
			return;
		}
		case Ailu::ELightType::kPoint:
		{
			Gizmo::DrawCircle(transf._position, _light._light_param.x, 24, _light._light_color, MatrixRotationX(ToRadius(90.0f)));
			Gizmo::DrawCircle(transf._position, _light._light_param.x, 24, _light._light_color, MatrixRotationZ(ToRadius(90.0f)));
			Gizmo::DrawCircle(transf._position, _light._light_param.x, 24, _light._light_color);
			return;
		}
		case Ailu::ELightType::kSpot:
		{
			float angleIncrement = 90.0;
			auto rot_mat = Quaternion::ToMat4f(transf._rotation);
			Vector3f light_to = _light._light_dir.xyz;
			light_to *= _light._light_param.x;
			{
				Vector3f inner_center = light_to + transf._position;
				float inner_radius = tan(ToRadius(_light._light_param.y / 2.0f)) * _light._light_param.x;
				Gizmo::DrawLine(transf._position, transf._position + light_to * 10.f, Colors::kYellow);
				Gizmo::DrawCircle(inner_center, inner_radius, 24, _light._light_color, rot_mat);
				for (int i = 0; i < 4; ++i)
				{
					float angle1 = ToRadius(angleIncrement * static_cast<float>(i));
					float angle2 = ToRadius(angleIncrement * static_cast<float>(i + 1));
					Vector3f point1(inner_center.x + inner_radius * cos(angle1), inner_center.y, inner_center.z + inner_radius * sin(angle1));
					Vector3f point2(inner_center.x + inner_radius * cos(angle2), inner_center.y, inner_center.z + inner_radius * sin(angle2));
					point1 -= inner_center;
					point2 -= inner_center;
					point1 = transf._rotation * point1;
					point2 = transf._rotation * point2;
					//TransformCoord(point1, rot_mat);
					//TransformCoord(point2, rot_mat);
					point1 += inner_center;
					point2 += inner_center;
					Gizmo::DrawLine(transf._position, point2, _light._light_color);
				}
			}
			light_to *= 0.9f;
			{
				Vector3f outer_center = light_to + transf._position;
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
					Gizmo::DrawLine(transf._position, point2, _light._light_color);
				}
			}
		}
		}
	}

	void LightComponent::UpdateShadowCamera()
	{
		if (_light_type == ELightType::kDirectional)
		{
			auto& transf = static_cast<SceneActor*>(_p_onwer)->GetTransform();
			Camera& selected_cam = Camera::sSelected ? *Camera::sSelected : *Camera::sCurrent;
			f32 cur_level_start = 0, cur_level_end = QuailtySetting::s_main_light_shaodw_distance;
			for(u16 i = 0; i < QuailtySetting::s_cascade_shadow_map_count; ++i)
			{
				cur_level_end = QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[i];
				if (i > 0)
				{
					cur_level_start = QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[i - 1];
				}
				Sphere cascade_sphere = Camera::GetShadowCascadeSphere(selected_cam, cur_level_start, cur_level_end);
				float height = cascade_sphere._radius * 2.5f;
				_shadow_cameras[i].Type(ECameraType::kOrthographic);
				_shadow_cameras[i].SetLens(90, 1, 10, height * 1.5f);
				_shadow_cameras[i].Size(cascade_sphere._radius * 2.0f + 200.0f);
				_shadow_cameras[i].LookTo(-_light._light_dir.xyz, Vector3f::kUp);
				//_shadow_cameras[i].Rotation(Quaternion::AngleAxis(45, Vector3f::kRight));
				//_shadow_cameras[i].Rotation(transf._rotation * Quaternion::AngleAxis(90, Vector3f::kRight));
				_shadow_cameras[i].Position(cascade_sphere._center + -_light._light_dir.xyz * height);
				_shadow_cameras[i].RecalculateMarix(true);
				//Gizmo::DrawAABB(cascade_aabb, Random::RandomColor32(i));
				Gizmo::DrawSphere(cascade_sphere, Random::RandomColor32(i));
				Camera::DrawGizmo(&_shadow_cameras[i], Random::RandomColor32(i));
				_shadow_cameras[i].Cull(g_pSceneMgr->_p_current);
				_cascade_shadow_data[i].xyz = cascade_sphere._center;
				_cascade_shadow_data[i].w = cascade_sphere._radius * cascade_sphere._radius;
			}

		}
		else if (_light_type == ELightType::kSpot)
		{
			_shadow_cameras[0].SetLens(90, 1, 10, _light._light_param.x);
			_shadow_cameras[0].Position(_light._light_pos.xyz);
			_shadow_cameras[0].LookTo(_light._light_dir.xyz, Vector3f::kUp);
			_shadow_cameras[0].Cull(g_pSceneMgr->_p_current);
			Camera::DrawGizmo(&_shadow_cameras[0], Random::RandomColor32(0));
		}
		else
		{
			const static Vector3f targets[] =
			{
				{1.f,0,0}, //+x
				{-1.f,0,0}, //-x
				{0,1.0f,0}, //+y
				{0,-1.f,0}, //-y
				{0,0,+1.f}, //+z
				{0,0,-1.f}  //-z
			};
			const static Vector3f ups[] =
			{
				{0.f,1.f,0.f}, //+x
				{0.f,1.f,0.f}, //-x
				{0.f,0.f,-1.f}, //+y
				{0.f,0.f,1.f}, //-y
				{0.f,1.f,0.f}, //+z
				{0.f,1.f,0.f}  //-z
			};
			for (int i = 0; i < 6; i++)
			{
				_shadow_cameras[i].SetLens(90, 1, 10, _light._light_param.x * 1.5f);
				_shadow_cameras[i].Position(_light._light_pos.xyz);
				_shadow_cameras[i].LookTo(targets[i], ups[i]);
				_shadow_cameras[i].Cull(g_pSceneMgr->_p_current);
			}
		}
	}
}
