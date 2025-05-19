#pragma once
#ifndef __SCENE_H__
#define __SCENE_H__
#include "Component.h"
#include "Entity.hpp"
#include "Framework/Math/Geometry.h"
#include "GlobalMarco.h"
#include "Objects/Serialize.h"


namespace Ailu
{
    namespace SceneManagement
    {
        struct LightingData
        {
            f32 _indirect_lighting_intensity = 0.25f;
        };

        class AILU_API Scene final : public Object, public IPersistentable
        {
            friend class SceneMgr;
        public:
            void Serialize(Archive &arch) final;
            void Deserialize(Archive &arch) final;
            Scene(const String &name);
            ECS::Entity AddObject(String name = "");
            ECS::Entity AddObject(Ref<Mesh> mesh, Ref<Material> mat);
            ECS::Entity DuplicateEntity(ECS::Entity e);
            void RemoveObject(ECS::Entity entity);
            void Attach(ECS::Entity current, ECS::Entity parent);
            void Detach(ECS::Entity current);
            void MarkDirty() { _dirty = true; };
            const Vector<ECS::Entity> &EntityView() const;
            ECS::Entity Pick(const Ray &ray);
            LightingData _light_data;
            auto GetAllStaticRenderable() const { return _register.View<ECS::StaticMeshComponent>(); };
            auto GetAllSkinedRenderable() const { return _register.View<ECS::CSkeletonMesh>(); };
            const ECS::Register &GetRegister() const { return _register; }
            ECS::Register &GetRegister() { return _register; }
            u32 EntityNum() const { return _register.EntityNum(); }

            String AcquireName() const { return std::format("new_object_{}", _register.EntityNum()); };

        private:
            void DeletePendingEntities();
        private:
            bool _dirty = true;
            u16 _total_renderable_count = 0u;
            ECS::Register _register;
            Queue<ECS::Entity> _pending_delete_entities;

        private:
            void Clear();
        };

        class AILU_API SceneMgr : public IRuntimeModule
        {
        public:
            DISALLOW_COPY_AND_ASSIGN(SceneMgr)
            SceneMgr() = default;
            int Initialize();
            void Finalize();
            void Tick(f32 delta_time);

            void MarkCurSceneDirty() { _p_current->MarkDirty(); };
            Ref<Scene> Create(String name);
            Ref<Scene> OpenScene(const WString &scene_path);
            Scene *ActiveScene() { return _p_current; }
            void EnterPlayMode();
            void ExitPlayMode();
            void EnterSimulateMode();
            void ExitSimulateMode();
        private:
            Map<WString, Ref<Scene>> _all_scene;
            u16 _scene_index = 0u;
            Scene *_p_current = nullptr;
            Scene *_runtime_scene = nullptr;
            Scene *_runtime_scene_src = nullptr;
            Vector<Transform> _transform_cache;
        };
    }
    extern AILU_API SceneManagement::SceneMgr *g_pSceneMgr;
}// namespace Ailu
#endif// !SCENE_H__
