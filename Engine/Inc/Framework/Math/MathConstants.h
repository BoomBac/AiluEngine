#pragma once


#include "Framework/Core/Types.h"
#include <limits>

namespace Ailu::Math
{
    inline constexpr f32 kEpsilon = 1.19209e-07f;
    inline constexpr f32 kPi = 3.14159265358979323846f;
    inline constexpr f32 kHalfPi = kPi * 0.5f;
    inline constexpr f32 kDPi = kPi * 2.0f;
    inline constexpr f32 kTwoPi = kDPi;
    inline constexpr f32 kFloatEpsilon = 1e-6f;
    inline constexpr f32 kRadToDeg = 180.0f / kPi;
    inline constexpr f32 kDegToRad = kPi / 180.0f;

    inline constexpr f32 k2Angle = kRadToDeg;
    inline constexpr f32 k2Radius = kDegToRad;
}
