#pragma once
#ifndef __SCENE_MGR_H__
#define __SCENE_MGR_H__
#include <vector>
#include "Framework/Interface/IRuntimeModule.h"
#include "Objects/SceneActor.h"

namespace Ailu
{
	class Scene
	{
		using ActorEvent = std::function<void(Ref<SceneActor>&)>;
	public:
		Scene(const std::string& name);
		void AddObject(Ref<SceneActor>& actor);
		std::list<Ref<SceneActor>>& GetAllActor();
		Ref<SceneActor>& GetSceneRoot() { return _p_root; }
		Ref<SceneActor>& GetSceneActorByID(const uint32_t& id);
		DECLARE_PRIVATE_PROPERTY(b_dirty, Dirty, bool)
		DECLARE_PRIVATE_PROPERTY(name, Name, std::string)
	private:
		Ref<SceneActor> _p_root;
		std::list<Ref<SceneActor>> _all_objects{};
		void TravelAllActor(Ref<SceneActor>& actor, ActorEvent& e);
		ActorEvent FillActorList;
	};
	class SceneMgr : public IRuntimeModule
	{
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick() final;
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

