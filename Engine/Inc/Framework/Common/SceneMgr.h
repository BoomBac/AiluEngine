#pragma once
#ifndef __SCENE_MGR_H__
#define __SCENE_MGR_H__
#include <vector>
#include "Framework/Interface/IRuntimeModule.h"
#include "Objects/SceneActor.h"
#include "Objects/LightComponent.h"

namespace Ailu
{
	class Scene
	{
		using ActorEvent = std::function<void(SceneActor*)>;
	public:
		Scene(const std::string& name);
		void AddObject(SceneActor* actor);
		std::list<SceneActor*>& GetAllActor();
		SceneActor* GetSceneRoot() { return _p_root; }
		SceneActor* GetSceneActorByID(const uint32_t& id);
		SceneActor* GetSceneActorByIndex(const uint32_t& index);
		static Scene* GetDefaultScene();
		std::list<LightComponent*>& GetAllLight();
		DECLARE_PRIVATE_PROPERTY(b_dirty, Dirty, bool)
		DECLARE_PRIVATE_PROPERTY(name, Name, std::string)
	private:
		SceneActor* _p_root;
		std::list<SceneActor*> _all_objects{};
		std::list<LightComponent*> _all_lights{};
		void TravelAllActor(SceneActor* actor, ActorEvent& e);
		ActorEvent FillActorList;
	};
	class SceneMgr : public IRuntimeModule
	{
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick(const float& delta_time) final;
		static Scene* Create(const std::string& name);
		//inline Scene* GetCurrentScene() { return _p_current.get(); };
		Scene* _p_current = nullptr;
	private:

		inline static std::list<Scope<Scene>> s_all_scene{};
		inline static uint16_t s_scene_index = 0u;
	};
	extern SceneMgr* g_pSceneMgr;
}


#endif // !SCENE_MGR_H__

