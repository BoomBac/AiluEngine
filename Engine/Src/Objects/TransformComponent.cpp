#include "pch.h"
#include "Objects/TransformComponent.h"

namespace Ailu
{
	TransformComponent::TransformComponent()
	{
		IMPLEMENT_REFLECT_FIELD(TransformComponent)
		_pos_data = _transform.Position();
		_scale_data = _transform.Scale();
		_rotation_data = _transform.Rotation();
	}
	TransformComponent::~TransformComponent()
	{
	}
	void TransformComponent::Tick(const float& delta_time)
	{
		_transform.Position(_pos_data);
		_transform.Scale(_scale_data);
		_transform.Rotation(_rotation_data);
		_transform.UpdateMatrix();
	}
	void TransformComponent::Serialize(std::ofstream& file, String indent)
	{
		Component::Serialize(file, indent);
		using namespace std;
		String prop_indent = indent.append("  ");
		file << prop_indent << "Position: " << _transform.Position() << endl;
		file << prop_indent << "Rotation: " << _transform.Rotation() << endl;
		file << prop_indent << "Scale: " << _transform.Scale() << endl;
	}
	void* TransformComponent::DeserializeImpl(Queue<std::tuple<String, String>>& formated_str)
	{
		formated_str.pop();
		TransformComponent* trans = new TransformComponent();
		Vector3f vec;
		LoadVector(std::get<1>(formated_str.front()).c_str(), trans->_pos_data);
		trans->_transform.Position(_pos_data);
		formated_str.pop();
		LoadVector(std::get<1>(formated_str.front()).c_str(), trans->_rotation_data);
		trans->_transform.Rotation(_rotation_data);
		formated_str.pop();
		LoadVector(std::get<1>(formated_str.front()).c_str(), trans->_scale_data);
		trans->_transform.Scale(_scale_data);
		formated_str.pop();
		return trans;
	}
}
