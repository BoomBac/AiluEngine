#include "pch.h"
#include "Objects/Actor.h"

namespace Ailu
{
	Ailu::Actor::~Actor()
	{
		Destroy();
	}

	void Actor::RemoveFromRoot() const
	{
		for (auto it = s_global_actors.begin(); it != s_global_actors.end(); it++)
		{
			if (it->get()->Id() == this->_Id)
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

	void Actor::Tick()
	{
		for (auto& component : _components)
		{
			component->Tick();
		}
	}

	std::list<Ref<Actor>>& Actor::GetAllChildren()
	{
		return _children;
	}

	void Actor::RemoveChild(Ref<Actor> child)
	{
		--_chilren_num;
		_children.remove(child);
	}

	void Actor::AddChild(Ref<Actor>& child)
	{
		++_chilren_num;
		_children.emplace_back(child);
	}

	void Actor::ClearChildren()
	{
		_chilren_num = 0;
		_children.clear();
	}

	void Actor::Destroy()
	{
		for (auto& component : _components)
		{
			component->Destroy();
		}
		ClearChildren();
		if(_Parent!=nullptr)
			_Parent->RemoveChild(shared_from_this());
	}
}
