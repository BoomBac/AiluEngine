#include "Scene/PrefabSystem.h"

#include "Assets/PrefabAsset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/2D/Sprite.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Scene/Component.h"
#include "Scene/EntitySerializer.h"

namespace Ailu::SceneManagement
{
    namespace
    {
        bool IsPrefabComponentPresent(const SceneEntityDocument &entity_doc, StringView component_name)
        {
            if (component_name == ECS::ScriptComponent::StaticTypeName()) return entity_doc._has_script_component;
            if (component_name == ECS::StaticMeshComponent::StaticTypeName()) return entity_doc._has_static_mesh_component;
            if (component_name == ECS::LightComponent::StaticTypeName()) return entity_doc._has_light_component;
            if (component_name == ECS::CCamera::StaticTypeName()) return entity_doc._has_camera_component;
            if (component_name == ECS::CLightProbe::StaticTypeName()) return entity_doc._has_lightprobe_component;
            if (component_name == ECS::CRigidBody::StaticTypeName()) return entity_doc._has_rigidbody_component;
            if (component_name == ECS::CCollider::StaticTypeName()) return entity_doc._has_collider_component;
            if (component_name == ECS::RigidBody2DComponent::StaticTypeName()) return entity_doc._has_rigidbody_2d_component;
            if (component_name == ECS::Collider2DComponent::StaticTypeName()) return entity_doc._has_collider_2d_component;
            if (component_name == ECS::CVXGI::StaticTypeName()) return entity_doc._has_vxgi_component;
            if (component_name == ECS::SpriteRendererComponent::StaticTypeName()) return entity_doc._has_sprite_renderer_component;
            return true;
        }

        bool IsComponentDisabled(const SceneEntityDocument &entity_doc, StringView component_name)
        {
            return std::find(entity_doc._disabled_components.begin(), entity_doc._disabled_components.end(), component_name) !=
                   entity_doc._disabled_components.end();
        }

        PrefabInstance *FindPrefabInstance(Scene &scene, ECS::Entity entity)
        {
            for (ECS::Entity current = entity; current != ECS::kInvalidEntity;)
            {
                const Guid *guid = scene.FindEntityGuid(current);
                if (guid == nullptr)
                    return nullptr;
                for (PrefabInstance &instance : scene.MutablePrefabInstances())
                    if (instance._root_entity == *guid)
                        return &instance;
                const auto *hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(current);
                current = hierarchy != nullptr ? hierarchy->_parent : ECS::kInvalidEntity;
            }
            return nullptr;
        }

        const PrefabEntityDocument *FindPrefabEntity(const PrefabAssetDocument &prefab, const Guid &prefab_guid)
        {
            const auto it = std::find_if(prefab._entities.begin(), prefab._entities.end(),
                                         [&prefab_guid](const PrefabEntityDocument &entity) { return entity._guid == prefab_guid; });
            return it != prefab._entities.end() ? &*it : nullptr;
        }

        ECS::Entity FindInstanceEntity(Scene &scene, ECS::Entity root_entity, const Guid &prefab_guid)
        {
            Vector<ECS::Entity> pending{root_entity};
            for (size_t index = 0u; index < pending.size(); ++index)
            {
                const ECS::Entity entity = pending[index];
                const auto *tag = scene.GetRegister().GetComponent<ECS::TagComponent>(entity);
                if (tag != nullptr && tag->_prefab_entity == prefab_guid)
                    return entity;
                const auto *hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(entity);
                if (hierarchy == nullptr)
                    continue;
                for (ECS::Entity child = hierarchy->_first_child; child != ECS::kInvalidEntity;)
                {
                    const auto *child_hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(child);
                    pending.emplace_back(child);
                    child = child_hierarchy != nullptr ? child_hierarchy->_next_sibling : ECS::kInvalidEntity;
                }
            }
            return ECS::kInvalidEntity;
        }

        bool IsSameOverride(const PrefabOverride &override, const Guid &prefab_entity, StringView component, StringView property)
        {
            return override._prefab_entity == prefab_entity && override._component == component && override._property == property;
        }
    }

    Ref<PrefabAssetDocument> PrefabSystem::CreatePrefabDocument(const Scene &scene, ECS::Entity root_entity)
    {
        auto prefab = MakeRef<PrefabAssetDocument>();
        if (!EntitySerializer::SerializeSubtree(scene, root_entity, *prefab))
            return nullptr;
        return prefab;
    }

    bool PrefabSystem::UpdatePrefabDocument(const Scene &scene, ECS::Entity root_entity, PrefabAssetDocument &prefab)
    {
        auto updated = CreatePrefabDocument(scene, root_entity);
        if (updated == nullptr || updated->_entities.empty())
            return false;

        Vector<ECS::Entity> runtime_entities;
        Vector<ECS::Entity> pending{root_entity};
        for (size_t index = 0u; index < pending.size(); ++index)
        {
            const ECS::Entity entity = pending[index];
            runtime_entities.emplace_back(entity);
            const auto *hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(entity);
            if (hierarchy == nullptr)
                continue;
            for (ECS::Entity child = hierarchy->_first_child; child != ECS::kInvalidEntity;)
            {
                pending.emplace_back(child);
                const auto *child_hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(child);
                child = child_hierarchy != nullptr ? child_hierarchy->_next_sibling : ECS::kInvalidEntity;
            }
        }
        if (runtime_entities.size() != updated->_entities.size())
            return false;

        HashMap<Guid, Guid, GuidHasher> guid_remap;
        std::unordered_set<Guid, GuidHasher> used_guids;
        guid_remap.reserve(updated->_entities.size());
        for (u32 index = 0u; index < updated->_entities.size(); ++index)
        {
            const Guid generated_guid = updated->_entities[index]._guid;
            Guid stable_guid = generated_guid;
            const auto *tag = scene.GetRegister().GetComponent<ECS::TagComponent>(runtime_entities[index]);
            if (tag != nullptr && !tag->_prefab_entity.IsEmpty() && !used_guids.contains(tag->_prefab_entity))
                stable_guid = tag->_prefab_entity;
            used_guids.insert(stable_guid);
            guid_remap.emplace(generated_guid, stable_guid);
        }

        for (PrefabEntityDocument &entity : updated->_entities)
        {
            const Guid old_guid = entity._guid;
            entity._guid = guid_remap.at(old_guid);
            entity._entity._entity_guid = entity._guid;
            if (!entity._parent.IsEmpty())
                entity._parent = guid_remap.at(entity._parent);
            if (entity._entity._has_hierarchy_component && !entity._entity._hierarchy_component._parent_guid.IsEmpty())
                entity._entity._hierarchy_component._parent_guid = guid_remap.at(entity._entity._hierarchy_component._parent_guid);
        }
        updated->_root = guid_remap.at(updated->_root);
        prefab._root = updated->_root;
        prefab._entities = std::move(updated->_entities);
        return true;
    }

    PrefabInstantiateResult PrefabSystem::Instantiate(Scene &scene, const PrefabAssetDocument &prefab)
    {
        PrefabInstantiateResult result;
        if (prefab._root.IsEmpty() || prefab._entities.empty())
            return result;

        auto &registry = scene.GetRegister();
        HashMap<Guid, ECS::Entity, GuidHasher> entity_map;
        entity_map.reserve(prefab._entities.size());
        for (const PrefabEntityDocument &prefab_entity : prefab._entities)
        {
            if (prefab_entity._guid.IsEmpty() || entity_map.contains(prefab_entity._guid))
                return {};
            ECS::Entity entity = scene.AddObject(prefab_entity._name);
            auto *tag = registry.GetComponent<ECS::TagComponent>(entity);
            tag->_name = prefab_entity._entity._tag_component._name.empty() ? prefab_entity._name : prefab_entity._entity._tag_component._name;
            tag->_tag = prefab_entity._entity._tag_component._tag;
            tag->_layer_mask = prefab_entity._entity._tag_component._layer_mask;
            tag->_prefab_entity = prefab_entity._guid;
            entity_map.emplace(prefab_entity._guid, entity);
        }

        auto is_component_disabled = [](const SceneEntityDocument &entity_doc, StringView component_name)
        {
            return std::find(entity_doc._disabled_components.begin(), entity_doc._disabled_components.end(), component_name) !=
                   entity_doc._disabled_components.end();
        };
        auto load_materials = [](const Vector<String> &material_guids, Mesh *mesh, Vector<Ref<Material>> &materials)
        {
            materials.clear();
            materials.reserve(material_guids.size());
            for (u32 index = 0u; index < material_guids.size(); ++index)
            {
                Ref<Material> material;
                if (!material_guids[index].empty())
                {
                    const Guid material_guid(material_guids[index]);
                    ResourceMgr::Get().Load<Material>(material_guid);
                    material = ResourceMgr::Get().GetRef<Material>(material_guid);
                }
                if (material == nullptr && mesh != nullptr)
                    material = ResourceMgr::Get().GetEmbeddedMaterial(mesh, static_cast<u16>(index));
                if (material != nullptr)
                    materials.emplace_back(std::move(material));
            }
        };

        for (const PrefabEntityDocument &prefab_entity : prefab._entities)
        {
            const ECS::Entity entity = entity_map.at(prefab_entity._guid);
            const SceneEntityDocument &entity_doc = prefab_entity._entity;
            if (entity_doc._has_transform_component)
            {
                auto *component = registry.GetComponent<ECS::TransformComponent>(entity);
                component->SetLocalPosition(entity_doc._transform_component._position);
                component->SetLocalRotation(entity_doc._transform_component._rotation);
                component->SetLocalScale(entity_doc._transform_component._scale);
            }
            if (entity_doc._has_script_component)
            {
                auto &component = registry.AddComponent<ECS::ScriptComponent>(entity);
                component._script_asset = entity_doc._script_component._script_asset;
                component._properties = entity_doc._script_component._properties;
                registry.SetComponentEnabled<ECS::ScriptComponent>(entity, !IsComponentDisabled(entity_doc, "ScriptComponent"));
            }
            if (entity_doc._has_static_mesh_component)
            {
                auto &component = registry.AddComponent<ECS::StaticMeshComponent>(entity);
                if (!entity_doc._static_mesh_component._mesh_guid.empty())
                {
                    const Guid mesh_guid(entity_doc._static_mesh_component._mesh_guid);
                    ResourceMgr::Get().Load<Mesh>(mesh_guid);
                    component._p_mesh = ResourceMgr::Get().GetRef<Mesh>(mesh_guid);
                    if (component._p_mesh != nullptr)
                        component._transformed_aabbs.resize(component._p_mesh->SubmeshCount() + 1u);
                }
                load_materials(entity_doc._static_mesh_component._material_guids, component._p_mesh.get(), component._p_mats);
                registry.SetComponentEnabled<ECS::StaticMeshComponent>(entity,
                                                                        !is_component_disabled(entity_doc, "StaticMeshComponent"));
            }
            if (entity_doc._has_light_component)
            {
                auto &component = registry.AddComponent<ECS::LightComponent>(entity);
                if (const Enum *enum_type = StaticEnum<ECS::ELightType>(); enum_type != nullptr && !entity_doc._light_component._type.empty())
                {
                    const i32 index = enum_type->GetIndexByName(entity_doc._light_component._type);
                    if (index != -1)
                        component._type = static_cast<ECS::ELightType>(index);
                }
                component._light._light_color = entity_doc._light_component._light._light_color;
                component._light._light_param = entity_doc._light_component._light._light_param;
                component._light._is_two_side = entity_doc._light_component._light._is_two_side;
                component._shadow._is_cast_shadow = entity_doc._light_component._shadow._is_cast_shadow;
                component._shadow._constant_bias = entity_doc._light_component._shadow._constant_bias;
                component._shadow._slope_bias = entity_doc._light_component._shadow._slope_bias;
                registry.SetComponentEnabled<ECS::LightComponent>(entity, !is_component_disabled(entity_doc, "LightComponent"));
            }
            if (entity_doc._has_camera_component)
            {
                auto &component = registry.AddComponent<ECS::CCamera>(entity);
                component._camera.Type(Render::CameraTypeFromString(entity_doc._camera_component._type));
                component._camera.Aspect(entity_doc._camera_component._aspect);
                component._camera.Far(entity_doc._camera_component._far_clip);
                component._camera.Near(entity_doc._camera_component._near_clip);
                component._camera.FovH(entity_doc._camera_component._fov_h);
                component._camera.Size(entity_doc._camera_component._size);
                component._camera.MarkDirty();
                component._camera.RecalculateMatrix(true);
                registry.SetComponentEnabled<ECS::CCamera>(entity, !is_component_disabled(entity_doc, "CCamera"));
            }
            if (entity_doc._has_lightprobe_component)
            {
                auto &component = registry.AddComponent<ECS::CLightProbe>(entity);
                component._size = entity_doc._lightprobe_component._size;
                component._is_update_every_tick = entity_doc._lightprobe_component._is_update_every_tick;
                component._is_dirty = true;
                registry.SetComponentEnabled<ECS::CLightProbe>(entity, !is_component_disabled(entity_doc, "CLightProbe"));
            }
            if (entity_doc._has_rigidbody_component)
            {
                auto &component = registry.AddComponent<ECS::CRigidBody>(entity);
                component._mass = entity_doc._rigidbody_component._mass;
                registry.SetComponentEnabled<ECS::CRigidBody>(entity, !IsComponentDisabled(entity_doc, "CRigidBody"));
            }
            if (entity_doc._has_collider_component)
            {
                auto &component = registry.AddComponent<ECS::CCollider>(entity);
                if (const Enum *enum_type = StaticEnum<ECS::EColliderType>(); enum_type != nullptr && !entity_doc._collider_component._type.empty())
                {
                    const i32 index = enum_type->GetIndexByName(entity_doc._collider_component._type);
                    if (index != -1)
                        component._type = static_cast<ECS::EColliderType>(index);
                }
                component._is_trigger = entity_doc._collider_component._is_trigger;
                component._center = entity_doc._collider_component._center;
                component._param = entity_doc._collider_component._param;
                registry.SetComponentEnabled<ECS::CCollider>(entity, !IsComponentDisabled(entity_doc, "CCollider"));
            }
            if (entity_doc._has_rigidbody_2d_component)
            {
                auto &component = registry.AddComponent<ECS::RigidBody2DComponent>(entity);
                const auto &document = entity_doc._rigidbody_2d_component;
                component._type = document._type;
                component._gravity_scale = document._gravity_scale;
                component._linear_damping = document._linear_damping;
                component._angular_damping = document._angular_damping;
                component._fixed_rotation = document._fixed_rotation;
                component._continuous = document._continuous;
                component._allow_sleep = document._allow_sleep;
                registry.SetComponentEnabled<ECS::RigidBody2DComponent>(entity,
                                                                         !IsComponentDisabled(entity_doc, "RigidBody2DComponent"));
            }
            if (entity_doc._has_collider_2d_component)
            {
                auto &component = registry.AddComponent<ECS::Collider2DComponent>(entity);
                component._preset = entity_doc._collider_2d_component._preset;
                component._collision_profile = entity_doc._collider_2d_component._collision_profile;
                component._shapes = entity_doc._collider_2d_component._shapes;
                registry.SetComponentEnabled<ECS::Collider2DComponent>(entity,
                                                                         !IsComponentDisabled(entity_doc, "Collider2DComponent"));
            }
            if (entity_doc._has_animator_component)
            {
                auto &component = registry.AddComponent<ECS::AnimatorComponent>(entity);
                if (!entity_doc._animator_component._controller_guid.empty())
                    component._controller = Guid(entity_doc._animator_component._controller_guid);
                component._speed = entity_doc._animator_component._speed;
                component._play_on_awake = entity_doc._animator_component._play_on_awake;
                registry.SetComponentEnabled<ECS::AnimatorComponent>(entity,
                                                                       !is_component_disabled(entity_doc, "AnimatorComponent"));
            }
            if (entity_doc._has_vxgi_component)
            {
                auto &component = registry.AddComponent<ECS::CVXGI>(entity);
                component._grid_num = entity_doc._vxgi_component._grid_num;
                component._distance = entity_doc._vxgi_component._distance;
                registry.SetComponentEnabled<ECS::CVXGI>(entity, !is_component_disabled(entity_doc, "CVXGI"));
            }
            if (entity_doc._has_sprite_renderer_component)
            {
                auto &component = registry.AddComponent<ECS::SpriteRendererComponent>(entity);
                if (!entity_doc._sprite_renderer_component._sprite_guid.empty())
                {
                    const Guid sprite_guid(entity_doc._sprite_renderer_component._sprite_guid);
                    ResourceMgr::Get().Load<Render::Sprite>(sprite_guid);
                    component._sprite = ResourceMgr::Get().Get<Render::Sprite>(sprite_guid);
                }
                if (!entity_doc._sprite_renderer_component._material_guid.empty())
                {
                    const Guid material_guid(entity_doc._sprite_renderer_component._material_guid);
                    ResourceMgr::Get().Load<Material>(material_guid);
                    component._material = ResourceMgr::Get().GetRef<Material>(material_guid);
                }
                const Vector4f &color = entity_doc._sprite_renderer_component._color;
                component._color = Color(color.x, color.y, color.z, color.w);
                component._sorting_layer = static_cast<i16>(entity_doc._sprite_renderer_component._sorting_layer);
                component._order_in_layer = entity_doc._sprite_renderer_component._order_in_layer;
                component._blend_mode = static_cast<Render::ESpriteBlendMode>(entity_doc._sprite_renderer_component._blend_mode);
                component._flip_x = entity_doc._sprite_renderer_component._flip_x;
                component._flip_y = entity_doc._sprite_renderer_component._flip_y;
                component._visible = entity_doc._sprite_renderer_component._visible;
                registry.SetComponentEnabled<ECS::SpriteRendererComponent>(entity,
                                                                           !is_component_disabled(entity_doc, "SpriteRendererComponent"));
            }
        }

        for (const PrefabEntityDocument &prefab_entity : prefab._entities)
        {
            const auto parent_it = entity_map.find(prefab_entity._parent);
            if (parent_it != entity_map.end())
                scene.Reparent(entity_map.at(prefab_entity._guid), parent_it->second, false);
            if (!prefab_entity._enabled)
                scene.SetEntityEnabled(entity_map.at(prefab_entity._guid), false);
        }

        const auto root_it = entity_map.find(prefab._root);
        if (root_it == entity_map.end())
            return {};
        result._root = root_it->second;
        const Guid prefab_asset_guid(prefab._header._guid);
        scene.MutablePrefabInstances().emplace_back(PrefabInstance{prefab_asset_guid, scene.GetEntityGuid(result._root), {}});
        scene.MarkStructureChanged();
        return result;
    }

    bool PrefabSystem::RecordPropertyOverride(Scene &scene, ECS::Entity entity, StringView component, StringView property)
    {
        const auto *tag = scene.GetRegister().GetComponent<ECS::TagComponent>(entity);
        if (tag == nullptr || tag->_prefab_entity.IsEmpty())
            return false;
        PrefabInstance *instance = FindPrefabInstance(scene, entity);
        if (instance == nullptr)
            return false;
        const auto existing = std::find_if(instance->_overrides.begin(), instance->_overrides.end(),
                                           [tag, component, property](const PrefabOverride &override)
                                           { return IsSameOverride(override, tag->_prefab_entity, component, property); });
        if (existing == instance->_overrides.end())
            instance->_overrides.emplace_back(PrefabOverride{tag->_prefab_entity, String(component), String(property)});
        scene.MarkEdited();
        return true;
    }

    bool PrefabSystem::RevertProperty(Scene &scene, ECS::Entity entity, const PrefabAssetDocument &prefab,
                                      StringView component, StringView property)
    {
        auto *tag = scene.GetRegister().GetComponent<ECS::TagComponent>(entity);
        if (tag == nullptr || tag->_prefab_entity.IsEmpty())
            return false;
        PrefabInstance *instance = FindPrefabInstance(scene, entity);
        const PrefabEntityDocument *prefab_entity = FindPrefabEntity(prefab, tag->_prefab_entity);
        if (instance == nullptr || prefab_entity == nullptr)
            return false;

        const SceneEntityDocument &entity_doc = prefab_entity->_entity;
        bool reverted = false;
        if (component == ECS::TagComponent::StaticTypeName())
        {
            if (property == "_name")
            {
                tag->_name = entity_doc._tag_component._name;
                reverted = true;
            }
            else if (property == "_tag")
            {
                tag->_tag = entity_doc._tag_component._tag;
                reverted = true;
            }
            else if (property == "_layer_mask")
            {
                tag->_layer_mask = entity_doc._tag_component._layer_mask;
                reverted = true;
            }
        }
        else if (component == ECS::TransformComponent::StaticTypeName() && entity_doc._has_transform_component)
        {
            auto *transform = scene.GetRegister().GetComponent<ECS::TransformComponent>(entity);
            if (transform == nullptr)
                return false;
            if (property == "_local_transform" || property == "_local_transform._position")
            {
                transform->SetLocalPosition(entity_doc._transform_component._position);
                reverted = true;
            }
            if (property == "_local_transform" || property == "_local_transform._rotation")
            {
                transform->SetLocalRotation(entity_doc._transform_component._rotation);
                reverted = true;
            }
            if (property == "_local_transform" || property == "_local_transform._scale")
            {
                transform->SetLocalScale(entity_doc._transform_component._scale);
                reverted = true;
            }
        }
        else if (component == ECS::LightComponent::StaticTypeName() && entity_doc._has_light_component)
        {
            auto *light = scene.GetRegister().GetComponent<ECS::LightComponent>(entity);
            if (light == nullptr)
                return false;
            if (property == "_light")
            {
                light->_light._light_color = entity_doc._light_component._light._light_color;
                light->_light._light_param = entity_doc._light_component._light._light_param;
                light->_light._is_two_side = entity_doc._light_component._light._is_two_side;
                reverted = true;
            }
            else if (property == "_shadow")
            {
                light->_shadow._is_cast_shadow = entity_doc._light_component._shadow._is_cast_shadow;
                light->_shadow._constant_bias = entity_doc._light_component._shadow._constant_bias;
                light->_shadow._slope_bias = entity_doc._light_component._shadow._slope_bias;
                reverted = true;
            }
            else if (property == "_type" && !entity_doc._light_component._type.empty())
            {
                if (const Enum *enum_type = StaticEnum<ECS::ELightType>())
                {
                    const i32 index = enum_type->GetIndexByName(entity_doc._light_component._type);
                    if (index != -1)
                    {
                        light->_type = static_cast<ECS::ELightType>(index);
                        reverted = true;
                    }
                }
            }
        }
        if (!reverted)
            return false;

        instance->_overrides.erase(std::remove_if(instance->_overrides.begin(), instance->_overrides.end(),
                                                  [tag, component, property](const PrefabOverride &override)
                                                  { return IsSameOverride(override, tag->_prefab_entity, component, property); }),
                                  instance->_overrides.end());
        scene.MarkEdited();
        return true;
    }

    bool PrefabSystem::RevertAll(Scene &scene, ECS::Entity root_entity, const PrefabAssetDocument &prefab)
    {
        PrefabInstance *instance = FindPrefabInstance(scene, root_entity);
        if (instance == nullptr)
            return false;
        const Vector<PrefabOverride> overrides = instance->_overrides;
        bool reverted = false;
        for (const PrefabOverride &override : overrides)
        {
            const ECS::Entity entity = FindInstanceEntity(scene, root_entity, override._prefab_entity);
            if (entity != ECS::kInvalidEntity)
                reverted |= RevertProperty(scene, entity, prefab, override._component, override._property);
        }
        return reverted;
    }

    bool PrefabSystem::Refresh(Scene &scene, ECS::Entity entity)
    {
        PrefabInstance *instance = FindPrefabInstance(scene, entity);
        if (instance == nullptr || instance->_prefab_asset.IsEmpty())
            return false;

        const ECS::Entity root_entity = scene.FindEntity(instance->_root_entity);
        if (root_entity == ECS::kInvalidEntity)
            return false;

        ResourceMgr::Get().Load<PrefabAssetDocument>(instance->_prefab_asset);
        const Ref<PrefabAssetDocument> prefab = ResourceMgr::Get().GetRef<PrefabAssetDocument>(instance->_prefab_asset);
        return prefab != nullptr && Refresh(scene, root_entity, *prefab);
    }

    bool PrefabSystem::Refresh(Scene &scene, ECS::Entity root_entity, const PrefabAssetDocument &prefab)
    {
        PrefabInstance *instance = FindPrefabInstance(scene, root_entity);
        if (instance == nullptr)
            return false;
        const Guid prefab_asset_guid(prefab._header._guid);
        if (!instance->_prefab_asset.IsEmpty() && !prefab_asset_guid.IsEmpty() && instance->_prefab_asset != prefab_asset_guid)
            return false;

        bool refreshed = false;
        std::unordered_set<Guid, GuidHasher> existing_prefab_entities;
        Vector<ECS::Entity> pending{root_entity};
        for (size_t index = 0u; index < pending.size(); ++index)
        {
            const ECS::Entity entity = pending[index];
            if (const auto *tag = scene.GetRegister().GetComponent<ECS::TagComponent>(entity); tag != nullptr && !tag->_prefab_entity.IsEmpty())
                existing_prefab_entities.emplace(tag->_prefab_entity);
            if (const auto *hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(entity))
                for (ECS::Entity child = hierarchy->_first_child; child != ECS::kInvalidEntity;)
                {
                    const auto *child_hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(child);
                    pending.emplace_back(child);
                    child = child_hierarchy != nullptr ? child_hierarchy->_next_sibling : ECS::kInvalidEntity;
                }
        }
        std::unordered_set<Guid, GuidHasher> prefab_entities;
        for (const PrefabEntityDocument &prefab_entity : prefab._entities)
            prefab_entities.emplace(prefab_entity._guid);
        for (const PrefabEntityDocument &prefab_entity : prefab._entities)
        {
            if (existing_prefab_entities.contains(prefab_entity._guid) ||
                (!prefab_entity._parent.IsEmpty() && !existing_prefab_entities.contains(prefab_entity._parent)))
                continue;
            PrefabAssetDocument subtree_prefab;
            subtree_prefab._header = prefab._header;
            subtree_prefab._root = prefab_entity._guid;
            Vector<Guid> subtree_guids{prefab_entity._guid};
            for (size_t index = 0u; index < subtree_guids.size(); ++index)
                for (const PrefabEntityDocument &candidate : prefab._entities)
                    if (candidate._parent == subtree_guids[index]) subtree_guids.emplace_back(candidate._guid);
            for (const Guid &guid : subtree_guids)
                if (const PrefabEntityDocument *candidate = FindPrefabEntity(prefab, guid)) subtree_prefab._entities.emplace_back(*candidate);
            const PrefabInstantiateResult created = Instantiate(scene, subtree_prefab);
            if (created._root != ECS::kInvalidEntity)
            {
                scene.MutablePrefabInstances().pop_back();
                if (!prefab_entity._parent.IsEmpty())
                {
                    const ECS::Entity parent = FindInstanceEntity(scene, root_entity, prefab_entity._parent);
                    if (parent != ECS::kInvalidEntity) scene.Reparent(created._root, parent, false);
                }
                existing_prefab_entities.insert(subtree_guids.begin(), subtree_guids.end());
                refreshed = true;
            }
        }
        for (ECS::Entity entity : pending)
        {
            const auto *tag = scene.GetRegister().GetComponent<ECS::TagComponent>(entity);
            if (tag == nullptr || tag->_prefab_entity.IsEmpty() || prefab_entities.contains(tag->_prefab_entity)) continue;
            const auto *hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(entity);
            const auto *parent_tag = hierarchy != nullptr ? scene.GetRegister().GetComponent<ECS::TagComponent>(hierarchy->_parent) : nullptr;
            if (parent_tag == nullptr || prefab_entities.contains(parent_tag->_prefab_entity))
            {
                scene.RemoveObject(entity);
                refreshed = true;
            }
        }

        for (const PrefabEntityDocument &prefab_entity : prefab._entities)
        {
            const ECS::Entity entity = FindInstanceEntity(scene, root_entity, prefab_entity._guid);
            if (entity == ECS::kInvalidEntity)
                continue;
            const SceneEntityDocument &entity_doc = prefab_entity._entity;
            auto has_override = [instance, &prefab_entity](StringView component, StringView property)
            {
                return std::any_of(instance->_overrides.begin(), instance->_overrides.end(),
                                   [&prefab_entity, component, property](const PrefabOverride &override)
                                   { return IsSameOverride(override, prefab_entity._guid, component, property); });
            };

            PrefabAssetDocument temporary_prefab;
            temporary_prefab._header = prefab._header;
            temporary_prefab._root = prefab_entity._guid;
            PrefabEntityDocument temporary_entity = prefab_entity;
            temporary_entity._parent = Guid::EmptyGuid();
            temporary_prefab._entities.emplace_back(std::move(temporary_entity));
            const PrefabInstantiateResult temporary_result = Instantiate(scene, temporary_prefab);
            if (temporary_result._root != ECS::kInvalidEntity)
            {
                scene.MutablePrefabInstances().pop_back();
                const ECS::ComponentTypeId persistent_id_type = ECS::PersistentIdComponent::StaticComponentTypeId();
                const ECS::ComponentTypeId hierarchy_type = ECS::CHierarchy::StaticComponentTypeId();
                const ECS::ComponentTypeId tag_type = ECS::TagComponent::StaticComponentTypeId();
                for (ECS::ComponentTypeId type_id : scene.GetRegister().GetEntityComponentTypes(temporary_result._root))
                {
                    if (type_id == persistent_id_type || type_id == hierarchy_type || type_id == tag_type)
                        continue;
                    const StringView component_name = ECS::GetComponentStableName(type_id);
                    const bool has_component_override = std::any_of(instance->_overrides.begin(), instance->_overrides.end(),
                                                                     [&prefab_entity, component_name](const PrefabOverride &override)
                                                                     { return override._prefab_entity == prefab_entity._guid &&
                                                                              override._component == component_name; });
                    if (!has_component_override)
                        scene.GetRegister().CopyComponent(temporary_result._root, entity, type_id);
                }
                scene.RemoveObject(temporary_result._root);
                refreshed = true;
            }

            auto *tag = scene.GetRegister().GetComponent<ECS::TagComponent>(entity);
            auto &registry = scene.GetRegister();
            if (entity_doc._has_script_component && registry.GetComponent<ECS::ScriptComponent>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::ScriptComponent>(entity);
                component._script_asset = entity_doc._script_component._script_asset;
                component._properties = entity_doc._script_component._properties;
                registry.SetComponentEnabled<ECS::ScriptComponent>(entity, !IsComponentDisabled(entity_doc, "ScriptComponent"));
                refreshed = true;
            }
            if (entity_doc._has_static_mesh_component && registry.GetComponent<ECS::StaticMeshComponent>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::StaticMeshComponent>(entity);
                if (!entity_doc._static_mesh_component._mesh_guid.empty())
                {
                    const Guid mesh_guid(entity_doc._static_mesh_component._mesh_guid);
                    ResourceMgr::Get().Load<Mesh>(mesh_guid);
                    component._p_mesh = ResourceMgr::Get().GetRef<Mesh>(mesh_guid);
                    if (component._p_mesh != nullptr) component._transformed_aabbs.resize(component._p_mesh->SubmeshCount() + 1u);
                }
                for (const String &material_guid_string : entity_doc._static_mesh_component._material_guids)
                {
                    if (material_guid_string.empty()) continue;
                    const Guid material_guid(material_guid_string);
                    ResourceMgr::Get().Load<Material>(material_guid);
                    if (Ref<Material> material = ResourceMgr::Get().GetRef<Material>(material_guid); material != nullptr)
                        component._p_mats.emplace_back(std::move(material));
                }
                registry.SetComponentEnabled<ECS::StaticMeshComponent>(entity, !IsComponentDisabled(entity_doc, "StaticMeshComponent"));
                refreshed = true;
            }
            if (entity_doc._has_light_component && registry.GetComponent<ECS::LightComponent>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::LightComponent>(entity);
                component._light._light_color = entity_doc._light_component._light._light_color;
                component._light._light_param = entity_doc._light_component._light._light_param;
                component._light._is_two_side = entity_doc._light_component._light._is_two_side;
                component._shadow._is_cast_shadow = entity_doc._light_component._shadow._is_cast_shadow;
                component._shadow._constant_bias = entity_doc._light_component._shadow._constant_bias;
                component._shadow._slope_bias = entity_doc._light_component._shadow._slope_bias;
                registry.SetComponentEnabled<ECS::LightComponent>(entity, !IsComponentDisabled(entity_doc, "LightComponent"));
                refreshed = true;
            }
            if (entity_doc._has_camera_component && registry.GetComponent<ECS::CCamera>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::CCamera>(entity);
                component._camera.Type(Render::CameraTypeFromString(entity_doc._camera_component._type));
                component._camera.Aspect(entity_doc._camera_component._aspect);
                component._camera.Far(entity_doc._camera_component._far_clip);
                component._camera.Near(entity_doc._camera_component._near_clip);
                component._camera.FovH(entity_doc._camera_component._fov_h);
                component._camera.Size(entity_doc._camera_component._size);
                component._camera.RecalculateMatrix(true);
                registry.SetComponentEnabled<ECS::CCamera>(entity, !IsComponentDisabled(entity_doc, "CCamera"));
                refreshed = true;
            }
            if (entity_doc._has_lightprobe_component && registry.GetComponent<ECS::CLightProbe>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::CLightProbe>(entity);
                component._size = entity_doc._lightprobe_component._size;
                component._is_update_every_tick = entity_doc._lightprobe_component._is_update_every_tick;
                component._is_dirty = true;
                registry.SetComponentEnabled<ECS::CLightProbe>(entity, !IsComponentDisabled(entity_doc, "CLightProbe"));
                refreshed = true;
            }
            if (entity_doc._has_vxgi_component && registry.GetComponent<ECS::CVXGI>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::CVXGI>(entity);
                component._grid_num = entity_doc._vxgi_component._grid_num;
                component._distance = entity_doc._vxgi_component._distance;
                registry.SetComponentEnabled<ECS::CVXGI>(entity, !IsComponentDisabled(entity_doc, "CVXGI"));
                refreshed = true;
            }
            if (entity_doc._has_sprite_renderer_component && registry.GetComponent<ECS::SpriteRendererComponent>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::SpriteRendererComponent>(entity);
                if (!entity_doc._sprite_renderer_component._sprite_guid.empty())
                {
                    const Guid sprite_guid(entity_doc._sprite_renderer_component._sprite_guid);
                    ResourceMgr::Get().Load<Render::Sprite>(sprite_guid);
                    component._sprite = ResourceMgr::Get().Get<Render::Sprite>(sprite_guid);
                }
                if (!entity_doc._sprite_renderer_component._material_guid.empty())
                {
                    const Guid material_guid(entity_doc._sprite_renderer_component._material_guid);
                    ResourceMgr::Get().Load<Material>(material_guid);
                    component._material = ResourceMgr::Get().GetRef<Material>(material_guid);
                }
                const Vector4f &color = entity_doc._sprite_renderer_component._color;
                component._color = Color(color.x, color.y, color.z, color.w);
                component._sorting_layer = static_cast<i16>(entity_doc._sprite_renderer_component._sorting_layer);
                component._order_in_layer = entity_doc._sprite_renderer_component._order_in_layer;
                component._blend_mode = static_cast<Render::ESpriteBlendMode>(entity_doc._sprite_renderer_component._blend_mode);
                component._flip_x = entity_doc._sprite_renderer_component._flip_x;
                component._flip_y = entity_doc._sprite_renderer_component._flip_y;
                component._visible = entity_doc._sprite_renderer_component._visible;
                registry.SetComponentEnabled<ECS::SpriteRendererComponent>(entity,
                                                                           !IsComponentDisabled(entity_doc, "SpriteRendererComponent"));
                refreshed = true;
            }
            if (entity_doc._has_rigidbody_component && registry.GetComponent<ECS::CRigidBody>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::CRigidBody>(entity);
                component._mass = entity_doc._rigidbody_component._mass;
                registry.SetComponentEnabled<ECS::CRigidBody>(entity, !IsComponentDisabled(entity_doc, "CRigidBody"));
                refreshed = true;
            }
            if (entity_doc._has_collider_component && registry.GetComponent<ECS::CCollider>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::CCollider>(entity);
                component._is_trigger = entity_doc._collider_component._is_trigger;
                component._center = entity_doc._collider_component._center;
                component._param = entity_doc._collider_component._param;
                if (const Enum *enum_type = StaticEnum<ECS::EColliderType>(); enum_type != nullptr)
                {
                    const i32 index = enum_type->GetIndexByName(entity_doc._collider_component._type);
                    if (index != -1) component._type = static_cast<ECS::EColliderType>(index);
                }
                registry.SetComponentEnabled<ECS::CCollider>(entity, !IsComponentDisabled(entity_doc, "CCollider"));
                refreshed = true;
            }
            if (entity_doc._has_rigidbody_2d_component && registry.GetComponent<ECS::RigidBody2DComponent>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::RigidBody2DComponent>(entity);
                const auto &document = entity_doc._rigidbody_2d_component;
                component._type = document._type;
                component._gravity_scale = document._gravity_scale;
                component._linear_damping = document._linear_damping;
                component._angular_damping = document._angular_damping;
                component._fixed_rotation = document._fixed_rotation;
                component._continuous = document._continuous;
                component._allow_sleep = document._allow_sleep;
                registry.SetComponentEnabled<ECS::RigidBody2DComponent>(entity,
                                                                         !IsComponentDisabled(entity_doc, "RigidBody2DComponent"));
                refreshed = true;
            }
            if (entity_doc._has_collider_2d_component && registry.GetComponent<ECS::Collider2DComponent>(entity) == nullptr)
            {
                auto &component = registry.AddComponent<ECS::Collider2DComponent>(entity);
                component._preset = entity_doc._collider_2d_component._preset;
                component._collision_profile = entity_doc._collider_2d_component._collision_profile;
                component._shapes = entity_doc._collider_2d_component._shapes;
                registry.SetComponentEnabled<ECS::Collider2DComponent>(entity,
                                                                         !IsComponentDisabled(entity_doc, "Collider2DComponent"));
                refreshed = true;
            }
            if (tag != nullptr)
            {
                if (!has_override(ECS::TagComponent::StaticTypeName(), "_name"))
                    tag->_name = entity_doc._tag_component._name;
                if (!has_override(ECS::TagComponent::StaticTypeName(), "_tag"))
                    tag->_tag = entity_doc._tag_component._tag;
                if (!has_override(ECS::TagComponent::StaticTypeName(), "_layer_mask"))
                    tag->_layer_mask = entity_doc._tag_component._layer_mask;
                refreshed = true;
            }
            if (entity_doc._has_transform_component && !has_override(ECS::TransformComponent::StaticTypeName(), "_local_transform"))
            {
                if (auto *transform = scene.GetRegister().GetComponent<ECS::TransformComponent>(entity))
                {
                    if (!has_override(ECS::TransformComponent::StaticTypeName(), "_local_transform._position"))
                        transform->SetLocalPosition(entity_doc._transform_component._position);
                    if (!has_override(ECS::TransformComponent::StaticTypeName(), "_local_transform._rotation"))
                        transform->SetLocalRotation(entity_doc._transform_component._rotation);
                    if (!has_override(ECS::TransformComponent::StaticTypeName(), "_local_transform._scale"))
                        transform->SetLocalScale(entity_doc._transform_component._scale);
                    refreshed = true;
                }
            }
            if (entity_doc._has_light_component)
            {
                if (auto *light = scene.GetRegister().GetComponent<ECS::LightComponent>(entity))
                {
                    if (!has_override(ECS::LightComponent::StaticTypeName(), "_light"))
                    {
                        light->_light._light_color = entity_doc._light_component._light._light_color;
                        light->_light._light_param = entity_doc._light_component._light._light_param;
                        light->_light._is_two_side = entity_doc._light_component._light._is_two_side;
                    }
                    if (!has_override(ECS::LightComponent::StaticTypeName(), "_shadow"))
                    {
                        light->_shadow._is_cast_shadow = entity_doc._light_component._shadow._is_cast_shadow;
                        light->_shadow._constant_bias = entity_doc._light_component._shadow._constant_bias;
                        light->_shadow._slope_bias = entity_doc._light_component._shadow._slope_bias;
                    }
                    if (!has_override(ECS::LightComponent::StaticTypeName(), "_type") && !entity_doc._light_component._type.empty())
                    {
                        if (const Enum *enum_type = StaticEnum<ECS::ELightType>())
                        {
                            const i32 index = enum_type->GetIndexByName(entity_doc._light_component._type);
                            if (index != -1)
                                light->_type = static_cast<ECS::ELightType>(index);
                        }
                    }
                    refreshed = true;
                }
            }

            for (ECS::ComponentTypeId type_id : scene.GetRegister().GetEntityComponentTypes(entity))
            {
                const StringView component_name = ECS::GetComponentStableName(type_id);
                if (!component_name.empty() && !IsPrefabComponentPresent(entity_doc, component_name))
                {
                    scene.GetRegister().RemoveComponentType(entity, type_id);
                    refreshed = true;
                }
            }
        }
        if (refreshed)
            scene.MarkEdited();
        return refreshed;
    }

    bool PrefabSystem::RefreshProperties(Scene &scene, ECS::Entity root_entity, const PrefabAssetDocument &prefab)
    {
        return Refresh(scene, root_entity, prefab);
    }

    bool PrefabSystem::Unpack(Scene &scene, ECS::Entity root_entity)
    {
        const Guid *root_guid = scene.FindEntityGuid(root_entity);
        if (root_guid == nullptr)
            return false;

        auto &instances = scene.MutablePrefabInstances();
        const auto instance_it = std::find_if(instances.begin(), instances.end(),
                                              [root_guid](const PrefabInstance &instance)
                                              { return instance._root_entity == *root_guid; });
        if (instance_it == instances.end())
            return false;

        Vector<ECS::Entity> pending_entities{root_entity};
        for (size_t index = 0u; index < pending_entities.size(); ++index)
        {
            const ECS::Entity entity = pending_entities[index];
            if (auto *tag = scene.GetRegister().GetComponent<ECS::TagComponent>(entity))
                tag->_prefab_entity = Guid::EmptyGuid();

            const auto *hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(entity);
            if (hierarchy == nullptr)
                continue;
            for (ECS::Entity child = hierarchy->_first_child; child != ECS::kInvalidEntity;)
            {
                const auto *child_hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(child);
                const ECS::Entity next_sibling = child_hierarchy != nullptr ? child_hierarchy->_next_sibling : ECS::kInvalidEntity;
                pending_entities.emplace_back(child);
                child = next_sibling;
            }
        }

        instances.erase(instance_it);
        scene.MarkStructureChanged();
        return true;
    }
}
