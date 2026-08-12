#include "Scene/EntitySerializer.h"

#include "Assets/PrefabAsset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/2D/Sprite.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

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
        const auto &registry = scene.GetRegister();
        auto asset_guid_string = [](Object *object)
        {
            if (object == nullptr)
                return String{};
            const Guid &guid = ResourceMgr::Get().GetAssetGuid(object);
            return guid.IsEmpty() ? String{} : guid.ToString();
        };
        auto fill_material_guids = [&asset_guid_string](const Vector<Ref<Material>> &materials, Vector<String> &guids)
        {
            guids.clear();
            guids.reserve(materials.size());
            for (const auto &material : materials)
                guids.emplace_back(asset_guid_string(material.get()));
        };

        for (ECS::Entity entity : source_entities)
        {
            PrefabEntityDocument prefab_entity;
            prefab_entity._guid = prefab_guids.at(entity);
            prefab_entity._entity._entity_guid = prefab_entity._guid;
            const auto *tag = registry.GetComponent<ECS::TagComponent>(entity);
            prefab_entity._name = tag != nullptr ? tag->_name : String{};
            prefab_entity._entity._tag_component._name = prefab_entity._name;
            if (tag != nullptr)
            {
                prefab_entity._entity._tag_component._tag = tag->_tag;
                prefab_entity._entity._tag_component._layer_mask = tag->_layer_mask;
            }

            if (const auto *hierarchy = registry.GetComponent<ECS::CHierarchy>(entity))
            {
                prefab_entity._enabled = hierarchy->_enabled;
                prefab_entity._entity._has_hierarchy_component = true;
                prefab_entity._entity._hierarchy_component._enabled = hierarchy->_enabled;
                prefab_entity._entity._hierarchy_component._inv_matrix_attach = hierarchy->_inv_matrix_attach.ToString();
                if (const auto parent_it = prefab_guids.find(hierarchy->_parent); parent_it != prefab_guids.end())
                {
                    prefab_entity._parent = parent_it->second;
                    prefab_entity._entity._hierarchy_component._parent_guid = parent_it->second;
                }
            }
            if (const auto *transform = registry.GetComponent<ECS::TransformComponent>(entity))
            {
                prefab_entity._entity._has_transform_component = true;
                prefab_entity._entity._transform_component._position = transform->_local_transform._position;
                prefab_entity._entity._transform_component._rotation = transform->_local_transform._rotation;
                prefab_entity._entity._transform_component._scale = transform->_local_transform._scale;
            }
            if (const auto *script = registry.GetComponent<ECS::ScriptComponent>(entity))
            {
                prefab_entity._entity._has_script_component = true;
                prefab_entity._entity._script_component._script_asset = script->_script_asset;
                prefab_entity._entity._script_component._properties = script->_properties;
                if (!registry.IsComponentEnabled<ECS::ScriptComponent>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("ScriptComponent");
            }
            if (const auto *static_mesh = registry.GetComponent<ECS::StaticMeshComponent>(entity))
            {
                prefab_entity._entity._has_static_mesh_component = true;
                prefab_entity._entity._static_mesh_component._mesh_guid = asset_guid_string(static_mesh->_p_mesh.get());
                fill_material_guids(static_mesh->_p_mats, prefab_entity._entity._static_mesh_component._material_guids);
                if (!registry.IsComponentEnabled<ECS::StaticMeshComponent>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("StaticMeshComponent");
            }
            if (const auto *light = registry.GetComponent<ECS::LightComponent>(entity))
            {
                prefab_entity._entity._has_light_component = true;
                if (const Enum *enum_type = StaticEnum<ECS::ELightType>())
                    prefab_entity._entity._light_component._type = enum_type->GetNameByEnum(light->_type);
                prefab_entity._entity._light_component._light._light_color = light->_light._light_color;
                prefab_entity._entity._light_component._light._light_param = light->_light._light_param;
                prefab_entity._entity._light_component._light._is_two_side = light->_light._is_two_side;
                prefab_entity._entity._light_component._shadow._is_cast_shadow = light->_shadow._is_cast_shadow;
                prefab_entity._entity._light_component._shadow._constant_bias = light->_shadow._constant_bias;
                prefab_entity._entity._light_component._shadow._slope_bias = light->_shadow._slope_bias;
                if (!registry.IsComponentEnabled<ECS::LightComponent>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("LightComponent");
            }
            if (const auto *camera = registry.GetComponent<ECS::CCamera>(entity))
            {
                prefab_entity._entity._has_camera_component = true;
                prefab_entity._entity._camera_component._type = CameraTypeToString(camera->_camera.Type());
                prefab_entity._entity._camera_component._aspect = camera->_camera.Aspect();
                prefab_entity._entity._camera_component._far_clip = camera->_camera.Far();
                prefab_entity._entity._camera_component._near_clip = camera->_camera.Near();
                prefab_entity._entity._camera_component._fov_h = camera->_camera.FovH();
                prefab_entity._entity._camera_component._size = camera->_camera.Size();
                if (!registry.IsComponentEnabled<ECS::CCamera>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("CCamera");
            }
            if (const auto *light_probe = registry.GetComponent<ECS::CLightProbe>(entity))
            {
                prefab_entity._entity._has_lightprobe_component = true;
                prefab_entity._entity._lightprobe_component._size = light_probe->_size;
                prefab_entity._entity._lightprobe_component._is_update_every_tick = light_probe->_is_update_every_tick;
                if (!registry.IsComponentEnabled<ECS::CLightProbe>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("CLightProbe");
            }
            if (const auto *rigidbody = registry.GetComponent<ECS::CRigidBody>(entity))
            {
                prefab_entity._entity._has_rigidbody_component = true;
                prefab_entity._entity._rigidbody_component._mass = rigidbody->_mass;
                if (!registry.IsComponentEnabled<ECS::CRigidBody>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("CRigidBody");
            }
            if (const auto *collider = registry.GetComponent<ECS::CCollider>(entity))
            {
                prefab_entity._entity._has_collider_component = true;
                if (const Enum *enum_type = StaticEnum<ECS::EColliderType>())
                    prefab_entity._entity._collider_component._type = enum_type->GetNameByEnum(collider->_type);
                prefab_entity._entity._collider_component._is_trigger = collider->_is_trigger;
                prefab_entity._entity._collider_component._center = collider->_center;
                prefab_entity._entity._collider_component._param = collider->_param;
                if (!registry.IsComponentEnabled<ECS::CCollider>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("CCollider");
            }
            if (const auto *rigidbody_2d = registry.GetComponent<ECS::RigidBody2DComponent>(entity))
            {
                prefab_entity._entity._has_rigidbody_2d_component = true;
                auto &document = prefab_entity._entity._rigidbody_2d_component;
                document._type = rigidbody_2d->_type;
                document._gravity_scale = rigidbody_2d->_gravity_scale;
                document._linear_damping = rigidbody_2d->_linear_damping;
                document._angular_damping = rigidbody_2d->_angular_damping;
                document._fixed_rotation = rigidbody_2d->_fixed_rotation;
                document._continuous = rigidbody_2d->_continuous;
                document._allow_sleep = rigidbody_2d->_allow_sleep;
                if (!registry.IsComponentEnabled<ECS::RigidBody2DComponent>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("RigidBody2DComponent");
            }
            if (const auto *collider_2d = registry.GetComponent<ECS::Collider2DComponent>(entity))
            {
                prefab_entity._entity._has_collider_2d_component = true;
                prefab_entity._entity._collider_2d_component._shapes = collider_2d->_shapes;
                if (!registry.IsComponentEnabled<ECS::Collider2DComponent>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("Collider2DComponent");
            }
            if (const auto *vxgi = registry.GetComponent<ECS::CVXGI>(entity))
            {
                prefab_entity._entity._has_vxgi_component = true;
                prefab_entity._entity._vxgi_component._grid_num = vxgi->_grid_num;
                prefab_entity._entity._vxgi_component._distance = vxgi->_distance;
                if (!registry.IsComponentEnabled<ECS::CVXGI>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("CVXGI");
            }
            if (const auto *sprite = registry.GetComponent<ECS::SpriteRendererComponent>(entity))
            {
                prefab_entity._entity._has_sprite_renderer_component = true;
                prefab_entity._entity._sprite_renderer_component._sprite_guid = asset_guid_string(sprite->_sprite);
                prefab_entity._entity._sprite_renderer_component._material_guid = asset_guid_string(sprite->_material.get());
                prefab_entity._entity._sprite_renderer_component._color = {sprite->_color.r, sprite->_color.g, sprite->_color.b, sprite->_color.a};
                prefab_entity._entity._sprite_renderer_component._sorting_layer = sprite->_sorting_layer;
                prefab_entity._entity._sprite_renderer_component._order_in_layer = sprite->_order_in_layer;
                prefab_entity._entity._sprite_renderer_component._blend_mode = static_cast<i32>(sprite->_blend_mode);
                prefab_entity._entity._sprite_renderer_component._flip_x = sprite->_flip_x;
                prefab_entity._entity._sprite_renderer_component._flip_y = sprite->_flip_y;
                prefab_entity._entity._sprite_renderer_component._visible = sprite->_visible;
                if (!registry.IsComponentEnabled<ECS::SpriteRendererComponent>(entity))
                    prefab_entity._entity._disabled_components.emplace_back("SpriteRendererComponent");
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
