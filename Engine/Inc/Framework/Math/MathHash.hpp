#pragma once
#include "Framework/Math/Vector.hpp"
#include <functional>

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        namespace ALHash
        {
            static inline std::size_t HashCombineStable(std::size_t seed, std::size_t value)
            {
                return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
            }

            struct Vector2fHash
            {
                inline std::size_t operator()(const Vector2f &v) const
                {
                    std::size_t seed = std::hash<float>{}(v.x);
                    return HashCombineStable(seed, std::hash<float>{}(v.y));
                }
            };

            struct Vector2Equal
            {
                inline bool operator()(const Vector2f &v1, const Vector2f &v2) const
                {
                    return v1.x == v2.x && v1.y == v2.y;
                }
            };

            struct Vector3fHash
            {
                inline std::size_t operator()(const Vector3f &v) const
                {
                    std::size_t seed = std::hash<float>{}(v.x);
                    seed = HashCombineStable(seed, std::hash<float>{}(v.y));
                    return HashCombineStable(seed, std::hash<float>{}(v.z));
                }
            };

            struct Vector3Equal
            {
                inline bool operator()(const Vector3f &v1, const Vector3f &v2) const
                {
                    return v1.x == v2.x && v1.y == v2.y && v1.z == v2.z;
                }
            };

            struct Vector4fHash
            {
                inline std::size_t operator()(const Vector4f &v) const
                {
                    std::size_t seed = std::hash<float>{}(v.x);
                    seed = HashCombineStable(seed, std::hash<float>{}(v.y));
                    seed = HashCombineStable(seed, std::hash<float>{}(v.z));
                    return HashCombineStable(seed, std::hash<float>{}(v.w));
                }
            };

            struct Vector4Equal
            {
                inline bool operator()(const Vector4f &v1, const Vector4f &v2) const
                {
                    return v1.x == v2.x && v1.y == v2.y && v1.z == v2.z && v1.w == v2.w;
                }
            };

            template<template<typename> typename TT, typename T>
            struct VectorHash
            {
                inline std::size_t operator()(const TT<T> &v) const
                {
                    std::size_t out_hash = std::hash<T>{}(v[0]);
                    for (u32 i = 1; i < CountOf(v.data); i++)
                    {
                        auto cur_hash = std::hash<T>{}(v[i]);
                        out_hash ^= cur_hash << i;
                    }
                    return out_hash;
                }
            };

            template<template<typename> typename TT, typename T>
            struct VectorEqual
            {
                inline bool operator()(const TT<T> &v1, const TT<T> &v2) const
                {
                    bool is_equal = true;
                    for (u32 i = 1; i < CountOf(v1.data); i++)
                    {
                        is_equal &= v1[i] == v2[i];
                    }
                    return is_equal;
                }
            };

            static inline std::size_t CombineHashes(const std::size_t &hash1, const std::size_t &hash2)
            {
                return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
            }

        }// namespace ALHash
    }
#pragma warning(pop)
}
