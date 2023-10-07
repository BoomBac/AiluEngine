#pragma once
#ifndef __ACTOR_H__
#define __ACTOR_H__

#include <string>
#include <list>
#include "Component.h"
#include "GlobalMarco.h"


namespace Ailu
{
	class Actor
	{
	public:
		virtual ~Actor();
		Actor();
		virtual void BeginPlay();
		virtual void Tick(const float& delta_time);
		
		template <typename Type>
		static Type* Create();
		template <typename Type>
		static Type* Create(const std::string& name);
		template <typename Type>
		Type* GetComponent();
		template <typename Type>
		inline Type* AddComponent();

		template <typename Type,typename... Args>
		inline Type* AddComponent(Args... args);

		std::list<Scope<Component>>& GetAllComponent() { return _components; }

		std::list<Actor*>& GetAllChildren();
		void RemoveChild(Actor* child);
		void AddChild(Actor* child);
		uint16_t GetChildNum() const { return _chilren_num; };
		void ClearChildren();

		DECLARE_PROTECTED_PROPERTY(name, Name, std::string)
		DECLARE_PROTECTED_PROPERTY(Id,Id,uint32_t)
		DECLARE_PROTECTED_PROPERTY_PTR(Parent,Actor)
	public:
		inline static std::vector<Actor*> s_global_actors{};
	protected:
		void RemoveFromRoot() const;	
	protected:
		std::list<Actor*> _children{};
		std::list<Scope<Component>> _components{};
		inline static uint32_t s_actor_count = 0u;
		uint16_t _chilren_num = 0u;
	};

	template<typename Type>
	inline Type* Actor::Create()
	{
		auto actor = new Type();
		actor->Id(Actor::s_actor_count);
		actor->Name(std::format("Actor{0}", Actor::s_actor_count++));
		s_global_actors.emplace_back(actor);
		return actor;
	}

	template<typename Type>
	inline Type* Actor::Create(const std::string& name)
	{
		auto actor = new Type();
		actor->Id(Actor::s_actor_count);
		actor->Name(name);
		++s_actor_count;
		s_global_actors.emplace_back(actor);
		return actor;
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

