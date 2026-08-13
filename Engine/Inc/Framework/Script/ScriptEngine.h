#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"

#include "generated/ScriptEngine.gen.h"

namespace Ailu
{
    ASTRUCT(Script; Global="engine")
    struct AILU_API ScriptEngine
    {
        GENERATED_BODY()

        AFUNCTION(Script)
        static void LogInfo(const String &message);
        AFUNCTION(Script)
        static void LogWarning(const String &message);
        AFUNCTION(Script)
        static void LogError(const String &message);
    };
}
