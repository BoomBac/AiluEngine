#include "pch.h"
#include "Objects/TransformComponent.h"
#include "Render/Gizmo.h"

namespace Ailu
{
	TransformComponent::TransformComponent()
	{
		IMPLEMENT_REFLECT_FIELD(TransformComponent)
		_pos_data = _transform._position;
		_scale_data = _transform._scale;
		_rotation_data = Quaternion::EulerAngles(_transform._rotation);
		_pre_rotation_data = _rotation_data;
	}
	TransformComponent::~TransformComponent()
	{
	}
	void TransformComponent::Tick(const float& delta_time)
	{
		if (!_b_enable)
		{
			LOG_WARNING("TransformComponent can't been disable!");
		}
		_transform._position = _pos_data;
		_transform._scale = _scale_data;
		auto r_d = _rotation_data - _pre_rotation_data;
		_transform._rotation = _transform._rotation * Quaternion::EulerAngles(r_d);
		_pre_rotation_data = _rotation_data;
		Transform::ToMatrix(_transform, _matrix);
	}
	void TransformComponent::OnGizmo()
	{
		Gizmo::DrawLine(_transform._position, _transform._position + _transform._rotation * _transform._forward * 100, Colors::kBlue);
		Gizmo::DrawLine(_transform._position, _transform._position + _transform._rotation * _transform._up * 100, Colors::kGreen);
		Gizmo::DrawLine(_transform._position, _transform._position + _transform._rotation * _transform._right * 100, Colors::kRed);
	}
	void TransformComponent::SetPosition(const Vector3f& pos)
	{
		_transform._position = pos;
	}
	void TransformComponent::SetRotation(const Quaternion& quat)
	{
		_transform._rotation = quat;
	}
	void TransformComponent::SetScale(const Vector3f& scale)
	{
		_transform._scale = scale;
	}

	void TransformComponent::Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent)
	{
		Component::Serialize(os, indent);
		using namespace std;
		String prop_indent = indent.append("  ");
		os << prop_indent << "Position: " << _transform._position << endl;
		os << prop_indent << "Rotation: " << _transform._rotation << endl;
		os << prop_indent << "Scale: " << _transform._scale << endl;
	}

	void* TransformComponent::DeserializeImpl(Queue<std::tuple<String, String>>& formated_str)
	{
		formated_str.pop();
		TransformComponent* trans = new TransformComponent();
		Vector3f vec;
		Vector4f vec4;
		LoadVector(std::get<1>(formated_str.front()).c_str(), trans->_pos_data);
		trans->_transform._position = _pos_data;
		formated_str.pop();
		LoadVector(std::get<1>(formated_str.front()).c_str(), vec4);
		trans->_transform._rotation = Quaternion(vec4);
		trans->_rotation_data =  Quaternion::EulerAngles(trans->_transform._rotation);
		formated_str.pop();
		LoadVector(std::get<1>(formated_str.front()).c_str(), trans->_scale_data);
		trans->_transform._scale = _scale_data;
		formated_str.pop();
		return trans;
	}
}
