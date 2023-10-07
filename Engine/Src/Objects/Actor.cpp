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

	void Actor::RemoveFromRoot() const
	{
		for (auto it = s_global_actors.begin(); it != s_global_actors.end(); it++)
		{
			if ((*it)->_Id == this->_Id)
			{
				s_global_actors.erase(it);
				return;
			}
		}
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


	std::list<Actor*>& Actor::GetAllChildren()
	{
		return _children;
	}

	void Actor::RemoveChild(Actor* child)
	{
		--_chilren_num;
		_children.remove(child);
	}

	void Actor::AddChild(Actor* child)
	{
		++_chilren_num;
		_children.emplace_back(child);
	}

	void Actor::ClearChildren()
	{
		_chilren_num = 0;
		_children.clear();
	}
}
