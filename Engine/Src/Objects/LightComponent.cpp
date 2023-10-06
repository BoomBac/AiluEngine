#include "pch.h"
#include "Objects/LightComponent.h"
#include "Objects/TransformComponent.h"
#include "Objects/Actor.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
	LightComponent::LightComponent()
	{
		_light._light_color = Colors::kWhite;
		_light._light_pos = {0.0f,0.0f,0.0f,0.0f};
		_light._light_dir = kDefaultDirectionalLightDir;
		_intensity = 1.0f;
		_b_shadow = false;
		_light_type = ELightType::kDirectional;
		DECLARE_REFLECT_PROPERTY(Color, Color, (_light._light_color))
		DECLARE_REFLECT_PROPERTY(float, Intensity, _intensity)
		DECLARE_REFLECT_PROPERTY(bool, CastShadow, _b_shadow)
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
			DrawLightGizmo();
		}
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
