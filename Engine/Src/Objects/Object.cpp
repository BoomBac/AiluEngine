#include "pch.h"
#include "Objects/Object.h"

namespace Ailu
{
	Object::Object()
	{
	}
	Object::Object(const Object& other)
	{
		_name = other._name;
		for (const auto& [prop_name, prop] : other._properties)
		{
			_properties[prop_name] = prop;
			auto offset = reinterpret_cast<std::uintptr_t>(prop._value_ptr) - reinterpret_cast<std::uintptr_t>(&other);
			_properties[prop_name]._value_ptr = reinterpret_cast<u8*>(this) + offset;
		}
	}

	void Object::Serialize(std::ostream& os, String indent)
	{
	}
	void* Object::DeserializeImpl(Queue<std::tuple<String,String>>& formated_str)
	{
		return nullptr;
	}
}
