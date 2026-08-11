#pragma once
#ifndef __PHYSICS_2D_COMPONENTS_H__
#define __PHYSICS_2D_COMPONENTS_H__

#include "Framework/Math/ALMath.hpp"
#include "Scene/Component.h"

#include "generated/Physics2DComponents.gen.h"

namespace Ailu::ECS
{
    AENUM()
    enum class EBody2DType : u8
    {
        kStatic,
        kKinematic,
        kDynamic
    };

    AENUM()
    enum class ECollider2DShape : u8
    {
        kBox,
        kCircle,
        kCapsule,
        kPolygon,
        kChain
    };

    ASTRUCT()
    struct AILU_API RigidBody2DComponent
    {
        GENERATED_BODY()
        DECLARE_COMPONENT(RigidBody2DComponent, "Ailu.ECS.RigidBody2DComponent")

        APROPERTY()
        EBody2DType _type = EBody2DType::kDynamic;
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
    struct AILU_API ColliderShape2D
    {
        GENERATED_BODY()
        APROPERTY()
        ECollider2DShape _type = ECollider2DShape::kBox;
        APROPERTY()
        Vector2f _center = Vector2f::kZero;
        APROPERTY()
        f32 _rotation = 0.0f;
        APROPERTY()
        Vector2f _size = Vector2f::kOne;
        APROPERTY()
        f32 _radius = 0.5f;
        APROPERTY()
        f32 _height = 1.0f;
        APROPERTY()
        bool _is_trigger = false;
        APROPERTY()
        f32 _density = 1.0f;
        APROPERTY()
        f32 _friction = 0.3f;
        APROPERTY()
        f32 _restitution = 0.0f;
        APROPERTY()
        u8 _layer = 0;
    };

    ASTRUCT()
    struct AILU_API Collider2DComponent
    {
        GENERATED_BODY()
        DECLARE_COMPONENT(Collider2DComponent, "Ailu.ECS.Collider2DComponent")

        APROPERTY()
        Vector<ColliderShape2D> _shapes;
    };
}

#endif
