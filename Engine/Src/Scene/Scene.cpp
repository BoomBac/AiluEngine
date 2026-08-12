#include "Scene/Scene.h"
#include "Animation/AnimationSystem.h"
#include "Audio/AudioSystem.h"
#include "Framework/Common/Application.h"
#include "Framework/Math/QuaternionMatrix.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Script/ScriptSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Physics/2D/Physics2DSystem.h"
#include "Scene/RenderSystem.h"
#include "Scene/EntitySerializer.h"
#include "Scene/TransformSystem.h"
//#include "pch.h"
#include <regex>

#include "Render/ShaderInterop.h"//lbvh
#include "Render/Gizmo.h"
#include "Render/RenderPipeline.h"
#include "Scene/SceneCommand.h"

using namespace Ailu::Render;

namespace Ailu::SceneManagement
{
    #pragma region Scene----------------------------------------------------------------------------
    Scene::Scene(const String &name, bool create_render_resources) : Object(name)
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
        _register.RegisterComponent<ECS::RigidBody2DComponent>();
        _register.RegisterComponent<ECS::Collider2DComponent>();
        _register.RegisterComponent<ECS::CSkeletonMesh>();
        _register.RegisterComponent<ECS::CVXGI>();
        _register.RegisterComponent<ECS::SpriteRendererComponent>();
        _register.RegisterComponent<ECS::AudioSourceComponent>();
        _register.RegisterComponent<ECS::AudioListenerComponent>();
        if (!create_render_resources)
            return;

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
        ECS::Signature phy_2d_sig;
        phy_2d_sig.set(_register.GetComponentTypeID<ECS::TransformComponent>(), true);
        phy_2d_sig.set(_register.GetComponentTypeID<ECS::Collider2DComponent>(), true);
        _register.RegisterSystem<ECS::Physics2DSystem>(phy_2d_sig);
        ECS::Signature anim_sig;
        anim_sig.set(_register.GetComponentTypeID<ECS::CSkeletonMesh>(), true);
        _register.RegisterSystem<ECS::AnimationSystem>(anim_sig);
        ECS::Signature audio_sig;
        audio_sig.set(_register.GetComponentTypeID<ECS::TransformComponent>(), true);
        _register.RegisterSystem<ECS::AudioSystem>(audio_sig);
        _register.RegisterOnComponentAdd<ECS::StaticMeshComponent>([](ECS::Entity entity){ RenderPipeline::Get().OnAddRenderObject(entity);
        });
        _register.RegisterOnComponentAdd<ECS::CSkeletonMesh>([](ECS::Entity entity){ RenderPipeline::Get().OnAddRenderObject(entity);});
        if (create_render_resources)
        {
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
    }

    Scene::~Scene()
    {
        Clear();
    }

    // =========================================================================
    // Hierarchy Helpers
    // =========================================================================
    void Scene::TouchStructure()
    {
        ++_structure_revision;
        ++_edit_revision;
        MarkDirty();
    }

    bool Scene::IsValidEntity(ECS::Entity entity) const
    {
        return _register.IsAlive(entity);
    }

    // =========================================================================
    // Entity GUID identity
    // =========================================================================
    ECS::Entity Scene::FindEntity(const Guid &guid) const
    {
        if (guid.IsEmpty())
            return ECS::kInvalidEntity;
        const auto it = _guid_to_entity.find(guid);
        if (it == _guid_to_entity.end())
            return ECS::kInvalidEntity;
        const ECS::Entity entity = it->second;
        if (!_register.IsAlive(entity))
            return ECS::kInvalidEntity;
        return entity;
    }

    const Guid &Scene::GetEntityGuid(ECS::Entity entity) const
    {
        if (entity == ECS::kInvalidEntity || !_register.IsAlive(entity))
            return Guid::EmptyGuid();
        const auto *id_comp = _register.GetComponent<ECS::PersistentIdComponent>(entity);
        return id_comp ? id_comp->_guid : Guid::EmptyGuid();
    }

    const Guid *Scene::FindEntityGuid(ECS::Entity entity) const
    {
        if (entity == ECS::kInvalidEntity || !_register.IsAlive(entity))
            return nullptr;
        const auto *id_comp = _register.GetComponent<ECS::PersistentIdComponent>(entity);
        return id_comp ? &id_comp->_guid : nullptr;
    }

    bool Scene::HasEntityGuid(const Guid &guid) const
    {
        return _guid_to_entity.contains(guid);
    }

    bool Scene::ValidateEntityGuidIndex() const
    {
        bool is_valid = true;
        for (const auto &[guid, entity] : _guid_to_entity)
        {
            if (!_register.IsAlive(entity))
            {
                LOG_ERROR("ValidateEntityGuidIndex: entry for guid {} points to dead entity {}", guid.ToString(), entity);
                is_valid = false;
                continue;
            }
            const auto *id_comp = _register.GetComponent<ECS::PersistentIdComponent>(entity);
            if (id_comp == nullptr)
            {
                LOG_ERROR("ValidateEntityGuidIndex: entity {} has no PersistentIdComponent but is indexed", entity);
                is_valid = false;
                continue;
            }
            if (!(id_comp->_guid == guid))
            {
                LOG_ERROR("ValidateEntityGuidIndex: entity {} guid {} mismatches index key {}", entity, id_comp->_guid.ToString(), guid.ToString());
                is_valid = false;
            }
        }
        u32 index = 0u;
        for (const auto &id_comp : _register.View<ECS::PersistentIdComponent>())
        {
            const ECS::Entity entity = _register.GetEntity<ECS::PersistentIdComponent>(index++);
            if (!_guid_to_entity.contains(id_comp._guid))
            {
                LOG_ERROR("ValidateEntityGuidIndex: entity {} guid {} is not indexed", entity, id_comp._guid.ToString());
                is_valid = false;
            }
        }
        return is_valid;
    }

    Guid Scene::GenerateUniqueEntityGuid() const
    {
        Guid guid = Guid::Generate();
        while (_guid_to_entity.contains(guid))
        {
            LOG_WARNING("GenerateUniqueEntityGuid: guid collision {}, regenerating", guid.ToString());
            guid = Guid::Generate();
        }
        return guid;
    }

    bool Scene::RegisterEntityGuid(ECS::Entity entity, const Guid &guid)
    {
        if (guid.IsEmpty())
        {
            LOG_ERROR("RegisterEntityGuid: refused to register empty guid for entity {}", entity);
            return false;
        }
        if (!_register.IsAlive(entity))
        {
            LOG_ERROR("RegisterEntityGuid: refused to register guid {} for dead entity {}", guid.ToString(), entity);
            return false;
        }
        const auto it = _guid_to_entity.find(guid);
        if (it != _guid_to_entity.end() && it->second != entity)
        {
            LOG_ERROR("RegisterEntityGuid: guid {} already used by entity {}, refuse to register entity {}", guid.ToString(), it->second, entity);
            return false;
        }
        _guid_to_entity[guid] = entity;
        return true;
    }

    void Scene::UnregisterEntityGuid(ECS::Entity entity)
    {
        const auto *id_comp = _register.GetComponent<ECS::PersistentIdComponent>(entity);
        if (id_comp == nullptr)
            return;
        const auto it = _guid_to_entity.find(id_comp->_guid);
        if (it != _guid_to_entity.end() && it->second == entity)
            _guid_to_entity.erase(it);
    }

    void Scene::RebuildEntityGuidIndex()
    {
        _guid_to_entity.clear();
        u32 index = 0u;
        for (const auto &id_comp : _register.View<ECS::PersistentIdComponent>())
        {
            const ECS::Entity entity = _register.GetEntity<ECS::PersistentIdComponent>(index++);
            if (id_comp._guid.IsEmpty())
                continue;
            if (!_guid_to_entity.emplace(id_comp._guid, entity).second)
                LOG_ERROR("RebuildEntityGuidIndex: duplicate guid {} detected, entity {} dropped", id_comp._guid.ToString(), entity);
        }
    }

    bool Scene::EnsureValidEntityIdentities()
    {
        bool repaired = false;
        // 以 TagComponent 视图近似“所有场景实体”：CreateEntityInternal 会同时添加 Tag 与 PersistentId。
        u32 index = 0u;
        Vector<ECS::Entity> entities;
        for (const auto &tag : _register.View<ECS::TagComponent>())
        {
            entities.push_back(_register.GetEntity<ECS::TagComponent>(index++));
        }
        for (ECS::Entity entity : entities)
        {
            auto *id_comp = _register.GetComponent<ECS::PersistentIdComponent>(entity);
            if (id_comp == nullptr || id_comp->_guid.IsEmpty())
            {
                Guid new_guid = GenerateUniqueEntityGuid();
                if (id_comp == nullptr)
                {
                    _register.AddComponent<ECS::PersistentIdComponent>(entity, new_guid);
                }
                else
                {
                    id_comp->_guid = new_guid;
                }
                LOG_ERROR("EnsureValidEntityIdentities: entity {} had missing/empty guid, generated {}", entity, new_guid.ToString());
                repaired = true;
            }
        }
        RebuildEntityGuidIndex();
        if (repaired)
            return ValidateEntityGuidIndex();
        return true;
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

    bool Scene::IsEntityEnabled(ECS::Entity entity) const
    {
        return _register.IsEntityEnabled(entity);
    }

    void Scene::RefreshEntityEnabledInHierarchy(ECS::Entity entity, bool parent_enabled)
    {
        auto *hierarchy = _register.GetComponent<ECS::CHierarchy>(entity);
        if (hierarchy == nullptr)
            return;

        hierarchy->_enabled_in_hierarchy = parent_enabled && hierarchy->_enabled;
        ECS::Entity child = hierarchy->_first_child;
        while (child != ECS::kInvalidEntity)
        {
            const auto *child_hierarchy = _register.GetComponent<ECS::CHierarchy>(child);
            const ECS::Entity next = child_hierarchy != nullptr ? child_hierarchy->_next_sibling : ECS::kInvalidEntity;
            RefreshEntityEnabledInHierarchy(child, hierarchy->_enabled_in_hierarchy);
            child = next;
        }
    }

    bool Scene::SetEntityEnabled(ECS::Entity entity, bool enabled)
    {
        auto *hierarchy = _register.GetComponent<ECS::CHierarchy>(entity);
        if (hierarchy == nullptr || hierarchy->_enabled == enabled)
            return false;

        hierarchy->_enabled = enabled;
        const bool parent_enabled = hierarchy->_parent == ECS::kInvalidEntity || IsEntityEnabled(hierarchy->_parent);
        RefreshEntityEnabledInHierarchy(entity, parent_enabled);
        MarkEdited();
        return true;
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

        const bool parent_enabled = new_parent == ECS::kInvalidEntity || IsEntityEnabled(new_parent);
        RefreshEntityEnabledInHierarchy(child, parent_enabled);
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

    ECS::Entity Scene::CreateEntityInternal(String name, const Guid &requested_guid)
    {
        ECS::Entity entity = _register.Create();
        const Guid guid = requested_guid.IsValid() ? requested_guid : GenerateUniqueEntityGuid();
        _register.AddComponent<ECS::PersistentIdComponent>(entity, guid);
        if (!RegisterEntityGuid(entity, guid))
        {
            LOG_ERROR("CreateEntityInternal: failed to register guid {} for entity {}", guid.ToString(), entity);
        }
        if (name.empty())
            name = AcquireName();
        _register.AddComponent<ECS::TagComponent>(entity, name);
        _register.AddComponent<ECS::TransformComponent>(entity);
        _register.AddComponent<ECS::CHierarchy>(entity);
        return entity;
    }

    ECS::Entity Scene::AddObject(Ref<Mesh> mesh, Ref<Material> mat)
    {
        ECS::Entity obj = CreateEntityInternal("", Guid::EmptyGuid());
        auto &comp = _register.AddComponent<ECS::StaticMeshComponent>(obj);
        comp._p_mesh = mesh ? mesh : Mesh::s_plane.lock();
        comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
        comp._p_mats.emplace_back(mat ? mat : Material::s_standard_defered_lit.lock());
        TouchStructure();
        return obj;
    }
    ECS::Entity Scene::AddObject(Ref<Mesh> mesh, const Vector<Ref<Material>> &mats)
    {
        ECS::Entity obj = CreateEntityInternal("", Guid::EmptyGuid());
        auto &comp = _register.AddComponent<ECS::StaticMeshComponent>(obj);
        comp._p_mesh = mesh ? mesh : Mesh::s_plane.lock();
        comp._transformed_aabbs.resize(comp._p_mesh->SubmeshCount() + 1);
        comp._p_mats = mats;
        TouchStructure();
        return obj;
    }
    ECS::Entity Scene::AddObject(String name)
    {
        const ECS::Entity entity = CreateEntityInternal(std::move(name), Guid::EmptyGuid());
        TouchStructure();
        return entity;
    }
    ECS::Entity Scene::AddObject(String name, const Guid &requested_guid)
    {
        const ECS::Entity entity = CreateEntityInternal(std::move(name), requested_guid);
        TouchStructure();
        return entity;
    }
    ECS::Entity Scene::DuplicateEntity(ECS::Entity e)
    {
        ECS::Entity new_one = EntitySerializer::CloneEntity(*this, e);
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
                ScriptSystem::Get().DestroyComponent(this, actor, *script_comp);
            }
            UnregisterEntityGuid(actor);
            _register.Destory(actor);
        }

        _pending_delete_entities.clear();
        TouchStructure();
    }

    void Scene::Clear()
    {
        Vector<ECS::Entity> entities;
        u32 index = 0u;
        for (const auto &id_comp : _register.View<ECS::PersistentIdComponent>())
        {
            entities.push_back(_register.GetEntity<ECS::PersistentIdComponent>(index++));
        }
        for (ECS::Entity entity : entities)
        {
            if (auto *script_comp = _register.GetComponent<ECS::ScriptComponent>(entity))
                ScriptSystem::Get().DestroyComponent(this, entity, *script_comp);
            UnregisterEntityGuid(entity);
            _register.Destory(entity);
        }
        _guid_to_entity.clear();
        _pending_delete_entities.clear();
        TouchStructure();
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

        if (auto *physics_2d_system = _register.GetSystem<ECS::Physics2DSystem>())
            physics_2d_system->Synchronize(_register);

        UpdateFixedScripts(fixed_delta_time);
        _register.ExecutePhase(ECS::ESystemPhase::kPrePhysics, fixed_delta_time);
        _register.ExecutePhase(ECS::ESystemPhase::kPhysics, fixed_delta_time);
        _register.ExecutePhase(ECS::ESystemPhase::kPostPhysics, fixed_delta_time);
    }

    void Scene::UpdateFixedScripts(f32 fixed_delta_time)
    {
        if (!Application::Get()._is_playing_mode)
            return;

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
        if (!Application::Get()._is_playing_mode)
            return;

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
        if (!Application::Get()._is_playing_mode)
            return;

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
            const ECS::Entity entity = reg.GetEntity<ECS::StaticMeshComponent>(index);
            if (!IsEntityEnabled(entity) || !reg.IsComponentEnabled<ECS::StaticMeshComponent>(entity))
            {
                ++index;
                continue;
            }
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
            const ECS::Entity entity = reg.GetEntity<ECS::CSkeletonMesh>(index);
            if (!IsEntityEnabled(entity) || !reg.IsComponentEnabled<ECS::CSkeletonMesh>(entity))
            {
                ++index;
                continue;
            }
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
        Camera::sMain = FindMainCamera();
        if (Application::Get()._is_playing_mode && Camera::sMain != nullptr)
            Camera::sCurrent = Camera::sMain;
        u32 index = 0;
        for (auto &component: reg.View<ECS::CCamera>())
        {
            const ECS::Entity entity = reg.GetEntity<ECS::CCamera>(index);
            auto transform = reg.GetComponent<ECS::CCamera, ECS::TransformComponent>(index++);
            if (!IsEntityEnabled(entity) || !reg.IsComponentEnabled<ECS::CCamera>(entity))
                continue;
            const auto &world_matrix = transform->GetWorldMatrix();
            component._camera.Position(Vector3f(world_matrix[3][0], world_matrix[3][1], world_matrix[3][2]));
            component._camera.Rotation(Quaternion::FromMat4f(world_matrix));
            component._camera.RecalculateMatrix(true);
        }
    }

    Camera *Scene::FindMainCamera()
    {
        auto &reg = _register;
        u32 index = 0;
        for (auto &camera : reg.View<ECS::CCamera>())
        {
            const ECS::Entity entity = reg.GetEntity<ECS::CCamera>(index++);
            const auto *tag = reg.GetComponent<ECS::TagComponent>(entity);
            if (tag != nullptr && tag->_tag == "MainCamera" && IsEntityEnabled(entity) &&
                reg.IsComponentEnabled<ECS::CCamera>(entity))
                return &camera._camera;
        }
        return nullptr;
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

        if (auto *physics_2d_system = _register.GetSystem<ECS::Physics2DSystem>())
            physics_2d_system->Synchronize(_register);

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
            const ECS::Entity entity = _register.GetEntity<ECS::StaticMeshComponent>(index++);
            if (!IsEntityEnabled(entity) || !_register.IsComponentEnabled<ECS::StaticMeshComponent>(entity))
                continue;
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
        u64 entity_idx = 0u;
        for (auto& c: _register.View<ECS::StaticMeshComponent>())
        {
            const ECS::Entity entity = _register.GetEntity<ECS::StaticMeshComponent>(entity_idx++);
            if (!IsEntityEnabled(entity) || !_register.IsComponentEnabled<ECS::StaticMeshComponent>(entity))
                continue;
            if (c._p_mesh == nullptr)
                continue;
            _triangle_count += c._p_mesh->GetTriangleCount();
            mesh_bvh_node_count += c._p_mesh->GetBVHNodes().size();
        }
        if (_triangle_count > 0)
        {
            entity_idx = 0u;
            Vector<Render::TriangleData> triangles;
            triangles.reserve(_triangle_count);
            Vector<BVHNode> mesh_bvh_nodes;
            mesh_bvh_nodes.reserve(mesh_bvh_node_count);
            u64 triangle_offset = 0u,bvh_offset = 0u;
            for (auto &c: _register.View<ECS::StaticMeshComponent>())
            {
                auto current_entity = _register.GetEntity<ECS::StaticMeshComponent>(entity_idx++);
                if (!IsEntityEnabled(current_entity) || !_register.IsComponentEnabled<ECS::StaticMeshComponent>(current_entity))
                    continue;
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
        RenderPipeline::Get().SetPreviewCamera(nullptr, nullptr);
        Application::Get()._is_playing_mode = true;
        _runtime_scene = new Scene(*_p_current);
        auto name = _runtime_scene->Name();
        name.append("_copy");
        _runtime_scene->Name(name);
        _runtime_scene->RebuildEntityGuidIndex();
        _runtime_scene_src = _p_current;
        _p_current = _runtime_scene;
        Camera::sMain = _runtime_scene->FindMainCamera();
        Camera::sCurrent = Camera::sMain != nullptr ? Camera::sMain : Camera::sScene;
    }

    void SceneMgr::ExitPlayMode()
    {
        _p_current = _runtime_scene_src;
        _runtime_scene_src = nullptr;
        if (Application::Get()._is_multi_thread_rendering.load())
        {
            // The render thread may still be recording draw commands captured from the runtime scene.
            // Keep it alive until RenderPipeline has waited for that frame to finish recording.
            _retired_runtime_scene = _runtime_scene;
            _runtime_scene = nullptr;
        }
        else
        {
            delete _runtime_scene;
            _runtime_scene = nullptr;
        }
        Camera::sMain = nullptr;
        Camera::sCurrent = Camera::sScene;
        Application::Get()._is_playing_mode = false;
    }

    void SceneMgr::ReleaseRetiredRuntimeScene()
    {
        if (_retired_runtime_scene == nullptr)
            return;

        delete _retired_runtime_scene;
        _retired_runtime_scene = nullptr;
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
