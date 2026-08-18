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
