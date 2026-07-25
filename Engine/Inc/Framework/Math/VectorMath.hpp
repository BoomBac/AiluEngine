#pragma once
#include "Framework/Math/Vector.hpp"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        template<typename T>
        T Normalize(const T &var)
        {
            size_t len = CountOf(var.data);
            double sum = 0.f;
            T temp{};
            for (u32 i = 0; i < len; i++)
            {
                sum += pow(var[i], 2.f);
                temp[i] = var[i];
            }
            sum = sum == 0 ? 1.0 : sum;
            sum = sqrt(sum);
            for (u32 i = 0; i < len; i++)
            {
                temp[i] /= sum;
            }
            return temp;
        }

        template<template<typename> class TT, typename T>
        static float Distance(const TT<T> &from, const TT<T> &to)
        {
            float dis = 0;
            for (u32 i = 0; i < CountOf(from.data); i++)
            {
                dis += (to.data[i] - from.data[i]) * (to.data[i] - from.data[i]);
            }
            return sqrt(dis);
        }

        // Calculate magnitude (length) of a vector
        template<template<typename> class TT, typename T>
        static float Magnitude(const TT<T> &v)
        {
            return sqrt(SqrMagnitude(v));
        }
        template<template<typename> class TT, typename T>
        static float SqrMagnitude(const TT<T> &v)
        {
            float dis = 0;
            for (u32 i = 0; i < CountOf(v.data); i++)
            {
                dis += v.data[i] * v.data[i];
            }
            return dis;
        }
        template<template<typename> class TT, typename T>
        static TT<T> Fract(const TT<T> &v)
        {
            TT<T> ret = v;
            for (u32 i = 0; i < CountOf(v.data); i++)
                ret.data[i] = v.data[i] - std::floor(v.data[i]);
            return ret;
        }
        template<double>
        static double Fract(double x)
        {
            return x - std::floor(x);
        }
        template<float>
        static float Fract(float x)
        {
            return x - std::floorf(x);
        }
        template<template<typename> class TT, typename T>
        static TT<T> Floor(const TT<T> &v)
        {
            TT<T> ret = v;
            for (u32 i = 0; i < CountOf(v.data); i++)
                ret.data[i] = std::floor(v.data[i]);
            return ret;
        }
        template<double>
        static double Floor(double x)
        {
            return std::floor(x);
        }
        template<float>
        static float Floor(float x)
        {
            return std::floorf(x);
        }
        template<template<typename> class TT, typename T>
        static TT<T> Ceil(const TT<T> &v)
        {
            TT<T> ret = v;
            for (u32 i = 0; i < CountOf(v.data); i++)
                ret.data[i] = std::ceil(v.data[i]);
            return ret;
        }
        template<double>
        static double Ceil(double x)
        {
            return std::ceil(x);
        }
        template<float>
        static float Ceil(float x)
        {
            return std::ceilf(x);
        }
        template<template<typename> class TT, typename T>
        static TT<T> Round(const TT<T> &v)
        {
            TT<T> ret = v;
            for (u32 i = 0; i < CountOf(v.data); i++)
                ret.data[i] = std::round(v.data[i]);
            return ret;
        }
        template<double>
        static double Round(double x)
        {
            return std::round(x);
        }
        template<float>
        static float Round(float x)
        {
            return std::roundf(x);
        }
        template<template<typename> class TT, typename T>
        TT<T> operator%(TT<T> lhs, TT<T> rhs)
        {
            u32 ele_num = CountOf(lhs.data);
            TT<T> ret;
            if constexpr (std::is_same<T, float>::value)
            {
                for (u32 i = 0; i < ele_num; i++)
                    ret.data[i] = std::fmodf(lhs.data[i], rhs.data[i]);
            }
            else if constexpr (std::is_same<T, double>::value)
            {
                for (u32 i = 0; i < ele_num; i++)
                    ret.data[i] = std::fmod(lhs.data[i], rhs.data[i]);
            }
            else
            {
                for (u32 i = 0; i < ele_num; i++)
                    ret.data[i] = lhs.data[i] % rhs.data[i];
            }
            return ret;
        }
        template<template<typename> class TT, typename T>
        TT<T> Smoothstep(const TT<T> &edge0, const TT<T> &edge1, const TT<T> &x)
        {
            TT<T> t = Clamp((x - edge0) / (edge1 - edge0), TT<T>::kZero, TT<T>::kOne);
            return t * t * (TT<T>::kOne - t);
        }

        template<template<typename> class TT, typename T>
        static bool LoadVector(const char *vec_str, const TT<T> &out_v)
        {
#pragma warning(push)
#pragma warning(disable : 4477)
            int length = CountOf(out_v.data);
            if (std::is_same<T, float>::value)
            {
                if (length == 2) return sscanf_s(vec_str, "%f,%f", &out_v.data[0], &out_v.data[1]) == 2;
                else if (length == 3)
                    return sscanf_s(vec_str, "%f,%f,%f", &out_v.data[0], &out_v.data[1], &out_v.data[2]) == 3;
                else if (length == 4)
                    return sscanf_s(vec_str, "%f,%f,%f,%f", &out_v.data[0], &out_v.data[1], &out_v.data[2], &out_v.data[3]) == 4;
            }
            return false;
#pragma warning(pop)
        }

        template<template<typename> class TT, typename T>
        static TT<T> LoadVector(const char *vec_str)
        {
#pragma warning(push)
#pragma warning(disable : 4477)
            const TT<T> out_v{};
            int length = CountOf(out_v.data);
            if (std::is_same<T, float>::value)
            {
                if (length == 3) return sscanf_s(vec_str, "%f,%f,%f", &out_v.x, &out_v.y, &out_v.z) == 3;
                else if (length == 4)
                    return sscanf_s(vec_str, "%f,%f,%f,%f", &out_v.x, &out_v.y, &out_v.z, &out_v.data[3]) == 4;
            }
            return out_v;
#pragma warning(pop)
        }

        template<template<typename> typename TT, typename T>
        TT<T> Clamp(const TT<T> &value, const T &min, const T &max)
        {
            TT<T> ret = value;
            for (u32 i = 0; i < CountOf(value.data); i++)
            {
                ret.data[i] = (value.data[i] > max) ? max : (value.data[i] < min) ? min
                                                                                  : value.data[i];
            }
            return ret;
        }

        template<template<typename> typename TT, typename T>
        TT<T> operator+(const TT<T> v1, const TT<T> v2)
        {
            TT<T> res;
            for (u32 i = 0; i < CountOf(v1.data); i++)
            {
                res.data[i] = v1.data[i] + v2.data[i];
            }
            return res;
        }
        template<template<typename> typename TT, typename T>
        TT<T> operator+(T s, const TT<T> v1)
        {
            TT<T> res;
            for (u32 i = 0; i < CountOf(v1.data); i++)
            {
                res.data[i] = v1.data[i] + s;
            }
            return res;
        }
        template<template<typename> typename TT, typename T>
        TT<T> operator+(const TT<T> v1, T s)
        {
            TT<T> res;
            for (u32 i = 0; i < CountOf(v1.data); i++)
            {
                res.data[i] = v1.data[i] + s;
            }
            return res;
        }
        template<template<typename> typename TT, typename T>
        TT<T> &operator+=(TT<T> &v1, T s)
        {
            for (u32 i = 0; i < CountOf(v1.data); i++)
            {
                v1.data[i] += s;
            }
            return v1;
        }
        template<template<typename> typename TT, typename T>
        TT<T> &operator-=(TT<T> &v1, T s)
        {
            for (u32 i = 0; i < CountOf(v1.data); i++)
            {
                v1.data[i] -= s;
            }
            return v1;
        }
        template<template<typename> typename TT, typename T>
        TT<T> &operator/=(TT<T> &v1, T s)
        {
            for (u32 i = 0; i < CountOf(v1.data); i++)
            {
                v1.data[i] /= s;
            }
            return v1;
        }
        template<template<typename> typename TT, typename T>
        TT<T> &operator*=(TT<T> &v1, T s)
        {
            for (u32 i = 0; i < CountOf(v1.data); i++)
            {
                v1.data[i] *= s;
            }
            return v1;
        }

        template<template<typename> typename TT, typename T>
        TT<T> operator*(const T &scalar, const TT<T> &v)
        {
            TT<T> res;
            for (u32 i = 0; i < CountOf(v.data); i++)
            {
                res.data[i] = scalar * v.data[i];
            }
            return res;
        }

        template<template<typename> typename TT, typename T>
        TT<T> operator*(const TT<T> &a, const TT<T> &b)
        {
            TT<T> res;
            for (u32 i = 0; i < CountOf(a.data); i++)
            {
                res.data[i] = a.data[i] * b.data[i];
            }
            return res;
        }

        template<template<typename> typename TT, typename T>
        TT<T> operator*(const TT<T> &v, const T &scalar)
        {
            return scalar * v;
        }

        template<template<typename> typename TT, typename T>
        TT<T> operator/(const TT<T> &v, const T &scalar)
        {
            TT<T> res = v;
            for (uint32_t i = 0; i < CountOf(v.data); i++)
            {
                res.data[i] /= scalar;
            }
            return res;
        }

        template<template<typename> typename TT, typename T>
        TT<T> operator+(const T &scalar, const TT<T> &v)
        {
            TT<T> res{};
            for (u32 i = 0; i < CountOf(v.data); i++)
            {
                res.data[i] = v.data[i] + scalar;
            }
            return res;
        }
        /// <summary>
        /// Any number of vector additions, with at least two parameters passed in
        /// </summary>
        template<template<typename> typename FF, typename F, template<typename> typename... TT, typename T>
        FF<F> VectorAdd(const FF<F> first, const TT<T> &...arg)
        {
            return (first + ... + arg);
        }
        template<template<typename> typename TT, typename T>
        TT<T> operator-(TT<T> v)
        {
            for (u32 i = 0; i < CountOf(v.data); i++)
            {
                v[i] = -v[i];
            }
            return v;
        }
        template<template<typename> typename TT, typename T>
        TT<T> operator-(const TT<T> v1, const TT<T> v2)
        {
            TT<T> res;
            for (u32 i = 0; i < CountOf(v1.data); i++)
            {
                res.data[i] = v1.data[i] - v2.data[i];
            }
            return res;
        }
        template<template<typename> typename FF, typename F, template<typename> typename... TT, typename T>
        FF<F> VectorSub(const FF<F> first, const TT<T> &...arg)
        {
            return (first - ... - arg);
        }

        template<typename T, template<typename> class VT1, template<typename> class VT2, int... Indexes>
        auto operator+(const VT1<T> &vec, const Swizzle<VT2, T, Indexes...> &swizzle)
                -> VT2<T>
        {
            VT2<T> result;
            int indexes[] = {Indexes...};
            constexpr int swizzle_size = sizeof...(Indexes);
            int count = std::min<int>(VT1<T>::s_length, swizzle_size);

            for (int i = 0; i < count; i++)
            {
                result.data[i] = vec.data[i] + swizzle.v[indexes[i]];
            }
            return result;
        }

        template<typename T, template<typename> class VT1, template<typename> class VT2, int... Indexes>
        auto operator-(const VT1<T> &vec, const Swizzle<VT2, T, Indexes...> &swizzle)
                -> VT2<T>
        {
            VT2<T> result;
            int indexes[] = {Indexes...};
            constexpr int swizzle_size = sizeof...(Indexes);
            int count = std::min<int>(VT1<T>::s_length, swizzle_size);
            for (int i = 0; i < count; i++)
            {
                result.data[i] = vec.data[i] - swizzle.v[indexes[i]];
            }
            return result;
        }

        template<typename T, template<typename> class VT1, template<typename> class VT2, int... Indexes>
        auto operator*(const VT1<T> &vec, const Swizzle<VT2, T, Indexes...> &swizzle)
                -> VT2<T>
        {
            VT2<T> result;
            int indexes[] = {Indexes...};
            constexpr int swizzle_size = sizeof...(Indexes);
            int count = std::min<int>(VT1<T>::s_length, swizzle_size);
            for (int i = 0; i < count; i++)
            {
                result.data[i] = vec.data[i] * swizzle.v[indexes[i]];
            }
            return result;
        }

        template<typename T, template<typename> class VT1, template<typename> class VT2, int... Indexes>
        auto operator/(const VT1<T> &vec, const Swizzle<VT2, T, Indexes...> &swizzle)
                -> VT2<T>
        {
            VT2<T> result;
            int indexes[] = {Indexes...};
            constexpr int swizzle_size = sizeof...(Indexes);
            int count = std::min<int>(VT1<T>::s_length, swizzle_size);
            for (int i = 0; i < count; i++)
            {
                result.data[i] = vec.data[i] / swizzle.v[indexes[i]];
            }
            return result;
        }


        template<template<typename> typename TT, typename T>
        T DotProduct(const TT<T> &first, const TT<T> &second)
        {
            T res{};
            for (u32 i = 0; i < CountOf(first.data); i++)
            {
                res += first.data[i] * second.data[i];
            }
            return res;
        }
        /// <summary>
        /// right-hand,first vector dir is forefinger,second dir is middlefinger
        /// </summary>
        template<template<typename> typename TT, typename T>
        TT<T> CrossProduct(const TT<T> &first, const TT<T> &second)
        {
            return TT<T>(first.data[1] * second.data[2] - first.data[2] * second.data[1],
                         first.data[2] * second.data[0] - first.data[0] * second.data[2],
                         first.data[0] * second.data[1] - first.data[1] * second.data[0]);
        }
        /// <summary>
        /// radians between two dir
        /// </summary>
        template<template<typename> class TT, typename T>
        static f32 Radian(const TT<T> &a, const TT<T> &b)
        {
            T dot = DotProduct(a, b);
            f32 l1 = Magnitude(a);
            f32 l2 = Magnitude(b);
            f32 cos_angle = dot / (l1 * l2);
            cos_angle = std::fmax(-1.f, std::fmin(1.f, cos_angle));
            return std::acos(cos_angle);
        }
        /// <summary>
        /// degrees between two dir
        /// </summary>
        template<template<typename> class TT, typename T>
        static f32 Angle(const TT<T> &a, const TT<T> &b)
        {
            return Radian(a.b) * k2Angle;
        }

        template<template<typename> typename TT, typename T>
        static TT<T> Max(const TT<T> &first, const TT<T> &second)
        {
            TT<T> max{};
            for (u8 i = 0; i < CountOf(first.data); i++)
            {
                max.data[i] = first.data[i] > second.data[i] ? first.data[i] : second.data[i];
            }
            return max;
        }

        template<template<typename> typename TT, typename T>
        static TT<T> Min(const TT<T> &first, const TT<T> &second)
        {
            TT<T> min{};
            for (u8 i = 0; i < CountOf(first.data); i++)
            {
                min.data[i] = first.data[i] < second.data[i] ? first.data[i] : second.data[i];
            }
            return min;
        }

        template<template<typename> typename TT, typename T>
        static T CompMax(const TT<T> &v)
        {
            T max_var = std::numeric_limits<T>::lowest();
            for (u8 i = 0; i < CountOf(v.data); i++)
            {
                max_var = max(v.data[i], max_var);
            }
            return max_var;
        }

        template<template<typename> typename TT, typename T>
        static T CompMin(const TT<T> &v)
        {
            T min_var = std::numeric_limits<T>::max();
            for (u8 i = 0; i < CountOf(v.data); i++)
            {
                min_var = min(v.data[i], min_var);
            }
            return min_var;
        }

        template<template<typename> typename TT, typename T>
        T DistanceToRay(const TT<T> &start, const TT<T> &dir, const TT<T> &p)
        {
            const TT<T> PA = p - start;
            // Calculate the dot product of PA and the ray's direction vector
            T dotProductValue = DotProduct(PA, dir);
            // Calculate the projection vector
            const TT<T> projection = dir * dotProductValue;

            // Vector from PA to the projection point on the ray
            const TT<T> distanceVector = PA - projection;

            // Calculate the distance as the magnitude of the distance vector
            T distance = Magnitude(distanceVector);
            return distance;
        }

        template<template<typename> typename TT, typename T>
        static bool NearbyEqual(const TT<T> v1, const TT<T> v2, T epsilon = kFloatEpsilon)
        {
            for (u32 i = 0; i < CountOf(v1.data); i++)
            {
                if (std::fabs(v1.data[i] - v2.data[i]) > epsilon)
                    return false;
            }
            return true;
        }
        //add check T is float or double
        template<std::floating_point T>
        static bool NearbyEqual(const T v1, const T v2, T epsilon = kFloatEpsilon)
        {
            return std::fabs(v1 - v2) <= epsilon;
        }

        template<template<typename> typename TT, typename T>
        static TT<T> Abs(const TT<T> v)
        {
            TT<T> res;
            for (u32 i = 0; i < CountOf(v.data); i++)
            {
                res.data[i] = std::fabs(v.data[i]);
            }
            return res;
        }

        template<typename V>
        static V Lerp(const V &src, const V &des, const float &weight)
        {
            V res{};
            for (u32 i = 0; i < CountOf(src.data); i++)
            {
                res[i] = (1.f - weight) * src[i] + weight * des[i];
            }
            return res;
        }
        template<>
        static float Lerp(const float &src, const float &des, const float &weight)
        {
            return (1.f - weight) * src + weight * des;
        }
    }
#pragma warning(pop)
}
