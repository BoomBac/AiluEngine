#include "pch.h"
#include "Framework/Common/SceneMgr.h"

namespace Ailu
{
	int SceneMgr::Initialize()
	{
		return 0;
	}
	void SceneMgr::Finalize()
	{
	}
	void SceneMgr::Tick()
	{
		if (_p_current)
		{
			for (auto& actor : _p_current->GetAllActor())
			{
				actor->Tick(16.6f);
			}
		}
	}

	Scene* SceneMgr::Create(const std::string& name)
	{
		s_all_scene.emplace_back(std::move(MakeScope<Scene>(name)));
		return s_all_scene.back().get();
	}

	//--------------------------------------------------------Scene begin-----------------------------------------------------------------------

	Scene::Scene(const std::string& name)
	{
		_p_root = Actor::Create<SceneActor>(name);
		_name = name;
		FillActorList = [this](Ref<SceneActor>& actor) {
			_all_objects.emplace_back(actor); 
			auto light = actor->GetComponent<LightComponent>();
			if (light != nullptr)
			{
				_all_lights.emplace_back(light);
			}
			};
	}

	void Scene::AddObject(Ref<SceneActor>& actor)
	{
		auto p = std::static_pointer_cast<Actor>(actor);
		_p_root->AddChild(p);
		_b_dirty = true;
	}

	std::list<Ref<SceneActor>>& Scene::GetAllActor()
	{
		if (_b_dirty)
		{
			_all_objects.clear();
			_all_lights.clear();
			TravelAllActor(_p_root,FillActorList);
			_b_dirty = false;
		}
		return _all_objects;
	}

	Ref<SceneActor>& Scene::GetSceneActorByID(const uint32_t& id)
	{
		for (auto& actor : _all_objects)
		{
			if (actor->Id() == id) return actor;
		}
		LOG_WARNING("Can't find actor with id: {} in scene {},will return first actor",id,_name);
		return _all_objects.front();
	}

	Ref<SceneActor>& Scene::GetSceneActorByIndex(const uint32_t& index)
	{
		if (index >= _all_objects.size())
		{
			return _all_objects.front();
			LOG_WARNING("Can't find actor with index: {} in scene {},will return first actor",index,_name);
		}
		else
		{
			int cur_index = 0;
			for (auto it = _all_objects.begin(); it!= _all_objects.end();it++)
			{
				if (cur_index == index) return *it;
				else ++cur_index;
			}
		}
	}

	std::list<LightComponent*>& Scene::GetAllLight()
	{
		return _all_lights;
	}

	void Scene::TravelAllActor(Ref<SceneActor>& actor,ActorEvent& e)
	{
		e(actor);
		LOG_INFO("{}", actor->Name());
		if (actor->GetChildNum() > 0)
		{
			for (auto& child : actor->GetAllChildren())
			{
				auto p = std::static_pointer_cast<SceneActor>(child);
				TravelAllActor(p,e);
			}
		}
	}
}
