#pragma once

#include "Framework/Core/CoreMinimal.h"

#include "generated/ScriptTime.gen.h"

namespace Ailu
{
    ASTRUCT(Script; Global="time")
    struct AILU_API ScriptTime
    {
        GENERATED_BODY()
        AFUNCTION(ScriptProperty)
        f32 GetDeltaTime() const;
        AFUNCTION(ScriptProperty)
        f32 GetFixedDeltaTime() const;
        AFUNCTION(ScriptProperty)
        f32 GetRenderAlpha() const;
        AFUNCTION(ScriptProperty)
        f32 GetTime() const;
    };
}
