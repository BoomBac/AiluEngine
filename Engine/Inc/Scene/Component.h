#pragma once
#ifndef __COMPONENT_H__
#define __COMPONENT_H__
#include "GlobalMarco.h"

#include "Entity.hpp"
#include "Framework/Math/Transform.h"
#include "Objects/Serialize.h"
#include "Render/Camera.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Pass/RenderPass.h"
#include "Animation/Clip.h"
#include "Animation/BlendSpace.h"

namespace Ailu
{
    struct AILU_API TagComponent
    {
        DECLARE_CLASS(TagComponent)
        String _name;
    };
    Archive &operator<<(Archive &ar, const TagComponent &c);
    Archive &operator>>(Archive &ar, TagComponent &c);

    struct AILU_API TransformComponent
    {
        DECLARE_CLASS(TransformComponent)
        Transform _transform;
    };

    Archive &operator<<(Archive &ar, TransformComponent &c);
    Archive &operator>>(Archive &ar, TransformComponent &c);

    struct LightData
    {
        Vector4f _light_pos;
        Vector4f _light_dir;
        Color _light_color;
        Vector4f _light_param;
        Vector3f _area_points[4];
        bool _is_two_side;
    };
    Archive &operator<<(Archive &ar, const LightData &c);
    Archive &operator>>(Archive &ar, LightData &c);

    struct ShadowData
    {
        bool _is_cast_shadow;
        float _constant_bias;
        float _slope_bias;
        float _padding;
    };

    Archive &operator<<(Archive &ar, const ShadowData &c);
    Archive &operator>>(Archive &ar, ShadowData &c);

    DECLARE_ENUM(ELightType, kDirectional, kPoint, kSpot,kArea)
    struct AILU_API LightComponent
    {
        DECLARE_CLASS(LightComponent)
        inline const static Vector3f kDefaultDirectionalLightDir = Vector3f(0.0f, -1.0f, 0.0f);
        LightData _light;
        ShadowData _shadow;
        ELightType::ELightType _type;
        Array<Camera, 6> _shadow_cameras;
        Array<Vector4f, 4> _cascade_shadow_data;
    };

    Archive &operator<<(Archive &ar, const LightComponent &c);
    Archive &operator>>(Archive &ar, LightComponent &c);

    struct AILU_API CCamera
    {
        DECLARE_CLASS(CCamera)
        Camera _camera;
    };
    Archive &operator<<(Archive &ar, const CCamera &c);
    Archive &operator>>(Archive &ar, CCamera &c);

    struct AILU_API StaticMeshComponent
    {
        DECLARE_CLASS(StaticMeshComponent)
        Ref<Mesh> _p_mesh;
        Vector<Ref<Material>> _p_mats;
        Vector<AABB> _transformed_aabbs;
    };

    Archive &operator<<(Archive &ar, const StaticMeshComponent &c);
    Archive &operator>>(Archive &ar, StaticMeshComponent &c);

    struct AILU_API CSkeletonMesh
    {
        DECLARE_CLASS(CSkeletonMesh)
        Ref<SkeletonMesh> _p_mesh;
        Vector<Ref<Material>> _p_mats;
        Vector<AABB> _transformed_aabbs;
        Ref<AnimationClip> _anim_clip;
        //temp
        f32 _anim_time = 0.0f;
        Ref<AnimationClip> _blend_anim_clip;
        BlendSpace _blend_space;
        //0 clip,1 blend space,2 anim graph
        i16 _anim_type;
    };

    Archive &operator<<(Archive &ar, const CSkeletonMesh &c);
    Archive &operator>>(Archive &ar, CSkeletonMesh &c);

    struct AILU_API CHierarchy
    {
        DECLARE_CLASS(CHierarchy)
        ECS::Entity _first_child = ECS::kInvalidEntity;
        ECS::Entity _prev_sibling = ECS::kInvalidEntity;
        ECS::Entity _next_sibling = ECS::kInvalidEntity;
        ECS::Entity _parent = ECS::kInvalidEntity;
        u32 _children_num = 0;
        Matrix4x4f _inv_matrix_attach;
    };
    Archive &operator<<(Archive &ar, const CHierarchy &c);
    Archive &operator>>(Archive &ar, CHierarchy &c);

    class CubeMapGenPass;
    struct AILU_API CLightProbe
    {
        DECLARE_CLASS(CLightProbe)
        f32 _size = 10.0f;
        bool _is_update_every_tick = false;
        bool _is_dirty = true;
        i32 _src_type = 0;
        f32 _mipmap = 0.0f;
        Ref<RenderTexture> _cubemap;
        Ref<CubeMapGenPass> _pass;
        Material *_debug_material;
        CLightProbe();
    };
    Archive &operator<<(Archive &ar, const CLightProbe &c);
    Archive &operator>>(Archive &ar, CLightProbe &c);

    struct CRigidBody
    {
        DECLARE_CLASS(CRigidBody)
        f32 _mass = 1.f;
        Vector3f _velocity;
        Vector3f _force;
        Vector3f _angular_velocity = Vector3f::kZero;// 角速度
        Vector3f _torque = Vector3f::kZero;          // 力矩
        f32 _inertia = 1.0f;                         // 转动惯量，假设为常量
    };
    Archive &operator<<(Archive &ar, const CRigidBody &c);
    Archive &operator>>(Archive &ar, CRigidBody &c);

    DECLARE_ENUM(EColliderType,kBox,kSphere,kCapsule)
    struct CCollider
    {
        DECLARE_CLASS(CCollider)
        EColliderType::EColliderType _type = EColliderType::kBox;
        bool                     _is_trigger = true;
        Vector3f                 _center = Vector3f::kZero;
        /*
        kBox : size
        kSphere : radius,0,0
        kCapsule: radius,height,direction(x-axis,y-axis,z-axis)
        */
        Vector3f _param = Vector3f{1.0f,1.0f,0.f};
        static Sphere AsShpere(const CCollider& c);
        static OBB AsBox(const CCollider &c);
        static Capsule AsCapsule(const CCollider &c);
    };
    Archive &operator<<(Archive &ar, const CCollider &c);
    Archive &operator>>(Archive &ar, CCollider &c);

    namespace DebugDrawer
    {
        void AILU_API DebugWireframe(const CCollider &c, const Transform &t, Color color = Colors::kGreen);

    }
}// namespace Ailu
#endif// __COMPONENT_H__