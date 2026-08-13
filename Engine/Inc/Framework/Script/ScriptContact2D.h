#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/ALMath.hpp"

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

        AFUNCTION(ScriptProperty)
        Vector2f GetPoint() const;
        AFUNCTION(ScriptProperty)
        Vector2f GetNormal() const;
        AFUNCTION(ScriptProperty)
        u32 GetSelfShape() const;
        AFUNCTION(ScriptProperty)
        u32 GetOtherShape() const;
    };
}
