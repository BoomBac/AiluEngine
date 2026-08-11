#pragma once
#ifndef __PHYSICS_2D_TYPES_H__
#define __PHYSICS_2D_TYPES_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/ALMath.hpp"
#include "Scene/Entity.h"

namespace Ailu
{
    AENUM()
    enum class EPhysicsContact2DType : u8
    {
        kCollisionBegin,
        kCollisionEnd,
        kTriggerBegin,
        kTriggerEnd
    };

    struct PhysicsContact2D
    {
        EPhysicsContact2DType _type = EPhysicsContact2DType::kCollisionBegin;
        ECS::Entity _entity_a = ECS::kInvalidEntity;
        ECS::Entity _entity_b = ECS::kInvalidEntity;
        u16 _shape_a = 0u;
        u16 _shape_b = 0u;
        Vector2f _point = Vector2f::kZero;
        Vector2f _normal = Vector2f::kZero;
    };

    struct PhysicsQueryFilter
    {
        u32 _layer_mask = 0xffffffffu;
        bool _hit_trigger = false;
    };

    struct Raycast2DDesc
    {
        Vector2f _origin = Vector2f::kZero;
        Vector2f _direction = Vector2f(1.0f, 0.0f);
        f32 _distance = 0.0f;
        PhysicsQueryFilter _filter;
    };

    struct RaycastHit2D
    {
        ECS::Entity _entity = ECS::kInvalidEntity;
        u16 _shape_index = 0u;
        Vector2f _point = Vector2f::kZero;
        Vector2f _normal = Vector2f::kZero;
        f32 _distance = 0.0f;
        f32 _fraction = 0.0f;
    };

    struct CircleCast2DDesc
    {
        Vector2f _origin = Vector2f::kZero;
        f32 _radius = 0.5f;
        Vector2f _direction = Vector2f(1.0f, 0.0f);
        f32 _distance = 0.0f;
        PhysicsQueryFilter _filter;
    };

    using ShapeCastHit2D = RaycastHit2D;

    struct OverlapCircle2DDesc
    {
        Vector2f _center = Vector2f::kZero;
        f32 _radius = 0.5f;
        PhysicsQueryFilter _filter;
    };

    struct OverlapBox2DDesc
    {
        Vector2f _center = Vector2f::kZero;
        Vector2f _size = Vector2f::kOne;
        f32 _rotation = 0.0f;
        PhysicsQueryFilter _filter;
    };

    struct OverlapHit2D
    {
        ECS::Entity _entity = ECS::kInvalidEntity;
        u16 _shape_index = 0u;
    };
}

#endif
