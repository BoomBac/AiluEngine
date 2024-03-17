#pragma warning(push)
#pragma warning(disable: 4251)
#pragma once
#ifndef __SCENE_MGR_H__
#define __SCENE_MGR_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "Objects/SceneActor.h"
#include "Objects/LightComponent.h"
#include "Objects/StaticMeshComponent.h"
#include "Render/Camera.h"

namespace Ailu
{
	class AILU_API Scene
	{
		using ActorEvent = std::function<void(SceneActor*)>;
		DECLARE_PRIVATE_PROPERTY_PTR(p_root,Root,SceneActor)
		DECLARE_PRIVATE_PROPERTY(b_dirty, Dirty, bool)
		DECLARE_PRIVATE_PROPERTY(name, Name, std::string)
	public:
		inline static u32 kMaxStaticRenderableNum = 500;
		Scene(const std::string& name);
		void AddObject(SceneActor* actor);
		void RemoveObject(SceneActor* actor);
		std::list<SceneActor*>& GetAllActor();
		SceneActor* GetSceneRoot() { return _p_root; }
		SceneActor* GetSceneActorByID(const u32& id);
		SceneActor* GetSceneActorByIndex(const u32& index);
		void MarkDirty();
		Camera* GetActiveCamera();
		static Scene* GetDefaultScene();
		std::list<LightComponent*>& GetAllLight();
		Vector<StaticMeshComponent*>& GetAllStaticRenderable() {return _all_static_renderalbes;};
	private:
		Camera _scene_cam;
		std::list<SceneActor*> _all_objects{};
		std::list<LightComponent*> _all_lights{};
		Vector<StaticMeshComponent*> _all_static_renderalbes{};
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
		SceneActor* AddSceneActor();
		void MarkCurSceneDirty() { _p_current->MarkDirty(); };
		void DeleteSceneActor(SceneActor* actor);
		void SaveScene(Scene* scene, const String& scene_path);
		Scene* LoadScene(const String& scene_path);
		Scene* _p_current = nullptr;
		SceneActor* _p_selected_actor = nullptr;
	private:
		void Cull(Camera* cam,Scene* p_scene);
		inline static std::list<Scope<Scene>> s_all_scene{};
		inline static uint16_t s_scene_index = 0u;
		Queue<SceneActor*> _pending_delete_actors;
	};
	extern AILU_API SceneMgr* g_pSceneMgr;
}
#endif // !SCENE_MGR_H__
#pragma warning(pop)