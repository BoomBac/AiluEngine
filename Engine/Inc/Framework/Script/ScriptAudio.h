#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/Vector.hpp"
#include "Framework/Script/ScriptAssetValue.h"

#include "generated/ScriptAudio.gen.h"

namespace Ailu
{
    using Math::Vector3f;

    ASTRUCT(Script; Global="audio")
    struct AILU_API ScriptAudio
    {
        GENERATED_BODY()

        AFUNCTION(Script)
        static bool PlayOneShot(const ScriptAssetValue &clip, const Vector3f &position);
        AFUNCTION(Script)
        static void StopMusic();
    };
}
