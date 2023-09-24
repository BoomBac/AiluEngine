#pragma once
#ifndef __ACTOR_H__
#define __ACTOR_H__

#include <string>
#include <list>
#include "Component.h"
#include "GlobalMarco.h"


namespace Ailu
{
	class Actor : public std::enable_shared_from_this<Actor>
	{
	public:
		virtual ~Actor();
		Actor();
		virtual void BeginPlay();
		virtual void Tick();
		
		template <typename Type>
		static Ref<Type> Create();
		template <typename Type>
		Type* GetComponent();
		template <typename Type>
		void AddComponent();


		const std::list<Ref<Actor>>& GetAllChildren() const;
		void RemoveChild(Ref<Actor> child);
		void AddChild(Ref<Actor> child);
		void ClearChildren();

		DECLARE__PROTECTED_PROPERTY(Name,std::string)
		DECLARE__PROTECTED_PROPERTY(Id,uint32_t)
		DECLARE__PROTECTED_PROPERTY_PTR(Parent,Actor)
	public:
		inline static std::vector<Ref<Actor>> s_global_actors{};
	protected:
		virtual void Destroy();
		void RemoveFromRoot() const;	
	protected:
		std::list<Ref<Actor>> _children{};
		std::list<Scope<Component>> _components{};
		inline static uint32_t s_actor_count = 0u;

	};

	template<typename Type>
	inline Ref<Type> Actor::Create()
	{
		auto actor = MakeRef<Type>();
		actor->Id(Actor::s_actor_count);
		actor->Name(std::format("Actor{0}", Actor::s_actor_count++));
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
	inline void Actor::AddComponent()
	{
		for (auto& component : _components)
		{
			if (std::is_same<decltype(*component.get()), Type>::value)
				return;
		}
		_components.emplace_back(std::move(MakeScope<Type>()));
	}
}


#endif // !ACTOR_H__

