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
				actor->Tick();
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
		FillActorList = [this](Ref<SceneActor>& actor) {_all_objects.emplace_back(actor); };
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
