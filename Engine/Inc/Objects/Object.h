#pragma warning(disable : 4251)
#pragma once
#ifndef __OBJECT_H__
#define __OBJECT_H__
#include "Framework/Common/Reflect.h"
#include <ranges>

namespace Ailu
{
	class AILU_API Object
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
	public:
		Object();
		Object(const String& name);
		Object(const Object& other);
		Object(Object&& other) noexcept;
		Object& operator=(const Object& other);
		Object& operator=(Object&& other) noexcept;
		bool operator==(const Object& other) const {return _id == other._id;};
		bool operator<(const Object& other) const {return _id < other._id;};
		virtual void Serialize(std::ostream& os, String indent);
		/// <summary>
		/// Deserialize
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="formated_str">prop name,prop value</param>
		/// <returns></returns>
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		virtual void Name(const String& value) { _name = value; }
		const String& Name() const { return _name; }
		void ID(const u32& value) { _id = value; }
		const u32& ID() const { return _id; }
	protected:
		/// <summary>
		/// DeserializeImpl
		/// </summary>
		/// <param name="formated_str">prop name,prop value</param>
		/// <returns></returns>
		virtual void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str);
	protected:
		String _name;
		u32 _id;
	private:
		inline static u32 s_global_object_id = 0u;
	};

	template<class T>
	T* Deserialize(Queue<std::tuple<String, String>>& formated_str)
	{
		T obj;
		return static_cast<T*>(obj.DeserializeImpl(formated_str));
	}
}


#endif // !OBJECT_H__

