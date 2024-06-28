#include "pch.h"
#include "Objects/Object.h"

namespace Ailu
{
	Object::Object()
	{
		_name = std::format("object_{}", s_global_object_id);
		_id = s_global_object_id++;
	}
	Object::Object(const String& name) : Object()
	{
		_name = name;
	}
	Object::Object(const Object& other)
	{
		_name = other._name;
		_id = other._id;
		for (const auto& [prop_name, prop] : other._properties)
		{
			_properties[prop_name] = prop;
			auto offset = reinterpret_cast<std::uintptr_t>(prop._value_ptr) - reinterpret_cast<std::uintptr_t>(&other);
			_properties[prop_name]._value_ptr = reinterpret_cast<u8*>(this) + offset;
		}
	}

	Object::Object(Object&& other) noexcept
	{
		_name = other._name;
		_id = other._id;
		_properties = std::move(other._properties);
		other._name.clear();
		other._id = -1;
		other._properties.clear();
	}

	Object& Object::operator=(const Object& other)
	{
		_name = other._name;
		_id = other._id;
		for (const auto& [prop_name, prop] : other._properties)
		{
			_properties[prop_name] = prop;
			auto offset = reinterpret_cast<std::uintptr_t>(prop._value_ptr) - reinterpret_cast<std::uintptr_t>(&other);
			_properties[prop_name]._value_ptr = reinterpret_cast<u8*>(this) + offset;
		}
		return *this;
	}

	Object& Object::operator=(Object&& other) noexcept
	{
		_name = other._name;
		_id = other._id;
		_properties = std::move(other._properties);
		other._name.clear();
		other._id = -1;
		other._properties.clear();
		return *this;
	}

	void Object::Serialize(std::ostream& os, String indent)
	{
	}
	void* Object::DeserializeImpl(Queue<std::tuple<String,String>>& formated_str)
	{
		return nullptr;
	}
}
