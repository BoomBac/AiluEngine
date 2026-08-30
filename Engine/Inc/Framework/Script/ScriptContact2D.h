#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/ALMath.hpp"
#include "Physics/2D/Physics2DComponents.h"

#include "generated/ScriptContact2D.gen.h"

namespace Ailu
{
    ASTRUCT(Script)
    struct AILU_API ScriptContact2D
    {
        GENERATED_BODY()
        ScriptContact2D() = default;
        ScriptContact2D(const Vector2f &point, const Vector2f &normal, u16 self_shape, u16 other_shape,
                        ECS::ECollisionChannel2D other_object_type)
            : _point(point), _normal(normal), _self_shape(self_shape), _other_shape(other_shape),
              _other_object_type(other_object_type)
        {
        }

        Vector2f _point = Vector2f::kZero;
        Vector2f _normal = Vector2f::kZero;
        u32 _self_shape = 0u;
        u32 _other_shape = 0u;
        ECS::ECollisionChannel2D _other_object_type = ECS::ECollisionChannel2D::kWorldDynamic;

        AFUNCTION(ScriptProperty)
        Vector2f GetPoint() const;
        AFUNCTION(ScriptProperty)
        Vector2f GetNormal() const;
        AFUNCTION(ScriptProperty)
        u32 GetSelfShape() const;
        AFUNCTION(ScriptProperty)
        u32 GetOtherShape() const;
        AFUNCTION(ScriptProperty)
        u32 GetOtherObjectType() const;
    };
}
