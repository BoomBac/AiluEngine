#pragma once
#ifndef __RANDOM__
#define __RANDOM__
#include "ALMath.hpp"
#include <limits>
#include <random>
#include <functional>

namespace
{
    inline float Fade(float t)
    {
        return t * t * t * (t * (t * 6 - 15) + 10);
    }
    template<template<typename> class TT>
    TT<float> Modulo(TT<f32> divident, TT<f32> divisor)
    {
        TT<f32> positiveDivident = divident % divisor + divisor;
        return positiveDivident % divisor;
    }
}
namespace Ailu
{
    #undef max
    #undef min
    namespace Random
    {
        Color AILU_API RandomColor(u32 seed, bool ignore_alpha = true);
        Color32 AILU_API RandomColor32(u32 seed, bool ignore_alpha = true);

        template<typename T>
        static T RandomValue(u32 seed = 2025, T min = std::numeric_limits<T>::min(), T max = std::numeric_limits<T>::max())
        {
            std::mt19937 mt(seed);
            std::uniform_real_distribution<T> dist(min, max);
            return dist(mt);
        }

        class AILU_API NoiseGenerator
        {
        public:
            NoiseGenerator(u32 seed = 2025);
            ~NoiseGenerator();
            f32 Perlin2D(f32 u, f32 v,f32 cell_size = 0.2f);
            f32 Perlin3D(f32 u, f32 v, f32 w,f32 cell_size = 0.2f);
            f32 Voronoi2D(f32 u, f32 v,f32 cell_size = 0.2f);
            f32 Voronoi3D(f32 u, f32 v, f32 w,f32 cell_size = 0.2f);
            /// @brief 2D fBM
            /// @param u 浮点u
            /// @param v 浮点v
            /// @param octaves 迭代次数
            /// @param lacunarity 频率增长因子
            /// @param gain 振幅衰减因子
            /// @param period 周期
            /// @param noise_func 2d噪声函数
            /// @return fBM噪声
            f32 FBM(f32 u, f32 v, u16 octaves, f32 lacunarity, f32 gain, f32 period, std::function<f32(NoiseGenerator*,f32, f32, f32)> noise_func);
            /// @brief 3D fBM
            /// @param u 浮点u
            /// @param v 浮点v
            /// @param w 浮点w
            /// @param octaves 迭代次数
            /// @param lacunarity 频率增长因子
            /// @param gain 振幅衰减因子
            /// @param period 周期
            /// @param noise_func 3d噪声函数
            /// @return fBM噪声
            f32 FBM(f32 u, f32 v, f32 w, u16 octaves, f32 lacunarity, f32 gain, f32 period, std::function<f32(NoiseGenerator*,f32, f32, f32, f32)> noise_func);
        private:
            inline static constexpr i32 kMaxNoiseCellCount = 128;
            Vector2f* _perlin_grads;
            Vector3f* _perlin_grad3ds;
            Vector3f* _voronoi_offsets;
        };
    }// namespace Random
}// namespace Ailu

#endif// !RANDOM__
