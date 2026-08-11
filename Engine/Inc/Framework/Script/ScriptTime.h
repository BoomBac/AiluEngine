#pragma once

#include "Framework/Core/CoreMinimal.h"

#include "generated/ScriptTime.gen.h"

namespace Ailu
{
    ASTRUCT()
    struct AILU_API ScriptTime
    {
        GENERATED_BODY()
        AFUNCTION(Script)
        f32 GetDeltaTime() const;
        AFUNCTION(Script)
        f32 GetFixedDeltaTime() const;
        AFUNCTION(Script)
        f32 GetRenderAlpha() const;
        AFUNCTION(Script)
        f32 GetTime() const;
    };
}
