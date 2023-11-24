#include "pch.h"
#include "Objects/Object.h"

namespace Ailu
{
	Object::Object()
	{
	}
	void Ailu::Object::Serialize(std::ofstream& file, String indent)
	{
	}
	void Object::Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent)
	{
	}
	void* Object::DeserializeImpl(Queue<std::tuple<String,String>>& formated_str)
	{
		return nullptr;
	}
}
