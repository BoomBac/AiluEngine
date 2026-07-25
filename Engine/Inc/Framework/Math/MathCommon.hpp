#pragma once
#pragma warning(disable:4251)

#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Types.h"
#include "Framework/Math/MathConstants.h"
#include "Framework/Platform/Api.h"
#include "Framework/Platform/Compiler.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace Ailu
{
    namespace
    {
        template<typename T, size_t size_of_arr>
        constexpr u32 CountOf(T (&)[size_of_arr])
        {
            return (u32) size_of_arr;
        }

        template<typename T, size_t row, size_t col>
        constexpr u32 CountOf(T (&)[row][col])
        {
            return (u32) (row * col);
        }
    }

#pragma warning(push)
#pragma warning(disable : 4244)

    namespace Math
    {
        static u64 AlignTo(u64 value, u64 alignment = 256u)
        {
            if (alignment == 0) return value;
            if ((alignment & (alignment - 1)) == 0)
            {
                return (value + alignment - 1) & ~(alignment - 1);
            }
            else
            {
                u64 remainder = value % alignment;
                return remainder == 0 ? value : value + alignment - remainder;
            }
        }

        static u32 NextPowOfTwo(u32 value)
        {
#if __cplusplus >= 202002L
            return std::bit_ceil(value);
#else
            if (value <= 0) return 1u;
            --value;
            value |= value >> 1u;
            value |= value >> 2u;
            value |= value >> 4u;
            value |= value >> 8u;
            value |= value >> 16u;
            return value + 1u;
#endif
        }

        static float LoadFloat(const char *f_str)
        {
            float tmp = 0.0f;
            return sscanf_s(f_str, "%f", &tmp) == 1 ? tmp : 0.0f;
        }

        inline static float ToRadius(const float &angle) { return angle * kDegToRad; }
        inline static float ToAngle(const float &radius) { return radius * kRadToDeg; }

        inline static void Clamp(float &value, const float &min, const float &max)
        {
            value = (value > max) ? max : (value < min) ? min
                                                        : value;
        }

        inline static float NormalizeAngle(float angle)
        {
            while (angle > 180.0f)
                angle -= 360.0f;
            while (angle < -180.0f)
                angle += 360.0f;
            return angle;
        }
    }
#pragma warning(pop)
}
