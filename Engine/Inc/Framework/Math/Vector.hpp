#pragma once
#include "Framework/Math/MathCommon.hpp"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        template<template<typename> class TT, typename T, int... Indexes>
        struct Swizzle
        {
            T v[sizeof...(Indexes)];
            TT<T> &operator=(const TT<T> &rhs)
            {
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    v[indexes[i]] = rhs[i];
                }
                return *(TT<T> *) this;
            }
            operator TT<T>() const
            {
                return TT<T>(v[Indexes]...);
            }
            TT<T> &operator*=(T scale)
            {
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    v[indexes[i]] *= scale;
                }
                return *(TT<T> *) this;
            }
            TT<T> &operator/=(T scale)
            {
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    v[indexes[i]] /= scale;
                }
                return *(TT<T> *) this;
            }
            TT<T> operator+(T scale)
            {
                TT<T> ret = v;
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    ret[indexes[i]] += scale;
                }
                return ret;
            }
            TT<T> operator-(T scale)
            {
                TT<T> ret = v;
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    ret[indexes[i]] -= scale;
                }
                return ret;
            }
            TT<T> &operator+=(T scale)
            {
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    v[indexes[i]] += scale;
                }
                return *(TT<T> *) this;
            }
            
            TT<T> &operator+=(TT<T> other)
            {
                int indexes[] = {Indexes...};
                int count = std::min<int>(sizeof...(Indexes), CountOf(other.data));
                for (int i = 0; i < count; i++)
                {
                    v[indexes[i]] += other.data[i];
                }
                return *(TT<T> *) this;
            }
            TT<T> &operator-=(T scale)
            {
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    v[indexes[i]] -= scale;
                }
                return *(TT<T> *) this;
            }
            TT<T> operator*(T s) const
            {
                TT<T> r = *this;
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    r[indexes[i]] *= s;
                }
                return r;
            }
            TT<T> operator/(T s) const
            {
                TT<T> r = *this;
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    r[indexes[i]] /= (s + kFloatEpsilon);
                }
                return r;
            }
            TT<T> operator-() const
            {
                TT<T> r = *this;
                int indexes[] = {Indexes...};
                for (int i = 0; i < sizeof...(Indexes); i++)
                {
                    r[indexes[i]] *= -1;
                }
                return r;
            }
            TT<T> operator+(TT<T> other) const
            {
                TT<T> result;
                int indexes[] = {Indexes...};
                int count = std::min<int>(sizeof...(Indexes), CountOf(other.data));
                for (int i = 0; i < count; i++)
                {
                    result.data[i] = v[indexes[i]] + other.data[i];
                }
                return result;
            }

            TT<T> &operator-=(TT<T> other)
            {
                int indexes[] = {Indexes...};
                int count = std::min<int>(sizeof...(Indexes), CountOf(other.data));
                for (int i = 0; i < count; i++)
                {
                    v[indexes[i]] -= other.data[i];
                }
                return *(TT<T> *) this;
            }

            TT<T> operator-(TT<T> other) const
            {
                TT<T> result;
                int indexes[] = {Indexes...};
                int count = std::min<int>(sizeof...(Indexes), CountOf(other.data));
                for (int i = 0; i < count; i++)
                {
                    result.data[i] = v[indexes[i]] - other.data[i];
                }
                return result;
            }

            TT<T> &operator*=(TT<T> other)
            {
                int indexes[] = {Indexes...};
                int count = std::min<int>(sizeof...(Indexes), CountOf(other.data));
                for (int i = 0; i < count; i++)
                {
                    v[indexes[i]] *= other.data[i];
                }
                return *(TT<T> *) this;
            }
            TT<T> operator*(TT<T> other) const
            {
                TT<T> result;
                int indexes[] = {Indexes...};
                int count = std::min<int>(sizeof...(Indexes), CountOf(other.data));
                for (int i = 0; i < count; i++)
                {
                    result.data[i] = v[indexes[i]] * other.data[i];
                }
                return result;
            }
            TT<T>& operator/=(TT<T> other)
            {
                int indexes[] = {Indexes...};
                int count = std::min<int>(sizeof...(Indexes), CountOf(other.data));
                for (int i = 0; i < count; i++)
                {
                    v[indexes[i]] /= (other.data[i] + kFloatEpsilon);
                }
                return *(TT<T> *) this;
            }
            TT<T> operator/(TT<T> other) const
            {
                TT<T> result;
                int indexes[] = {Indexes...};
                int count = std::min<int>(sizeof...(Indexes), CountOf(other.data));
                for (int i = 0; i < count; i++)
                {
                    result.data[i] = v[indexes[i]] / other.data[i];
                }
                return result;
            }
        };
        template<typename T>
        struct Vector2D
        {
            static const Vector2D<T> kZero;
            static const Vector2D<T> kOne;
            inline static constexpr u16 s_length = 2u;
            union
            {
                T data[2];
                struct
                {
                    T x, y;
                };
                struct
                {
                    T r, g;
                };
                struct
                {
                    T u, v;
                };
                Swizzle<Vector2D, T, 0, 1> xy;
                Swizzle<Vector2D, T, 1, 0> yx;
            };
            Vector2D<T>() : x(0), y(0){};
            explicit Vector2D<T>(const T &v) : x(v), y(v){};
            Vector2D<T>(const T &v, const T &w) : x(v), y(w){};
            //operator T *() { return data; };
            //operator const T *() const { return static_cast<const T *>(data); };
            T *Data() { return data; }
            const T *Data() const { return data; }
            T &operator[](u32 index) { return data[index]; }
            const T &operator[](u32 index) const { return data[index]; }

            std::string ToString(int precision = 2) const
            {
                if constexpr (std::is_same<T, float>::value || std::is_same<T, double>::value)
                {
                    return std::format("{:.{}f},{:.{}f}", data[0], precision, data[1], precision);
                }
                else
                {
                    return std::format("{},{}", x, y);
                }
            }
            bool FromString(const std::string &str)
            {
                std::stringstream ss(str);
                char delimiter;
                if (!(ss >> data[0] >> delimiter) || delimiter != ',' ||
                    !(ss >> data[1] >> delimiter) || delimiter != ',')
                {
                    return false;
                }
                return true;
            }

            friend std::ostream &operator<<(std::ostream &os, const Vector2D<T> &vec)
            {
                os << vec.x << "," << vec.y;
                return os;
            }
            bool operator==(const Vector2D<T> &other) const
            {
                return x == other.x && y == other.y;
            }
            bool operator<(const Vector2D<T> &other) const
            {
                if (x != other.x) return x < other.x;
                return y < other.y;
            }

            Vector2D<T> &operator+=(const Vector2D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] += other.data[i];
                }
                return *this;
            }

            Vector2D<T> &operator-=(const Vector2D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] -= other.data[i];
                }
                return *this;
            }

            Vector2D<T> &operator*=(const Vector2D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] *= other.data[i];
                }
                return *this;
            }
            Vector2D<T> &operator*=(T other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] *= other;
                }
                return *this;
            }

            Vector2D<T> &operator/=(const Vector2D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    //AL_ASSERT(other.data[i] == 0.f, "vector divide by zero commpoent!")
                    data[i] /= other.data[i];
                }
                return *this;
            }

            bool operator>=(const Vector2D<T> &other) const
            {
                bool greater = true;
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    greater &= data[i] >= other.data[i];
                }
                return greater;
            }
            bool operator<=(const Vector2D<T> &other) const
            {
                bool less = true;
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    less &= data[i] <= other.data[i];
                }
                return less;
            }
        };
        template<typename T>
        const Vector2D<T> Vector2D<T>::kZero(0, 0);
        template<typename T>
        const Vector2D<T> Vector2D<T>::kOne(1, 1);
        using Vector2f = Vector2D<float>;
        using Vector2Int = Vector2D<i32>;
        using Vector2UInt = Vector2D<u32>;

        template<typename T>
        struct Vector3D
        {
            static const Vector3D<T> kZero;
            static const Vector3D<T> kOne;
            static const Vector3D<T> kForward;
            static const Vector3D<T> kUp;
            static const Vector3D<T> kRight;
            static const Vector3D<T> kVector3Epsilon;
            inline static constexpr u16 s_length = 3u;
            union
            {
                T data[3];
                struct
                {
                    T x, y, z;
                };
                struct
                {
                    T r, g, b;
                };
                Swizzle<Vector2D, T, 0, 1> xy;
                Swizzle<Vector2D, T, 1, 0> yx;
                Swizzle<Vector2D, T, 0, 2> xz;
                Swizzle<Vector2D, T, 2, 0> zx;
                Swizzle<Vector2D, T, 1, 2> yz;
                Swizzle<Vector2D, T, 2, 1> zy;
                Swizzle<Vector3D, T, 0, 1, 2> xyz;
                Swizzle<Vector3D, T, 1, 0, 2> yxz;
                Swizzle<Vector3D, T, 0, 2, 1> xzy;
                Swizzle<Vector3D, T, 2, 0, 1> zxy;
                Swizzle<Vector3D, T, 1, 2, 0> yzx;
                Swizzle<Vector3D, T, 2, 1, 0> zyx;
            };

            Vector3D<T>() : x(0), y(0), z(0){};
            explicit Vector3D<T>(const T &_v) : x(_v), y(_v), z(_v){};
            Vector3D<T>(const T &_x, const T &_y, const T &_z) : x(_x), y(_y), z(_z){};
            Vector3D<T>(Vector2D<T> v, const T &_z) : x(v.x), y(v.y), z(_z){};
            template<typename Archive>
            void serialize(Archive &ar, u32 version)
            {
                ar &ToString();
            }

            bool operator==(const Vector3D<T> &other) const
            {
                return other.x == x && other.y == y && other.z == z;
            }
            bool operator<(const Vector3D<T> &other) const
            {
                if (x != other.x) return x < other.x;
                if (y != other.y) return y < other.y;
                return z < other.z;
            }

            Vector3D<T> &operator=(const Vector3D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] = other.data[i];
                }
                return *this;
            }

            Vector3D<T> &operator+=(const Vector3D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] += other.data[i];
                }
                return *this;
            }

            Vector3D<T> &operator-=(const Vector3D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] -= other.data[i];
                }
                return *this;
            }

            Vector3D<T> &operator*=(const Vector3D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] *= other.data[i];
                }
                return *this;
            }

            Vector3D<T> &operator*=(T &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] *= other;
                }
                return *this;
            }

            Vector3D<T> operator/(const Vector3D<T> &other) const
            {
                Vector3D<T> r;
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    r.data[i] = data[i] / other.data[i];
                }
                return r;
            }

            Vector3D<T> &operator/=(const Vector3D<T> &other)
            {
                try
                {
                    for (u8 i = 0; i < CountOf(data); i++)
                    {
                        AL_ASSERT_MSG(other.data[i] != 0.f, "vector divide by zero commpoent!");
                        //if (other.data[i] == 0.f) throw(std::runtime_error("vector divide by zero commpoent!"));
                        data[i] /= other.data[i];
                    }
                }
                catch (const std::exception &e)
                {
                    //LOG_ERROR(e.what());
                }
                return *this;
            }

            Vector3D<T> operator*(float scale) const
            {
                Vector3D<T> v{*this};
                v *= scale;
                return v;
            }

            friend std::ostream &operator<<(std::ostream &os, const Vector3D<T> &vec)
            {
                os << vec.x << "," << vec.y << "," << vec.z;
                return os;
            }

            //operator T *() { return data; };
            //operator const T *() const { return static_cast<const T *>(data); };
            T *Data() { return data; }
            const T *Data() const { return data; }
            T &operator[](u32 index) { return data[index]; }
            const T &operator[](u32 index) const { return data[index]; }

            std::string ToString(int precision = 2) const
            {
                if constexpr (std::is_same<T, float>::value || std::is_same<T, double>::value)
                {
                    return std::format("{:.{}f},{:.{}f},{:.{}f}", data[0], precision, data[1], precision, data[2], precision);
                }
                else
                {
                    return std::format("{},{},{}", x, y, z);
                }
            }
            bool FromString(const std::string &str)
            {
                std::stringstream ss(str);
                char delimiter;
                if (!(ss >> data[0] >> delimiter) || delimiter != ',' ||
                    !(ss >> data[1] >> delimiter) || delimiter != ',' ||
                    !(ss >> data[2]))
                {
                    return false;
                }
                return true;
            }
        };
        template<typename T>
        const Vector3D<T> Vector3D<T>::kZero(0, 0, 0);
        template<typename T>
        const Vector3D<T> Vector3D<T>::kOne(1, 1, 1);
        template<typename T>
        const Vector3D<T> Vector3D<T>::kForward(0, 0, 1);
        template<typename T>
        const Vector3D<T> Vector3D<T>::kUp(0, 1, 0);
        template<typename T>
        const Vector3D<T> Vector3D<T>::kRight(1, 0, 0);
        template<typename T>
        const Vector3D<T> Vector3D<T>::kVector3Epsilon(kFloatEpsilon, kFloatEpsilon, kFloatEpsilon);

        using Vector3f = Vector3D<float>;
        using Vector3Int = Vector3D<i32>;
        using Vector3UInt = Vector3D<u32>;

        template<typename T>
        struct Vector4D
        {
            static const Vector4D<T> kZero;
            static const Vector4D<T> kOne;
            inline static constexpr u16 s_length = 4u;
            union
            {
                T data[4];
                struct
                {
                    T x, y, z, w;
                };
                struct
                {
                    T r, g, b, a;
                };
                Swizzle<Vector2D, T, 0, 1> xy;
                Swizzle<Vector2D, T, 0, 2> xz;
                Swizzle<Vector2D, T, 1, 2> yz;
                Swizzle<Vector2D, T, 2, 3> zw;
                Swizzle<Vector3D, T, 0, 1, 2> xyz;
                Swizzle<Vector3D, T, 0, 2, 1> xzy;
                Swizzle<Vector3D, T, 1, 0, 2> yxz;
                Swizzle<Vector3D, T, 1, 2, 0> yzx;
                Swizzle<Vector3D, T, 2, 0, 1> zxy;
                Swizzle<Vector3D, T, 2, 1, 0> zyx;
                Swizzle<Vector4D, T, 2, 1, 0, 3> bgra;
            };

            Vector4D<T>() : x(0), y(0), z(0), w(0){};
            explicit Vector4D<T>(const T &_v) : x(_v), y(_v), z(_v), w(_v){};
            Vector4D<T>(const T &_x, const T &_y, const T &_z, const T &_w) : x(_x), y(_y), z(_z), w(_w){};
            Vector4D<T>(const Vector2D<T> &v2) : x(v2.x), y(v2.y), z(0), w(0){};
            Vector4D<T>(Vector2D<T> v1,Vector2D<T> v2) : x(v1.x), y(v1.y), z(v2.x), w(v2.y){};
            Vector4D<T>(const Vector2D<T> &v2, T _z, T _w) : x(v2.x), y(v2.y), z(_z), w(_w){};
            Vector4D<T>(const Vector3D<T> &v3) : x(v3.x), y(v3.y), z(v3.z), w(1.0f){};
            Vector4D<T>(const Vector3D<T> &v3, const T &_w) : x(v3.x), y(v3.y), z(v3.z), w(_w){};

            //operator T *() { return data; };
            //operator const T *() const { return static_cast<const T *>(data); };
            T *Data() { return data; }
            const T *Data() const { return data; }
            T &operator[](u32 index) { return data[index]; }
            const T &operator[](u32 index) const { return data[index]; }

            friend std::ostream &operator<<(std::ostream &os, const Vector4D<T> &vec)
            {
                os << vec.x << "," << vec.y << "," << vec.z << "," << vec.w;
                return os;
            }

            bool operator==(const Vector4D<T> &other) const
            {
                return x == other.x && y == other.y && z == other.z && w == other.w;
            }
            bool operator<(const Vector4D<T> &other) const
            {
                if (x != other.x) return x < other.x;
                if (y != other.y) return y < other.y;
                if (z != other.z) return z < other.z;
                return w < other.w;
            }

            Vector4D<T> &operator+=(const Vector4D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] += other.data[i];
                }
                return *this;
            }

            Vector4D<T> &operator-=(const Vector4D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] -= other.data[i];
                }
                return *this;
            }

            Vector4D<T> &operator*=(const Vector4D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] *= other.data[i];
                }
                return *this;
            }

            Vector4D<T> &operator*=(T other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    data[i] *= other;
                }
                return *this;
            }

            Vector4D<T> &operator/=(const Vector4D<T> &other)
            {
                for (u8 i = 0; i < CountOf(data); i++)
                {
                    if (other.data[i] == 0.f)
                    {
                        throw(std::runtime_error("vector divide by zero commpoent!"));
                    }
                    data[i] /= other.data[i];
                }
                return *this;
            }
            Vector4D<T> operator+(const Vector4D<T> &other) const
            {
                Vector4D<T> v{*this};
                v += other;
                return v;
            }
            Vector4D<T> operator-(const Vector4D<T> &other) const
            {
                Vector4D<T> v{*this};
                v -= other;
                return v;
            }
            Vector4D<T> operator*(float scale) const
            {
                Vector4D<T> v{*this};
                v *= scale;
                return v;
            }
            std::string ToString(int precision = 2) const
            {
                if constexpr (std::is_same<T, float>::value || std::is_same<T, double>::value)
                {
                    return std::format("{:.{}f},{:.{}f},{:.{}f},{:.{}f}", data[0], precision, data[1], precision, data[2], precision, data[3], precision);
                }
                else
                {
                    return std::format("{},{},{},{}", x, y, z, w);
                }
            }
            bool FromString(const std::string &str)
            {
                std::stringstream ss(str);
                char delimiter;
                if (!(ss >> data[0] >> delimiter) || delimiter != ',' ||
                    !(ss >> data[1] >> delimiter) || delimiter != ',' ||
                    !(ss >> data[2] >> delimiter) || delimiter != ',' ||
                    !(ss >> data[3]))
                {
                    return false;
                }
                return true;
            }
        };
        template<typename T>
        const Vector4D<T> Vector4D<T>::kZero(0, 0, 0, 0);
        template<typename T>
        const Vector4D<T> Vector4D<T>::kOne(1, 1, 1, 1);

        using Vector4f = Vector4D<float>;
        using Vector4Int = Vector4D<i32>;
        using Vector4UInt = Vector4D<u32>;
    }
#pragma warning(pop)
}
