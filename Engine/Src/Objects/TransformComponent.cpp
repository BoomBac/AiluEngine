#include "pch.h"
#include "Objects/TransformComponent.h"
#include "Render/Gizmo.h"

namespace Ailu
{
	TransformComponent::TransformComponent()
	{
		IMPLEMENT_REFLECT_FIELD(TransformComponent)
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
        if(_is_need_update_mat)
		    Transform::ToMatrix(_transform, _matrix);
        _is_need_update_mat = true;
	}
	void TransformComponent::OnGizmo()
	{
		// Gizmo::DrawLine(_transform._position, _transform._position + _transform._rotation * _transform._forward, Colors::kBlue);
		// Gizmo::DrawLine(_transform._position, _transform._position + _transform._rotation * _transform._up, Colors::kGreen);
		// Gizmo::DrawLine(_transform._position, _transform._position + _transform._rotation * _transform._right, Colors::kRed);
	}
	//Vector3f TransformComponent::GetEuler() const
	//{
	//	return Quaternion::GetAngle(_transform._rotation);
	//}
	void TransformComponent::SetPosition(const Vector3f& pos)
	{
		_transform._position = pos;
	}
	void TransformComponent::SetRotation(const Quaternion& quat)
	{
		_transform._rotation = quat;
	}
	void TransformComponent::SetRotation(const Vector3f& euler)
	{
		_transform._rotation = Quaternion::EulerAngles(euler);
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
		LoadVector(std::get<1>(formated_str.front()).c_str(), vec);
		trans->_transform._position = vec;
		formated_str.pop();
		LoadVector(std::get<1>(formated_str.front()).c_str(), vec4);
		trans->_transform._rotation = Quaternion(vec4);
		//trans->_rotation_data =  Quaternion::EulerAngles(trans->_transform._rotation);
		formated_str.pop();
		LoadVector(std::get<1>(formated_str.front()).c_str(), vec);
		trans->_transform._scale = vec;
		formated_str.pop();
		return trans;
	}
    void TransformComponent::SetMatrix(const Matrix4x4f &new_mat)
    {
        _matrix = new_mat;
        _is_need_update_mat = false;
    }
}
