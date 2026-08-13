#pragma once

#include <optional>

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Script/ScriptContact2D.h"
#include "Framework/Script/ScriptEntity.h"

#include "generated/ScriptPhysics2D.gen.h"

namespace Ailu
{
    ASTRUCT(Script)
    struct AILU_API ScriptRaycastHit2D
    {
        GENERATED_BODY()
        ScriptEntity _entity;
        Vector2f _point = Vector2f::kZero;
        Vector2f _normal = Vector2f::kZero;
        f32 _distance = 0.0f;

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
        AFUNCTION(ScriptProperty)
        ScriptEntity GetEntity() const;
        AFUNCTION(ScriptProperty)
        Vector2f GetPoint() const;
        AFUNCTION(ScriptProperty)
        Vector2f GetNormal() const;
        AFUNCTION(ScriptProperty)
        f32 GetDistance() const;
    };

    ASTRUCT(Script; Global="physics2d")
    struct AILU_API ScriptPhysics2D
    {
        GENERATED_BODY()
        AFUNCTION(Script)
        static std::optional<ScriptRaycastHit2D> Raycast(const Vector2f &origin, const Vector2f &direction, f32 distance, u32 layer_mask = 0xffffffffu);
        AFUNCTION(Script)
        static Vector<ScriptRaycastHit2D> OverlapCircle(const Vector2f &center, f32 radius, u32 layer_mask = 0xffffffffu);
        AFUNCTION(Script)
        static Vector<ScriptRaycastHit2D> OverlapBox(const Vector2f &center, const Vector2f &size, f32 rotation = 0.0f, u32 layer_mask = 0xffffffffu);
    };
}
