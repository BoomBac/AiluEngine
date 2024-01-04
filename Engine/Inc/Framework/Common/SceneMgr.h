#pragma warning(push)
#pragma warning(disable: 4251)
#pragma once
#ifndef __SCENE_MGR_H__
#define __SCENE_MGR_H__
#include <queue>
#include <vector>
#include "Framework/Interface/IRuntimeModule.h"
#include "Objects/SceneActor.h"
#include "Objects/LightComponent.h"
#include "Render/Camera.h"

namespace Ailu
{
	class AILU_API Scene
	{
		using ActorEvent = std::function<void(SceneActor*)>;
		DECLARE_PRIVATE_PROPERTY_PTR(p_root,Root,SceneActor)
	public:
		Scene(const std::string& name);
		void AddObject(SceneActor* actor);
		void RemoveObject(SceneActor* actor);
		std::list<SceneActor*>& GetAllActor();
		SceneActor* GetSceneRoot() { return _p_root; }
		SceneActor* GetSceneActorByID(const uint32_t& id);
		SceneActor* GetSceneActorByIndex(const uint32_t& index);
		void MarkDirty();
		Camera* GetActiveCamera();
		static Scene* GetDefaultScene();
		std::list<LightComponent*>& GetAllLight();
		DECLARE_PRIVATE_PROPERTY(b_dirty, Dirty, bool)
		DECLARE_PRIVATE_PROPERTY(name, Name, std::string)
	private:
		Camera _scene_cam;
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
		SceneActor* AddSceneActor(std::string_view name,std::string_view mesh);
		SceneActor* AddSceneActor(std::string_view name,const Camera& camera);
		void DeleteSceneActor(SceneActor* actor);
		void SaveScene(Scene* scene, const String& scene_path);
		Scene* LoadScene(const String& scene_path);
		//inline Scene* GetCurrentScene() { return _p_current.get(); };
		Scene* _p_current = nullptr;
	private:
		inline static std::list<Scope<Scene>> s_all_scene{};
		inline static uint16_t s_scene_index = 0u;
		Queue<SceneActor*> _pending_delete_actors;
	};
	extern AILU_API SceneMgr* g_pSceneMgr;
}
#endif // !SCENE_MGR_H__
#pragma warning(pop)