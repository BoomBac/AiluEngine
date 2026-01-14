#include "Scene/Scene.h"
#include "Animation/AnimationSystem.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Physics/PhysicsSystem.h"
#include "Scene/RenderSystem.h"
//#include "pch.h"
#include <regex>

#include "Render/ShaderInterop.h"//lbvh
#include "Render/Gizmo.h"

using namespace Ailu::Render;

namespace Ailu::SceneManagement
{
    void Scene::Serialize(Archive &arch)
    {
        u64 index = 0;
        auto &os = arch.GetOStream();
        arch << "scene_register:";
        arch.NewLine();
        arch.IncreaseIndent();
        for (auto &tag_comp: _register.View<ECS::TagComponent>())
        {
            ECS::Entity e = _register.GetEntity<ECS::TagComponent>(index++);
            arch << std::format("Entity:{}", e);
            arch.IncreaseIndent();
            arch.NewLine();
            {
                arch.InsertIndent();
                arch << "_tag_component:";
                arch.NewLine();
                arch << tag_comp;
            }
            if (auto c = _register.GetComponent<ECS::TransformComponent>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_transform_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<ECS::StaticMeshComponent>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_static_mesh_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<ECS::LightComponent>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_light_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<ECS::CHierarchy>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_hierarchy_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<ECS::CCamera>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_camera_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<ECS::CLightProbe>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_lightprobe_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<ECS::CRigidBody>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_rigidbody_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<ECS::CCollider>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_collider_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<ECS::CSkeletonMesh>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_skeleton_mesh_component:";
                arch.NewLine();
                arch << (*c);
            }
            if (auto c = _register.GetComponent<ECS::CVXGI>(e); c != nullptr)
            {
                arch.InsertIndent();
                arch << "_vxgi_component:";
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
                arch >> _register.AddComponent<ECS::TagComponent>(e);
            }
            else if (su::BeginWith(line, "_transform_component"))
            {
                arch >> _register.AddComponent<ECS::TransformComponent>(e);
            }
            else if (su::BeginWith(line, "_static_mesh_component"))
            {
                arch >> _register.AddComponent<ECS::StaticMeshComponent>(e);
            }
            else if (su::BeginWith(line, "_light_component"))
            {
                arch >> _register.AddComponent<ECS::LightComponent>(e);
            }
            else if (su::BeginWith(line, "_hierarchy_component"))
            {
                arch >> _register.AddComponent<ECS::CHierarchy>(e);
            }
            else if (su::BeginWith(line, "_camera_component"))
            {
                arch >> _register.AddComponent<ECS::CCamera>(e);
            }
            else if (su::BeginWith(line, "_lightprobe_component"))
            {
                arch >> _register.AddComponent<ECS::CLightProbe>(e);
            }
            else if (su::BeginWith(line, "_rigidbody_component"))
            {
                arch >> _register.AddComponent<ECS::CRigidBody>(e);
            }
            else if (su::BeginWith(line, "_collider_component"))
            {
                arch >> _register.AddComponent<ECS::CCollider>(e);
            }
            else if (su::BeginWith(line, "_skeleton_mesh_component"))
            {
                arch >> _register.AddComponent<ECS::CSkeletonMesh>(e);
            }
            else if (su::BeginWith(line, "_vxgi_component"))
            {
                arch >> _register.AddComponent<ECS::CVXGI>(e);
            }
            else
            {
                AL_ASSERT_MSG(true, "Unkown Component");
            };
        }
        _triangle_count = 0u;
        u64 mesh_bvh_node_count = 0u;
        for (auto& c: _register.View<ECS::StaticMeshComponent>())
        {
            _triangle_count += c._p_mesh->GetTriangleCount();
            mesh_bvh_node_count += c._p_mesh->GetBVHNodes().size();
        }
        if (_triangle_count > 0)
        {
            u64 entity_idx = 0u;
            Vector<Render::TriangleData> triangles;
            triangles.reserve(_triangle_count);
            Vector<BVHNode> mesh_bvh_nodes;
            mesh_bvh_nodes.reserve(mesh_bvh_node_count);
            u64 triangle_offset = 0u,bvh_offset = 0u;
            for (auto &c: _register.View<ECS::StaticMeshComponent>())
            {
                auto current_entity = _register.GetEntity<ECS::StaticMeshComponent>(entity_idx++);
                auto current_tri_count = c._p_mesh->GetTriangleCount();
                auto cur_bvh_node_count = c._p_mesh->GetBVHNodes().size();
                triangles.insert(triangles.end(),c._p_mesh->GetTriangleData().begin(),c._p_mesh->GetTriangleData().end());
                mesh_bvh_nodes.insert(mesh_bvh_nodes.end(), c._p_mesh->GetBVHNodes().begin(), c._p_mesh->GetBVHNodes().end());
                _bvh_nodes_range[current_entity] = Vector2UInt{(u32) bvh_offset, (u32) (cur_bvh_node_count)};
                _mesh_bvh_node_triangle_offset[current_entity] = (u32)triangle_offset;
                triangle_offset += current_tri_count;
                bvh_offset += cur_bvh_node_count;
            }
            BufferDesc buf_desc;
            buf_desc._element_num = _triangle_count;
            buf_desc._element_size = sizeof(TriangleData);
            buf_desc._is_random_write = false;
            buf_desc._target = EGPUBufferTarget::kStructured | EGPUBufferTarget::kConstant;
            buf_desc._size = buf_desc._element_size * buf_desc._element_num;
            _scene_mesh_data = GPUBuffer::Create(buf_desc);
            _scene_mesh_data->Name(std::format("{}_mesh_data_buffer", Name()));
            _scene_mesh_data->SetData(reinterpret_cast<const u8 *>(triangles.data()), _triangle_count * sizeof(Render::TriangleData));

            buf_desc._element_num = (u32) mesh_bvh_nodes.size();
            buf_desc._element_size = sizeof(LBVHNode);
            buf_desc._size = buf_desc._element_size * buf_desc._element_num;
            /*
                struct LBVHNode
                {
                    float3 _min;
                    float _left_or_offset;
                    float3 _max;
                    float _neg_right_or_count_or_inst_idx;
                };
            */
            Vector<LBVHNode> gpu_nodes;
            gpu_nodes.reserve(mesh_bvh_nodes.size());
            for (u64 i = 0; i < mesh_bvh_nodes.size(); i++)
            {
                const auto &node = mesh_bvh_nodes[i];
                gpu_nodes.emplace_back(node._aabb._min,(f32)node._child_index_or_first, node._aabb._max, (f32)node._count_or_flag);
            }
            _blas_buffer = GPUBuffer::Create(buf_desc);
            _blas_buffer->Name(std::format("{}_blas_buffer", Name()));
            _blas_buffer->SetData(reinterpret_cast<const u8 *>(gpu_nodes.data()), static_cast<u32>(gpu_nodes.size() * sizeof(LBVHNode)));
            _blas_node_count = (u32)gpu_nodes.size();

            buf_desc._element_num = RenderConstants::kMaxRenderObjectCount * 4;
            buf_desc._element_size = sizeof(LBVHNode);
            buf_desc._size = buf_desc._element_size * buf_desc._element_num;
            _tlas_buffer = GPUBuffer::Create(buf_desc);
            _tlas_buffer->Name(std::format("{}_tlas_buffer", Name()));
        }
    }

    Scene::Scene(const String &name) : Object(name)
    {
        _register.RegisterComponent<ECS::TagComponent>();
        _register.RegisterComponent<ECS::TransformComponent>();
        _register.RegisterComponent<ECS::StaticMeshComponent>();
        _register.RegisterComponent<ECS::LightComponent>();
        _register.RegisterComponent<ECS::CCamera>();
        _register.RegisterComponent<ECS::CHierarchy>();
        _register.RegisterComponent<ECS::CLightProbe>();
        _register.RegisterComponent<ECS::CRigidBody>();
        _register.RegisterComponent<ECS::CCollider>();
        _register.RegisterComponent<ECS::CSkeletonMesh>();
        _register.RegisterComponent<ECS::CVXGI>();
        ECS::Signature ls_sig;
        ls_sig.set(_register.GetComponentTypeID<ECS::TransformComponent>(), true);
        ls_sig.set(_register.GetComponentTypeID<ECS::LightComponent>(), true);
        _register.RegisterSystem<ECS::LightingSystem>(ls_sig);
        ECS::Signature phy_sig;
        phy_sig.set(_register.GetComponentTypeID<ECS::TransformComponent>(), true);
        phy_sig.set(_register.GetComponentTypeID<ECS::CRigidBody>(), true);
        _register.RegisterSystem<ECS::PhysicsSystem>(phy_sig);
        ECS::Signature anim_sig;
        anim_sig.set(_register.GetComponentTypeID<ECS::CSkeletonMesh>(), true);
        _register.RegisterSystem<ECS::AnimationSystem>(anim_sig);
        _register.RegisterOnComponentAdd<ECS::StaticMeshComponent>([](ECS::Entity entity){ RenderPipeline::Get().OnAddRenderObject(entity);
        });
        _register.RegisterOnComponentAdd<ECS::CSkeletonMesh>([](ECS::Entity entity){ RenderPipeline::Get().OnAddRenderObject(entity);
                                                      });
    }

    void Scene::Attach(ECS::Entity current, ECS::Entity parent)
    {
        if (parent == ECS::kInvalidEntity)
            return;
        AL_ASSERT(_register.HasComponent<ECS::CHierarchy>(current) && _register.HasComponent<ECS::CHierarchy>(parent));
        auto parent_hierarchy = _register.GetComponent<ECS::CHierarchy>(parent);
        auto cur_hierarchy = _register.GetComponent<ECS::CHierarchy>(current);
        if (cur_hierarchy->_parent != ECS::kInvalidEntity)
        {
            Detach(current);
        }
        else
        {
            if (parent_hierarchy->_children_num == 0)
            {
                parent_hierarchy->_first_child = current;
            }
            else
            {
                auto last_child = _register.GetComponent<ECS::CHierarchy>(parent_hierarchy->_first_child);
                ECS::Entity last_child_entity = parent_hierarchy->_first_child;
                while (last_child->_next_sibling != ECS::kInvalidEntity)
                {
                    last_child_entity = last_child->_next_sibling;
                    last_child = _register.GetComponent<ECS::CHierarchy>(last_child_entity);
                }
                last_child->_next_sibling = current;
                cur_hierarchy->_prev_sibling = last_child_entity;
            }
        }
        cur_hierarchy->_parent = parent;
        parent_hierarchy->_children_num++;
        auto mgr = _register.GetComponentMgr<ECS::CHierarchy>();
        mgr->MoveToLast(mgr->GetIndex(current));
        if (auto parent_transf = _register.GetComponent<ECS::TransformComponent>(parent))
        {
            cur_hierarchy->_inv_matrix_attach = Math::MatrixInverse(parent_transf->_transform._world_matrix);
            if (auto cur_transf = _register.GetComponent<ECS::TransformComponent>(current))
            {
                cur_transf->_transform._p_parent = &parent_transf->_transform;
            }
        }
    }
    void Scene::Detach(ECS::Entity current)
    {
        auto cur_hierarchy = _register.GetComponent<ECS::CHierarchy>(current);
        if (cur_hierarchy->_parent == ECS::kInvalidEntity)
            return;
        auto parent_hierarchy = _register.GetComponent<ECS::CHierarchy>(cur_hierarchy->_parent);
        if (parent_hierarchy->_children_num == 1)
        {
            parent_hierarchy->_first_child = ECS::kInvalidEntity;
        }
        else
        {
            auto child = _register.GetComponent<ECS::CHierarchy>(parent_hierarchy->_first_child);
            while (child != cur_hierarchy)
            {
                child = _register.GetComponent<ECS::CHierarchy>(child->_next_sibling);
            }
            auto prev_child = _register.GetComponent<ECS::CHierarchy>(child->_prev_sibling);
            auto next_child = _register.GetComponent<ECS::CHierarchy>(child->_next_sibling);
            prev_child->_next_sibling = child->_next_sibling;
            next_child->_prev_sibling = child->_prev_sibling;
        }
        cur_hierarchy->_parent = ECS::kInvalidEntity;
        cur_hierarchy->_prev_sibling = ECS::kInvalidEntity;
        cur_hierarchy->_next_sibling = ECS::kInvalidEntity;
        parent_hierarchy->_children_num--;
        if (auto cur_transf = _register.GetComponent<ECS::TransformComponent>(current))
        {
            cur_transf->_transform._p_parent = nullptr;
        }
    }
    const Vector<ECS::Entity> &Scene::EntityView() const
    {
        return _register.EntityView<ECS::CHierarchy>();
    }

    ECS::Entity Scene::AddObject(Ref<Mesh> mesh, Ref<Material> mat)
    {
        ECS::Entity obj = _register.Create();
        _register.AddComponent<ECS::TagComponent>(obj, AcquireName());
        _register.AddComponent<ECS::TransformComponent>(obj);
        _register.AddComponent<ECS::CHierarchy>(obj);
        auto &comp = _register.AddComponent<ECS::StaticMeshComponent>(obj);
        comp._p_mesh = mesh ? mesh : Mesh::s_plane.lock();
        comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
        comp._p_mats.emplace_back(mat ? mat : Material::s_standard_defered_lit.lock());
        return obj;
    }
    ECS::Entity Scene::AddObject(Ref<Mesh> mesh, const Vector<Ref<Material>> &mats)
    {
        ECS::Entity obj = _register.Create();
        _register.AddComponent<ECS::TagComponent>(obj, AcquireName());
        _register.AddComponent<ECS::TransformComponent>(obj);
        _register.AddComponent<ECS::CHierarchy>(obj);
        auto &comp = _register.AddComponent<ECS::StaticMeshComponent>(obj);
        comp._p_mesh = mesh ? mesh : Mesh::s_plane.lock();
        comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
        comp._p_mats = mats;
        return obj;
    }
    ECS::Entity Scene::AddObject(String name)
    {
        ECS::Entity obj = _register.Create();
        name = name.empty() ? AcquireName() : name;
        _register.AddComponent<ECS::TagComponent>(obj, name);
        _register.AddComponent<ECS::TransformComponent>(obj);
        _register.AddComponent<ECS::CHierarchy>(obj);
        return obj;
    }
    ECS::Entity Scene::DuplicateEntity(ECS::Entity e)
    {
        ECS::Entity new_one = _register.Create();
        auto &tag_comp = _register.AddComponent<ECS::TagComponent>(new_one, *_register.GetComponent<ECS::TagComponent>(e));
        String base_name = tag_comp._name.substr(0, tag_comp._name.find_first_of('(') - 1);
        i32 max_index = 0;
        std::regex name_pattern(base_name + R"(\((\d+)\))");// 匹配 A(*) 的正则表达式
        for (const auto &tag: _register.View<ECS::TagComponent>())
        {
            std::smatch match;
            if (std::regex_match(tag._name, match, name_pattern))
            {
                int index = std::stoi(match[1].str());
                max_index = std::max(max_index, index);
            }
        }
        tag_comp._name += "(" + std::to_string(max_index + 1) + ")";
        _register.AddComponent<ECS::TransformComponent>(new_one, *_register.GetComponent<ECS::TransformComponent>(e));
        if (_register.HasComponent<ECS::StaticMeshComponent>(e))
            _register.AddComponent<ECS::StaticMeshComponent>(new_one, *_register.GetComponent<ECS::StaticMeshComponent>(e));
        if (_register.HasComponent<ECS::LightComponent>(e))
            _register.AddComponent<ECS::LightComponent>(new_one, *_register.GetComponent<ECS::LightComponent>(e));
        if (_register.HasComponent<ECS::CCamera>(e))
            _register.AddComponent<ECS::CCamera>(new_one, *_register.GetComponent<ECS::CCamera>(e));
        if (_register.HasComponent<ECS::CHierarchy>(e))
        {
            ECS::CHierarchy src_heri = *_register.GetComponent<ECS::CHierarchy>(e);
            auto &new_heri = _register.AddComponent<ECS::CHierarchy>(new_one, src_heri);
            Attach(new_one, src_heri._parent);
        }
        if (_register.HasComponent<ECS::CLightProbe>(e))
            _register.AddComponent<ECS::CLightProbe>(new_one, *_register.GetComponent<ECS::CLightProbe>(e));
        if (_register.HasComponent<ECS::CRigidBody>(e))
            _register.AddComponent<ECS::CRigidBody>(new_one, *_register.GetComponent<ECS::CRigidBody>(e));
        if (_register.HasComponent<ECS::CCollider>(e))
            _register.AddComponent<ECS::CCollider>(new_one, *_register.GetComponent<ECS::CCollider>(e));
        LOG_INFO("Duplicate entity {}", e);
        return new_one;
    }
    ECS::Entity Scene::Pick(const Ray &ray)
    {
        ECS::Entity closest_entity = ECS::kInvalidEntity;
        u32 comp_index = 0;
        for (auto &comp: _register.View<Ailu::ECS::StaticMeshComponent>())
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
                    closest_entity = _register.GetEntity<Ailu::ECS::StaticMeshComponent>(comp_index);
                    LOG_INFO("Pick entity {} with name {}", closest_entity, _register.GetComponent<ECS::TagComponent>(closest_entity)->_name);
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

    void Scene::Clear()
    {
        LOG_WARNING("Scene::Clear: TODO");
    }

    void Scene::Update(f32 dt)
    {
        auto &r = _register;
        for (auto &comp: r.View<ECS::TransformComponent>())
        {
            comp._transform._world_matrix = Transform::GetWorldMatrix(comp._transform);
            //Transform::ToMatrix(comp._transform, comp._transform._world_matrix);
        }
        u32 index = 0;
        for (auto &comp: r.View<ECS::StaticMeshComponent>())
        {
            if (comp._p_mesh)
            {
                const auto &transf = r.GetComponent<ECS::StaticMeshComponent, ECS::TransformComponent>(index)->_transform;
                auto &bound_box = comp._p_mesh->BoundBox();
                for (int i = 0; i < bound_box.size(); i++)
                {
                    comp._transformed_aabbs[i] = bound_box[i] * transf._world_matrix;
                }
            }
            ++index;
        }
        index = 0;
        for (auto &comp: r.View<ECS::CSkeletonMesh>())
        {
            if (comp._p_mesh)
            {
                const auto &transf = r.GetComponent<ECS::CSkeletonMesh, ECS::TransformComponent>(index)->_transform;
                auto &bound_box = comp._p_mesh->BoundBox();
                for (int i = 0; i < bound_box.size(); i++)
                {
                    comp._transformed_aabbs[i] = bound_box[i] * transf._world_matrix;
                }
            }
            ++index;
        }
        index = 0;
        for (auto &comp: r.View<ECS::CCamera>())
        {
            auto t = r.GetComponent<ECS::CCamera, ECS::TransformComponent>(index);
            comp._camera.Position(t->_transform._position);
            comp._camera.Rotation(t->_transform._rotation);
            comp._camera.RecalculateMatrix(true);
        }
        for (auto &it: _register.SystemView())
        {
            auto &[type, sys] = it;
            sys->Update(_register, dt);
        }
        RebuildBVHTree();
        DeletePendingEntities();
    }

    static Vector<LBVHNode> s_temp_tlas_gpu_data(RenderConstants::kMaxRenderObjectCount*2);

    void Scene::RebuildBVHTree()
    {
        PROFILE_BLOCK_CPU(Scene_RebuildBVHTree)
        u32 index = 0;
        Vector<AABB> aabbs;
        for (auto &comp: _register.View<ECS::StaticMeshComponent>())
        {
            if (comp._p_mesh)
            {
                auto &bound_box = comp._transformed_aabbs;
                for (int i = 1; i < bound_box.size(); i++)
                {
                    aabbs.push_back(bound_box[i]);
                }
            }
        }
        BVHBuilder builder(aabbs);
        auto ret = builder.Build(1);
        u64 node_size = ret._nodes.size();
        _tlas_nodes = std::move(ret._nodes);
        AL_ASSERT(_tlas_nodes.size() < RenderConstants::kMaxRenderObjectCount * 2);
        for (u64 i = 0; i < node_size; i++)
        {
            if (_tlas_nodes[i].IsLeaf())
                _tlas_nodes[i]._child_index_or_first = ret._reordered_indices[_tlas_nodes[i]._child_index_or_first];
            const auto &node = _tlas_nodes[i];
            s_temp_tlas_gpu_data[i] = {node._aabb._min, (f32) node._child_index_or_first, node._aabb._max, (f32) node._count_or_flag};
        }
        _tlas_buffer->SetData(reinterpret_cast<const u8 *>(s_temp_tlas_gpu_data.data()), (u32) (node_size * sizeof(LBVHNode)));
    }

    //-----------------------------------------------------------------------SceneMgr----------------------------------------------------------------------------
    int SceneMgr::Initialize()
    {
        TIMER_BLOCK("-----------------------------------------------------------SceneMgr::Initialize")
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
    Ref<Scene> SceneMgr::Create(String name)
    {
        return MakeRef<Scene>(name);
    }
    void SceneMgr::Tick(f32 delta_time)
    {
        if (_p_current)
        {
            _p_current->Update(delta_time);
        }
    }

    Ref<Scene> SceneMgr::OpenScene(const WString &scene_path)
    {
        AL_ASSERT(!scene_path.empty());
        if (auto it = _all_scene.find(scene_path); it != _all_scene.end())
        {
            _p_current = it->second.get();
            return it->second;
        }
        auto s = g_pResourceMgr->Load<Scene>(scene_path);
        _all_scene[scene_path] = s;
        _p_current = s.get();
        return s;
        //bool test = false;
        //if (test)
        //{
        //    Ref<Scene> default_scene = MakeRef<Scene>("DefaultScene");
        //    auto p = default_scene->AddObject("empty");
        //    auto child1 = default_scene->AddObject(g_pResourceMgr->GetRef<Mesh>(L"Meshs/plane.alasset"), g_pResourceMgr->GetRef<Material>(L"Materials/StandardPBR.alasset"));
        //    default_scene->Attach(child1, p);
        //    {
        //        auto &comp = default_scene->GetRegister().AddComponent<LightComponent>(default_scene->AddObject("directional_light"));
        //        comp._type = ELightType::kDirectional;
        //        comp._light._light_color = Colors::kWhite;
        //    }
        //    auto cube = default_scene->AddObject(Mesh::s_cube.lock(), g_pResourceMgr->GetRef<Material>(L"Materials/StandardPBR.alasset"));
        //    default_scene->GetRegister().GetComponent<TagComponent>(cube)->_name = "cube";
        //    _all_scene.push_back(std::move(default_scene));
        //    _all_scene
        //}
    }
    void SceneMgr::EnterPlayMode()
    {
        Application::Get()._is_playing_mode = true;
        _runtime_scene = new Scene(*_p_current);
        auto name = _runtime_scene->Name();
        name.append("_copy");
        _runtime_scene->Name(name);
        _runtime_scene_src = _p_current;
        _p_current = _runtime_scene;
    }

    void SceneMgr::ExitPlayMode()
    {
        _p_current = _runtime_scene_src;
        _runtime_scene_src = nullptr;
        DESTORY_PTR(_runtime_scene);
        Application::Get()._is_playing_mode = false;
    }
    void SceneMgr::EnterSimulateMode()
    {
        auto &r = _p_current->GetRegister();
        for (auto &t: r.View<ECS::TransformComponent>())
        {
            _transform_cache.emplace_back(t._transform);
        }
        Application::Get()._is_simulate_mode = true;
    }
    void SceneMgr::ExitSimulateMode()
    {
        auto &r = _p_current->GetRegister();
        u32 index = 0;
        for (auto &t: r.View<ECS::TransformComponent>())
        {
            t._transform = _transform_cache[index++];
        }
        for (auto &c: r.View<ECS::CRigidBody>())
        {
            c._velocity = Vector3f::kZero;
            c._angular_velocity = Vector3f::kZero;
        }
        Application::Get()._is_simulate_mode = false;
        _transform_cache.clear();
    }
    //-----------------------------------------------------------------------SceneMgr----------------------------------------------------------------------------
}// namespace Ailu
