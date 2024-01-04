#pragma once
#ifndef __OBJECT_H__
#define __OBJECT_H__
#include "Framework/Common/Reflect.h"
#include <ranges>

namespace Ailu
{
	class Object
	{
		//--------------------Reflect
	protected: 
		std::unordered_map<String, SerializableProperty> _properties{};
	public: 
		template<typename T> T GetProperty(const String& name) 
		{
			return *reinterpret_cast<T*>(_properties.find(name)->second._value_ptr);
		} 
		SerializableProperty& GetProperty(const String& name) 
		{
			static SerializableProperty null{};
			if (_properties.contains(name))
				return _properties.find(name)->second;
			else
				return null;
		}
		std::unordered_map<String, SerializableProperty>& GetAllProperties()
		{
			return _properties;
		}
		auto PropertyBegin()
		{	
			return _properties.begin();
		}
		auto PropertyEnd()
		{
			return _properties.end();
		}
		//--------------------Reflect
		DECLARE_PROTECTED_PROPERTY(name,Name,String)
	public:
		Object();
		Object(const Object& other);
		virtual void Serialize(std::ofstream& file, String indent);
		virtual void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent);
		/// <summary>
		/// Deserialize
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="formated_str">prop name,prop value</param>
		/// <returns></returns>
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String,String>>& formated_str);
	protected:
		/// <summary>
		/// DeserializeImpl
		/// </summary>
		/// <param name="formated_str">prop name,prop value</param>
		/// <returns></returns>
		virtual void* DeserializeImpl(Queue<std::tuple<String,String>>& formated_str);
	};

	template<class T>
	T* Deserialize(Queue<std::tuple<String, String>>& formated_str)
	{
		T obj;
		return static_cast<T*>(obj.DeserializeImpl(formated_str));
	}
}


#endif // !OBJECT_H__

