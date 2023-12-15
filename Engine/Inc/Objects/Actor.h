#pragma once
#ifndef __ACTOR_H__
#define __ACTOR_H__

#include <string>
#include <list>
#include "Object.h"
#include "Component.h"
#include "GlobalMarco.h"


namespace Ailu
{
	class Actor : public Object
	{
		DECLARE_PROTECTED_PROPERTY(Id, Id, uint32_t)
		DECLARE_PROTECTED_PROPERTY_PTR(Parent, Actor)
	public:
		virtual ~Actor();
		Actor();
		virtual void BeginPlay();
		virtual void Tick(const float& delta_time);
		
		void Serialize(std::ofstream& file, String indent) override;
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) override;
		
		template <typename Type>
		static Type* Create();
		template <typename Type>
		static Type* Create(const std::string& name);
		static void RemoveFromRoot(Actor* actor);
		template <typename Type>
		Type* GetComponent();
		template <typename Type>
		inline Type* AddComponent();

		template <typename Type,typename... Args>
		inline Type* AddComponent(Args... args);

		void AddComponent(Component* comp);

		std::list<Scope<Component>>& GetAllComponent() { return _components; }

		void RemoveAllComponent();

		std::list<Actor*>& GetAllChildren();
		void RemoveChild(Actor* child);
		void AddChild(Actor* child);
		uint16_t GetChildNum() const { return _chilren_num; };
		void ClearChildren();
		bool operator==(const Actor& other) const
		{
			return this->_Id == other._Id;
		}
		Actor& operator=(const Actor& other) 
		{
			if (this != &other) 
			{
				_children = other._children;
				_chilren_num = other._chilren_num;
			}
			return *this;
		}
	public:
		inline static std::vector<Scope<Actor>> s_global_actors{};
	protected:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) override;
		std::list<Actor*> _children{};
		std::list<Scope<Component>> _components{};
		inline static uint32_t s_actor_count = 0u;
		uint16_t _chilren_num = 0u;
	};

	template<typename Type>
	inline Type* Actor::Create()
	{
		s_global_actors.emplace_back(MakeScope<Type>());
		s_global_actors.back()->Id(Actor::s_actor_count);
		s_global_actors.back()->Name(std::format("Actor{0}", Actor::s_actor_count++));
		return static_cast<Type*>(s_global_actors.back().get());
	}

	template<typename Type>
	inline Type* Actor::Create(const std::string& name)
	{
		s_global_actors.emplace_back(MakeScope<Type>());
		s_global_actors.back()->Id(Actor::s_actor_count++);
		s_global_actors.back()->Name(name);
		return static_cast<Type*>(s_global_actors.back().get());
	}

	template<typename Type>
	inline Type* Actor::GetComponent()
	{
		for (auto& component : _components)
		{
			if (auto castedComponent = dynamic_cast<Type*>(component.get())) {
				return castedComponent;
			}
		}
		return nullptr;
	}

	template<typename Type>
	inline Type* Actor::AddComponent()
	{
		for (auto& component : _components)
		{
			if (std::is_same<decltype(*component.get()), Type>::value)
				return static_cast<Type*>(component.get());
		}
		_components.emplace_back(std::move(MakeScope<Type>()));
		_components.back()->SetOwner(this);
		return static_cast<Type*>(_components.back().get());
	}

	template<typename Type, typename ...Args>
	inline Type* Actor::AddComponent(Args ...args)
	{
		for (auto& component : _components)
		{
			if (std::is_same<decltype(*component.get()), Type>::value)
				return static_cast<Type*>(component.get());
		}
		_components.emplace_back(std::move(MakeScope<Type>(args...)));
		_components.back()->SetOwner(this);
		return static_cast<Type*>(_components.back().get());
	}
}


#endif // !ACTOR_H__

