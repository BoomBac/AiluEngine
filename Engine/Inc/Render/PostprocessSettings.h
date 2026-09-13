#pragma once
#ifndef __POSTPROCESS_SETTINGS_H__
#define __POSTPROCESS_SETTINGS_H__

#include "Framework/Core/CoreMinimal.h"
#include "Objects/Type.h"
#include "generated/PostprocessSettings.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }
}

namespace Ailu
{
namespace Render
{
    AENUM()
    enum class EToneMappingMode
    {
        kNone,
        kAces
    };

    AENUM()
    enum class EDisplayOutput
    {
        kSdrSrgb,
        kHdrScRgb,
        kHdr10Pq
    };

    ASTRUCT()
    struct AILU_API BloomSettings
    {
        GENERATED_BODY()

        APROPERTY()
        bool _enabled = true;

        APROPERTY()
        f32 _intensity = 0.35f;

        APROPERTY()
        f32 _radius = 0.005f;
    };

    ASTRUCT()
    struct AILU_API ToneMappingSettings
    {
        GENERATED_BODY()

        APROPERTY()
        EToneMappingMode _mode = EToneMappingMode::kAces;

        APROPERTY()
        f32 _exposure = 1.0f;
    };

    ASTRUCT()
    struct AILU_API PostprocessSettings
    {
        GENERATED_BODY()

        APROPERTY()
        BloomSettings _bloom;

        APROPERTY()
        ToneMappingSettings _tone_mapping;
    };

    AILU_API PostprocessSettings ResolvePostprocessSettings(const SceneManagement::Scene &scene);
}
}

#endif // __POSTPROCESS_SETTINGS_H__
