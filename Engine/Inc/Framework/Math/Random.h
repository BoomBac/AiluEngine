#pragma once
#ifndef __RANDOM__
#define __RANDOM__
#include "ALMath.hpp"
#include <random>
#include <limits>

namespace Ailu
{
    #undef max;
    namespace Random
    {
        static Color RandomColor(u32 seed)
        {
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            float random_r = dist(rng);
            float random_g = dist(rng);
            float random_b = dist(rng);
            return Color(random_r, random_g, random_b, 1.0f);
        }
        static Color32 RandomColor32(u32 seed)
        {
            std::mt19937 rng(seed);
            std::uniform_int_distribution<i32> dist(0, 255);
            u8 random_r = dist(rng);
            u8 random_g = dist(rng);
            u8 random_b = dist(rng);
            return Color32(random_r, random_g, random_b, 255);
        }

        static u64 RandomInt()
        {
            const static uint64_t u64_max = std::numeric_limits<uint64_t>::max();
            std::uniform_int_distribution<uint64_t> dist(1u, u64_max);
            std::random_device rd;
            std::mt19937 rng(rd());
            return dist(rng);
        }
    }// namespace Random
}// namespace Ailu

#endif// !RANDOM__
