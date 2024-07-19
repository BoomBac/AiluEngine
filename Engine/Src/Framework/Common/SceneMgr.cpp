#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/LogMgr.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Objects/CameraComponent.h"
#include "Objects/CommonActor.h"
#include "Objects/StaticMeshComponent.h"
#include "Render/Camera.h"
#include "Render/RenderQueue.h"
#include "pch.h"


namespace Ailu
{
    int SceneMgr::Initialize()
    {
        return 0;
    }
    void SceneMgr::Finalize()
    {
    }

    void SceneMgr::Tick(const float &delta_time)
    {
        if (_p_current)
        {
            while (!_pending_delete_actors.empty())
            {
                auto actor = _pending_delete_actors.front();
                _pending_delete_actors.pop();
                _p_current->RemoveObject(actor);
            }
            for (auto &actor: _p_current->GetAllActor())
            {
                actor->Tick(delta_time);
            }
            {
                CPUProfileBlock b("CameraCull");
                Camera *active_cam = Camera::sSelected ? Camera::sSelected : Camera::sCurrent;
                active_cam->Cull(_p_current);
            }
        }
    }

    Ref<Scene> SceneMgr::Create(const WString &name, bool empty)
    {
        auto scene_root = Actor::Create<SceneActor>("root");
        if (!empty)
        {
            auto _p_actor = Actor::Create<SceneActor>("stone");
            _p_actor->AddComponent<StaticMeshComponent>(g_pResourceMgr->GetRef<Mesh>(L"Meshs/cube.alasset"), g_pResourceMgr->GetRef<Material>(L"Materials/StandardPBR.alasset"));
            auto _p_light = Actor::Create<LightActor>("directional_light");
            SceneActor *point_light = Actor::Create<LightActor>("point_light");
            point_light->GetComponent<LightComponent>()->LightType(ELightType::kPoint);
            point_light->GetComponent<LightComponent>()->_light._light_param.x = 500.0f;

            SceneActor *spot_light = Actor::Create<LightActor>("spot_light");
            auto transf_comp = spot_light->GetTransformComponent();
            transf_comp->SetPosition({0.0, 500.0, 0.0});
            transf_comp->SetRotation(Quaternion());
            auto light_comp = spot_light->GetComponent<LightComponent>();
            light_comp->LightType(ELightType::kSpot);
            light_comp->_light._light_param.x = 500.0f;
            light_comp->_light._light_param.y = 45.0f;
            light_comp->_light._light_param.z = 60.0f;
            scene_root->AddChild(_p_actor);
            scene_root->AddChild(_p_light);
            scene_root->AddChild(point_light);
            scene_root->AddChild(spot_light);
        }
        auto scene = MakeRef<Scene>(ToChar(name));
        scene->Root(scene_root);
        s_all_scene.push_back(scene);
        return s_all_scene.back();
    }

    SceneActor *SceneMgr::AddSceneActor(std::string_view name, Ref<Mesh> mesh)
    {
        auto p_actor = Actor::Create<SceneActor>(name.data());
        p_actor->AddComponent<StaticMeshComponent>(mesh, g_pResourceMgr->GetRef<Material>(L"Materials/StandardPBR.alasset"));
        _p_current->AddObject(p_actor);
        return p_actor;
    }

    SceneActor *SceneMgr::AddSceneActor(std::string_view name, const Camera &camera)
    {
        auto p_actor = Actor::Create<SceneActor>(name.data());
        p_actor->AddComponent<CameraComponent>(camera);
        _p_current->AddObject(p_actor);
        return p_actor;
    }

    SceneActor *SceneMgr::AddSceneActor()
    {
        auto p_actor = Actor::Create<SceneActor>();
        _p_current->AddObject(p_actor);
        return p_actor;
    }

    SceneActor *SceneMgr::AddSceneActor(Ref<Mesh> mesh)
    {
        auto p_actor = Actor::Create<SceneActor>();
        p_actor->AddComponent<StaticMeshComponent>(mesh, g_pResourceMgr->GetRef<Material>(L"Materials/StandardPBR.alasset"));
        _p_current->AddObject(p_actor);
        return p_actor;
    }

    void SceneMgr::DeleteSceneActor(SceneActor *actor)
    {
        _pending_delete_actors.push(actor);
    }

    Scene *SceneMgr::OpenScene(const WString &scene_path)
    {
        auto new_scene = g_pResourceMgr->Get<Scene>(scene_path);
        if (new_scene == nullptr)
        {
            new_scene = g_pResourceMgr->Load<Scene>(scene_path);
        }
        if (_p_current != new_scene)
            _p_current = new_scene;
        _p_current->MarkDirty();
        return _p_current;
    }
    void SceneMgr::OnSceneActorEvent(ActorEvent &e)
    {
        if (_p_current)
        {
            for (auto &actor: _p_current->GetAllActor())
            {
                e(actor);
            }
        }
    }

    //--------------------------------------------------------Scene begin-----------------------------------------------------------------------

    Scene::Scene(const std::string &name)
    {
        //_p_root = Actor::Create<SceneActor>(name);
        _name = name;
        _all_static_renderalbes.reserve(RenderConstants::kMaxRenderObjectCount);
        FillActorList = [this](SceneActor *actor)
        {
            _all_objects.emplace_back(actor);
            for (auto &comp: actor->GetAllComponent())
            {
                _all_comps.push_back(comp.get());
            }
            auto static_mesh = actor->GetComponent<StaticMeshComponent>();
            if (static_mesh != nullptr && static_mesh->Active())
            {
                u16 submesh_count = static_mesh->GetMesh()->SubmeshCount();
                if (_total_renderable_count + submesh_count <= RenderConstants::kMaxRenderObjectCount)
                {
                    _all_static_renderalbes.emplace_back(static_mesh);
                    _total_renderable_count += submesh_count;
                }
                else
                {
                    g_pLogMgr->LogWarningFormat("Scene: {} has much render object than limit {}", _name, RenderConstants::kMaxRenderObjectCount);
                }
            }
            auto light = actor->GetComponent<LightComponent>();
            if (light != nullptr)
            {
                _all_lights.emplace_back(light);
            }
        };
    }

    void Scene::AddObject(SceneActor *actor)
    {
        auto p = static_cast<Actor *>(actor);
        _p_root->AddChild(p);
        _b_dirty = true;
    }

    void Scene::RemoveObject(SceneActor *actor)
    {
        auto p = static_cast<Actor *>(actor);
        _p_root->RemoveChild(p);
        _b_dirty = true;
    }


    std::list<SceneActor *> &Scene::GetAllActor()
    {
        if (_b_dirty)
        {
            Clear();
            TravelAllActor(_p_root, FillActorList);
            _b_dirty = false;
        }
        return _all_objects;
    }

    SceneActor *Scene::GetSceneActorByID(const u32 &id)
    {
        for (auto &actor: _all_objects)
        {
            if (actor->Id() == id) return actor;
        }
        LOG_WARNING("Can't find actor with id: {} in scene {},will return first actor", id, _name);
        return _all_objects.front();
    }

    SceneActor *Scene::GetSceneActorByIndex(const u32 &index)
    {
        if (index >= _all_objects.size())
        {
            return _all_objects.front();
            LOG_WARNING("Can't find actor with index: {} in scene {},will return first actor", index, _name);
        }
        else
        {
            int cur_index = 0;
            for (auto it = _all_objects.begin(); it != _all_objects.end(); it++)
            {
                if (cur_index == index) return *it;
                else
                    ++cur_index;
            }
        }
        return _all_objects.front();
    }

    void Scene::MarkDirty()
    {
        _b_dirty = true;
    }

    std::list<LightComponent *> &Scene::GetAllLight()
    {
        return _all_lights;
    }

    void Scene::TravelAllActor(SceneActor *actor, ActorEvent &e)
    {
        e(actor);
        if (actor->GetChildNum() > 0)
        {
            for (auto &child: actor->GetAllChildren())
            {
                TravelAllActor(static_cast<SceneActor *>(child), e);
            }
        }
    }
    void Scene::Clear()
    {
        _all_objects.clear();
        _all_lights.clear();
        _all_comps.clear();
        _all_static_renderalbes.clear();
        _total_renderable_count = 0;
    }
}// namespace Ailu
