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
        kNone = 0,
        kAces = 1,
        kAgx = 2,
        kStandard = 3
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

        APROPERTY()
        f32 _gamma = 1.0f;
    };

    AENUM()
    enum class EExposureMeteringMode : u8
    {
        kAverage,
        kCenterWeighted
    };

    ASTRUCT()
    struct AILU_API AutoExposureSettings
    {
        GENERATED_BODY()

        // LightUnits converts photometric light inputs to an RGB radiometric approximation using 683 lm/W.
        // This is the inverse mapping for controlled white-light scenes; artists can calibrate other scene content.
        APROPERTY()
        f32 _scene_luminance_to_cd_m2 = 683.0f;

        APROPERTY()
        f32 _low_percentile = 5.0f;

        APROPERTY()
        f32 _high_percentile = 95.0f;

        APROPERTY()
        f32 _adaptation_speed = 3.0f;

        APROPERTY()
        f32 _adaptation_speed_brighten = -1.0f;

        APROPERTY()
        f32 _adaptation_speed_darken = -1.0f;

        APROPERTY()
        f32 _minimum_ev = -12.0f;

        APROPERTY()
        f32 _maximum_ev = 16.0f;

        APROPERTY()
        EExposureMeteringMode _metering_mode = EExposureMeteringMode::kAverage;
    };

    ASTRUCT()
    struct AILU_API DepthOfFieldSettings
    {
        GENERATED_BODY()

        APROPERTY()
        bool _enabled = false;

        APROPERTY()
        f32 _maximum_coc_diameter_px = 12.0f;

        APROPERTY()
        i32 _aperture_blade_count = 6;
    };

    AILU_API f32 ResolveSceneLuminanceToCdM2(const AutoExposureSettings &settings);

    ASTRUCT()
    struct AILU_API PostprocessSettings
    {
        GENERATED_BODY()

        APROPERTY()
        BloomSettings _bloom;

        APROPERTY()
        ToneMappingSettings _tone_mapping;

        APROPERTY()
        AutoExposureSettings _auto_exposure;

        APROPERTY()
        DepthOfFieldSettings _depth_of_field;
    };

    AILU_API PostprocessSettings ResolvePostprocessSettings(const SceneManagement::Scene &scene);
}
}

#endif // __POSTPROCESS_SETTINGS_H__
