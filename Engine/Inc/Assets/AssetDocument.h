#pragma once
#ifndef __ASSET_DOCUMENT_H__
#define __ASSET_DOCUMENT_H__

#include "Framework/Math/Guid.h"
#include "Framework/Math/ALMath.hpp"
#include "Graph/GraphTypes.h"
#include "Objects/JsonArchive.h"
#include "Objects/Object.h"
#include "Objects/SerializeSpecializations.h"
#include "AssetCommon.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Scene/Component.h"
#include "generated/AssetDocument.gen.h"

namespace Ailu
{
    ASTRUCT()
    struct AILU_API AssetDocumentHeader
    {
        GENERATED_BODY()

        APROPERTY()
        u32 _format_version = kSerializedAssetDocumentVersion;
        APROPERTY()
        String _guid;
        APROPERTY()
        String _asset_type;
        APROPERTY()
        String _asset_name;
        APROPERTY()
        Vector<AssetDependency> _dependencies;
    };

    ASTRUCT()
    struct AILU_API AssetNamedUIntProperty
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        u32 _value = 0u;
    };

    ASTRUCT()
    struct AILU_API AssetNamedFloatProperty
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        f32 _value = 0.0f;
    };

    ASTRUCT()
    struct AILU_API AssetNamedVectorProperty
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        Vector4f _value = Vector4f::kZero;
    };

    ASTRUCT()
    struct AILU_API AssetNamedIntVectorProperty
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        Vector4Int _value = Vector4Int::kZero;
    };

    ASTRUCT()
    struct AILU_API AssetTextureBinding
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        String _texture_guid;
    };

    ACLASS()
    class AILU_API AssetHeaderProbeDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
    };

    ACLASS()
    class AILU_API ShaderAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        String _file;
        APROPERTY()
        String _vs_entry;
        APROPERTY()
        String _ps_entry;
    };

    ACLASS()
    class AILU_API ComputeShaderAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        String _file;
        APROPERTY()
        String _kernel;
    };

    ACLASS()
    class AILU_API ScriptAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        String _file;
    };

    ACLASS()
    class AILU_API Texture2DAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        String _file;
        APROPERTY()
        bool _is_srgb = true;
    };

    ACLASS()
    class AILU_API MeshAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        String _file;
        APROPERTY()
        String _inner_file_name;
        APROPERTY()
        bool _is_combine_mesh = false;
    };

    ACLASS()
    class AILU_API MaterialAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        String _shader_guid;
        APROPERTY()
        Vector<String> _keywords;
        APROPERTY()
        Vector<AssetNamedUIntProperty> _uint_properties;
        APROPERTY()
        Vector<AssetNamedFloatProperty> _float_properties;
        APROPERTY()
        Vector<AssetNamedVectorProperty> _vector_properties;
        APROPERTY()
        Vector<AssetNamedIntVectorProperty> _int_vector_properties;
        APROPERTY()
        Vector<AssetTextureBinding> _texture_properties;
    };

    ASTRUCT()
    struct AILU_API AnimationClipFrameDocument
    {
        GENERATED_BODY()

        APROPERTY()
        Vector3f _position = Vector3f::kZero;
        APROPERTY()
        Quaternion _rotation;
        APROPERTY()
        Vector3f _scale = Vector3f::kOne;
    };

    ASTRUCT()
    struct AILU_API AnimationClipTrackDocument
    {
        GENERATED_BODY()

        APROPERTY()
        u16 _joint_index = 0u;
        APROPERTY()
        Vector<AnimationClipFrameDocument> _frames;
    };

    ACLASS()
    class AILU_API AnimationClipAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        String _clip_name;
        APROPERTY()
        u32 _frame_count = 0u;
        APROPERTY()
        f32 _duration = 0.0f;
        APROPERTY()
        f32 _frame_rate = 30.0f;
        APROPERTY()
        f32 _frame_duration = 0.0f;
        APROPERTY()
        bool _is_looping = true;
        APROPERTY()
        Vector<AnimationClipTrackDocument> _tracks;
    };

    ACLASS()
    class AILU_API SpriteAssetDocument : public Object
    {
        GENERATED_BODY()
    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        Guid _texture = Guid::EmptyGuid();
        APROPERTY()
        Vector4f _uv_rect = {0.0f,0.0f,1.0f,1.0f};
        APROPERTY()
        Vector2f _pivot = {0.5f,0.5f};
        APROPERTY()
        Vector2f _size = Vector2f::kOne;
        //九宫格slice,RLBT，靠近边界的像素不会被拉伸
        APROPERTY()
        Vector4f _border = Vector4f::kZero;
    };

    ASTRUCT()
    struct AILU_API InputProcessorDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _type;
        APROPERTY()
        Vector4f _params = Vector4f::kZero;
    };

    ASTRUCT()
    struct AILU_API InputInteractionDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _type;
        APROPERTY()
        Vector4f _params = Vector4f::kZero;
    };

    ASTRUCT()
    struct AILU_API InputBindingDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        String _control_path;
        APROPERTY()
        String _groups;
        APROPERTY()
        Vector<InputProcessorDocument> _processors;
        APROPERTY()
        Vector<InputInteractionDocument> _interactions;
        APROPERTY()
        bool _is_composite = false;
        APROPERTY()
        bool _is_part_of_composite = false;
        APROPERTY()
        String _composite_part_name;
    };

    ASTRUCT()
    struct AILU_API InputActionDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        u32 _id = 0u;
        APROPERTY()
        u8 _action_type = 0u;
        APROPERTY()
        u8 _value_type = 0u;
        APROPERTY()
        u8 _merge_strategy = 0u;
        APROPERTY()
        Vector<InputBindingDocument> _bindings;
        APROPERTY()
        String _composite_type;
        APROPERTY()
        Vector4f _composite_params = Vector4f::kZero;
        APROPERTY()
        Vector<InputBindingDocument> _composite_bindings;
    };

    ASTRUCT()
    struct AILU_API InputActionMapDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        u32 _id = 0u;
        APROPERTY()
        Vector<InputActionDocument> _actions;
    };

    ASTRUCT()
    struct AILU_API InputContextDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        i32 _priority = 0;
        APROPERTY()
        bool _consume_input = true;
        APROPERTY()
        bool _block_lower_contexts = false;
        APROPERTY()
        bool _active = false;
        APROPERTY()
        Vector<String> _action_map_names;
    };

    ACLASS()
    class AILU_API InputActionAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        Vector<InputActionMapDocument> _action_maps;
        APROPERTY()
        Vector<InputContextDocument> _contexts;
    };


    ASTRUCT()
    struct AILU_API SceneTagComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        String _tag = "Untagged";
        APROPERTY()
        u32 _layer_mask = 1u;
    };

    ASTRUCT()
    struct AILU_API SceneTransformComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        Vector3f _position = Vector3f::kZero;
        APROPERTY()
        Quaternion _rotation;
        APROPERTY()
        Vector3f _scale = Vector3f::kOne;
    };

    ASTRUCT()
    struct AILU_API SceneScriptComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        Guid _script_asset = Guid::EmptyGuid();
        APROPERTY()
        Vector<ECS::ScriptPropertyData> _properties;
    };

    ASTRUCT()
    struct AILU_API SceneStaticMeshComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _mesh_guid;
        APROPERTY()
        Vector<String> _material_guids;
    };

    ASTRUCT()
    struct AILU_API SceneLightDataDocument
    {
        GENERATED_BODY()

        APROPERTY()
        Vector4f _light_color = Vector4f::kZero;
        APROPERTY()
        Vector4f _light_param = Vector4f::kZero;
        APROPERTY()
        bool _is_two_side = false;
    };

    ASTRUCT()
    struct AILU_API SceneShadowDataDocument
    {
        GENERATED_BODY()

        APROPERTY()
        bool _is_cast_shadow = false;
        APROPERTY()
        f32 _constant_bias = 0.0f;
        APROPERTY()
        f32 _slope_bias = 0.0f;
    };

    ASTRUCT()
    struct AILU_API SceneLightComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _type;
        APROPERTY()
        SceneLightDataDocument _light;
        APROPERTY()
        SceneShadowDataDocument _shadow;
    };

    ASTRUCT()
    struct AILU_API SceneHierarchyComponentDocument
    {
        GENERATED_BODY()

        inline static const String kParentGuid = "_parent_guid";
        inline static const String kSiblingIndex = "_sibling_index";
        inline static const String kFirstChild = "_first_child";
        inline static const String kPrevSibling = "_prev_sibling";
        inline static const String kNextSibling = "_next_sibling";
        inline static const String kParent = "_parent";
        inline static const String kChildrenNum = "_children_num";
        inline static const String kInvMatrixAttach = "_inv_matrix_attach";
        inline static const String kEnabled = "_enabled";

        void Serialize(FArchive &ar)
        {
            SerializerWrapper<Guid>::Serialize(&_parent_guid, ar, &kParentGuid);
            SerializerWrapper<u32>::Serialize(&_sibling_index, ar, &kSiblingIndex);
            SerializerWrapper<String>::Serialize(&_inv_matrix_attach, ar, &kInvMatrixAttach);
            SerializerWrapper<bool>::Serialize(&_enabled, ar, &kEnabled);
        }

        void Deserialize(FArchive &ar)
        {
            auto *json_ar = dynamic_cast<JsonArchive *>(&ar);
            const bool is_v2 = json_ar != nullptr && json_ar->HasField(kParentGuid);
            const bool has_enabled = json_ar != nullptr && json_ar->HasField(kEnabled);
            if (is_v2)
            {
                SerializerWrapper<Guid>::Deserialize(&_parent_guid, ar, &kParentGuid);
                SerializerWrapper<u32>::Deserialize(&_sibling_index, ar, &kSiblingIndex);
                SerializerWrapper<String>::Deserialize(&_inv_matrix_attach, ar, &kInvMatrixAttach);
                if (has_enabled)
                    SerializerWrapper<bool>::Deserialize(&_enabled, ar, &kEnabled);
            }
            else
            {
                SerializerWrapper<u64>::Deserialize(&_first_child, ar, &kFirstChild);
                SerializerWrapper<u64>::Deserialize(&_prev_sibling, ar, &kPrevSibling);
                SerializerWrapper<u64>::Deserialize(&_next_sibling, ar, &kNextSibling);
                SerializerWrapper<u64>::Deserialize(&_parent, ar, &kParent);
                SerializerWrapper<u32>::Deserialize(&_children_num, ar, &kChildrenNum);
                SerializerWrapper<String>::Deserialize(&_inv_matrix_attach, ar, &kInvMatrixAttach);
            }
        }

        APROPERTY()
        Guid _parent_guid;
        APROPERTY()
        u32 _sibling_index = 0u;
        APROPERTY()
        u64 _first_child = 0u;
        APROPERTY()
        u64 _prev_sibling = 0u;
        APROPERTY()
        u64 _next_sibling = 0u;
        APROPERTY()
        u64 _parent = 0u;
        APROPERTY()
        u32 _children_num = 0u;
        APROPERTY()
        String _inv_matrix_attach;
        bool _enabled = true;
    };

    ASTRUCT()
    struct AILU_API SceneCameraComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _type;
        APROPERTY()
        f32 _aspect = 1.0f;
        APROPERTY()
        f32 _far_clip = 1000.0f;
        APROPERTY()
        f32 _near_clip = 0.1f;
        APROPERTY()
        f32 _fov_h = 60.0f;
        APROPERTY()
        f32 _size = 1.0f;
    };

    ASTRUCT()
    struct AILU_API SceneLightProbeComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        f32 _size = 10.0f;
        APROPERTY()
        bool _is_update_every_tick = false;
    };

    ASTRUCT()
    struct AILU_API SceneRigidBodyComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        f32 _mass = 1.0f;
    };

    ASTRUCT()
    struct AILU_API SceneRigidBody2DComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        ECS::EBody2DType _type = ECS::EBody2DType::kDynamic;
        APROPERTY()
        f32 _gravity_scale = 1.0f;
        APROPERTY()
        f32 _linear_damping = 0.0f;
        APROPERTY()
        f32 _angular_damping = 0.0f;
        APROPERTY()
        bool _fixed_rotation = false;
        APROPERTY()
        bool _continuous = false;
        APROPERTY()
        bool _allow_sleep = true;
    };

    ASTRUCT()
    struct AILU_API SceneColliderComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _type;
        APROPERTY()
        bool _is_trigger = true;
        APROPERTY()
        Vector3f _center = Vector3f::kZero;
        APROPERTY()
        Vector3f _param = Vector3f{1.0f, 1.0f, 0.0f};
    };

    ASTRUCT()
    struct AILU_API SceneCollider2DComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        Vector<ECS::ColliderShape2D> _shapes;
    };

    ASTRUCT()
    struct AILU_API SceneSkeletonMeshComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _mesh_guid;
        APROPERTY()
        Vector<String> _material_guids;
        APROPERTY()
        String _anim_clip_guid;
    };

    ASTRUCT()
    struct AILU_API SceneVXGIComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        Vector3Int _grid_num = Vector3Int(64, 64, 64);
        APROPERTY()
        f32 _distance = 20.0f;
    };

    ASTRUCT()
    struct AILU_API SceneSpriteRendererComponentDocument
    {
        GENERATED_BODY()

        APROPERTY()
        String _sprite_guid;
        APROPERTY()
        String _material_guid;
        APROPERTY()
        Vector4f _color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
        APROPERTY()
        i32 _sorting_layer = 0;
        APROPERTY()
        i32 _order_in_layer = 0;
        APROPERTY()
        i32 _blend_mode = 0;
        APROPERTY()
        bool _flip_x = false;
        APROPERTY()
        bool _flip_y = false;
        APROPERTY()
        bool _visible = true;
    };

    ASTRUCT()
    struct AILU_API SceneEntityDocument
    {
        GENERATED_BODY()

        inline static const String kEntityId = "_entity_id";
        inline static const String kEntityGuid = "_entity_guid";
        inline static const String kTagComponent = "_tag_component";
        inline static const String kHasTransformComponent = "_has_transform_component";
        inline static const String kTransformComponent = "_transform_component";
        inline static const String kHasScriptComponent = "_has_script_component";
        inline static const String kScriptComponent = "_script_component";
        inline static const String kHasStaticMeshComponent = "_has_static_mesh_component";
        inline static const String kStaticMeshComponent = "_static_mesh_component";
        inline static const String kHasLightComponent = "_has_light_component";
        inline static const String kLightComponent = "_light_component";
        inline static const String kHasHierarchyComponent = "_has_hierarchy_component";
        inline static const String kHierarchyComponent = "_hierarchy_component";
        inline static const String kHasCameraComponent = "_has_camera_component";
        inline static const String kCameraComponent = "_camera_component";
        inline static const String kHasLightprobeComponent = "_has_lightprobe_component";
        inline static const String kLightprobeComponent = "_lightprobe_component";
        inline static const String kHasRigidbodyComponent = "_has_rigidbody_component";
        inline static const String kRigidbodyComponent = "_rigidbody_component";
        inline static const String kHasColliderComponent = "_has_collider_component";
        inline static const String kColliderComponent = "_collider_component";
        inline static const String kHasRigidBody2DComponent = "_has_rigidbody_2d_component";
        inline static const String kRigidBody2DComponent = "_rigidbody_2d_component";
        inline static const String kHasCollider2DComponent = "_has_collider_2d_component";
        inline static const String kCollider2DComponent = "_collider_2d_component";
        inline static const String kHasSkeletonMeshComponent = "_has_skeleton_mesh_component";
        inline static const String kSkeletonMeshComponent = "_skeleton_mesh_component";
        inline static const String kHasSpriteRendererComponent = "_has_sprite_renderer_component";
        inline static const String kSpriteRendererComponent = "_sprite_renderer_component";
        inline static const String kHasVxgiComponent = "_has_vxgi_component";
        inline static const String kVxgiComponent = "_vxgi_component";
        inline static const String kDisabledComponents = "_disabled_components";

        void Serialize(FArchive &ar)
        {
            SerializerWrapper<Guid>::Serialize(&_entity_guid, ar, &kEntityGuid);
            SerializerWrapper<SceneTagComponentDocument>::Serialize(&_tag_component, ar, &kTagComponent);
            SerializerWrapper<Vector<String>>::Serialize(&_disabled_components, ar, &kDisabledComponents);

            if (_has_transform_component)
                SerializerWrapper<SceneTransformComponentDocument>::Serialize(&_transform_component, ar, &kTransformComponent);
            if (_has_script_component)
                SerializerWrapper<SceneScriptComponentDocument>::Serialize(&_script_component, ar, &kScriptComponent);
            if (_has_static_mesh_component)
                SerializerWrapper<SceneStaticMeshComponentDocument>::Serialize(&_static_mesh_component, ar, &kStaticMeshComponent);
            if (_has_light_component)
                SerializerWrapper<SceneLightComponentDocument>::Serialize(&_light_component, ar, &kLightComponent);
            if (_has_hierarchy_component)
                SerializerWrapper<SceneHierarchyComponentDocument>::Serialize(&_hierarchy_component, ar, &kHierarchyComponent);
            if (_has_camera_component)
                SerializerWrapper<SceneCameraComponentDocument>::Serialize(&_camera_component, ar, &kCameraComponent);
            if (_has_lightprobe_component)
                SerializerWrapper<SceneLightProbeComponentDocument>::Serialize(&_lightprobe_component, ar, &kLightprobeComponent);
            if (_has_rigidbody_component)
                SerializerWrapper<SceneRigidBodyComponentDocument>::Serialize(&_rigidbody_component, ar, &kRigidbodyComponent);
            if (_has_collider_component)
                SerializerWrapper<SceneColliderComponentDocument>::Serialize(&_collider_component, ar, &kColliderComponent);
            if (_has_rigidbody_2d_component)
                SerializerWrapper<SceneRigidBody2DComponentDocument>::Serialize(&_rigidbody_2d_component, ar,
                                                                                  &kRigidBody2DComponent);
            if (_has_collider_2d_component)
                SerializerWrapper<SceneCollider2DComponentDocument>::Serialize(&_collider_2d_component, ar,
                                                                                 &kCollider2DComponent);
            if (_has_skeleton_mesh_component)
                SerializerWrapper<SceneSkeletonMeshComponentDocument>::Serialize(&_skeleton_mesh_component, ar, &kSkeletonMeshComponent);
            if (_has_vxgi_component)
                SerializerWrapper<SceneVXGIComponentDocument>::Serialize(&_vxgi_component, ar, &kVxgiComponent);
            if (_has_sprite_renderer_component)
                SerializerWrapper<SceneSpriteRendererComponentDocument>::Serialize(&_sprite_renderer_component, ar, &kSpriteRendererComponent);
        }

        void Deserialize(FArchive &ar)
        {
            auto *json_ar = dynamic_cast<JsonArchive *>(&ar);
            const bool is_v2 = json_ar != nullptr && json_ar->HasField(kEntityGuid);
            if (is_v2)
            {
                SerializerWrapper<Guid>::Deserialize(&_entity_guid, ar, &kEntityGuid);
            }
            else
            {
                SerializerWrapper<u64>::Deserialize(&_entity_id, ar, &kEntityId);
            }
            SerializerWrapper<SceneTagComponentDocument>::Deserialize(&_tag_component, ar, &kTagComponent);
            if (json_ar != nullptr && json_ar->HasField(kDisabledComponents))
                SerializerWrapper<Vector<String>>::Deserialize(&_disabled_components, ar, &kDisabledComponents);

            auto deserialize_component = [&](bool &has_component, auto &component, const String &legacy_flag_name, const String &component_name)
            {
                using ComponentType = std::remove_reference_t<decltype(component)>;
                const bool has_legacy_flag = json_ar != nullptr && json_ar->HasField(legacy_flag_name);
                if (has_legacy_flag)
                {
                    SerializerWrapper<bool>::Deserialize(&has_component, ar, &legacy_flag_name);
                    if (has_component)
                    {
                        SerializerWrapper<ComponentType>::Deserialize(&component, ar, &component_name);
                    }
                    return;
                }

                has_component = json_ar != nullptr ? json_ar->HasField(component_name) : false;
                if (has_component)
                {
                    SerializerWrapper<ComponentType>::Deserialize(&component, ar, &component_name);
                }
            };

            deserialize_component(_has_transform_component, _transform_component, kHasTransformComponent, kTransformComponent);
            deserialize_component(_has_script_component, _script_component, kHasScriptComponent, kScriptComponent);
            deserialize_component(_has_static_mesh_component, _static_mesh_component, kHasStaticMeshComponent, kStaticMeshComponent);
            deserialize_component(_has_light_component, _light_component, kHasLightComponent, kLightComponent);
            deserialize_component(_has_hierarchy_component, _hierarchy_component, kHasHierarchyComponent, kHierarchyComponent);
            deserialize_component(_has_camera_component, _camera_component, kHasCameraComponent, kCameraComponent);
            deserialize_component(_has_lightprobe_component, _lightprobe_component, kHasLightprobeComponent, kLightprobeComponent);
            deserialize_component(_has_rigidbody_component, _rigidbody_component, kHasRigidbodyComponent, kRigidbodyComponent);
            deserialize_component(_has_collider_component, _collider_component, kHasColliderComponent, kColliderComponent);
            deserialize_component(_has_rigidbody_2d_component, _rigidbody_2d_component, kHasRigidBody2DComponent,
                                  kRigidBody2DComponent);
            deserialize_component(_has_collider_2d_component, _collider_2d_component, kHasCollider2DComponent,
                                  kCollider2DComponent);
            deserialize_component(_has_skeleton_mesh_component, _skeleton_mesh_component, kHasSkeletonMeshComponent, kSkeletonMeshComponent);
            deserialize_component(_has_sprite_renderer_component, _sprite_renderer_component, kHasSpriteRendererComponent, kSpriteRendererComponent);
            deserialize_component(_has_vxgi_component, _vxgi_component, kHasVxgiComponent, kVxgiComponent);
        }

        APROPERTY()
        u64 _entity_id = 0u;
        APROPERTY()
        Guid _entity_guid;
        APROPERTY()
        SceneTagComponentDocument _tag_component;
        Vector<String> _disabled_components;
        APROPERTY()
        bool _has_transform_component = false;
        APROPERTY()
        SceneTransformComponentDocument _transform_component;
        APROPERTY()
        bool _has_script_component = false;
        APROPERTY()
        SceneScriptComponentDocument _script_component;
        APROPERTY()
        bool _has_static_mesh_component = false;
        APROPERTY()
        SceneStaticMeshComponentDocument _static_mesh_component;
        APROPERTY()
        bool _has_light_component = false;
        APROPERTY()
        SceneLightComponentDocument _light_component;
        APROPERTY()
        bool _has_hierarchy_component = false;
        APROPERTY()
        SceneHierarchyComponentDocument _hierarchy_component;
        APROPERTY()
        bool _has_camera_component = false;
        APROPERTY()
        SceneCameraComponentDocument _camera_component;
        APROPERTY()
        bool _has_lightprobe_component = false;
        APROPERTY()
        SceneLightProbeComponentDocument _lightprobe_component;
        APROPERTY()
        bool _has_rigidbody_component = false;
        APROPERTY()
        SceneRigidBodyComponentDocument _rigidbody_component;
        APROPERTY()
        bool _has_collider_component = false;
        APROPERTY()
        SceneColliderComponentDocument _collider_component;
        APROPERTY()
        bool _has_rigidbody_2d_component = false;
        APROPERTY()
        SceneRigidBody2DComponentDocument _rigidbody_2d_component;
        APROPERTY()
        bool _has_collider_2d_component = false;
        APROPERTY()
        SceneCollider2DComponentDocument _collider_2d_component;
        APROPERTY()
        bool _has_skeleton_mesh_component = false;
        APROPERTY()
        SceneSkeletonMeshComponentDocument _skeleton_mesh_component;
        APROPERTY()
        bool _has_vxgi_component = false;
        APROPERTY()
        SceneVXGIComponentDocument _vxgi_component;
        APROPERTY()
        bool _has_sprite_renderer_component = false;
        APROPERTY()
        SceneSpriteRendererComponentDocument _sprite_renderer_component;
    };

    ACLASS()
    class AILU_API GraphAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        u32 _version = 1u;
        APROPERTY()
        String _schema_type;
        APROPERTY()
        Vector<GraphNodeData> _nodes;
        APROPERTY()
        Vector<GraphLinkData> _links;
        APROPERTY()
        Vector<GraphCommentData> _comments;
    };

    ACLASS()
    class AILU_API SceneAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        inline static constexpr u32 kCurrentSceneFormatVersion = 2u;
        inline static const String kHeader = "_header";
        inline static const String kSceneFormatVersion = "_scene_format_version";
        inline static const String kEntities = "_entities";

        void Serialize(FArchive &ar)
        {
            SerializerWrapper<AssetDocumentHeader>::Serialize(&_header, ar, &kHeader);
            SerializerWrapper<u32>::Serialize(&_scene_format_version, ar, &kSceneFormatVersion);
            SerializerWrapper<Vector<SceneEntityDocument>>::Serialize(&_entities, ar, &kEntities);
        }

        void Deserialize(FArchive &ar)
        {
            auto *json_ar = dynamic_cast<JsonArchive *>(&ar);
            SerializerWrapper<AssetDocumentHeader>::Deserialize(&_header, ar, &kHeader);
            // 旧场景缺失该字段：保持默认 1u，自然进入 V1 迁移路径。
            // 必须用 HasField 守卫，因为 JsonArchive 对缺失的标量字段会写入未初始化值而非保留默认值。
            if (json_ar == nullptr || json_ar->HasField(kSceneFormatVersion))
                SerializerWrapper<u32>::Deserialize(&_scene_format_version, ar, &kSceneFormatVersion);
            SerializerWrapper<Vector<SceneEntityDocument>>::Deserialize(&_entities, ar, &kEntities);
        }

        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        u32 _scene_format_version = 1u;
        APROPERTY()
        Vector<SceneEntityDocument> _entities;
    };
}

#endif// !__ASSET_DOCUMENT_H__
