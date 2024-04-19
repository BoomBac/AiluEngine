#include "pch.h"
#include "Objects/Actor.h"
#include "Framework/Common/LogMgr.h"

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
		LOG_WARNING("Destory actor {}", _name);
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


	void Actor::AddComponent(Component* comp)
	{
		for (auto& component : _components)
		{
			if (component->GetType() == comp->GetType()) return;
		}
		Scope<Component> ptr(comp);
		_components.emplace_back(std::move(ptr));
		_components.back()->SetOwner(this);
	}

	void Actor::RemoveComponent(Component* comp)
	{
		_components.remove_if([&](const Scope<Component>& comp_ptr)->bool {return comp_ptr->GetType() == comp->GetType(); });
	}

	void Actor::RemoveAllComponent()
	{
		_components.clear();
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

	void Actor::SetParent(Actor* new_parent)
	{
		_Parent = new_parent;
	}

	void Actor::ClearChildren()
	{
		for (auto c : _children)
		{
			c->SetParent(nullptr);
		}
		_chilren_num = 0;
		_children.clear();
	}

	void Actor::Serialize(std::ostream& os, String indent)
	{
		using namespace std;
		os << indent << "Name: " << _name << endl;
		auto second_indent = std::format("{}{}", indent,"  ");
		auto third_indent = std::format("{}{}", second_indent, "  ");
		os << second_indent << "Components: " << endl;
		for (const auto& comp : _components)
		{
			comp->Serialize(os, third_indent);
		}
		os << second_indent << "Children: " << _chilren_num << endl;
		for (const auto& child : _children)
		{
			child->Serialize(os, third_indent);
		}
	}

	void* Actor::DeserializeImpl(Queue<std::tuple<String, String>>& formated_str)
	{
		return nullptr;
	}
}
