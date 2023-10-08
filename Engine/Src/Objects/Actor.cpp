#include "pch.h"
#include "Objects/Actor.h"

namespace Ailu
{
	Actor::~Actor()
	{
		for (auto& component : _components)
		{
			component->Destroy();
		}
		ClearChildren();
		if (_Parent != nullptr)
			_Parent->RemoveChild(this);
	}


	Actor::Actor()
	{
		_Parent = nullptr;
	}

	void Actor::BeginPlay()
	{
		for (auto& component : _components)
		{
			component->BeginPlay();
		}
	}

	void Actor::Tick(const float& delta_time)
	{
		for (auto& component : _components)
		{
			component->Tick(delta_time);
		}
	}

	void Actor::RemoveFromRoot(Actor* actor)
	{
		for (auto it = s_global_actors.begin(); it != s_global_actors.end(); it++)
		{
			if (*(*it) == *actor)
			{
				s_global_actors.erase(it);
				return;
			}
		}
	}


	std::list<Actor*>& Actor::GetAllChildren()
	{
		return _children;
	}

	void Actor::RemoveChild(Actor* child)
	{
		--_chilren_num;
		_children.remove(child);
		//DESTORY_PTR(child);
	}

	void Actor::AddChild(Actor* child)
	{
		++_chilren_num;
		_children.emplace_back(child);
	}

	void Actor::ClearChildren()
	{
		_chilren_num = 0;
		//for (auto e : _children)
		//{
		//	DESTORY_PTR(e);
		//}
		_children.clear();
	}
}
