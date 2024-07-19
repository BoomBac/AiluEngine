#pragma warning(push)
#pragma warning(disable: 4251)
#pragma once
#ifndef __SCENE_MGR_H__
#define __SCENE_MGR_H__
#include <queue>

#include "Framework/Interface/IRuntimeModule.h"
#include "Objects/SceneActor.h"
#include "Objects/LightComponent.h"
#include "Objects/StaticMeshComponent.h"
#include "Render/Camera.h"

namespace Ailu
{
	struct LightingData
	{
		f32 _indirect_lighting_intensity = 0.25f;
	};
    using ActorEvent = std::function<void(SceneActor*)>;
	class AILU_API Scene : public Object
	{
		DECLARE_PRIVATE_PROPERTY_PTR(p_root,Root,SceneActor)
		DECLARE_PRIVATE_PROPERTY(b_dirty, Dirty, bool)
	public:
		Scene(const std::string& name);
		void AddObject(SceneActor* actor);
		void RemoveObject(SceneActor* actor);
		std::list<SceneActor*>& GetAllActor();
		SceneActor* GetSceneRoot() { return _p_root; }
		SceneActor* GetSceneActorByID(const u32& id);
		SceneActor* GetSceneActorByIndex(const u32& index);
		void MarkDirty();
		std::list<LightComponent*>& GetAllLight();
		Vector<StaticMeshComponent*>& GetAllStaticRenderable() {return _all_static_renderalbes;};
		//world_pos:gizmo type
        Vector<Component*> GetAllComponents() {return _all_comps;}
		LightingData _light_data;
	private:
		u16 _total_renderable_count = 0u;
		std::list<SceneActor*> _all_objects{};
		std::list<LightComponent*> _all_lights{};
		Vector<StaticMeshComponent*> _all_static_renderalbes{};
		Vector<Component*> _all_comps;
		ActorEvent FillActorList;
    private:
		void TravelAllActor(SceneActor* actor, ActorEvent& e);
        void Clear();
	};

	class AILU_API SceneMgr// : public IRuntimeModule
	{
	public:
		int Initialize();
		void Finalize();
		void Tick(const float& delta_time);
		static Ref<Scene> Create(const WString& name,bool empty = false);
		SceneActor* AddSceneActor(std::string_view name,Ref<Mesh> mesh);
		SceneActor* AddSceneActor(std::string_view name,const Camera& camera);
		SceneActor* AddSceneActor();
		SceneActor* AddSceneActor(Ref<Mesh> mesh);
		void MarkCurSceneDirty() { _p_current->MarkDirty(); };
		void DeleteSceneActor(SceneActor* actor);
		//void SaveScene(Scene* scene, const String& scene_path);
		Scene* OpenScene(const WString& scene_path);
		Scene* _p_current = nullptr;
		SceneActor* _p_selected_actor = nullptr;
        void OnSceneActorEvent(ActorEvent& e);
	private:
		inline static std::list<Ref<Scene>> s_all_scene{};
		inline static uint16_t s_scene_index = 0u;
		std::queue<SceneActor*> _pending_delete_actors;
	};
	extern AILU_API SceneMgr* g_pSceneMgr;
}
#endif // !SCENE_MGR_H__
#pragma warning(pop)