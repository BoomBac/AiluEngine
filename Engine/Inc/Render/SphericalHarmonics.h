#pragma once
#ifndef __SPHERICAL_HARMONICS_H__
#define __SPHERICAL_HARMONICS_H__

#include "Framework/Math/ALMath.hpp"

namespace Ailu::Render
{
    struct SphericalHarmonicsL2
    {
        static constexpr u32 kCoefficientCount = 9u;
        Vector4f _coefficients[kCoefficientCount] = {};

        void Clear() { *this = {}; }
    };

    SphericalHarmonicsL2 operator+(const SphericalHarmonicsL2 &lhs, const SphericalHarmonicsL2 &rhs);
    SphericalHarmonicsL2 operator*(const SphericalHarmonicsL2 &sh, f32 weight);
    void Accumulate(SphericalHarmonicsL2 &dst, const SphericalHarmonicsL2 &src, f32 weight);
}

#endif // __SPHERICAL_HARMONICS_H__
