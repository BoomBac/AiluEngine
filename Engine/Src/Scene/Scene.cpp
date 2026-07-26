#include "Scene/Scene.h"
#include "Animation/AnimationSystem.h"
#include "Audio/AudioSystem.h"
#include "Framework/Common/Application.h"
#include "Framework/Math/QuaternionMatrix.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Script/ScriptSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Scene/RenderSystem.h"
#include "Scene/TransformSystem.h"
//#include "pch.h"
#include <regex>

#include "Render/ShaderInterop.h"//lbvh
#include "Render/Gizmo.h"

using namespace Ailu::Render;

namespace Ailu::SceneManagement
{
    #pragma region Scene----------------------------------------------------------------------------
    ReparentSceneCommand::ReparentSceneCommand(ECS::Entity child, ECS::Entity new_parent, bool keep_world_transform)
        : _child(child), _new_parent(new_parent), _keep_world_transform(keep_world_transform)
    {
    }

    const String &ReparentSceneCommand::ToString() const
    {
        static String name = "Reparent";
        return name;
    }

    void ReparentSceneCommand::CaptureOldState(Scene &scene)
    {
        auto &reg = scene.GetRegister();
        if (auto *hier = reg.GetComponent<ECS::CHierarchy>(_child))
            _old_parent = hier->_parent;
        if (auto *transform = reg.GetComponent<ECS::TransformComponent>(_child))
            _old_local_transform = transform->_local_transform;
    }

    void ReparentSceneCommand::CaptureNewState(Scene &scene)
    {
        if (auto *transform = scene.GetRegister().GetComponent<ECS::TransformComponent>(_child))
            _new_local_transform = transform->_local_transform;
    }

    bool ReparentSceneCommand::Apply(Scene &scene, ECS::Entity parent, const Transform &local_transform) const
    {
        if (!scene.IsValidEntity(_child))
            return false;
        if (parent != ECS::kInvalidEntity && !scene.IsValidEntity(parent))
            return false;

        if (!scene.Reparent(_child, parent, false))
            return false;

        if (auto *transform = scene.GetRegister().GetComponent<ECS::TransformComponent>(_child))
        {
            transform->_local_transform = local_transform;
            transform->_local_dirty = true;
            transform->_world_dirty = true;
        }
        return true;
    }

    bool ReparentSceneCommand::Execute(Scene &scene)
    {
        if (!_has_executed)
        {
            CaptureOldState(scene);
            if (!scene.Reparent(_child, _new_parent, _keep_world_transform))
                return false;
            CaptureNewState(scene);
            _has_executed = true;
            return true;
        }

        return Apply(scene, _new_parent, _new_local_transform);
    }

    bool ReparentSceneCommand::Undo(Scene &scene)
    {
        if (!_has_executed)
            return false;
        return Apply(scene, _old_parent, _old_local_transform);
    }

    Scene::Scene(const String &name) : Object(name)
    {
        _register.RegisterComponent<ECS::TagComponent>();
        _register.RegisterComponent<ECS::PersistentIdComponent>();
        _register.RegisterComponent<ECS::TransformComponent>();
        _register.RegisterComponent<ECS::ScriptComponent>();
        _register.RegisterComponent<ECS::StaticMeshComponent>();
        _register.RegisterComponent<ECS::LightComponent>();
        _register.RegisterComponent<ECS::CCamera>();
        _register.RegisterComponent<ECS::CHierarchy>();
        _register.RegisterComponent<ECS::CLightProbe>();
        _register.RegisterComponent<ECS::CRigidBody>();
        _register.RegisterComponent<ECS::CCollider>();
        _register.RegisterComponent<ECS::CSkeletonMesh>();
        _register.RegisterComponent<ECS::CVXGI>();
        _register.RegisterComponent<ECS::SpriteRendererComponent>();
        _register.RegisterComponent<ECS::AudioSourceComponent>();
        _register.RegisterComponent<ECS::AudioListenerComponent>();
        ECS::Signature transf_sig;
        transf_sig.set(_register.GetComponentTypeID<ECS::TransformComponent>(), true);
        _register.RegisterSystem<ECS::TransformSystem>(transf_sig);
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
        ECS::Signature audio_sig;
        audio_sig.set(_register.GetComponentTypeID<ECS::TransformComponent>(), true);
        _register.RegisterSystem<ECS::AudioSystem>(audio_sig);
        _register.RegisterOnComponentAdd<ECS::StaticMeshComponent>([](ECS::Entity entity){ RenderPipeline::Get().OnAddRenderObject(entity);
        });
        _register.RegisterOnComponentAdd<ECS::CSkeletonMesh>([](ECS::Entity entity){ RenderPipeline::Get().OnAddRenderObject(entity);});
        BufferDesc buf_desc;
        buf_desc._is_random_write = false;
        buf_desc._target = EGPUBufferTarget::kStructured | EGPUBufferTarget::kConstant;
        buf_desc._size = buf_desc._element_size * buf_desc._element_num;
        buf_desc._element_num = 2000;
        buf_desc._element_size = sizeof(LBVHNode);
        buf_desc._size = buf_desc._element_size * buf_desc._element_num;
        _tlas_buffer = GPUBuffer::Create(buf_desc);
        _tlas_buffer->Name(std::format("{}_tlas_buffer", Name()));
    }

    // =========================================================================
    // Hierarchy Helpers
    // =========================================================================
    void Scene::TouchStructure()
    {
        ++_structure_revision;
        MarkDirty();
    }

    bool Scene::IsValidEntity(ECS::Entity entity) const
    {
        return _register.IsAlive(entity);
    }

    bool Scene::IsDescendantOf(ECS::Entity entity, ECS::Entity potential_ancestor) const
    {
        if (entity == ECS::kInvalidEntity || potential_ancestor == ECS::kInvalidEntity)
            return false;
        if (!_register.IsAlive(entity) || !_register.IsAlive(potential_ancestor))
            return false;

        std::unordered_set<ECS::Entity> visited;
        ECS::Entity current = entity;
        constexpr u32 kMaxIterations = 1024u;
        u32 iterations = 0u;

        while (current != ECS::kInvalidEntity && iterations < kMaxIterations)
        {
            if (!visited.insert(current).second)
            {
                LOG_ERROR("IsDescendantOf: cycle detected at entity {}", current);
                return false;
            }
            if (current == potential_ancestor)
                return true;

            auto *hier = _register.GetComponent<ECS::CHierarchy>(current);
            if (!hier)
                break;
            current = hier->_parent;
            ++iterations;
        }

        if (iterations >= kMaxIterations)
            LOG_ERROR("IsDescendantOf: max iterations exceeded from entity {}", entity);

        return false;
    }

    bool Scene::UnlinkFromParent(ECS::Entity entity)
    {
        auto *child_hier = _register.GetComponent<ECS::CHierarchy>(entity);
        if (!child_hier || child_hier->_parent == ECS::kInvalidEntity)
            return false;

        ECS::Entity old_parent_entity = child_hier->_parent;
        auto *old_parent_hier = _register.GetComponent<ECS::CHierarchy>(old_parent_entity);
        if (!old_parent_hier)
            return false;

        ECS::Entity prev = child_hier->_prev_sibling;
        ECS::Entity next = child_hier->_next_sibling;

        if (prev != ECS::kInvalidEntity)
        {
            auto *prev_hier = _register.GetComponent<ECS::CHierarchy>(prev);
            if (prev_hier)
                prev_hier->_next_sibling = next;
        }
        else
        {
            old_parent_hier->_first_child = next;
        }

        if (next != ECS::kInvalidEntity)
        {
            auto *next_hier = _register.GetComponent<ECS::CHierarchy>(next);
            if (next_hier)
                next_hier->_prev_sibling = prev;
        }

        --old_parent_hier->_children_num;

        child_hier->_parent = ECS::kInvalidEntity;
        child_hier->_prev_sibling = ECS::kInvalidEntity;
        child_hier->_next_sibling = ECS::kInvalidEntity;

        _register.TouchHierarchy();
        return true;
    }

    bool Scene::LinkAsLastChild(ECS::Entity entity, ECS::Entity parent)
    {
        auto *child_hier = _register.GetComponent<ECS::CHierarchy>(entity);
        auto *parent_hier = _register.GetComponent<ECS::CHierarchy>(parent);
        if (!child_hier || !parent_hier)
            return false;

        child_hier->_parent = parent;

        if (parent_hier->_children_num == 0)
        {
            parent_hier->_first_child = entity;
        }
        else
        {
            ECS::Entity last_child_entity = parent_hier->_first_child;
            auto *last_child = _register.GetComponent<ECS::CHierarchy>(last_child_entity);
            while (last_child && last_child->_next_sibling != ECS::kInvalidEntity)
            {
                last_child_entity = last_child->_next_sibling;
                last_child = _register.GetComponent<ECS::CHierarchy>(last_child_entity);
            }
            if (last_child)
            {
                last_child->_next_sibling = entity;
                child_hier->_prev_sibling = last_child_entity;
            }
        }

        ++parent_hier->_children_num;
        _register.TouchHierarchy();
        return true;
    }

    void Scene::CollectSubtreePostOrder(ECS::Entity root, Vector<ECS::Entity>& result) const
    {
        auto *hier = _register.GetComponent<ECS::CHierarchy>(root);
        if (!hier)
        {
            result.push_back(root);
            return;
        }

        ECS::Entity child = hier->_first_child;
        while (child != ECS::kInvalidEntity)
        {
            ECS::Entity next = child;
            auto *child_hier = _register.GetComponent<ECS::CHierarchy>(child);
            if (child_hier)
                next = child_hier->_next_sibling;
            CollectSubtreePostOrder(child, result);
            child = next;
        }

        result.push_back(root);
    }

    // =========================================================================
    // Public Hierarchy API
    // =========================================================================
    bool Scene::Reparent(ECS::Entity child, ECS::Entity new_parent, bool keep_world_transform)
    {
        // --- Validation ---
        if (child == ECS::kInvalidEntity)
            return false;
        if (!_register.IsAlive(child))
            return false;
        if (!_register.HasComponent<ECS::CHierarchy>(child))
            return false;
        if (new_parent != ECS::kInvalidEntity)
        {
            if (!_register.IsAlive(new_parent))
                return false;
            if (!_register.HasComponent<ECS::CHierarchy>(new_parent))
                return false;
        }

        // child cannot be its own parent
        if (child == new_parent)
            return false;

        // new_parent must not be in child's subtree
        if (new_parent != ECS::kInvalidEntity && IsDescendantOf(new_parent, child))
            return false;

        // Check current parent — if already correct, no-op
        auto *child_hier = _register.GetComponent<ECS::CHierarchy>(child);
        if (child_hier->_parent == new_parent)
            return true;

        // --- Save world transform ---
        auto *child_transf = _register.GetComponent<ECS::TransformComponent>(child);
        Transform old_world;
        bool has_transform = (child_transf != nullptr);
        if (has_transform)
            old_world = Transform::FromMatrix(child_transf->_world_matrix);

        // --- Unlink from old parent ---
        UnlinkFromParent(child);

        // --- Link to new parent ---
        if (new_parent != ECS::kInvalidEntity)
        {
            LinkAsLastChild(child, new_parent);

            auto *parent_transf = _register.GetComponent<ECS::TransformComponent>(new_parent);
            if (parent_transf)
            {
                child_hier->_inv_matrix_attach = Math::MatrixInverse(parent_transf->_world_matrix);
            }
        }

        // --- Restore world transform ---
        if (keep_world_transform && has_transform)
        {
            Matrix4x4f local_matrix = Transform::ToMatrix(old_world);
            if (new_parent != ECS::kInvalidEntity)
            {
                if (auto *parent_transf = _register.GetComponent<ECS::TransformComponent>(new_parent))
                    local_matrix = local_matrix * Math::MatrixInverse(parent_transf->_world_matrix);
            }
            child_transf->_local_transform = Transform::FromMatrix(local_matrix);
            child_transf->_local_dirty = true;
            child_transf->_world_dirty = true;
        }

        TouchStructure();
        return true;
    }

    bool Scene::Detach(ECS::Entity child, bool keep_world_transform)
    {
        return Reparent(child, ECS::kInvalidEntity, keep_world_transform);
    }

    void Scene::EnqueueSceneCommand(ISceneCommand *command, bool undo)
    {
        if (command == nullptr)
            return;
        _pending_scene_commands.push_back({command, undo});
    }

    void Scene::ProcessSceneCommands()
    {
        if (_pending_scene_commands.empty())
            return;

        Vector<QueuedSceneCommand> pending = std::move(_pending_scene_commands);
        _pending_scene_commands.clear();
        for (const auto &entry: pending)
        {
            if (entry._command == nullptr)
                continue;
            if (entry._undo)
                entry._command->Undo(*this);
            else
                entry._command->Execute(*this);
        }
    }

    bool Scene::RenameEntity(ECS::Entity entity, const String& new_name)
    {
        if (!_register.IsAlive(entity))
            return false;
        auto *tag = _register.GetComponent<ECS::TagComponent>(entity);
        if (!tag)
            return false;
        tag->_name = new_name;
        TouchStructure();
        return true;
    }

    // --- Old Detach (non-static wrapper removed, now inline in header) ---
    const Vector<ECS::Entity> &Scene::EntityView() const
    {
        return _register.EntityView<ECS::CHierarchy>();
    }

    ECS::Entity Scene::AddObject(Ref<Mesh> mesh, Ref<Material> mat)
    {
        ECS::Entity obj = _register.Create();
        _register.AddComponent<ECS::PersistentIdComponent>(obj);
        _register.AddComponent<ECS::TagComponent>(obj, AcquireName());
        _register.AddComponent<ECS::TransformComponent>(obj);
        _register.AddComponent<ECS::CHierarchy>(obj);
        auto &comp = _register.AddComponent<ECS::StaticMeshComponent>(obj);
        comp._p_mesh = mesh ? mesh : Mesh::s_plane.lock();
        comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
        comp._p_mats.emplace_back(mat ? mat : Material::s_standard_defered_lit.lock());
        TouchStructure();
        return obj;
    }
    ECS::Entity Scene::AddObject(Ref<Mesh> mesh, const Vector<Ref<Material>> &mats)
    {
        ECS::Entity obj = _register.Create();
        _register.AddComponent<ECS::PersistentIdComponent>(obj);
        _register.AddComponent<ECS::TagComponent>(obj, AcquireName());
        _register.AddComponent<ECS::TransformComponent>(obj);
        _register.AddComponent<ECS::CHierarchy>(obj);
        auto &comp = _register.AddComponent<ECS::StaticMeshComponent>(obj);
        comp._p_mesh = mesh ? mesh : Mesh::s_plane.lock();
        comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
        comp._p_mats = mats;
        TouchStructure();
        return obj;
    }
    ECS::Entity Scene::AddObject(String name)
    {
        ECS::Entity obj = _register.Create();
        name = name.empty() ? AcquireName() : name;
        _register.AddComponent<ECS::PersistentIdComponent>(obj);
        _register.AddComponent<ECS::TagComponent>(obj, name);
        _register.AddComponent<ECS::TransformComponent>(obj);
        _register.AddComponent<ECS::CHierarchy>(obj);
        TouchStructure();
        return obj;
    }
    ECS::Entity Scene::DuplicateEntity(ECS::Entity e)
    {
        ECS::Entity new_one = _register.Create();
        _register.AddComponent<ECS::PersistentIdComponent>(new_one);  // new entity gets a new GUID
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

        // Save source local transform before copying
        Vector3f source_local_pos = Vector3f::kZero;
        Quaternion source_local_rot = Quaternion();
        Vector3f source_local_scale = Vector3f::kOne;
        ECS::Entity source_parent = ECS::kInvalidEntity;
        bool source_has_hierarchy = _register.HasComponent<ECS::CHierarchy>(e);

        if (auto *src_transf = _register.GetComponent<ECS::TransformComponent>(e))
        {
            source_local_pos = src_transf->_local_transform._position;
            source_local_rot = src_transf->_local_transform._rotation;
            source_local_scale = src_transf->_local_transform._scale;
        }
        if (source_has_hierarchy)
        {
            source_parent = _register.GetComponent<ECS::CHierarchy>(e)->_parent;
        }

        _register.AddComponent<ECS::TransformComponent>(new_one, *_register.GetComponent<ECS::TransformComponent>(e));

        // Add default CHierarchy (no sibling/child fields copied)
        if (source_has_hierarchy)
            _register.AddComponent<ECS::CHierarchy>(new_one);

        // Copy business components
        if (_register.HasComponent<ECS::ScriptComponent>(e))
            _register.AddComponent<ECS::ScriptComponent>(new_one, *_register.GetComponent<ECS::ScriptComponent>(e));
        if (_register.HasComponent<ECS::StaticMeshComponent>(e))
            _register.AddComponent<ECS::StaticMeshComponent>(new_one, *_register.GetComponent<ECS::StaticMeshComponent>(e));
        if (_register.HasComponent<ECS::LightComponent>(e))
            _register.AddComponent<ECS::LightComponent>(new_one, *_register.GetComponent<ECS::LightComponent>(e));
        if (_register.HasComponent<ECS::CCamera>(e))
            _register.AddComponent<ECS::CCamera>(new_one, *_register.GetComponent<ECS::CCamera>(e));
        if (_register.HasComponent<ECS::CLightProbe>(e))
            _register.AddComponent<ECS::CLightProbe>(new_one, *_register.GetComponent<ECS::CLightProbe>(e));
        if (_register.HasComponent<ECS::CRigidBody>(e))
            _register.AddComponent<ECS::CRigidBody>(new_one, *_register.GetComponent<ECS::CRigidBody>(e));
        if (_register.HasComponent<ECS::CCollider>(e))
            _register.AddComponent<ECS::CCollider>(new_one, *_register.GetComponent<ECS::CCollider>(e));
        if (_register.HasComponent<ECS::SpriteRendererComponent>(e))
            _register.AddComponent<ECS::SpriteRendererComponent>(new_one, *_register.GetComponent<ECS::SpriteRendererComponent>(e));
        if (_register.HasComponent<ECS::AudioSourceComponent>(e))
            _register.AddComponent<ECS::AudioSourceComponent>(new_one, *_register.GetComponent<ECS::AudioSourceComponent>(e));
        if (_register.HasComponent<ECS::AudioListenerComponent>(e))
            _register.AddComponent<ECS::AudioListenerComponent>(new_one, *_register.GetComponent<ECS::AudioListenerComponent>(e));

        // Reparent to same parent, then restore local transform
        if (source_has_hierarchy && source_parent != ECS::kInvalidEntity)
        {
            Reparent(new_one, source_parent, false);
        }

        // Restore saved local transform
        if (auto *new_transf = _register.GetComponent<ECS::TransformComponent>(new_one))
        {
            new_transf->_local_transform._position = source_local_pos;
            new_transf->_local_transform._rotation = source_local_rot;
            new_transf->_local_transform._scale = source_local_scale;
        }

        LOG_INFO("Duplicate entity {}", e);
        TouchStructure();
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
        if (_pending_delete_entities.empty())
            return;

        // Step 1: Collect all entities to delete (roots + all descendants), post-order
        std::unordered_set<ECS::Entity> all_to_delete;
        Vector<ECS::Entity> delete_order;

        for (ECS::Entity root : _pending_delete_entities)
        {
            if (all_to_delete.contains(root))
                continue;
            Vector<ECS::Entity> subtree;
            CollectSubtreePostOrder(root, subtree);
            for (ECS::Entity entity : subtree)
            {
                if (all_to_delete.insert(entity).second)
                    delete_order.push_back(entity);
            }
        }

        // Step 2: Unlink roots whose parent is NOT being deleted
        for (ECS::Entity root : _pending_delete_entities)
        {
            auto *hier = _register.GetComponent<ECS::CHierarchy>(root);
            if (hier && hier->_parent != ECS::kInvalidEntity && !all_to_delete.contains(hier->_parent))
                UnlinkFromParent(root);
        }

        // Step 3: Destroy from leaves to root (post-order)
        for (ECS::Entity actor : delete_order)
        {
            if (!_register.IsAlive(actor))
                continue;
            if (auto *script_comp = _register.GetComponent<ECS::ScriptComponent>(actor))
            {
                ScriptSystem::Get().DestroyComponent(*script_comp);
            }
            _register.Destory(actor);
        }

        _pending_delete_entities.clear();
        TouchStructure();
    }

    void Scene::Clear()
    {
        LOG_WARNING("Scene::Clear: TODO");
    }

    void Scene::BeginUpdate()
    {
        ProcessSceneCommands();
    }

    void Scene::BeginLateUpdate()
    {
    }

    void Scene::FixedUpdate(f32 fixed_delta_time)
    {
        BeginUpdate();
        UpdateFixedScripts(fixed_delta_time);
        _register.ExecutePhase(ECS::ESystemPhase::kPrePhysics, fixed_delta_time);
        _register.ExecutePhase(ECS::ESystemPhase::kPhysics, fixed_delta_time);
        _register.ExecutePhase(ECS::ESystemPhase::kPostPhysics, fixed_delta_time);
    }

    void Scene::UpdateFixedScripts(f32 fixed_delta_time)
    {
        auto &reg = _register;
        u32 index = 0;
        for (auto &component: reg.View<ECS::ScriptComponent>())
        {
            const ECS::Entity entity = reg.GetEntity<ECS::ScriptComponent>(index++);
            ScriptSystem::Get().FixedUpdateComponent(this, entity, component, fixed_delta_time);
        }
    }

    void Scene::UpdateScripts(f32 delta_time)
    {
        auto &reg = _register;
        u32 index = 0;
        for (auto &component: reg.View<ECS::ScriptComponent>())
        {
            const ECS::Entity entity = reg.GetEntity<ECS::ScriptComponent>(index++);
            ScriptSystem::Get().UpdateComponent(this, entity, component, delta_time);
        }
    }

    void Scene::UpdateLateScripts(f32 delta_time, f32 render_alpha)
    {
        auto &reg = _register;
        u32 index = 0;
        for (auto &component: reg.View<ECS::ScriptComponent>())
        {
            const ECS::Entity entity = reg.GetEntity<ECS::ScriptComponent>(index++);
            ScriptSystem::Get().LateUpdateComponent(this, entity, component, delta_time, render_alpha);
        }
    }

    void Scene::UpdateRenderTransforms(f32 render_alpha)
    {
        const f32 clamped_alpha = std::clamp(render_alpha, 0.0f, 1.0f);
        for (auto &transform: _register.View<ECS::TransformComponent>())
        {
            //这里是一个小热点
            const bool has_history = transform._world_version > 1u;
            const Transform render_transform = has_history
                                                   ? Transform::Mix(
                                                           Transform(transform._prev_position, transform._prev_rotation, transform._prev_scale),
                                                           Transform(transform._position, transform._rotation, transform._scale),
                                                           clamped_alpha)
                                                   : Transform(transform._position, transform._rotation, transform._scale);

            transform._prev_render_world_matrix = transform._render_world_matrix;
            transform._render_position = render_transform._position;
            transform._render_rotation = render_transform._rotation;
            transform._render_scale = render_transform._scale;
            Transform::ToMatrix(render_transform, transform._render_world_matrix);

            if (!has_history)
                transform._prev_render_world_matrix = transform._render_world_matrix;
        }
    }

    void Scene::UpdateBounds()
    {
        auto &reg = _register;
        u32 index = 0;
        for (auto &component: reg.View<ECS::StaticMeshComponent>())
        {
            if (component._p_mesh)
            {
                const auto *transform = reg.GetComponent<ECS::StaticMeshComponent, ECS::TransformComponent>(index);
                auto &bound_box = component._p_mesh->BoundBox();
                for (i32 i = 0; i < bound_box.size(); ++i)
                    component._transformed_aabbs[i] = bound_box[i] * transform->GetRenderWorldMatrix();
            }
            ++index;
        }

        index = 0;
        for (auto &component: reg.View<ECS::CSkeletonMesh>())
        {
            if (component._p_mesh)
            {
                const auto *transform = reg.GetComponent<ECS::CSkeletonMesh, ECS::TransformComponent>(index);
                auto &bound_box = component._p_mesh->BoundBox();
                for (i32 i = 0; i < bound_box.size(); ++i)
                    component._transformed_aabbs[i] = bound_box[i] * transform->GetRenderWorldMatrix();
            }
            ++index;
        }
    }

    void Scene::UpdateCameras()
    {
        auto &reg = _register;
        u32 index = 0;
        for (auto &component: reg.View<ECS::CCamera>())
        {
            auto transform = reg.GetComponent<ECS::CCamera, ECS::TransformComponent>(index++);
            const auto &world_matrix = transform->GetWorldMatrix();
            component._camera.Position(Vector3f(world_matrix[3][0], world_matrix[3][1], world_matrix[3][2]));
            component._camera.Rotation(Quaternion::FromMat4f(world_matrix));
            component._camera.RecalculateMatrix(true);
        }
    }

    void Scene::UpdateAccelerationStructures()
    {
        RebuildBVHTree();
    }

    void Scene::UpdateGpuSceneIfNeeded()
    {
        if (!_dirty)
            return;

        UpdateGpuScene();
        _dirty = false;
    }

    void Scene::EndUpdate()
    {
        _register.FlushDestroy();
        DeletePendingEntities();
    }

    void Scene::Update(f32 dt)
    {
        BeginUpdate();
        UpdateScripts(dt);
        _register.ExecutePhase(ECS::ESystemPhase::kAnimation, dt);
        _register.ExecutePhase(ECS::ESystemPhase::kGameplay, dt);
    }

    void Scene::LateUpdate(f32 delta_time, f32 render_alpha)
    {
        BeginLateUpdate();
        UpdateLateScripts(delta_time, render_alpha);
        _register.ExecutePhase(ECS::ESystemPhase::kTransform, delta_time);
        _register.ExecutePhase(ECS::ESystemPhase::kPostAnimation, delta_time);
        UpdateRenderTransforms(render_alpha);
        UpdateBounds();
        UpdateCameras();
        _register.ExecutePhase(ECS::ESystemPhase::kRenderData, delta_time);
        UpdateAccelerationStructures();
        EndUpdate();
        UpdateGpuSceneIfNeeded();
    }

    static Vector<LBVHNode> s_temp_tlas_gpu_data;

    void Scene::RebuildBVHTree()
    {
        PROFILE_BLOCK_CPU("Scene::RebuildBVHTree")
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
        //AL_ASSERT(_tlas_nodes.size() < RenderConstants::kMaxRenderObjectCount * 2);
        for (u64 i = 0; i < node_size; i++)
        {
            if (_tlas_nodes[i].IsLeaf())
                _tlas_nodes[i]._child_index_or_first = ret._reordered_indices[_tlas_nodes[i]._child_index_or_first];
            const auto &node = _tlas_nodes[i];
            s_temp_tlas_gpu_data.emplace_back(node._aabb._min, (f32) node._child_index_or_first, node._aabb._max, (f32) node._count_or_flag);
        }
        _tlas_buffer->SetData(reinterpret_cast<const u8 *>(s_temp_tlas_gpu_data.data()), (u32) (node_size * sizeof(LBVHNode)));
        s_temp_tlas_gpu_data.clear();
    }

    void Scene::UpdateGpuScene()
    {
        _triangle_count = 0u;
        u64 mesh_bvh_node_count = 0u;
        _bvh_nodes_range.clear();
        _mesh_bvh_node_triangle_offset.clear();
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
                auto *mesh = c._p_mesh.get();
                if (mesh == nullptr)
                    continue;

                const auto mesh_triangles = mesh->GetTriangleData();
                const auto mesh_bvh = mesh->GetBVHNodes();
                for (u16 submesh_index = 0u; submesh_index < mesh->SubmeshCount(); ++submesh_index)
                {
                    const u32 current_tri_count = mesh->GetTriangleCount(submesh_index);
                    const u32 mesh_tri_start = mesh->GetTriangleStart(submesh_index);
                    const u32 cur_bvh_node_count = mesh->GetBVHNodeCount(submesh_index);
                    const u32 mesh_bvh_start = mesh->GetBVHNodeStart(submesh_index);
                    if (current_tri_count == 0u || cur_bvh_node_count == 0u)
                        continue;

                    triangles.insert(triangles.end(), mesh_triangles.begin() + mesh_tri_start, mesh_triangles.begin() + mesh_tri_start + current_tri_count);
                    mesh_bvh_nodes.insert(mesh_bvh_nodes.end(), mesh_bvh.begin() + mesh_bvh_start, mesh_bvh.begin() + mesh_bvh_start + cur_bvh_node_count);

                    const u64 key = (static_cast<u64>(current_entity) << 32u) | static_cast<u64>(submesh_index);
                    _bvh_nodes_range[key] = Vector2UInt{ static_cast<u32>(bvh_offset), cur_bvh_node_count };
                    _mesh_bvh_node_triangle_offset[key] = static_cast<u32>(triangle_offset);
                    triangle_offset += current_tri_count;
                    bvh_offset += cur_bvh_node_count;
                }
            }
            if (_scene_mesh_data)
            {
                _scene_mesh_data.reset();
                _blas_buffer.reset();
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
        }
    }
    #pragma endregion

    #pragma region SceneMgr---------------------------------------------------------------------------- 

    static SceneMgr* s_scene_mgr;
    SceneMgr& SceneMgr::Get()
    {
        return *s_scene_mgr;
    }
    void SceneMgr::Init()
    {
        if (!s_scene_mgr)
        {
            s_scene_mgr = new SceneMgr();
        }
    }
    void SceneMgr::Shutdown()
    {

    }

    SceneMgr::SceneMgr()
    {
        TIMER_BLOCK("-----------------------------------------------------------SceneMgr::Initialize")
    }
    SceneMgr::~SceneMgr()
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
    void SceneMgr::FixedUpdate(f32 fixed_delta_time)
    {
        if (_p_current)
            _p_current->FixedUpdate(fixed_delta_time);
    }

    void SceneMgr::Update(f32 delta_time)
    {
        if (_p_current)
            _p_current->Update(delta_time);
    }

    void SceneMgr::LateUpdate(f32 delta_time, f32 render_alpha)
    {
        if (_p_current)
            _p_current->LateUpdate(delta_time, render_alpha);
    }

    void SceneMgr::Tick(f32 delta_time)
    {
        Update(delta_time);
        LateUpdate(delta_time, 0.0f);
    }

    Ref<Scene> SceneMgr::OpenScene(const WString &scene_path)
    {
        AL_ASSERT(!scene_path.empty());
        if (auto it = _all_scene.find(scene_path); it != _all_scene.end())
        {
            _p_current = it->second.get();
            return it->second;
        }
        auto s = ResourceMgr::Get().Load<Scene>(scene_path);
        _all_scene[scene_path] = s;
        _p_current = s.get();
        return s;
        //bool test = false;
        //if (test)
        //{
        //    Ref<Scene> default_scene = MakeRef<Scene>("DefaultScene");
        //    auto p = default_scene->AddObject("empty");
        //    auto child1 = default_scene->AddObject(ResourceMgr::Get().GetRef<Mesh>(L"Meshs/src_res/plane.alasset"), ResourceMgr::Get().GetRef<Material>(L"Materials/StandardPBR.alasset"));
        //    default_scene->Attach(child1, p);
        //    {
        //        auto &comp = default_scene->GetRegister().AddComponent<LightComponent>(default_scene->AddObject("directional_light"));
        //        comp._type = ELightType::kDirectional;
        //        comp._light._light_color = Colors::kWhite;
        //    }
        //    auto cube = default_scene->AddObject(Mesh::s_cube.lock(), ResourceMgr::Get().GetRef<Material>(L"Materials/StandardPBR.alasset"));
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
        delete _runtime_scene; _runtime_scene = nullptr;
        Application::Get()._is_playing_mode = false;
    }
    void SceneMgr::EnterSimulateMode()
    {
        auto &r = _p_current->GetRegister();
        for (auto &t: r.View<ECS::TransformComponent>())
        {
            _transform_cache.emplace_back(t._local_transform);
        }
        Application::Get()._is_simulate_mode = true;
    }
    void SceneMgr::ExitSimulateMode()
    {
        auto &r = _p_current->GetRegister();
        u32 index = 0;
        for (auto &t: r.View<ECS::TransformComponent>())
        {
            t._local_transform = _transform_cache[index++];
        }
        for (auto &c: r.View<ECS::CRigidBody>())
        {
            c._velocity = Vector3f::kZero;
            c._angular_velocity = Vector3f::kZero;
        }
        Application::Get()._is_simulate_mode = false;
        _transform_cache.clear();
    }
    #pragma endregion
    //-----------------------------------------------------------------------SceneMgr----------------------------------------------------------------------------
}// namespace Ailu
