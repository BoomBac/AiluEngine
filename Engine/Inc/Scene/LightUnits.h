#pragma once

#include <algorithm>
#include <cmath>

#include "Framework/Math/MathConstants.h"
#include "Scene/Component.h"

namespace Ailu::ECS
{
    constexpr f32 kLuminousEfficacyLmPerWatt = 683.0f;
    constexpr f32 kFourPi = 12.566370614359172f;

    inline Vector3f GetBlackbodyColor(f32 temperature_kelvin)
    {
        if (!std::isfinite(temperature_kelvin))
            temperature_kelvin = 6500.0f;
        const f32 temperature = std::clamp(temperature_kelvin, 1000.0f, 40000.0f) / 100.0f;
        f32 red_srgb = 0.0f;
        f32 green_srgb = 0.0f;
        f32 blue_srgb = 0.0f;

        if (temperature <= 66.0f)
        {
            red_srgb = 255.0f;
            green_srgb = 99.4708025861f * std::log(temperature) - 161.1195681661f;
            blue_srgb = temperature <= 19.0f ? 0.0f
                : 138.5177312231f * std::log(temperature - 10.0f) - 305.0447927307f;
        }
        else
        {
            red_srgb = 329.698727446f * std::pow(temperature - 60.0f, -0.1332047592f);
            green_srgb = 288.1221695283f * std::pow(temperature - 60.0f, -0.0755148492f);
            blue_srgb = 255.0f;
        }

        const auto to_linear = [](f32 channel)
        {
            const f32 srgb = std::clamp(channel / 255.0f, 0.0f, 1.0f);
            return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
        };
        Vector3f color(to_linear(red_srgb), to_linear(green_srgb), to_linear(blue_srgb));
        const f32 luminance = color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
        return color / std::max(luminance, 1e-4f);
    }

    inline Vector3f GetLightColor(const LightComponent &component)
    {
        const LightData &light = component._light;
        Vector3f color = light._light_color.xyz;
        if (light._use_color_temperature)
            color *= GetBlackbodyColor(light._color_temperature_kelvin);
        return color;
    }

    inline f32 GetLightRadiometricScale(const LightComponent &component)
    {
        const LightData &light = component._light;
        const f32 intensity = std::max(0.0f, light._intensity);
        f32 area = light._area_shape == EAreaLightShape::kDisc
            ? kPi * light._area_disc_radius * light._area_disc_radius
            : light._area_width * light._area_height;
        if (component._type == ELightType::kArea && light._area_shape == EAreaLightShape::kRectangle)
        {
            const f32 world_area = Magnitude(CrossProduct(light._area_points[1] - light._area_points[0],
                                                          light._area_points[3] - light._area_points[0]));
            if (world_area > 1e-8f)
                area = world_area;
        }
        const f32 spot_inner_cos = cos(light._spot_inner_angle * 0.5f * k2Radius);
        const f32 spot_outer_cos = cos(light._spot_outer_angle * 0.5f * k2Radius);
        const f32 spot_transition = std::max(spot_inner_cos - spot_outer_cos, 0.0f);
        const f32 spot_solid_angle = kTwoPi * (1.0f - spot_inner_cos + spot_transition / 3.0f);
        const f32 side_count = light._is_two_side ? 2.0f : 1.0f;

        if (light._intensity_unit == ELightIntensityUnit::kUnitlessLegacy)
        {
            if (component._type == ELightType::kDirectional || component._type == ELightType::kArea)
                return intensity * kPi;
            return intensity * kPi * light._range * light._range * 0.5f;
        }

        switch (component._type)
        {
        case ELightType::kDirectional:
            return light._intensity_unit == ELightIntensityUnit::kLux
                ? intensity / kLuminousEfficacyLmPerWatt : intensity;
        case ELightType::kPoint:
            if (light._intensity_unit == ELightIntensityUnit::kCandela)
                return intensity / kLuminousEfficacyLmPerWatt;
            if (light._intensity_unit == ELightIntensityUnit::kLumen)
                return intensity / (kFourPi * kLuminousEfficacyLmPerWatt);
            return intensity / kFourPi;
        case ELightType::kSpot:
            if (light._intensity_unit == ELightIntensityUnit::kCandela)
                return intensity / kLuminousEfficacyLmPerWatt;
            if (light._intensity_unit == ELightIntensityUnit::kLumen)
                return intensity / (std::max(spot_solid_angle, 1e-4f) * kLuminousEfficacyLmPerWatt);
            return intensity / std::max(spot_solid_angle, 1e-4f);
        case ELightType::kArea:
        default:
            if (light._intensity_unit == ELightIntensityUnit::kCandela)
                return intensity / (kLuminousEfficacyLmPerWatt * std::max(area, 1e-4f));
            if (light._intensity_unit == ELightIntensityUnit::kLumen)
                return intensity / (side_count * kPi * std::max(area, 1e-4f) * kLuminousEfficacyLmPerWatt);
            return intensity / (side_count * kPi * std::max(area, 1e-4f));
        }
    }
}
