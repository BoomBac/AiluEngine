#include "Scene/EntitySerializer.h"

#include "Assets/PrefabAsset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/2D/Sprite.h"
#include "Render/Material.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <stdexcept>

namespace Ailu::SceneManagement
{
    ECS::Entity EntitySerializer::CloneEntity(Scene &scene, ECS::Entity source)
    {
        if (!scene.IsValidEntity(source))
            return ECS::kInvalidEntity;

        const auto *source_tag = scene.GetRegister().GetComponent<ECS::TagComponent>(source);
        const String source_name = source_tag != nullptr ? source_tag->_name : String{};
        const String target_name = AcquireDuplicateName(scene, source_name);
        ECS::Entity target = scene.AddObject(target_name);
        CopyComponents(scene, source, target);
        scene.GetRegister().GetComponent<ECS::TagComponent>(target)->_name = target_name;

        const auto *source_hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(source);
        if (source_hierarchy != nullptr && source_hierarchy->_parent != ECS::kInvalidEntity)
            scene.Reparent(target, source_hierarchy->_parent, false);
        return target;
    }

    ECS::Entity EntitySerializer::CloneSubtree(Scene &scene, ECS::Entity source_root)
    {
        if (!scene.IsValidEntity(source_root))
            return ECS::kInvalidEntity;

        Vector<ECS::Entity> source_entities;
        CollectSubtree(scene, source_root, source_entities);
        HashMap<ECS::Entity, ECS::Entity> entity_map;
        entity_map.reserve(source_entities.size());

        for (ECS::Entity source : source_entities)
        {
            const auto *source_tag = scene.GetRegister().GetComponent<ECS::TagComponent>(source);
            const String source_name = source_tag != nullptr ? source_tag->_name : String{};
            const String target_name = source == source_root ? AcquireDuplicateName(scene, source_name) : source_name;
            ECS::Entity target = scene.AddObject(target_name);
            CopyComponents(scene, source, target);
            scene.GetRegister().GetComponent<ECS::TagComponent>(target)->_name = target_name;
            entity_map.emplace(source, target);
        }

        for (ECS::Entity source : source_entities)
        {
            const auto *source_hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(source);
            if (source_hierarchy == nullptr)
                continue;
            ECS::Entity target = entity_map.at(source);
            const auto parent_it = entity_map.find(source_hierarchy->_parent);
            if (parent_it != entity_map.end())
                scene.Reparent(target, parent_it->second, false);
            else if (source == source_root && source_hierarchy->_parent != ECS::kInvalidEntity)
                scene.Reparent(target, source_hierarchy->_parent, false);
        }
        return entity_map.at(source_root);
    }

    SceneEntityDocument EntitySerializer::BuildEntityDocument(const Scene &scene, ECS::Entity entity, u32 sibling_index)
    {
        SceneEntityDocument entity_doc;
        if (!scene.IsValidEntity(entity))
            return entity_doc;

        const auto &registry = scene.GetRegister();
        entity_doc._entity_guid = scene.GetEntityGuid(entity);
        if (const auto *tag = registry.GetComponent<ECS::TagComponent>(entity); tag != nullptr)
        {
            entity_doc._tag_component._name = tag->_name;
            entity_doc._tag_component._tag = tag->_tag;
            entity_doc._tag_component._layer_mask = tag->_layer_mask;
        }

        auto asset_guid_string = [](Object *object)
        {
            if (object == nullptr)
                return String{};
            const Guid &guid = ResourceMgr::Get().GetAssetGuid(object);
            return guid.IsEmpty() ? String{} : guid.ToString();
        };
        auto fill_material_guids = [&asset_guid_string](const Vector<Ref<Material>> &materials,
                                                        const Vector<Guid> &stored_guids, Vector<String> &guids)
        {
            guids.clear();
            guids.reserve(std::max(materials.size(), stored_guids.size()));
            for (u32 index = 0u; index < std::max(materials.size(), stored_guids.size()); ++index)
            {
                if (index < stored_guids.size() && !stored_guids[index].IsEmpty())
                    guids.emplace_back(stored_guids[index].ToString());
                else
                    guids.emplace_back(index < materials.size() ? asset_guid_string(materials[index].get()) : String{});
            }
        };
        auto mark_disabled = [&]<typename T>(StringView component_name)
        {
            if (!registry.IsComponentEnabled<T>(entity))
                entity_doc._disabled_components.emplace_back(component_name);
        };

        if (const auto *transform = registry.GetComponent<ECS::TransformComponent>(entity); transform != nullptr)
        {
            entity_doc._has_transform_component = true;
            entity_doc._transform_component._position = transform->_local_transform._position;
            entity_doc._transform_component._rotation = transform->_local_transform._rotation;
            entity_doc._transform_component._scale = transform->_local_transform._scale;
        }
        if (const auto *script = registry.GetComponent<ECS::ScriptComponent>(entity); script != nullptr)
        {
            entity_doc._has_script_component = true;
            entity_doc._script_component._script_asset = script->_script_asset;
            entity_doc._script_component._properties = script->_properties;
            mark_disabled.template operator()<ECS::ScriptComponent>("ScriptComponent");
        }
        if (const auto *static_mesh = registry.GetComponent<ECS::StaticMeshComponent>(entity); static_mesh != nullptr)
        {
            entity_doc._has_static_mesh_component = true;
            entity_doc._static_mesh_component._mesh_guid = !static_mesh->_mesh_guid.IsEmpty()
                ? static_mesh->_mesh_guid.ToString() : asset_guid_string(static_mesh->_p_mesh.get());
            fill_material_guids(static_mesh->_p_mats, static_mesh->_material_guids,
                                entity_doc._static_mesh_component._material_guids);
            mark_disabled.template operator()<ECS::StaticMeshComponent>("StaticMeshComponent");
        }
        if (const auto *light = registry.GetComponent<ECS::LightComponent>(entity); light != nullptr)
        {
            entity_doc._has_light_component = true;
            if (const Enum *enum_type = StaticEnum<ECS::ELightType>())
                entity_doc._light_component._type = enum_type->GetNameByEnum(light->_type);
            entity_doc._light_component._light._light_color = light->_light._light_color;
            entity_doc._light_component._light._light_param = light->_light._light_param;
            entity_doc._light_component._light._is_two_side = light->_light._is_two_side;
            entity_doc._light_component._shadow._is_cast_shadow = light->_shadow._is_cast_shadow;
            entity_doc._light_component._shadow._constant_bias = light->_shadow._constant_bias;
            entity_doc._light_component._shadow._slope_bias = light->_shadow._slope_bias;
            mark_disabled.template operator()<ECS::LightComponent>("LightComponent");
        }
        if (const auto *hierarchy = registry.GetComponent<ECS::CHierarchy>(entity); hierarchy != nullptr)
        {
            entity_doc._has_hierarchy_component = true;
            entity_doc._hierarchy_component._enabled = hierarchy->_enabled;
            entity_doc._hierarchy_component._parent_guid = scene.GetEntityGuid(hierarchy->_parent);
            entity_doc._hierarchy_component._sibling_index = sibling_index;
            entity_doc._hierarchy_component._inv_matrix_attach = hierarchy->_inv_matrix_attach.ToString();
        }
        if (const auto *camera = registry.GetComponent<ECS::CCamera>(entity); camera != nullptr)
        {
            entity_doc._has_camera_component = true;
            entity_doc._camera_component._type = CameraTypeToString(camera->_camera.Type());
            entity_doc._camera_component._aspect = camera->_camera.Aspect();
            entity_doc._camera_component._far_clip = camera->_camera.Far();
            entity_doc._camera_component._near_clip = camera->_camera.Near();
            entity_doc._camera_component._fov_h = camera->_camera.FovH();
            entity_doc._camera_component._size = camera->_camera.Size();
            mark_disabled.template operator()<ECS::CCamera>("CCamera");
        }
        if (const auto *light_probe = registry.GetComponent<ECS::CLightProbe>(entity); light_probe != nullptr)
        {
            entity_doc._has_lightprobe_component = true;
            entity_doc._lightprobe_component._size = light_probe->_size;
            entity_doc._lightprobe_component._is_update_every_tick = light_probe->_is_update_every_tick;
            mark_disabled.template operator()<ECS::CLightProbe>("CLightProbe");
        }
        if (const auto *rigidbody = registry.GetComponent<ECS::CRigidBody>(entity); rigidbody != nullptr)
        {
            entity_doc._has_rigidbody_component = true;
            entity_doc._rigidbody_component._mass = rigidbody->_mass;
            mark_disabled.template operator()<ECS::CRigidBody>("CRigidBody");
        }
        if (const auto *collider = registry.GetComponent<ECS::CCollider>(entity); collider != nullptr)
        {
            entity_doc._has_collider_component = true;
            if (const Enum *enum_type = StaticEnum<ECS::EColliderType>())
                entity_doc._collider_component._type = enum_type->GetNameByEnum(collider->_type);
            entity_doc._collider_component._is_trigger = collider->_is_trigger;
            entity_doc._collider_component._center = collider->_center;
            entity_doc._collider_component._param = collider->_param;
            mark_disabled.template operator()<ECS::CCollider>("CCollider");
        }
        if (const auto *rigidbody_2d = registry.GetComponent<ECS::RigidBody2DComponent>(entity); rigidbody_2d != nullptr)
        {
            entity_doc._has_rigidbody_2d_component = true;
            auto &document = entity_doc._rigidbody_2d_component;
            document._type = rigidbody_2d->_type;
            document._gravity_scale = rigidbody_2d->_gravity_scale;
            document._linear_damping = rigidbody_2d->_linear_damping;
            document._angular_damping = rigidbody_2d->_angular_damping;
            document._fixed_rotation = rigidbody_2d->_fixed_rotation;
            document._continuous = rigidbody_2d->_continuous;
            document._allow_sleep = rigidbody_2d->_allow_sleep;
            mark_disabled.template operator()<ECS::RigidBody2DComponent>("RigidBody2DComponent");
        }
        if (const auto *collider_2d = registry.GetComponent<ECS::Collider2DComponent>(entity); collider_2d != nullptr)
        {
            entity_doc._has_collider_2d_component = true;
            entity_doc._collider_2d_component._preset = collider_2d->_preset;
            entity_doc._collider_2d_component._collision_profile = collider_2d->_collision_profile;
            entity_doc._collider_2d_component._shapes = collider_2d->_shapes;
            mark_disabled.template operator()<ECS::Collider2DComponent>("Collider2DComponent");
        }
        if (const auto *skeleton_mesh = registry.GetComponent<ECS::CSkeletonMesh>(entity); skeleton_mesh != nullptr)
        {
            entity_doc._has_skeleton_mesh_component = true;
            entity_doc._skeleton_mesh_component._mesh_guid = !skeleton_mesh->_mesh_guid.IsEmpty()
                ? skeleton_mesh->_mesh_guid.ToString() : asset_guid_string(skeleton_mesh->_p_mesh.get());
            fill_material_guids(skeleton_mesh->_p_mats, skeleton_mesh->_material_guids,
                                entity_doc._skeleton_mesh_component._material_guids);
            mark_disabled.template operator()<ECS::CSkeletonMesh>("CSkeletonMesh");
        }
        if (const auto *animator = registry.GetComponent<ECS::AnimatorComponent>(entity); animator != nullptr)
        {
            entity_doc._has_animator_component = true;
            entity_doc._animator_component._controller_guid = animator->_controller.IsEmpty() ? String{} : animator->_controller.ToString();
            entity_doc._animator_component._clip_guid = animator->_clip.IsEmpty() ? String{} : animator->_clip.ToString();
            entity_doc._animator_component._speed = animator->_speed;
            entity_doc._animator_component._play_on_awake = animator->_play_on_awake;
            entity_doc._animator_component._root_motion_mode = static_cast<u8>(animator->_root_motion_mode);
            mark_disabled.template operator()<ECS::AnimatorComponent>("AnimatorComponent");
        }
        if (const auto *vxgi = registry.GetComponent<ECS::CVXGI>(entity); vxgi != nullptr)
        {
            entity_doc._has_vxgi_component = true;
            entity_doc._vxgi_component._grid_num = vxgi->_grid_num;
            entity_doc._vxgi_component._distance = vxgi->_distance;
            mark_disabled.template operator()<ECS::CVXGI>("CVXGI");
        }
        if (const auto *sprite = registry.GetComponent<ECS::SpriteRendererComponent>(entity); sprite != nullptr)
        {
            entity_doc._has_sprite_renderer_component = true;
            entity_doc._sprite_renderer_component._sprite_guid = !sprite->_sprite_guid.IsEmpty()
                ? sprite->_sprite_guid.ToString() : asset_guid_string(sprite->_sprite);
            entity_doc._sprite_renderer_component._material_guid = !sprite->_material_guid.IsEmpty()
                ? sprite->_material_guid.ToString() : asset_guid_string(sprite->_material.get());
            entity_doc._sprite_renderer_component._color =
                Vector4f(sprite->_color.r, sprite->_color.g, sprite->_color.b, sprite->_color.a);
            entity_doc._sprite_renderer_component._sorting_layer = sprite->_sorting_layer;
            entity_doc._sprite_renderer_component._order_in_layer = sprite->_order_in_layer;
            entity_doc._sprite_renderer_component._blend_mode = static_cast<i32>(sprite->_blend_mode);
            entity_doc._sprite_renderer_component._flip_x = sprite->_flip_x;
            entity_doc._sprite_renderer_component._flip_y = sprite->_flip_y;
            entity_doc._sprite_renderer_component._visible = sprite->_visible;
            mark_disabled.template operator()<ECS::SpriteRendererComponent>("SpriteRendererComponent");
        }

        const Vector<ECS::ComponentTypeId> handled_component_types{
            ECS::TagComponent::StaticComponentTypeId(),
            ECS::PersistentIdComponent::StaticComponentTypeId(),
            ECS::TransformComponent::StaticComponentTypeId(),
            ECS::CHierarchy::StaticComponentTypeId(),
            ECS::ScriptComponent::StaticComponentTypeId(),
            ECS::StaticMeshComponent::StaticComponentTypeId(),
            ECS::LightComponent::StaticComponentTypeId(),
            ECS::CCamera::StaticComponentTypeId(),
            ECS::CLightProbe::StaticComponentTypeId(),
            ECS::CRigidBody::StaticComponentTypeId(),
            ECS::CCollider::StaticComponentTypeId(),
            ECS::RigidBody2DComponent::StaticComponentTypeId(),
            ECS::Collider2DComponent::StaticComponentTypeId(),
            ECS::CSkeletonMesh::StaticComponentTypeId(),
            ECS::AnimatorComponent::StaticComponentTypeId(),
            ECS::CVXGI::StaticComponentTypeId(),
            ECS::SpriteRendererComponent::StaticComponentTypeId()};
        for (const ECS::ComponentTypeId component_type : registry.GetEntityComponentTypes(entity))
        {
            if (std::find(handled_component_types.begin(), handled_component_types.end(), component_type) !=
                handled_component_types.end())
                continue;

            const StringView component_name = ECS::GetComponentStableName(component_type);
            throw std::runtime_error(std::format(
                "EntitySerializer::BuildEntityDocument: unhandled component '{}' (type id {}) on entity {} ('{}')",
                component_name.empty() ? StringView("Unknown") : component_name, component_type, entity,
                entity_doc._tag_component._name));
        }
        return entity_doc;
    }

    bool EntitySerializer::SerializeSubtree(const Scene &scene, ECS::Entity source_root, PrefabAssetDocument &prefab)
    {
        if (!scene.IsValidEntity(source_root))
            return false;

        Vector<ECS::Entity> source_entities;
        CollectSubtree(scene, source_root, source_entities);
        HashMap<ECS::Entity, Guid> prefab_guids;
        prefab_guids.reserve(source_entities.size());
        for (ECS::Entity entity : source_entities)
            prefab_guids.emplace(entity, Guid::Generate());

        prefab._root = prefab_guids.at(source_root);
        prefab._entities.clear();
        prefab._entities.reserve(source_entities.size());
        for (ECS::Entity entity : source_entities)
        {
            PrefabEntityDocument prefab_entity;
            prefab_entity._guid = prefab_guids.at(entity);
            prefab_entity._entity = BuildEntityDocument(scene, entity);
            prefab_entity._entity._entity_guid = prefab_entity._guid;
            prefab_entity._name = prefab_entity._entity._tag_component._name;
            prefab_entity._enabled = prefab_entity._entity._has_hierarchy_component
                ? prefab_entity._entity._hierarchy_component._enabled
                : true;
            if (const auto *hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(entity); hierarchy != nullptr)
            {
                if (const auto parent_it = prefab_guids.find(hierarchy->_parent); parent_it != prefab_guids.end())
                {
                    prefab_entity._parent = parent_it->second;
                    prefab_entity._entity._hierarchy_component._parent_guid = parent_it->second;
                }
                else
                {
                    prefab_entity._entity._hierarchy_component._parent_guid = Guid::EmptyGuid();
                }
            }
            prefab._entities.emplace_back(std::move(prefab_entity));
        }
        return true;
    }

    void EntitySerializer::CopyComponents(Scene &scene, ECS::Entity source, ECS::Entity target)
    {
        auto &registry = scene.GetRegister();
        const ECS::ComponentTypeId persistent_id_type = ECS::PersistentIdComponent::StaticComponentTypeId();
        const ECS::ComponentTypeId hierarchy_type = ECS::CHierarchy::StaticComponentTypeId();
        for (ECS::ComponentTypeId type_id : registry.GetEntityComponentTypes(source))
        {
            if (type_id != persistent_id_type && type_id != hierarchy_type)
                registry.CopyComponent(source, target, type_id);
        }
    }

    void EntitySerializer::CollectSubtree(const Scene &scene, ECS::Entity entity, Vector<ECS::Entity> &entities)
    {
        if (!scene.IsValidEntity(entity))
            return;
        entities.push_back(entity);
        const auto *hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(entity);
        if (hierarchy == nullptr)
            return;
        for (ECS::Entity child = hierarchy->_first_child; child != ECS::kInvalidEntity;)
        {
            const auto *child_hierarchy = scene.GetRegister().GetComponent<ECS::CHierarchy>(child);
            const ECS::Entity next_sibling = child_hierarchy != nullptr ? child_hierarchy->_next_sibling : ECS::kInvalidEntity;
            CollectSubtree(scene, child, entities);
            child = next_sibling;
        }
    }

    String EntitySerializer::AcquireDuplicateName(const Scene &scene, StringView source_name)
    {
        String base_name(source_name);
        const size_t suffix_start = base_name.find_last_of('(');
        if (suffix_start != String::npos && base_name.ends_with(')'))
            base_name.erase(suffix_start);
        if (base_name.empty())
            base_name = "Entity";
        return std::format("{}({})", base_name, scene.EntityNum());
    }
}
