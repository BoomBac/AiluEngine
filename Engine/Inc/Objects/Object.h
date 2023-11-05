#pragma once
#ifndef __OBJECT_H__
#define __OBJECT_H__
#include "Framework/Common/Reflect.h"

namespace Ailu
{
	class Object
	{
		//--------------------Reflect
	protected: 
		std::unordered_map<String, SerializableProperty> properties{}; 
	public: 
		template<typename T> T GetProperty(const String& name) 
		{
			return *reinterpret_cast<T*>(properties.find(name)->second._value_ptr);
		} 
		SerializableProperty& GetProperty(const String& name) 
		{
			return properties.find(name)->second;
		}
		std::unordered_map<String, SerializableProperty>& GetAllProperties() 
		{
			return properties;
		}
		//--------------------Reflect
		DECLARE_PROTECTED_PROPERTY(name,Name,String)
	public:
		Object();
		virtual void Serialize(std::ofstream& file, String indent);
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String,String>>& formated_str);
	protected:
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

