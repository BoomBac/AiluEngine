#include "pch.h"
#include "Framework/Common/SceneMgr.h"
#include "Objects/CommonActor.h"
#include "Objects/StaticMeshComponent.h"

namespace Ailu
{
	int SceneMgr::Initialize()
	{
		g_pSceneMgr->_p_current = Scene::GetDefaultScene();
		return 0;
	}
	void SceneMgr::Finalize()
	{
	}
	void SceneMgr::Tick(const float& delta_time)
	{
		if (_p_current)
		{
			for (auto& actor : _p_current->GetAllActor())
			{
				actor->Tick(delta_time);
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
		FillActorList = [this](SceneActor* actor) {
			_all_objects.emplace_back(actor); 
			auto light = actor->GetComponent<LightComponent>();
			if (light != nullptr)
			{
				_all_lights.emplace_back(light);
			}
			};
	}

	void Scene::AddObject(SceneActor* actor)
	{
		auto p = static_cast<Actor*>(actor);
		_p_root->AddChild(p);
		_b_dirty = true;
	}


	std::list<SceneActor*>& Scene::GetAllActor()
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

	SceneActor* Scene::GetSceneActorByID(const uint32_t& id)
	{
		for (auto& actor : _all_objects)
		{
			if (actor->Id() == id) return actor;
		}
		LOG_WARNING("Can't find actor with id: {} in scene {},will return first actor",id,_name);
		return _all_objects.front();
	}

	SceneActor* Scene::GetSceneActorByIndex(const uint32_t& index)
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

	Scene* Scene::GetDefaultScene()
	{
		auto _p_actor = Actor::Create<SceneActor>("stone");
		_p_actor->AddComponent<StaticMeshComponent>(MeshPool::GetMesh("Sphere"), MaterialPool::GetMaterial("StandardPBR"));
		auto _p_light = Actor::Create<LightActor>("directional_light");
		SceneActor* point_light = Actor::Create<LightActor>("point_light");
		point_light->GetComponent<LightComponent>()->_light_type = ELightType::kPoint;
		point_light->GetComponent<LightComponent>()->_light._light_param.x = 500.0f;

		SceneActor* spot_light = Actor::Create<LightActor>("spot_light");
		spot_light->GetTransform().Position({ 0.0,500.0,0.0 });
		spot_light->GetTransform().Rotation({ 82.0,0.0,0.0 });
		auto light_comp = spot_light->GetComponent<LightComponent>();
		light_comp->_light_type = ELightType::kSpot;
		light_comp->_light._light_param.x = 500.0f;
		light_comp->_light._light_param.y = 45.0f;
		light_comp->_light._light_param.z = 60.0f;

		auto scene = SceneMgr::Create("default_scene");
		scene->AddObject(_p_actor);
		scene->AddObject(_p_light);
		scene->AddObject(point_light);
		scene->AddObject(spot_light);
		return scene;
	}

	std::list<LightComponent*>& Scene::GetAllLight()
	{
		return _all_lights;
	}

	void Scene::TravelAllActor(SceneActor* actor, ActorEvent& e)
	{
		e(actor);
		LOG_INFO("{}", actor->Name());
		if (actor->GetChildNum() > 0)
		{
			for (auto& child : actor->GetAllChildren())
			{
				TravelAllActor(static_cast<SceneActor*>(child), e);
			}
		}
	}
}
