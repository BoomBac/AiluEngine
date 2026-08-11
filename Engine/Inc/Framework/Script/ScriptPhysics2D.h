#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Script/ScriptEntity.h"

#include "generated/ScriptPhysics2D.gen.h"

namespace Ailu
{
    ASTRUCT()
    struct AILU_API ScriptPhysics2D
    {
        GENERATED_BODY()
        AFUNCTION(Script)
        static bool IsValidBody(const ScriptEntity &entity);
        AFUNCTION(Script)
        static void SetPosition(const ScriptEntity &entity, const Vector2f &position);
        AFUNCTION(Script)
        static Vector2f GetPosition(const ScriptEntity &entity);
        AFUNCTION(Script)
        static void SetLinearVelocity(const ScriptEntity &entity, const Vector2f &velocity);
        AFUNCTION(Script)
        static Vector2f GetLinearVelocity(const ScriptEntity &entity);
        AFUNCTION(Script)
        static void SetAngularVelocity(const ScriptEntity &entity, f32 velocity);
        AFUNCTION(Script)
        static void AddForce(const ScriptEntity &entity, const Vector2f &force);
        AFUNCTION(Script)
        static void AddImpulse(const ScriptEntity &entity, const Vector2f &impulse);
    };
}
