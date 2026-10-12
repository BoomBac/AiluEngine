#pragma once
#ifndef __COMPONENT_H__
#define __COMPONENT_H__
#include "Framework/Core/CoreMinimal.h"
#include "Assets/AssetRegistry.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Array.h"

#include "Animation/AnimationInstance.h"
#include "Animation/RootMotion.h"
#include "Audio/AudioHandle.h"
#include "Entity.h"
#include "Framework/Math/Guid.h"
#include "Framework/Math/Transform.h"
#include "Objects/Serialize.h"
#include "Objects/Type.h"
#include "Render/Camera.h"
#include "Render/Features/CommonPasses.h"
#include "Render/PostprocessSettings.h"
#include "Render/StaticDiffuseProbeData.h"
#include "Render/Mesh.h"
#include "Render/2D/SpriteRenderData.h"

namespace Ailu
{
    namespace Render
    {
        class Material;
    }
}

#include "generated/Component.gen.h"

using Ailu::Render::Camera;
using Ailu::Render::Material;
using Ailu::Render::Mesh;
using Ailu::Render::RenderTexture;
using Ailu::Render::SkeletonMesh;

namespace Ailu
{
    namespace Render
    {
        class Texture2D;
        class Sprite;
    }
    /*
    只访问自身字段
    不访问 Registry / World
    不访问其它 Component
    不遍历 Entity 层级
    不触发渲染、物理、脚本等系统行为
    不产生跨组件副作用
    主要用于维护数据一致性或提供便捷访问
    */
    namespace ECS
    {
        using StableComponentTypeId = u64;
        struct ComponentTypeInfo
        {
            ComponentTypeId _runtime_id;
            String _stable_name;
        };

        AILU_API ComponentTypeId RegisterComponentType(StringView stable_name);
        // Reverse lookup: runtime ComponentTypeId -> stable type name.
        AILU_API StringView GetComponentStableName(ComponentTypeId type_id);

        template<typename T>
        ComponentTypeId GetComponentTypeId()
        {
            static const ComponentTypeId kTypeId = RegisterComponentType(T::StaticTypeName());
            return kTypeId;
        }

        constexpr u64 HashString(std::string_view value)
        {
            u64 hash = 14695981039346656037ull;

            for (char ch : value)
            {
                hash ^= static_cast<u8>(ch);
                hash *= 1099511628211ull;
            }

            return hash;
        }


#define DECLARE_COMPONENT(name, stable_name)                            \
public:                                                                 \
    static ComponentTypeId StaticComponentTypeId()                      \
    {                                                                   \
        return GetComponentTypeId<name>();                              \
    }                                                                   \
                                                                        \
    static constexpr StableComponentTypeId StaticStableTypeId()         \
    {                                                                   \
        return HashString(stable_name);                                 \
    }                                                                   \
                                                                        \
    static constexpr std::string_view StaticTypeName()                  \
    {                                                                   \
        return stable_name;                                             \
    }

        AENUM()
        enum class EMotionVectorType
        {
            kCameraOnly,
            kPerObject,
            kForceZero
        };
        struct AILU_API TagComponent
        {
            DECLARE_COMPONENT(TagComponent, "Ailu.ECS.TagComponent")
            String _name;
            String _tag = "Untagged";
            u32 _layer_mask = 1u;
            Guid _prefab_entity = Guid::EmptyGuid();
            bool _is_transient = false;
            String _source_scene_guid;
            String _source_object_guid;
        };


        struct AILU_API PersistentIdComponent
        {
            DECLARE_COMPONENT(PersistentIdComponent, "Ailu.ECS.PersistentIdComponent")
            Guid _guid;

            PersistentIdComponent() : _guid(Guid::Generate()) {}
            explicit PersistentIdComponent(Guid guid) : _guid(std::move(guid)) {}
        };


        struct AILU_API TransformComponent
        {
            DECLARE_COMPONENT(TransformComponent, "Ailu.ECS.TransformComponent")
            inline static u64 kInvalidVersion = std::numeric_limits<u64>::max();
            Transform _local_transform;

            Matrix4x4f _local_matrix = Matrix4x4f::Identity();
            Matrix4x4f _world_matrix = Matrix4x4f::Identity();
            Matrix4x4f _prev_world_matrix = Matrix4x4f::Identity();
            Matrix4x4f _render_world_matrix = Matrix4x4f::Identity();
            Matrix4x4f _prev_render_world_matrix = Matrix4x4f::Identity();
            Vector3f _position = Vector3f::kZero;
            Vector3f _prev_position = Vector3f::kZero;
            Vector3f _render_position = Vector3f::kZero;
            Vector3f _scale = Vector3f::kOne;
            Vector3f _prev_scale = Vector3f::kOne;
            Vector3f _render_scale = Vector3f::kOne;
            Quaternion _rotation = Quaternion::Identity();
            Quaternion _prev_rotation = Quaternion::Identity();
            Quaternion _render_rotation = Quaternion::Identity();

            u64 _local_version = 0u;
            u64 _world_version = 0u;
            u64 _cached_parent_world_version = kInvalidVersion;

            bool _local_dirty = true;
            bool _world_dirty = true;
            bool _world_to_local_dirty = true;
            bool _reset_render_interpolation = false;

            void ResetRenderInterpolation()
            {
                _reset_render_interpolation = true;
            }

            bool SetLocalPosition(const Vector3f &position)
            {
                if (_local_transform._position == position)
                    return false;

                _local_transform._position = position;
                MarkLocalDirty();
                return true;
            }

            Vector3f GetLocalPosition() const
            {
                return _local_transform._position;
            }

            bool SetLocalRotation(const Quaternion &rotation)
            {
                if (_local_transform._rotation == rotation)
                    return false;

                _local_transform._rotation = rotation;
                MarkLocalDirty();
                return true;
            }

            Quaternion GetLocalRotation() const
            {
                return _local_transform._rotation;
            }

            bool SetLocalScale(const Vector3f &scale)
            {
                if (_local_transform._scale == scale)
                    return false;

                _local_transform._scale = scale;
                MarkLocalDirty();
                return true;
            }

            bool SetLocalTransform(const Transform &transform)
            {
                if (_local_transform._position == transform._position && _local_transform._rotation == transform._rotation &&
                    _local_transform._scale == transform._scale)
                    return false;

                _local_transform = transform;
                MarkLocalDirty();
                return true;
            }

            Vector3f GetLocalScale() const
            {
                return _local_transform._scale;
            }

            const Matrix4x4f &GetWorldMatrix() const
            {
                AL_ASSERT(!_world_dirty);
                return _world_matrix;
            }

            u64 GetWorldVersion() const
            {
                AL_ASSERT(!_world_dirty);
                return _world_version;
            }

            const Matrix4x4f &GetRenderWorldMatrix() const
            {
                AL_ASSERT(!_world_dirty);
                return _render_world_matrix;
            }
            //world space
            Vector3f GetPosition() const
            {
                AL_ASSERT(!_world_dirty);
                return _position;
            }

            Vector3f GetScale() const
            {
                AL_ASSERT(!_world_dirty);
                return _scale;
            }

            Quaternion GetRotation() const
            {
                AL_ASSERT(!_world_dirty);
                return _rotation;
            }

            Vector3f GetRenderPosition() const
            {
                AL_ASSERT(!_world_dirty);
                return _render_position;
            }

            Vector3f GetRenderScale() const
            {
                AL_ASSERT(!_world_dirty);
                return _render_scale;
            }

            Quaternion GetRenderRotation() const
            {
                AL_ASSERT(!_world_dirty);
                return _render_rotation;
            }

        private:
            void MarkLocalDirty()
            {
                _local_dirty = true;
                _world_dirty = true;
                ++_local_version;
            }
        };

        AENUM()
        enum class EScriptPropertyType : u8
        {
            kBool,
            kInt,
            kFloat,
            kString,
            kVector2,
            kVector3,
            kVector4,
            kColor,
            kEntity,
            kAsset
        };

        ASTRUCT()
        struct AILU_API ScriptPropertyData
        {
            GENERATED_BODY()

            APROPERTY()
            String _name;
            APROPERTY()
            EScriptPropertyType _type = EScriptPropertyType::kFloat;
            APROPERTY()
            bool _bool_value = false;
            APROPERTY()
            i32 _int_value = 0;
            APROPERTY()
            f32 _float_value = 0.0f;
            APROPERTY()
            String _string_value;
            APROPERTY()
            Vector4f _vector_value = Vector4f::kZero;
            APROPERTY()
            Guid _guid_value = Guid::EmptyGuid();
            String _asset_type;
            APROPERTY()
            bool _is_orphan = false;
        };

        struct AILU_API ScriptComponent
        {
            DECLARE_COMPONENT(ScriptComponent, "Ailu.ECS.ScriptComponent")
            Guid _script_asset = Guid::EmptyGuid();
            Vector<ScriptPropertyData> _properties;

            ScriptComponent() = default;
            explicit ScriptComponent(Guid script_asset) : _script_asset(std::move(script_asset)) {}
        };

        struct LightData
        {
            Vector4f _light_pos;
            Vector4f _light_dir;
            Color _light_color; // Runtime linear; JSON serialization stores sRGB.
            Vector4f _light_param;
            Vector3f _area_points[4];
            bool _is_two_side;
        };

        struct ShadowData
        {
            bool _is_cast_shadow;
            float _constant_bias;
            float _slope_bias;
            float _padding;
        };

        AENUM()
        enum class ELightType
        {
            kDirectional,
            kPoint,
            kSpot,
            kArea
        };

        struct AILU_API LightComponent
        {
            DECLARE_COMPONENT(LightComponent, "Ailu.ECS.LightComponent")
            inline const static Vector3f kDefaultDirectionalLightDir = Vector3f(0.0f, -1.0f, 0.0f);
            LightData _light;
            ShadowData _shadow;
            ELightType _type;
        };

        struct AILU_API CCamera
        {
            DECLARE_COMPONENT(CCamera, "Ailu.ECS.CCamera")
            Camera _camera;
        };

        AENUM()
        enum class EEnvironmentSource
        {
            kProceduralSky,
            kTexture
        };

        ASTRUCT()
        struct AILU_API ProceduralSkySettings
        {
            GENERATED_BODY()

            APROPERTY()
            Color _sky_color = Color(0.18f, 0.32f, 0.58f, 1.0f);

            APROPERTY()
            Color _ground_color = Color(0.08f, 0.06f, 0.04f, 1.0f);
        };

        ASTRUCT()
        struct AILU_API EnvironmentComponent
        {
            GENERATED_BODY()

            DECLARE_COMPONENT(EnvironmentComponent, "Ailu.ECS.EnvironmentComponent")

            APROPERTY()
            EEnvironmentSource _source = EEnvironmentSource::kProceduralSky;

            APROPERTY()
            Guid _texture = Guid::EmptyGuid();

            APROPERTY()
            ProceduralSkySettings _procedural_sky;

            APROPERTY()
            f32 _intensity = 1.0f;

            APROPERTY()
            f32 _rotation = 0.0f;

            APROPERTY()
            bool _render_sky = true;

            APROPERTY()
            bool _enable_ibl = true;

            //编辑器选中环境实体时的预览设置，不参与运行时环境配置。
            i32 _src_type = 0;
            f32 _mipmap = 0.0f;
        };

        AENUM()
        enum class ELocalFogVolumeShape : u32
        {
            kBox,
            kSphere
        };

        ASTRUCT()
        struct AILU_API LocalFogVolumeComponent
        {
            GENERATED_BODY()
            DECLARE_COMPONENT(LocalFogVolumeComponent, "Ailu.ECS.LocalFogVolumeComponent")

            APROPERTY()
            bool _enabled = true;
            APROPERTY()
            ELocalFogVolumeShape _shape = ELocalFogVolumeShape::kBox;
            // Extinction per world unit; box domain [-0.5,0.5], sphere radius 1.
            APROPERTY(Range(0.0f,10.0f))
            f32 _density = 0.1f;
            APROPERTY()
            Vector3f _scattering_albedo = Vector3f::kOne;
            // Fade distance in normalized local coordinates.
            APROPERTY(Range(0.001f,0.5f))
            f32 _edge_fade = 0.1f;
        };

        ASTRUCT()
        struct AILU_API PostprocessVolumeComponent
        {
            GENERATED_BODY()

            DECLARE_COMPONENT(PostprocessVolumeComponent, "Ailu.ECS.PostprocessVolumeComponent")

            APROPERTY()
            bool _is_global = true;

            APROPERTY()
            i32 _priority = 0;

            APROPERTY()
            f32 _blend_weight = 1.0f;

            APROPERTY()
            f32 _blend_distance = 0.0f;

            APROPERTY()
            Render::PostprocessSettings _settings;
        };

        struct AILU_API StaticMeshComponent
        {
            DECLARE_COMPONENT(StaticMeshComponent, "Ailu.ECS.StaticMeshComponent")
            Ref<Mesh> _p_mesh;
            Guid _mesh_guid = Guid::EmptyGuid();
            Vector<Ref<Material>> _p_mats;
            Vector<Guid> _material_guids;
            Vector<AABB> _transformed_aabbs;
            EMotionVectorType _motion_vector_type = EMotionVectorType::kPerObject;
        };

        struct AILU_API CSkeletonMesh
        {
            DECLARE_COMPONENT(CSkeletonMesh, "Ailu.ECS.CSkeletonMesh")
            Ref<SkeletonMesh> _p_mesh;
            Guid _mesh_guid = Guid::EmptyGuid();
            Vector<Ref<Material>> _p_mats;
            Vector<Guid> _material_guids;
            Vector<AABB> _transformed_aabbs;
            EMotionVectorType _motion_vector_type = EMotionVectorType::kPerObject;
        };

        struct AILU_API AnimatorComponent
        {
            DECLARE_COMPONENT(AnimatorComponent, "Ailu.ECS.AnimatorComponent")
            Guid _controller = Guid::EmptyGuid();
            Guid _clip = Guid::EmptyGuid();
            f32 _speed = 1.0f;
            bool _play_on_awake = true;
            ERootMotionMode _root_motion_mode = ERootMotionMode::kDisabled;
            AnimationInstanceHandle _instance = kInvalidAnimationInstanceHandle;
            bool _started = false;
        };


        struct AILU_API CHierarchy
        {
            DECLARE_COMPONENT(CHierarchy, "Ailu.ECS.CHierarchy")
            ECS::Entity _first_child = ECS::kInvalidEntity;
            ECS::Entity _prev_sibling = ECS::kInvalidEntity;
            ECS::Entity _next_sibling = ECS::kInvalidEntity;
            ECS::Entity _parent = ECS::kInvalidEntity;
            u32 _children_num = 0;
            Matrix4x4f _inv_matrix_attach;
            bool _enabled = true;
            bool _enabled_in_hierarchy = true;
        };

        struct AILU_API CLightProbe
        {
            DECLARE_COMPONENT(CLightProbe, "Ailu.ECS.CLightProbe")
            f32 _size = 10.0f;
            bool _is_update_every_tick = false;
            bool _is_dirty = true;
            i32 _src_type = 0;
            f32 _mipmap = 0.0f;
            Ref<RenderTexture> _cubemap;
            Ref<Render::CubeMapGenPass> _pass;
            Material *_debug_material;
            CLightProbe();
        };

        ASTRUCT()
        struct AILU_API CLightProbeGroup
        {
            GENERATED_BODY()

            DECLARE_COMPONENT(CLightProbeGroup, "Ailu.ECS.CLightProbeGroup")

            APROPERTY()
            Vector3Int _probe_count = Vector3Int(8, 4, 8);
            APROPERTY()
            Vector3f _size = Vector3f(10.0f, 5.0f, 10.0f);
            APROPERTY()
            Guid _baked_data = Guid::EmptyGuid();
            APROPERTY()
            bool _draw_probes = true;

            bool _bake_requested = false;
            bool _capture_bake_requested = false;
            bool _preview_cubemap = false;
        };

        AENUM()
        enum class EDDGIProbeDebugMode
        {
            kRadiance,
            kDistance,
            kHitType,
            kRayDirection,
            kIrradiance,
            kDistanceMean,
            kDistanceVariance,
            kFinalWeight
        };

        ASTRUCT()
        struct AILU_API DDGIVolumeComponent
        {
            GENERATED_BODY()

            DECLARE_COMPONENT(DDGIVolumeComponent, "Ailu.ECS.DDGIVolumeComponent")

            APROPERTY()
            Vector3UInt _probe_count = Vector3UInt(8u, 4u, 8u);
            APROPERTY()
            Vector3f _probe_spacing = Vector3f(2.0f, 2.0f, 2.0f);
            APROPERTY()
            bool _draw_probes = true;
            APROPERTY()
            f32 _max_ray_distance = 50.0f;
            APROPERTY()
            bool _draw_probe_rays = false;
            APROPERTY()
            bool _use_probe_relocation = false;
            APROPERTY(Range(0.0f, 1.0f))
            f32 _relocation_backface_threshold = 0.25f;
            APROPERTY(Range(0.0f, 1.0f))
            f32 _random_ray_backface_threshold = 0.10f;
            APROPERTY(Range(0.0f, 0.5f))
            f32 _relocation_min_frontface_distance_ratio = 0.1f;
            APROPERTY()
            bool _draw_relocation_debug = false;
            APROPERTY()
            bool _freeze_ray_rotation = false;
            APROPERTY()
            f32 _debug_probe_radius = 0.12f;
            APROPERTY()
            f32 _debug_ray_length = 4.0f;
            APROPERTY()
            EDDGIProbeDebugMode _debug_mode = EDDGIProbeDebugMode::kHitType;
        };

        ASTRUCT()
        struct AILU_API SurfelGIComponent
        {
            GENERATED_BODY()

            DECLARE_COMPONENT(SurfelGIComponent, "Ailu.ECS.SurfelGIComponent")

            APROPERTY()
            bool _enabled = true;
            APROPERTY()
            bool _debug_draw_surfels = true;
            APROPERTY()
            bool _debug_show_irradiance = false;
            APROPERTY()
            bool _debug_opaque_disks = false;
            APROPERTY()
            f32 _debug_irradiance_exposure = 1.0f;
            APROPERTY()
            u32 _debug_lighting_mode = 0u;
            APROPERTY()
            bool _debug_coverage = false;
            APROPERTY()
            f32 _base_cell_size = 0.125f;
            APROPERTY()
            u32 _cascade_count = 5u;
            APROPERTY()
            u32 _grid_resolution = 64u;
            APROPERTY()
            f32 _target_pixel_radius = 8.0f;
            APROPERTY(Range(0.0f, 1.0f))
            f32 _spawn_coverage_threshold = 0.2f;
            APROPERTY(Range(0.5f, 3.0f))
            f32 _spawn_spacing_scale = 1.25f;
            APROPERTY(Range(1, 4096))
            u32 _max_spawn_per_frame = 512u;
            APROPERTY()
            bool _lighting_enabled = true;
            APROPERTY(Range(1, 8))
            u32 _rays_per_surfel = 4u;
            APROPERTY(Range(1, 4096))
            u32 _max_lighting_updates_per_frame = 256u;
            APROPERTY(Range(0.0f, 0.99f))
            f32 _lighting_hysteresis = 0.9f; // Legacy scene field; unused by progressive accumulation.
            APROPERTY()
            f32 _lighting_max_ray_distance = 50.0f;
            APROPERTY(Range(0.0f, 1.0f))
            f32 _spawn_normal_threshold = 0.75f;
            APROPERTY(Range(0.0f, 1.0f))
            f32 _blue_noise_strength = 0.15f;
            APROPERTY()
            u32 _max_surfel_count = 262144u;
            APROPERTY()
            i32 _debug_cascade = -1;

            bool _debug_draw_tile_grid = false;
            bool _manual_spawn = false;
            mutable bool _spawn_once_requested = false;
        };

        struct CRigidBody
        {
            DECLARE_COMPONENT(CRigidBody, "Ailu.ECS.CRigidBody")
            f32 _mass = 1.f;
            Vector3f _velocity;
            Vector3f _force;
            Vector3f _angular_velocity = Vector3f::kZero;// 角速度
            Vector3f _torque = Vector3f::kZero;          // 力矩
            f32 _inertia = 1.0f;                         // 转动惯量，假设为常量
        };

        AENUM()
        enum class EColliderType
        {
            kBox,
            kSphere,
            kCapsule
        };

        struct CCollider
        {
            DECLARE_COMPONENT(CCollider, "Ailu.ECS.CCollider")
            EColliderType _type = EColliderType::kBox;
            bool _is_trigger = true;
            Vector3f _center = Vector3f::kZero;
            /*
            kBox : size
            kSphere : radius,0,0
            kCapsule: radius,height,direction(x-axis,y-axis,z-axis)
            */
            Vector3f _param = Vector3f{1.0f, 1.0f, 0.f};
            static Sphere AsShpere(const CCollider &c);
            static OBB AsBox(const CCollider &c);
            static Capsule AsCapsule(const CCollider &c);
        };


        struct CVXGI
        {
            DECLARE_COMPONENT(CVXGI, "Ailu.ECS.CVXGI")
            Vector3Int _grid_num = Vector3Int(64, 64, 64);
            f32 _distance = 20.0f;
            //runtime prop
            Vector3f _grid_size = Vector3f{32.f, 32.f, 32.f};
            Vector3f _center = Vector3f::kZero;
            Vector3f _size = Vector3f::kOne;
            bool _is_draw_grid = false;
            bool _is_draw_voxel = false;
            u16 _mipmap = 0u;
            //texture space
            f32 _max_distance = 0.6f;
            f32 _min_distance = 0.12f;
            f32 _diffuse_cone_angle = 60.0f;
        };

        struct AILU_API SpriteRendererComponent
        {
            DECLARE_COMPONENT(SpriteRendererComponent, "Ailu.ECS.SpriteRendererComponent")

            Render::Sprite* _sprite = nullptr;
            Guid _sprite_guid = Guid::EmptyGuid();
            mutable Guid _sprite_handle_guid = Guid::EmptyGuid();
            mutable AssetHandle<Render::Sprite> _sprite_handle;
            Ref<Render::Material> _material;
            Guid _material_guid = Guid::EmptyGuid();
            mutable Guid _material_handle_guid = Guid::EmptyGuid();
            mutable AssetHandle<Render::Material> _material_handle;

            Color _color = Colors::kWhite; // Runtime linear; JSON serialization stores sRGB.

            i16 _sorting_layer = 0;
            i32 _order_in_layer = 0;

            Render::ESpriteBlendMode _blend_mode = Render::ESpriteBlendMode::kAlpha;

            bool _flip_x = false;
            bool _flip_y = false;
            bool _visible = true;
        };

        struct AILU_API AudioSourceComponent
        {
            DECLARE_COMPONENT(AudioSourceComponent, "Ailu.ECS.AudioSourceComponent")
            Guid _audio_event;
            bool _play_on_awake = false;
            bool _loop = false;
            bool _spatial = true;
            f32 _volume = 1.0f;
            f32 _pitch = 1.0f;
            f32 _min_distance = 1.0f;
            f32 _max_distance = 30.0f;
            f32 _priority = 0.5f;
            AudioHandle _runtime_handle;
        };

        struct AILU_API AudioListenerComponent
        {
            DECLARE_COMPONENT(AudioListenerComponent, "Ailu.ECS.AudioListenerComponent")
        };
    }// namespace ECS
};// namespace Ailu

namespace Ailu::DebugDrawer
{
    void AILU_API DebugWireframe(const ECS::CCollider &c, const Matrix4x4f &mat, Color color = Colors::kGreen);
    void AILU_API DebugWireframe(const ECS::CVXGI &c, const Matrix4x4f &mat, Color color = Colors::kGreen);
};// namespace Ailu::DebugDrawer
#endif// __COMPONENT_H__
