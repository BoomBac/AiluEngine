#include "Scene/Scene.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/RenderSystem.h"
#include "pch.h"

namespace Ailu
{
    void Scene::Serialize(Archive &arch)
    {
        u64 index = 0;
        auto &os = arch.GetOStream();
        arch << "scene_register:";
        arch.NewLine();
        arch.IncreaseIndent();
        for (auto &tag_comp: _register.View<TagComponent>())
        {
            ECS::Entity e = _register.GetEntity<TagComponent>(index++);
            arch << std::format("Entity:{}", e);
            arch.IncreaseIndent();
            arch.NewLine();
            {
                arch.InsertIndent();
                arch << "_tag_component:";
                arch.NewLine();
                arch << tag_comp;
            }
            if (auto c = _register.GetComponent<TransformComponent>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_transform_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<StaticMeshComponent>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_static_mesh_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<LightComponent>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_light_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<CHierarchy>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_hierarchy_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<CCamera>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_camera_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<CLightProbe>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_lightprobe_component:";
                arch.NewLine();
                arch << (*c);
            }
            arch.DecreaseIndent();
        }
    }
    void Scene::Deserialize(Archive &arch)
    {
        auto &is = arch.GetIStream();
        ECS::Entity e = ECS::kInvalidEntity;
        std::string line;
        do
        {
            std::getline(is, line);
        } while (line != "scene_register:");
        AL_ASSERT(line == "scene_register:");
        while (is.good())
        {
            std::getline(is, line);
            if (line.empty())
                continue;
            if (line.find("Entity:") != std::string::npos)
            {
                e = _register.Create();
                std::getline(is, line);
            }
            if (su::BeginWith(line, "_tag_component"))
            {
                arch >> _register.AddComponent<TagComponent>(e);
            }
            else if (su::BeginWith(line, "_transform_component"))
            {
                arch >> _register.AddComponent<TransformComponent>(e);
            }
            else if (su::BeginWith(line, "_static_mesh_component"))
            {
                arch >> _register.AddComponent<StaticMeshComponent>(e);
            }
            else if (su::BeginWith(line, "_light_component"))
            {
                arch >> _register.AddComponent<LightComponent>(e);
            }
            else if (su::BeginWith(line, "_hierarchy_component"))
            {
                arch >> _register.AddComponent<CHierarchy>(e);
            }
            else if (su::BeginWith(line, "_camera_component"))
            {
                arch >> _register.AddComponent<CCamera>(e);
            }
            else if (su::BeginWith(line, "_lightprobe_component"))
            {
                arch >> _register.AddComponent<CLightProbe>(e);
            }
            else 
            {
                AL_ASSERT_MSG(true, "Unkown Component");
            };
        }
    }
    Scene::Scene(const String &name) : Object(name)
    {
        _register.RegisterComponent<TagComponent>();
        _register.RegisterComponent<TransformComponent>();
        _register.RegisterComponent<StaticMeshComponent>();
        _register.RegisterComponent<LightComponent>();
        _register.RegisterComponent<CCamera>();
        _register.RegisterComponent<CHierarchy>();
        _register.RegisterComponent<CLightProbe>();
        ECS::Signature rs_sig;
        rs_sig.set(_register.GetComponentTypeID<TransformComponent>(), true);
        rs_sig.set(_register.GetComponentTypeID<StaticMeshComponent>(), true);
        _register.RegisterSystem<ECS::RenderSystem>(rs_sig);
        ECS::Signature ls_sig;
        ls_sig.set(_register.GetComponentTypeID<TransformComponent>(), true);
        ls_sig.set(_register.GetComponentTypeID<LightComponent>(), true);
        _register.RegisterSystem<ECS::LightingSystem>(ls_sig);
    }

    void Scene::Attach(ECS::Entity current, ECS::Entity parent)
    {
        AL_ASSERT(_register.HasComponent<CHierarchy>(current) && _register.HasComponent<CHierarchy>(parent));
        auto parent_hierarchy = _register.GetComponent<CHierarchy>(parent);
        auto cur_hierarchy = _register.GetComponent<CHierarchy>(current);
        if (parent_hierarchy->_children_num == 0)
        {
            parent_hierarchy->_first_child = current;
        }
        else
        {
            auto last_child = _register.GetComponent<CHierarchy>(parent_hierarchy->_first_child);
            ECS::Entity last_child_entity = parent_hierarchy->_first_child;
            while (last_child->_next_sibling != ECS::kInvalidEntity)
            {
                last_child_entity = last_child->_next_sibling;
                last_child = _register.GetComponent<CHierarchy>(last_child_entity);
            }
            last_child->_next_sibling = current;
            cur_hierarchy->_prev_sibling = last_child_entity;
        }
        if (auto parent_transf = _register.GetComponent<TransformComponent>(parent))
        {
            cur_hierarchy->_inv_matrix_attach = Math::MatrixInverse(parent_transf->_world_matrix);
        }
        cur_hierarchy->_parent = parent;
        parent_hierarchy->_children_num++;
    }
    void Scene::Detach(ECS::Entity current)
    {
        auto cur_hierarchy = _register.GetComponent<CHierarchy>(current);
        if (cur_hierarchy->_parent == ECS::kInvalidEntity)
            return;
        auto parent_hierarchy = _register.GetComponent<CHierarchy>(cur_hierarchy->_parent);
        if (parent_hierarchy->_children_num == 1)
        {
            parent_hierarchy->_first_child = ECS::kInvalidEntity;
        }
        else
        {
            auto child = _register.GetComponent<CHierarchy>(parent_hierarchy->_first_child);
            while (child != cur_hierarchy)
            {
                child = _register.GetComponent<CHierarchy>(child->_next_sibling);
            }
            auto prev_child = _register.GetComponent<CHierarchy>(child->_prev_sibling);
            auto next_child = _register.GetComponent<CHierarchy>(child->_next_sibling);
            prev_child->_next_sibling = child->_next_sibling;
            next_child->_prev_sibling = child->_prev_sibling;
        }
        cur_hierarchy->_parent = ECS::kInvalidEntity;
        cur_hierarchy->_prev_sibling = ECS::kInvalidEntity;
        cur_hierarchy->_next_sibling = ECS::kInvalidEntity;
        parent_hierarchy->_children_num--;
    }
    const Vector<ECS::Entity> &Scene::EntityView() const
    {
        return _register.EntityView<CHierarchy>();
    }

    ECS::Entity Scene::AddObject(Ref<Mesh> mesh, Ref<Material> mat)
    {
        ECS::Entity obj = _register.Create();
        _register.AddComponent<TagComponent>(obj, AcquireName());
        _register.AddComponent<TransformComponent>(obj);
        _register.AddComponent<CHierarchy>(obj);
        auto &comp = _register.AddComponent<StaticMeshComponent>(obj);
        comp._p_mesh = mesh ? mesh : Mesh::s_p_plane.lock();
        comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
        comp._p_mats.emplace_back(mat ? mat : Material::s_standard_lit.lock());
        return obj;
    }
    ECS::Entity Scene::AddObject(String name)
    {
        ECS::Entity obj = _register.Create();
        name = name.empty() ? AcquireName() : name;
        _register.AddComponent<TagComponent>(obj, name);
        _register.AddComponent<TransformComponent>(obj);
        _register.AddComponent<CHierarchy>(obj);
        return obj;
    }
    ECS::Entity Scene::Pick(const Ray &ray)
    {
        ECS::Entity closest_entity = ECS::kInvalidEntity;
        u32 comp_index = 0;
        for (auto &comp: _register.View<Ailu::StaticMeshComponent>())
        {
            if (closest_entity != ECS::kInvalidEntity)
                break;
            auto &aabbs = comp._transformed_aabbs;
            if (!AABB::Intersect(aabbs[0], ray))
            {
                ++comp_index;
                continue;
            }
            for (int i = 1; i < aabbs.size(); i++)
            {
                if (AABB::Intersect(aabbs[i], ray))
                {
                    closest_entity = _register.GetEntity<Ailu::StaticMeshComponent>(comp_index);
                    LOG_INFO("Pick entity {} with name {}", closest_entity, _register.GetComponent<TagComponent>(closest_entity)->_name);
                }
            }
            ++comp_index;
        }
        return closest_entity;
    }
    void Scene::RemoveObject(ECS::Entity entity)
    {
        _pending_delete_entities.emplace(entity);
    }
    void Scene::DeletePendingEntities()
    {
        while (!_pending_delete_entities.empty())
        {
            auto actor = _pending_delete_entities.front();
            _pending_delete_entities.pop();
            _register.Destory(actor);
        }
    }

    //-----------------------------------------------------------------------SceneMgr----------------------------------------------------------------------------
    int SceneMgr::Initialize()
    {

        return 0;
    }
    void SceneMgr::Finalize()
    {
        //std::ostringstream oss;
        //TextOArchive ar(&oss);
        //_p_current->Serialize(ar);
        //FileManager::CreateFile(ResourceMgr::GetResSysPath(L"Test/a.txt"));
        //FileManager::WriteFile(ResourceMgr::GetResSysPath(L"Test/a.txt"), false, oss.str());
    }
    Ref<Scene> Ailu::SceneMgr::Create(String name)
    {
        _all_scene.emplace_back(MakeRef<Scene>(name));
        return _all_scene.back();
    }
    void SceneMgr::Tick(f32 delta_time)
    {
        if (_p_current)
        {
            for (auto &comp: _p_current->_register.View<TransformComponent>())
            {
                Transform::ToMatrix(comp._transform, comp._world_matrix);
            }
            for (auto &it: _p_current->GetRegister().SystemView())
            {
                auto &[type, sys] = it;
                sys->Update(_p_current->GetRegister(), delta_time);
            }
            _p_current->DeletePendingEntities();
        }
    }

    Ref<Scene> SceneMgr::OpenScene(const WString &scene_path)
    {
        bool test = false;
        if (test)
        {
            Ref<Scene> default_scene = MakeRef<Scene>("DefaultScene");
            auto p = default_scene->AddObject("empty");
            auto child1 = default_scene->AddObject(g_pResourceMgr->GetRef<Mesh>(L"Meshs/plane.alasset"), g_pResourceMgr->GetRef<Material>(L"Materials/StandardPBR.alasset"));
            default_scene->Attach(child1, p);
            {
                auto &comp = default_scene->GetRegister().AddComponent<LightComponent>(default_scene->AddObject("directional_light"));
                comp._type = ELightType::kDirectional;
                comp._light._light_color = Colors::kWhite;
            }
            auto cube = default_scene->AddObject(Mesh::s_p_cube.lock(), g_pResourceMgr->GetRef<Material>(L"Materials/StandardPBR.alasset"));
            default_scene->GetRegister().GetComponent<TagComponent>(cube)->_name = "cube";
            _all_scene.push_back(std::move(default_scene));
        }
        else
        {
            WString sys_path = ResourceMgr::GetResSysPath(scene_path);
            String scene_data;
            FileManager::ReadFile(sys_path, scene_data);
            su::RemoveSpaces(scene_data);
            std::stringstream ss(scene_data);
            String line;
            std::getline(ss, line);
            Guid guid(su::Split(line, ":")[1]);
            std::getline(ss, line);
            std::getline(ss, line);
            line = su::Split(line, ":")[1];
            //这里到达场景根节点
            TextIArchive arch(&ss);
            auto loaded_scene = g_pSceneMgr->Create(line);
            loaded_scene->Deserialize(arch);
        }
        _p_current = _all_scene.back().get();
        return _all_scene.back();
    }
    //-----------------------------------------------------------------------SceneMgr----------------------------------------------------------------------------
}// namespace Ailu
