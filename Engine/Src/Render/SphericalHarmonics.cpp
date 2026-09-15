#include "pch.h"
#include "Render/SphericalHarmonics.h"

namespace Ailu::Render
{
    SphericalHarmonicsL2 operator+(const SphericalHarmonicsL2 &lhs, const SphericalHarmonicsL2 &rhs)
    {
        SphericalHarmonicsL2 result;
        for (u32 index = 0u; index < SphericalHarmonicsL2::kCoefficientCount; ++index)
            result._coefficients[index] = lhs._coefficients[index] + rhs._coefficients[index];
        return result;
    }

    SphericalHarmonicsL2 operator*(const SphericalHarmonicsL2 &sh, f32 weight)
    {
        SphericalHarmonicsL2 result;
        for (u32 index = 0u; index < SphericalHarmonicsL2::kCoefficientCount; ++index)
            result._coefficients[index] = sh._coefficients[index] * weight;
        return result;
    }

    void Accumulate(SphericalHarmonicsL2 &dst, const SphericalHarmonicsL2 &src, f32 weight)
    {
        for (u32 index = 0u; index < SphericalHarmonicsL2::kCoefficientCount; ++index)
            dst._coefficients[index] += src._coefficients[index] * weight;
    }
}
