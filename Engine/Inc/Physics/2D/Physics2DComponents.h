#pragma once
#ifndef __PHYSICS_2D_COMPONENTS_H__
#define __PHYSICS_2D_COMPONENTS_H__

#include "Framework/Math/ALMath.hpp"
#include "Scene/Component.h"

#include "generated/Physics2DComponents.gen.h"

namespace Ailu::ECS
{
    AENUM()
    enum class ECollisionResponse2D : u8
    {
        kIgnore,
        kOverlap,
        kBlock
    };

    AENUM()
    enum class ECollisionChannel2D : u8
    {
        kWorldStatic,
        kWorldDynamic,
        kPlayer,
        kEnemy,
        kProjectile,
        kTrigger,
        kPickup,
        kCount
    };

    AENUM()
    enum class ECollisionPreset2D : u8
    {
        kDefault,
        kPlayer,
        kEnemy,
        kWorldStatic,
        kWorldDynamic,
        kProjectile,
        kTrigger,
        kPickup,
        kCustom
    };

    ASTRUCT()
    struct AILU_API CollisionProfile2D
    {
        GENERATED_BODY()

        APROPERTY()
        ECollisionChannel2D _object_type = ECollisionChannel2D::kWorldDynamic;
        APROPERTY()
        Vector<ECollisionResponse2D> _responses;

        CollisionProfile2D()
            : _responses(static_cast<size_t>(ECollisionChannel2D::kCount), ECollisionResponse2D::kBlock)
        {
        }

        ECollisionResponse2D GetResponse(ECollisionChannel2D channel) const
        {
            const size_t index = static_cast<size_t>(channel);
            return index < _responses.size() ? _responses[index] : ECollisionResponse2D::kIgnore;
        }

        void SetResponse(ECollisionChannel2D channel, ECollisionResponse2D response)
        {
            const size_t index = static_cast<size_t>(channel);
            if (index < _responses.size())
                _responses[index] = response;
        }

        bool operator==(const CollisionProfile2D &) const = default;
    };

    inline ECollisionResponse2D ResolveCollisionResponse(ECollisionResponse2D lhs, ECollisionResponse2D rhs)
    {
        if (lhs == ECollisionResponse2D::kIgnore || rhs == ECollisionResponse2D::kIgnore)
            return ECollisionResponse2D::kIgnore;
        if (lhs == ECollisionResponse2D::kOverlap || rhs == ECollisionResponse2D::kOverlap)
            return ECollisionResponse2D::kOverlap;
        return ECollisionResponse2D::kBlock;
    }

    inline void SetCollisionPresetResponse(CollisionProfile2D &profile, ECollisionChannel2D channel,
                                            ECollisionResponse2D response)
    {
        profile.SetResponse(channel, response);
    }

    inline CollisionProfile2D MakeCollisionProfile2D(ECollisionPreset2D preset)
    {
        CollisionProfile2D profile;
        profile._object_type = ECollisionChannel2D::kWorldDynamic;
        profile._responses.assign(static_cast<size_t>(ECollisionChannel2D::kCount), ECollisionResponse2D::kIgnore);

        const auto block_world = [&profile] {
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kWorldStatic, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kWorldDynamic, ECollisionResponse2D::kBlock);
        };
        switch (preset)
        {
        case ECollisionPreset2D::kWorldStatic:
            profile._object_type = ECollisionChannel2D::kWorldStatic;
            block_world();
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kPlayer, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kEnemy, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kProjectile, ECollisionResponse2D::kBlock);
            break;
        case ECollisionPreset2D::kTrigger:
            profile._object_type = ECollisionChannel2D::kTrigger;
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kPlayer, ECollisionResponse2D::kOverlap);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kEnemy, ECollisionResponse2D::kOverlap);
            break;
        case ECollisionPreset2D::kEnemy:
            profile._object_type = ECollisionChannel2D::kEnemy;
            block_world();
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kPlayer, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kEnemy, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kProjectile, ECollisionResponse2D::kBlock);
            break;
        case ECollisionPreset2D::kProjectile:
            profile._object_type = ECollisionChannel2D::kProjectile;
            block_world();
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kPlayer, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kEnemy, ECollisionResponse2D::kBlock);
            break;
        case ECollisionPreset2D::kPickup:
            profile._object_type = ECollisionChannel2D::kPickup;
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kPlayer, ECollisionResponse2D::kOverlap);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kEnemy, ECollisionResponse2D::kOverlap);
            break;
        case ECollisionPreset2D::kPlayer:
            profile._object_type = ECollisionChannel2D::kPlayer;
            block_world();
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kPlayer, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kEnemy, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kProjectile, ECollisionResponse2D::kBlock);
            break;
        case ECollisionPreset2D::kWorldDynamic:
        case ECollisionPreset2D::kDefault:
            block_world();
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kPlayer, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kEnemy, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kProjectile, ECollisionResponse2D::kBlock);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kTrigger, ECollisionResponse2D::kOverlap);
            SetCollisionPresetResponse(profile, ECollisionChannel2D::kPickup, ECollisionResponse2D::kOverlap);
            break;
        case ECollisionPreset2D::kCustom:
            break;
        default:
            break;
        }
        return profile;
    }

    inline void ApplyCollisionPreset2D(CollisionProfile2D &profile, ECollisionPreset2D preset)
    {
        profile = MakeCollisionProfile2D(preset);
    }

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
        ECollisionPreset2D _preset = ECollisionPreset2D::kDefault;
        APROPERTY()
        CollisionProfile2D _collision_profile = MakeCollisionProfile2D(ECollisionPreset2D::kDefault);
        APROPERTY()
        Vector<ColliderShape2D> _shapes;
    };
}

#endif
