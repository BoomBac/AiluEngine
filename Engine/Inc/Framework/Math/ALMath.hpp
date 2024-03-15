#ifndef __AL_MATH_H__
#define __AL_MATH_H__

#include <math.h>
#include <immintrin.h> //simd
#include <string>
#include <limits>
#include <format>
#include <limits>
#include <bitset>
#include <limits>
#include "GlobalMarco.h"

namespace Ailu
{
	namespace MathInternal
	{
		template<typename T, size_t size_of_arr>
		constexpr u32 CountOf(T(&)[size_of_arr]) { return (u32)size_of_arr; }

		template<typename T, size_t row, size_t col>
		constexpr u32 CountOf(T(&)[row][col]) { return (u32)(row * col); }

		static float NormalizeScaleFactor(int32_t var)
		{
			return (var == 0) ? 1.0f / sqrt(2.0f) : 1.0f;
		}
		constexpr float kPi = 3.1415926f;
		constexpr float kHalfPi = kPi / 2.0f;
		constexpr float kDPi = kPi * 2.f;
		constexpr float one_over_four = 1.f / 4.f;
		constexpr float Pi_over_sixteen = kPi / 16.f;
		constexpr float kFloatEpsilon = 1e-6f;

	}
	using namespace MathInternal;

#pragma warning(push)
#pragma warning(disable: 4244)

	//template<typename T>
	//T Normalize(T& var)
	//{
	//	size_t len = CountOf(var.data);
	//	double sum = 0.f;
	//	T temp{};
	//	for (u32 i = 0; i < len; i++)
	//		sum += pow(var[i], 2.f);
	//	sum = sqrt(sum);
	//	for (u32 i = 0; i < len; i++) {
	//		var[i] /= sum;
	//		temp[i] = var[i];
	//	}
	//	return temp;
	//}

	template<typename T>
	T Normalize(const T& var)
	{
		size_t len = CountOf(var.data);
		double sum = 0.f;
		T temp{};
		for (u32 i = 0; i < len; i++)
		{
			sum += pow(var[i], 2.f);
			temp[i] = var[i];
		}
		sum = sqrt(sum);
		for (u32 i = 0; i < len; i++) {
			temp[i] /= sum;
		}
		return temp;
	}

	template <template<typename> class TT, typename T, int ... Indexes>
	class Swizzle
	{
		T v[sizeof...(Indexes)];
	public:
		TT<T>& operator=(const TT<T>& rhs)
		{
			int indexes[] = { Indexes... };
			for (int i = 0; i < sizeof...(Indexes); i++)
			{
				v[indexes[i]] = rhs[i];
			}
			return *(TT<T>*)this;
		}
		operator TT<T>() const
		{
			return TT<T>(v[Indexes]...);
		}
		TT<T>& operator*=(T scale)
		{
			int indexes[] = { Indexes... };
			for (int i = 0; i < sizeof...(Indexes); i++)
			{
				v[indexes[i]] *= scale;
			}
			return *(TT<T>*)this;
		}
		TT<T>& operator+=(T scale)
		{
			int indexes[] = { Indexes... };
			for (int i = 0; i < sizeof...(Indexes); i++)
			{
				v[indexes[i]] += scale;
			}
			return *(TT<T>*)this;
		}
	};
	template<typename T>
	struct Vector2D
	{
		static const Vector2D<T> kZero;
		static const Vector2D<T> kOne;
		union
		{
			T data[2];
			struct { T x, y; };
			struct { T r, g; };
			struct { T u, v; };
			Swizzle<Vector2D, T, 0, 1> xy;
			Swizzle<Vector2D, T, 1, 0> yx;
		};
		Vector2D<T>() : x(0),y(0) {};
		Vector2D<T>(const T& v) : x(v), y(v) {};
		Vector2D<T>(const T& v, const T& w) : x(v), y(w) {};
		operator T* () { return data; };
		operator const T* () { return static_cast<const T*>(data); };
		T& operator[](u32 index) { return data[index]; }
		const T& operator[](u32 index) const { return data[index]; }

		std::string ToString(int precision = 2)
		{
			return std::format("({:.{}f},{:.{}f})", data[0], precision, data[1], precision);
		}

		friend std::ostream& operator<<(std::ostream& os, const Vector2D<T>& vec)
		{
			os << vec.x << "," << vec.y;
			return os;
		}

		Vector2D<T>& operator+=(const Vector2D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] += other.data[i];
			}
			return *this;
		}

		Vector2D<T>& operator-=(const Vector2D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] -= other.data[i];
			}
			return *this;
		}

		Vector2D<T>& operator*=(const Vector2D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] *= other.data[i];
			}
			return *this;
		}

		Vector2D<T>& operator/=(const Vector2D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				//AL_ASSERT(other.data[i] == 0.f, "vector divide by zero commpoent!")
				data[i] /= other.data[i];
			}
			return *this;
		}
	};
	template <typename T>
	const Vector2D<T> Vector2D<T>::kZero(0, 0);
	template <typename T>
	const Vector2D<T> Vector2D<T>::kOne(1, 1);
	using Vector2f = Vector2D<float>;

	template <typename T>
	struct Vector3D
	{
		static const Vector3D<T> kZero;
		static const Vector3D<T> kOne;
		static const Vector3D<T> kForward;
		static const Vector3D<T> kUp;
		static const Vector3D<T> kRight;
		static const Vector3D<T> kVector3Epsilon;
		union {
			T data[3];
			struct { T x, y, z; };
			struct { T r, g, b; };
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

		Vector3D<T>() : x(0), y(0),z(0) {};
		Vector3D<T>(const T& _v) : x(_v), y(_v), z(_v) {};
		Vector3D<T>(const T& _x, const T& _y, const T& _z) : x(_x), y(_y), z(_z) {};

		bool operator==(const Vector3D<T>& other) const
		{
			return memcmp(this, &other, sizeof(Vector3D<T>));
		}

		Vector3D<T>& operator=(const Vector3D<T>& other)
		{
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] = other.data[i];
			}
			return *this;
		}

		Vector3D<T>& operator+=(const Vector3D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] += other.data[i];
			}
			return *this;
		}

		Vector3D<T>& operator-=(const Vector3D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] -= other.data[i];
			}
			return *this;
		}

		Vector3D<T>& operator*=(const Vector3D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] *= other.data[i];
			}
			return *this;
		}

		Vector3D<T>& operator*=(const float& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] *= other;
			}
			return *this;
		}

		Vector3D<T>& operator/=(const Vector3D<T>& other) {
			try
			{
				for (u8 i = 0; i < CountOf(data); i++) {
					AL_ASSERT(other.data[i] == 0.f, "vector divide by zero commpoent!")
						//if (other.data[i] == 0.f) throw(std::runtime_error("vector divide by zero commpoent!"));
						data[i] /= other.data[i];
				}
			}
			catch (const std::exception& e)
			{
				LOG_ERROR(e.what());
			}
			return *this;
		}

		Vector3D<T> operator*(float scale) const
		{
			Vector3D<T> v{ *this };
			v *= scale;
			return v;
		}

		friend std::ostream& operator<<(std::ostream& os, const Vector3D<T>& vec)
		{
			os << vec.x << "," << vec.y << "," << vec.z;
			return os;
		}

		operator T* () { return data; };
		operator const T* () const { return static_cast<const T*>(data); };
		T& operator[](u32 index) { return data[index]; }
		const T& operator[](u32 index) const { return data[index]; }

		std::string ToString(int precision = 2) const
		{
			return std::format("({:.{}f},{:.{}f},{:.{}f})", data[0], precision, data[1], precision, data[2], precision);
		}
	};
	template <typename T>
	const Vector3D<T> Vector3D<T>::kZero(0, 0, 0);
	template <typename T>
	const Vector3D<T> Vector3D<T>::kOne(1, 1, 1);
	template <typename T>
	const Vector3D<T> Vector3D<T>::kForward(0, 0, 1);
	template <typename T>
	const Vector3D<T> Vector3D<T>::kUp(0, 1, 0);
	template <typename T>
	const Vector3D<T> Vector3D<T>::kRight(1, 0, 0);
	template <typename T>
	const Vector3D<T> Vector3D<T>::kVector3Epsilon(kFloatEpsilon, kFloatEpsilon, kFloatEpsilon);

	using Vector3f = Vector3D<float>;

	template <typename T>
	struct Vector4D
	{
		static const Vector4D<T> kZero;
		static const Vector4D<T> kOne;
		union {
			T data[4];
			struct { T x, y, z, w; };
			struct { T r, g, b, a; };
			Swizzle<Vector3D, T, 0, 1, 2> xyz;
			Swizzle<Vector3D, T, 0, 2, 1> xzy;
			Swizzle<Vector3D, T, 1, 0, 2> yxz;
			Swizzle<Vector3D, T, 1, 2, 0> yzx;
			Swizzle<Vector3D, T, 2, 0, 1> zxy;
			Swizzle<Vector3D, T, 2, 1, 0> zyx;
			Swizzle<Vector4D, T, 2, 1, 0, 3> bgra;
		};

		Vector4D<T>() : x(0), y(0),z(0),w(0) { };
		Vector4D<T>(const T& _v) : x(_v), y(_v), z(_v), w(_v) {};
		Vector4D<T>(const T& _x, const T& _y, const T& _z, const T& _w) : x(_x), y(_y), z(_z), w(_w) {};
		Vector4D<T>(const Vector3D<T>& v3) : x(v3.x), y(v3.y), z(v3.z), w(1.0f) {};
		Vector4D<T>(const Vector3D<T>& v3, const T& _w) : x(v3.x), y(v3.y), z(v3.z), w(_w) {};

		operator T* () { return data; };
		operator const T* () const { return static_cast<const T*>(data); };
		T& operator[](u32 index) { return data[index]; }
		const T& operator[](u32 index) const { return data[index]; }

		friend std::ostream& operator<<(std::ostream& os, const Vector4D<T>& vec)
		{
			os << vec.x << "," << vec.y << "," << vec.z << "," << vec.w;
			return os;
		}

		bool operator==(const Vector4D<T>& other) const
		{
			return x == other.x && y == other.y && z == other.z && w == other.w;
		}

		Vector4D<T>& operator+=(const Vector4D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] += other.data[i];
			}
			return *this;
		}

		Vector4D<T>& operator-=(const Vector4D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] -= other.data[i];
			}
			return *this;
		}

		Vector4D<T>& operator*=(const Vector4D<T>& other) {
			for (u8 i = 0; i < CountOf(data); i++) {
				data[i] *= other.data[i];
			}
			return *this;
		}

		Vector4D<T>& operator/=(const Vector4D<T>& other) {
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
		Vector4D<T> operator+(const Vector4D<T>& other) const
		{
			Vector4D<T> v{ *this };
			v += other;
			return v;
		}
		Vector4D<T> operator-(const Vector4D<T>& other) const
		{
			Vector4D<T> v{ *this };
			v -= other;
			return v;
		}
		Vector4D<T> operator*(float scale) const
		{
			Vector4D<T> v{ *this };
			v *= scale;
			return v;
		}
	};
	template <typename T>
	const Vector4D<T> Vector4D<T>::kZero(0, 0, 0, 0);
	template <typename T>
	const Vector4D<T> Vector4D<T>::kOne(1, 1, 1, 1);

	using Vector4f = Vector4D<float>;
	using R8G8B8A8Unorm = Vector4D<u8>;
	using R8G8B8Unorm = Vector3D<u8>;
	using R32G32B32Float = Vector3D<float>;
	using R32G32B32A32Float = Vector3D<float>;
	using Vector4i = Vector4D<u8>;

	template<template<typename> class TT, typename T>
	static float Distance(const TT<T>& from, const TT<T>& to)
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
	static float Magnitude(const TT<T>& v)
	{
		float dis = 0;
		for (u32 i = 0; i < CountOf(v.data); i++)
		{
			dis += v.data[i] * v.data[i];
		}
		return sqrt(dis);
	}

	template<template<typename> class TT, typename T>
	static bool LoadVector(const char* vec_str, const TT<T>& out_v)
	{
#pragma warning(push)
#pragma warning(disable: 4477)
		int length = CountOf(out_v.data);
		if (std::is_same<T, float>::value)
		{
			if (length == 2) return sscanf_s(vec_str, "%f,%f", &out_v.data[0], &out_v.data[1]) == 2;
			else if (length == 3) return sscanf_s(vec_str, "%f,%f,%f", &out_v.data[0], &out_v.data[1], &out_v.data[2]) == 3;
			else if (length == 4) return sscanf_s(vec_str, "%f,%f,%f,%f", &out_v.data[0], &out_v.data[1], &out_v.data[2],&out_v.data[3]) == 4;
		}
		return false;
#pragma warning(pop)
	}

	template<template<typename> class TT, typename T>
	static TT<T> LoadVector(const char* vec_str)
	{
#pragma warning(push)
#pragma warning(disable: 4477)
		const TT<T> out_v{};
		int length = CountOf(out_v.data);
		if (std::is_same<T, float>::value)
		{
			if (length == 3) return sscanf_s(vec_str, "%f,%f,%f", &out_v.x, &out_v.y, &out_v.z) == 3;
			else if (length == 4) return sscanf_s(vec_str, "%f,%f,%f,%f", &out_v.x, &out_v.y, &out_v.z, &out_v.data[3]) == 4;
		}
		return out_v;
#pragma warning(pop)
	}


	static float LoadFloat(const char* f_str)
	{
		float tmp = 0.0f;
		return sscanf_s(f_str, "%f", &tmp) == 1 ? tmp : 0.0f;
	}



	inline static float ToRadius(const float& angle) { return angle * kPi / 180.f; }
	inline static float ToAngle(const float& radius) { return 180.f * radius / kPi; }

	inline static float k2Angle = 180.f / kPi;
	inline static float k2Radius = kPi / 180.f;

	//inline static float Clamp(const float& value, const float& max, const float& min)
	//{
	//	return (value > max) ? max : (value < min) ? min : value;
	//}

	inline static void Clamp(float& value, const float& min, const float& max)
	{
		value = (value > max) ? max : (value < min) ? min : value;
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
	TT<T> operator*(const T& scalar, const TT<T>& v)
	{
		TT<T> res;
		for (u32 i = 0; i < CountOf(v.data); i++)
		{
			res.data[i] = scalar * v.data[i];
		}
		return res;
	}

	template<template<typename> typename TT, typename T>
	TT<T> operator*(const TT<T>& a, const TT<T>& b)
	{
		TT<T> res;
		for (u32 i = 0; i < CountOf(a.data); i++)
		{
			res.data[i] = a.data[i] * b.data[i];
		}
		return res;
	}

	template<template<typename> typename TT, typename T>
	TT<T> operator*(const TT<T>& v, const T& scalar)
	{
		return scalar * v;
	}

	template<template<typename> typename TT, typename T>
	TT<T> operator/(const TT<T>& v, const T& scalar)
	{
		TT<T> res;
		for (uint32_t i = 0; i < CountOf(v.data); i++)
		{
			res.data[i] /= scalar;
		}
		return res;
	}

	template<template<typename> typename TT, typename T>
	TT<T> operator+(const T& scalar, const TT<T>& v)
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
	FF<F> VectorAdd(const FF<F> first, const TT<T>&... arg)
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
	FF<F> VectorSub(const FF<F> first, const TT<T>&... arg)
	{
		return (first - ... - arg);
	}
	template<template<typename> typename TT, typename T>
	T DotProduct(const TT<T>& first, const TT<T>& second)
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
	TT<T> CrossProduct(const TT<T>& first, const TT<T>& second)
	{
		return TT<T>(first.data[1] * second.data[2] - first.data[2] * second.data[1],
			first.data[2] * second.data[0] - first.data[0] * second.data[2],
			first.data[0] * second.data[1] - first.data[1] * second.data[0]);
	}

	template<template<typename> typename TT, typename T>
	static TT<T> Max(const TT<T>& first, const TT<T>& second)
	{
		TT<T> max{};
		for (u8 i = 0; i < CountOf(first.data); i++)
		{
			max.data[i] = first.data[i] > second.data[i] ? first.data[i] : second.data[i];
		}
		return max;
	}

	template<template<typename> typename TT, typename T>
	static TT<T> Min(const TT<T>& first, const TT<T>& second)
	{
		TT<T> min{};
		for (u8 i = 0; i < CountOf(first.data); i++)
		{
			min.data[i] = first.data[i] < second.data[i] ? first.data[i] : second.data[i];
		}
		return min;
	}

	template<template<typename> typename TT, typename T>
	static T CompMax(const TT<T>& v)
	{
		T max_var = std::numeric_limits<T>::lowest();
		for (u8 i = 0; i < CountOf(v.data); i++)
		{
			max_var = max(v.data[i], max_var);
		}
		return max_var;
	}

	template<template<typename> typename TT, typename T>
	static T CompMin(const TT<T>& v)
	{
#undef max
		T min_var = std::numeric_limits<T>::max();
		for (u8 i = 0; i < CountOf(v.data); i++)
		{
			min_var = min(v.data[i], min_var);
		}
		return min_var;
#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif
	}

	template<template<typename> typename TT, typename T>
	T DistanceToRay(const TT<T>& start, const TT<T>& dir, const TT<T>& p) 
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

	template <typename T, int rows, int cols>
	struct Matrix
	{
		union
		{
			T data[rows][cols];
		};

		T* operator[](int row_index)
		{
			return data[row_index];
		}
		const T* operator[](int row_index) const
		{
			return data[row_index];
		}
		T& operator=(const T& other)
		{
			return *this;
		}
		template<template<typename> typename TT, typename T>
		void SetRow(u16 row,const TT<T>& v)
		{
			memcpy(data[row],&v,sizeof(v));
		}
		operator T* () { return &data[0][0]; };
		operator const T* () const { return static_cast<const T*>(&data[0][0]); };
	};
	using Matrix4x4f = Matrix<float, 4, 4>;
	using Matrix3x3f = Matrix<float, 3, 3>;

	template<typename T, int rows, int cols>
	std::string MatrixToString(Matrix<T, rows, cols> mat)
	{
		std::stringstream ss;
		for (u32 i = 0; i < rows; i++)
		{
			ss << "[ ";
			for (u32 j = 0; j < cols; j++)
			{
				if (j != cols - 1) ss << mat[i][j] << ",";
				else ss << mat[i][j] << " ]" << "\n";
			}
		}
		std::string temp;
		std::string res;
		while (!ss.eof())
		{
			getline(ss, temp);
			temp.append("\n");
			res.append(temp);
		}
		return res;
	}

	template<typename T, int rows, int cols>
	void MatrixAdd(Matrix<T, rows, cols>& ret, const Matrix<T, rows, cols>& m1, const Matrix<T, rows, cols>& m2)
	{
		for (u32 i = 0; i < CountOf(m1.data); i++)
		{
			for (u32 j = 0; j < CountOf(m2.data); j++)
			{
				ret[i][j] = m1[i][j] + m2[i][j];
			}
		}
	}

	template<typename T, int rows, int cols>
	Matrix<T, rows, cols> operator+(const Matrix<T, rows, cols>& m1, const Matrix<T, rows, cols>& m2)
	{
		Matrix<T, rows, cols> ret;
		for (u32 i = 0; i < CountOf(m1.data); i++)
		{
			for (u32 j = 0; j < CountOf(m2.data); j++)
			{
				ret[i][j] = m1[i][j] + m2[i][j];
			}
		}
		return ret;
	}

	template<typename T, int rows, int cols>
	void MatrixSub(Matrix<T, rows, cols>& ret, const Matrix<T, rows, cols>& m1, const Matrix<T, rows, cols>& m2)
	{
		for (u32 i = 0; i < CountOf(m1.data); i++)
		{
			for (u32 j = 0; j < CountOf(m2.data); j++)
			{
				ret[i][j] = m1[i][j] - m2[i][j];
			}
		}
	}

	template<typename T, int rows, int cols>
	Matrix<T, rows, cols> operator-(const Matrix<T, rows, cols>& m1, const Matrix<T, rows, cols>& m2)
	{
		Matrix<T, rows, cols> ret;
		for (u32 i = 0; i < CountOf(m1.data); i++)
		{
			for (u32 j = 0; j < CountOf(m2.data); j++)
			{
				ret[i][j] = m1[i][j] - m2[i][j];
			}
		}
		return ret;
	}

	template<typename T, int rows, int cols>
	Matrix<T, cols, rows> Transpose(Matrix<T, rows, cols>& mat)
	{
		Matrix<T, cols, rows> ret;
		for (u32 i = 0; i < rows; i++)
		{
			for (u32 j = 0; j < cols; j++)
			{
				ret[j][i] = mat[i][j];
			}
		}
		mat = ret;
		return ret;
	}
	template<typename T, int rows, int cols>
	Matrix<T, cols, rows> Transpose(const Matrix<T, rows, cols>& mat)
	{
		Matrix<T, cols, rows> ret;
		for (u32 i = 0; i < rows; i++)
		{
			for (u32 j = 0; j < cols; j++)
			{
				ret[j][i] = mat[i][j];
			}
		}
		return ret;
	}

	template <typename T, int Da, int Db, int Dc>
	void MatrixMultipy(Matrix<T, Da, Dc>& ret, const Matrix<T, Da, Db>& m1, const Matrix<T, Dc, Db>& m2)
	{
		//Matrix<T, Db, Dc> m2_t;
		//m2_t = Transpose(m2);
		for (u32 i = 0; i < Da; i++)
		{
			for (u32 j = 0; j < Dc; j++)
			{
				for (u32 k = 0; k < Dc; k++)
					ret[j][i] += m1[j][k] * m2[k][i];
			}
		}
		//Transpose(ret);
	}

	template <template <typename, int, int> class M, template <typename> class V, typename T, int ROWS, int COLS>
	inline void GetOrigin(V<T>& result, const M<T, ROWS, COLS>& matrix)
	{
		static_assert(ROWS >= 3, "[Error] Only 3x3 and above matrix can be passed to this method!");
		static_assert(COLS >= 3, "[Error] Only 3x3 and above matrix can be passed to this method!");
		result = { matrix[3][0], matrix[3][1], matrix[3][2] };
	}

	template <typename T, int row, int col>
	Matrix<T, row, col> operator*(const Matrix<T, row, col>& m1, const Matrix<T, row, col>& m2)
	{
		Matrix<T, row, col> ret{};
		MatrixMultipy(ret, m1, m2);
		return ret;
	}

	static float NormalizeAngle(float angle)
	{
		float normalizedAngle = std::fmod(angle, 360.0f);
		if (normalizedAngle > 180.0f)  normalizedAngle -= 360.0f;
		else if (normalizedAngle < -180.0f) normalizedAngle += 360.0f;
		return normalizedAngle;
	}

	//row-major matrix * vector
	static void TransformVector(Vector4f& vector, const Matrix4x4f& matrix)
	{
#ifdef _SIMD
		__m128 xmm_vector1 = _mm_loadu_ps(&vector.x); // Load vector1 into SSE register

		__m128 xmm_matrix_row1 = _mm_loadu_ps(&matrix[0][0]); // Load matrix row 1 into SSE register
		__m128 xmm_matrix_row2 = _mm_loadu_ps(&matrix[1][0]); // Load matrix row 2 into SSE register
		__m128 xmm_matrix_row3 = _mm_loadu_ps(&matrix[2][0]); // Load matrix row 3 into SSE register
		__m128 xmm_matrix_row4 = _mm_loadu_ps(&matrix[3][0]); // Load matrix row 4 into SSE register

		// Multiply and accumulate operations
		xmm_vector1 = _mm_add_ps(
			_mm_add_ps(
				_mm_mul_ps(_mm_shuffle_ps(xmm_vector1, xmm_vector1, _MM_SHUFFLE(0, 0, 0, 0)), xmm_matrix_row1),
				_mm_mul_ps(_mm_shuffle_ps(xmm_vector1, xmm_vector1, _MM_SHUFFLE(1, 1, 1, 1)), xmm_matrix_row2)
			),
			_mm_add_ps(
				_mm_mul_ps(_mm_shuffle_ps(xmm_vector1, xmm_vector1, _MM_SHUFFLE(2, 2, 2, 2)), xmm_matrix_row3),
				_mm_mul_ps(_mm_shuffle_ps(xmm_vector1, xmm_vector1, _MM_SHUFFLE(3, 3, 3, 3)), xmm_matrix_row4)
			)
		);
		// Store the result back into the vector
		_mm_storeu_ps(&vector.x, xmm_vector1); // Store xmm_vector1 into vector1
#else
		Vector4f temp{};
		temp.x = vector.x * matrix[0][0] + vector.y * matrix[1][0] + vector.z * matrix[2][0] + vector.w * matrix[3][0];
		temp.y = vector.x * matrix[0][1] + vector.y * matrix[1][1] + vector.z * matrix[2][1] + vector.w * matrix[3][1];
		temp.z = vector.x * matrix[0][2] + vector.y * matrix[1][2] + vector.z * matrix[2][2] + vector.w * matrix[3][2];
		temp.w = vector.x * matrix[0][3] + vector.y * matrix[1][3] + vector.z * matrix[2][3] + vector.w * matrix[3][3];
		vector = temp;
		return;
#endif // !_SIMD
	}

	static Vector3f MultipyVector(const Vector3f& v, const Matrix4x4f& mat)
	{
		Vector4f temp{ v,1.f };
		TransformVector(temp, mat);
		return temp.xyz;
	}

	//Transform for point,w is 1.f
	static void TransformCoord(Vector3f& vector, const Matrix4x4f& matrix)
	{
		Vector4f temp{ vector,1.f };
		TransformVector(temp, matrix);
		memcpy(&vector,temp,12);
	}

	static Vector3f TransformCoord(const Matrix4x4f& matrix, const Vector3f& vector)
	{
		Vector4f temp{ vector,1.f };
		TransformVector(temp, matrix);
		return temp.xyz;
	}

	//	//Transform for normal,w is 0.f
	static Vector3f& TransformNormal(Vector3f& vector, const Matrix4x4f& matrix)
	{
		Vector4f temp{ vector,0.f };
		TransformVector(temp, matrix);
		vector[0] = temp[0];
		vector[1] = temp[1];
		vector[2] = temp[2];
		return vector;
	}

	static Matrix4x4f BuildViewMatrixLookToLH(Matrix4x4f& result, Vector3f position, Vector3f lookTo, Vector3f up)
	{
		Vector3f zAxis, xAxis, yAxis;
		zAxis = lookTo;
		Normalize(zAxis);
		xAxis = CrossProduct(up, zAxis);
		Normalize(xAxis);
		yAxis = CrossProduct(zAxis, xAxis);
		result = { {{
			{ xAxis.x, yAxis.x, zAxis.x, 0.0f },
			{ xAxis.y, yAxis.y, zAxis.y, 0.0f },
			{ xAxis.z, yAxis.z, zAxis.z, 0.0f },
			{ -DotProduct(xAxis, position), -DotProduct(yAxis, position), -DotProduct(zAxis, position), 1.0f }
		}} };
		return result;
	}
	//https://stackoverflow.com/questions/349050/calculating-a-lookat-matrix
	static Matrix4x4f BuildViewMatrixLookAtLH(Matrix4x4f& result, Vector3f position, Vector3f look_at, Vector3f up)
	{
		Vector3f zAxis, xAxis, yAxis;
		zAxis = look_at - position;
		return BuildViewMatrixLookToLH(result, position, zAxis, up);
	}

	static Matrix4x4f BuildViewRHMatrix(Matrix4x4f& result, Vector3f position, Vector3f lookAt, Vector3f up)
	{
		Vector3f zAxis, xAxis, yAxis;
		zAxis = lookAt - position;
		Normalize(zAxis);
		xAxis = CrossProduct(up, zAxis);
		Normalize(xAxis);
		yAxis = CrossProduct(zAxis, xAxis);
		result = { {{
			{ xAxis.x, yAxis.x, zAxis.x, 0.0f },
			{ xAxis.y, yAxis.y, zAxis.y, 0.0f },
			{ xAxis.z, yAxis.z, zAxis.z, 0.0f },
			{ DotProduct(xAxis, -position), DotProduct(yAxis, -position), DotProduct(zAxis, -position), 1.0f }
		}} };
		return result;
	}

	template <typename T, int n>
	static auto SubMatrix(const Matrix<T, n, n>& mat, const int& r, const int& c) -> Matrix<T, n - 1, n - 1>
	{
		int i = 0, j = 0;
		Matrix<T, n - 1, n - 1> temp{};
		for (int row = 0; row < n; row++)
		{
			for (int col = 0; col < n; col++)
			{
				if (row != r && col != c)
				{
					temp[i][j++] = mat[row][col];
					if (j == n - 1)
					{
						j = 0;
						i++;
					}
				}
			}
		}
		return temp;
	}

	static float MatrixDeterminat(Matrix3x3f matrix)
	{
		float ret = 0.f, sign = 1.f;
		for (int i = 0; i < 3; ++i)
		{
			Matrix<float, 2, 2> mat2 = SubMatrix(matrix, 0, i);
			ret += sign * matrix[0][i] * (mat2[0][0] * mat2[1][1] - mat2[0][1] * mat2[1][0]);
			sign = -sign;
		}
		return ret;
	}
	//The floating point type will have a small deviation from the library implementation, 
	//in addition there is a +-0 problem
	static float MatrixDeterminat(const Matrix4x4f& matrix)
	{
		float ret = 0.F, sign = 1.F;
		for (int i = 0; i < 4; ++i)
		{
			ret += sign * matrix[0][i] * MatrixDeterminat(SubMatrix(matrix, 0, i));
			sign = -sign;
		}
		return ret;
	}

	static const Matrix4x4f& BuildIdentityMatrix()
	{
		static Matrix4x4f identity = { {{
			{ 1.0f, 0.0f, 0.0f, 0.0f},
			{ 0.0f, 1.0f, 0.0f, 0.0f},
			{ 0.0f, 0.0f, 1.0f, 0.0f},
			{ 0.0f, 0.0f, 0.0f, 1.0f}
		}} };
		return identity;
	}

	static void BuildIdentityMatrix(Matrix4x4f& matrix)
	{
		Matrix4x4f identity = { {{
			{ 1.0f, 0.0f, 0.0f, 0.0f},
			{ 0.0f, 1.0f, 0.0f, 0.0f},
			{ 0.0f, 0.0f, 1.0f, 0.0f},
			{ 0.0f, 0.0f, 0.0f, 1.0f}
		}} };
		matrix = identity;
		return;
	}

	static void MatrixInverse(Matrix4x4f& mat)
	{
		Matrix4x4f adj{};
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				auto sub = SubMatrix(mat, i, j);
				adj[i][j] = MatrixDeterminat(sub) * powf(-1.f, static_cast<float>((i + j)));
			}
		}
		float f = MatrixDeterminat(mat);
		if (f == 0) return;
		mat = Transpose(adj);
	}
	//The matrix is first inverted and then transposed to obtain the matrix with the correct transformation normals
	static Matrix4x4f MatrixInversetranspose(Matrix4x4f mat)
	{
		MatrixInverse(mat);
		return Transpose(mat);
	}
	// directx: left f > n > 0		NDC 0~1
	static void BuildOrthographicMatrix(Matrix4x4f& matrix, const float left, const float right, const float top, const float bottom, const float near_plane, const float far_plane)
	{
		const float width = right - left;
		const float height = top - bottom;
		const float depth = far_plane - near_plane;
		matrix = { {{
			{ 2.0f / width, 0.0f, 0.0f, 0.0f },
			{ 0.0f, 2.0f / height, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f / depth, 0.0f },
			{ -(right + left) / width, -(top + bottom) / height, -near_plane / depth, 1.0f }
			} } };
		return;
	}
	static void BuildPerspectiveFovLHMatrix(Matrix4x4f& matrix, const float fieldOfView, const float screenAspect, const float screenNear, const float screenDepth)
	{
		Matrix4x4f perspective = { {{
			{ 1.0f / (screenAspect * tanf(fieldOfView * 0.5f)), 0.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f / tanf(fieldOfView * 0.5f), 0.0f, 0.0f },
			{ 0.0f, 0.0f, screenDepth / (screenDepth - screenNear), 1.0f },
			{ 0.0f, 0.0f, (-screenNear * screenDepth) / (screenDepth - screenNear), 0.0f }
		}} };
		matrix = perspective;
		return;
	}
	static void BuildPerspectiveFovRHMatrix(Matrix4x4f& matrix, const float fieldOfView, const float screenAspect, const float screenNear, const float screenDepth)
	{
		Matrix4x4f perspective = { {{
			{ 1.0f / (screenAspect * tanf(fieldOfView * 0.5f)), 0.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f / tanf(fieldOfView * 0.5f), 0.0f, 0.0f },
			{ 0.0f, 0.0f, screenDepth / (screenNear - screenDepth), -1.0f },
			{ 0.0f, 0.0f, (-screenNear * screenDepth) / (screenDepth - screenNear), 0.0f }
		}} };

		matrix = perspective;
		return;
	}
	static void MatrixTranslation(Matrix4x4f& matrix, const float x, const float y, const float z)
	{
		Matrix4x4f translation = { {{
			{ 1.0f, 0.0f, 0.0f, 0.0f},
			{ 0.0f, 1.0f, 0.0f, 0.0f},
			{ 0.0f, 0.0f, 1.0f, 0.0f},
			{    x,    y,    z, 1.0f}
		}} };
		matrix = translation;
	}
	static Matrix4x4f MatrixTranslation(const float x, const float y, const float z)
	{
		Matrix4x4f translation = { {{
			{ 1.0f, 0.0f, 0.0f, 0.0f},
			{ 0.0f, 1.0f, 0.0f, 0.0f},
			{ 0.0f, 0.0f, 1.0f, 0.0f},
			{    x,    y,    z, 1.0f}
		}} };
		return translation;
	}

	static Matrix4x4f MatrixTranslation(const Vector3f& translation)
	{
		return { {{
			{ 1.0f, 0.0f, 0.0f, 0.0f},
			{ 0.0f, 1.0f, 0.0f, 0.0f},
			{ 0.0f, 0.0f, 1.0f, 0.0f},
			{ translation.x, translation.y, translation.z, 1.0f}
		}} };
	}

	static void MatrixRotationX(Matrix4x4f& matrix, const float radius)
	{
		float c = cosf(radius), s = sinf(radius);
		Matrix4x4f rotation = { {{
			{  1.0f, 0.0f, 0.0f, 0.0f },
			{  0.0f,    c,    s, 0.0f },
			{  0.0f,   -s,    c, 0.0f },
			{  0.0f, 0.0f, 0.0f, 1.0f },
		}} };
		matrix = rotation;
		return;
	}

	static Matrix4x4f MatrixRotationX(const float& radius)
	{
		float c = cosf(radius), s = sinf(radius);
		return { {{
			{  1.0f, 0.0f, 0.0f, 0.0f },
			{  0.0f,    c,    s, 0.0f },
			{  0.0f,   -s,    c, 0.0f },
			{  0.0f, 0.0f, 0.0f, 1.0f },
		}} };
	}

	static void MatrixRotationY(Matrix4x4f& matrix, const float radius)
	{
		float c = cosf(radius), s = sinf(radius);
		Matrix4x4f rotation = { {{
			{    c, 0.0f,   -s, 0.0f },
			{ 0.0f, 1.0f, 0.0f, 0.0f },
			{    s, 0.0f,    c, 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f },
		}} };
		matrix = rotation;
		return;
	}

	static Matrix4x4f MatrixRotationY(const float& radius)
	{
		float c = cosf(radius), s = sinf(radius);
		return { {{
			{    c, 0.0f,   -s, 0.0f },
			{ 0.0f, 1.0f, 0.0f, 0.0f },
			{    s, 0.0f,    c, 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f },
		}} };
	}

	static void MatrixRotationZ(Matrix4x4f& matrix, const float& radius)
	{
		float c = cosf(radius), s = sinf(radius);
		Matrix4x4f rotation = { {{
			{    c,    s, 0.0f, 0.0f },
			{   -s,    c, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f }
		}} };
		matrix = rotation;
		return;
	}

	static Matrix4x4f MatrixRotationZ(const float& radius)
	{
		float c = cosf(radius), s = sinf(radius);
		return { {{
			{    c,    s, 0.0f, 0.0f },
			{   -s,    c, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f }
		}} };;
	}

	static void MatrixRotationAxis(Matrix4x4f& matrix, const Vector3f& axis, const float radius)
	{
		float c = cosf(radius), s = sinf(radius), one_minus_c = 1.0f - c;
		Matrix4x4f rotation = { {{
			{   c + axis.x * axis.x * one_minus_c,  axis.x * axis.y * one_minus_c + axis.z * s, axis.x * axis.z * one_minus_c - axis.y * s, 0.0f    },
			{   axis.x * axis.y * one_minus_c - axis.z * s, c + axis.y * axis.y * one_minus_c,  axis.y * axis.z * one_minus_c + axis.x * s, 0.0f    },
			{   axis.x * axis.z * one_minus_c + axis.y * s, axis.y * axis.z * one_minus_c - axis.x * s, c + axis.z * axis.z * one_minus_c, 0.0f },
			{   0.0f,  0.0f,  0.0f,  1.0f   }
		}} };
		matrix = rotation;
	}

	static void MatrixRotationYawPitchRoll(Matrix4x4f& matrix, const float& yaw, const float& pitch, const float& roll)
	{
		float cYaw, cPitch, cRoll, sYaw, sPitch, sRoll;
		// Get the cosine and sin of the yaw, pitch, and roll.
		cYaw = cosf(yaw);
		cPitch = cosf(pitch);
		cRoll = cosf(roll);
		sYaw = sinf(yaw);
		sPitch = sinf(pitch);
		sRoll = sinf(roll);
		// Calculate the yaw, pitch, roll rotation matrix.
		Matrix4x4f tmp = { {{
			{ (cRoll * cYaw) + (sRoll * sPitch * sYaw), (sRoll * cPitch), (cRoll * -sYaw) + (sRoll * sPitch * cYaw), 0.0f },
			{ (-sRoll * cYaw) + (cRoll * sPitch * sYaw), (cRoll * cPitch), (sRoll * sYaw) + (cRoll * sPitch * cYaw), 0.0f },
			{ (cPitch * sYaw), -sPitch, (cPitch * cYaw), 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f }
		}} };
		matrix = tmp;
		return;
	}
	static Matrix4x4f MatrixRotationYawPitchRoll(const float& yaw, const float& pitch, const float& roll)
	{
		float cYaw, cPitch, cRoll, sYaw, sPitch, sRoll;
		// Get the cosine and sin of the yaw, pitch, and roll.
		cYaw = cosf(yaw);
		cPitch = cosf(pitch);
		cRoll = cosf(roll);
		sYaw = sinf(yaw);
		sPitch = sinf(pitch);
		sRoll = sinf(roll);
		// Calculate the yaw, pitch, roll rotation matrix.
		return{ {{
			{ (cRoll * cYaw) + (sRoll * sPitch * sYaw), (sRoll * cPitch), (cRoll * -sYaw) + (sRoll * sPitch * cYaw), 0.0f },
			{ (-sRoll * cYaw) + (cRoll * sPitch * sYaw), (cRoll * cPitch), (sRoll * sYaw) + (cRoll * sPitch * cYaw), 0.0f },
			{ (cPitch * sYaw), -sPitch, (cPitch * cYaw), 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f }
		}} };
	}

	static Matrix4x4f MatrixRotationYawPitchRoll(const Vector3f& rotation)
	{
		float yaw = ToRadius(rotation.y), pitch = ToRadius(rotation.x), roll = ToRadius(rotation.z);
		float cYaw, cPitch, cRoll, sYaw, sPitch, sRoll;
		// Get the cosine and sin of the yaw, pitch, and roll.
		cYaw = cosf(yaw);
		cPitch = cosf(pitch);
		cRoll = cosf(roll);
		sYaw = sinf(yaw);
		sPitch = sinf(pitch);
		sRoll = sinf(roll);
		// Calculate the yaw, pitch, roll rotation matrix.
		return{ {{
			{ (cRoll * cYaw) + (sRoll * sPitch * sYaw), (sRoll * cPitch), (cRoll * -sYaw) + (sRoll * sPitch * cYaw), 0.0f },
			{ (-sRoll * cYaw) + (cRoll * sPitch * sYaw), (cRoll * cPitch), (sRoll * sYaw) + (cRoll * sPitch * cYaw), 0.0f },
			{ (cPitch * sYaw), -sPitch, (cPitch * cYaw), 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f }
		}} };
	}

	static void MatrixScale(Matrix4x4f& matrix, const float x, const float y, const float z)
	{
		Matrix4x4f scale = { {{
		{    x, 0.0f, 0.0f, 0.0f},
		{ 0.0f,    y, 0.0f, 0.0f},
		{ 0.0f, 0.0f,    z, 0.0f},
		{ 0.0f, 0.0f, 0.0f, 1.0f},
		}} };
		matrix = scale;
	}
	static Matrix4x4f MatrixScale(const float x, const float y, const float z)
	{
		Matrix4x4f scale = { {{
		{    x, 0.0f, 0.0f, 0.0f},
		{ 0.0f,    y, 0.0f, 0.0f},
		{ 0.0f, 0.0f,    z, 0.0f},
		{ 0.0f, 0.0f, 0.0f, 1.0f},
		}} };
		return scale;
	}

	static Matrix4x4f MatrixScale(const Vector3f& scale)
	{
		Matrix4x4f mat = { {{
		{    scale.x, 0.0f, 0.0f, 0.0f},
		{ 0.0f,    scale.y, 0.0f, 0.0f},
		{ 0.0f, 0.0f,	scale.z, 0.0f},
		{ 0.0f, 0.0f, 0.0f, 1.0f},
		}} };
		return mat;
	}

	//https://github.com/apachecn/apachecn-c-cpp-zh/blob/master/docs/handson-cpp-game-ani-prog/04.md
	struct Quaternion
	{
	public:
		inline static float kQuatEpsilon = kFloatEpsilon;
		union
		{
			Vector4f _quat;
			struct { float x, y, z, w; };
			struct { Vector3f v; float s; };
		};
	public:
		Quaternion() : _quat(0.0f, 0.0f, 0.0f, 1.0f) {};
		Quaternion(const Vector4f& quat) : _quat(quat) {};
		Quaternion(float x, float y, float z, float w) : _quat(Vector4f(x, y, z, w)) {};
		Quaternion(Vector3f v, float s) : _quat(Vector4f(v, w)) {};
		friend std::ostream& operator<<(std::ostream& os, const Quaternion& q)
		{
			os << q.x << "," << q.y << "," << q.z << "," << q.w;
			return os;
		}
		Quaternion& operator=(const Quaternion& other)
		{
			memcpy(this,&other,sizeof(Quaternion));
			return *this;
		}
		Quaternion operator+(const Quaternion& other) const { return Quaternion(_quat + other._quat); };
		Quaternion operator-(const Quaternion& other) const { return Quaternion(_quat - other._quat); };
		Quaternion operator*(float scale) const { return Quaternion(_quat * scale); };
		// left mulipty: other * current
		Quaternion operator*(const Quaternion& other) const
		{
			return Quaternion(
				other.x * w + other.y * z - other.z * y + other.w * x,
				-other.x * z + other.y * w + other.z * x + other.w * y,
				other.x * y - other.y * x + other.z * w + other.w * z,
				-other.x * x - other.y * y - other.z * z + other.w * w
			);
		}

		Vector3f operator*(const Vector3f& v) const
		{
			return this->v * 2.0f * DotProduct(this->v, v) +
				v * (this->s * this->s - DotProduct(this->v, this->v)) +
				CrossProduct(this->v, v) * 2.0f * this->s;
		}

		// right mulipty: current * other
		Quaternion operator^(const Quaternion& other) const
		{
			return Quaternion(
				w * other.w - x * other.x - y * other.y - z * other.z,
				w * other.x + x * other.w - y * other.z + z * other.y,
				w * other.y + x * other.z + y * other.w - z * other.x,
				w * other.z - x * other.y + y * other.x + z * other.w
			);
		}

		// right mulipty: current * other
		Quaternion operator^(float f) const
		{
			float angle = 2.0f * acosf(s);
			Vector3f axis = Normalize(v);
			float halfCos = cosf(f * angle * 0.5f);
			float halfSin = sinf(f * angle * 0.5f);
			return Quaternion(axis.x * halfSin, axis.y * halfSin, axis.z * halfSin, halfCos);
		}

		bool operator==(const Quaternion& other) const
		{
			return (fabsf(this->x - other.x) <= kQuatEpsilon &&
				fabsf(this->y - other.y) <= kQuatEpsilon &&
				fabsf(this->z - other.z) <= kQuatEpsilon &&
				fabsf(this->w - other.w) <= kQuatEpsilon);
		}
		bool operator!=(const Quaternion& other) const
		{
			return !(*this == other);
		}

		Quaternion operator-() const
		{
			return Conjugate(*this);
		}

		std::string ToString(int precision = 2) const
		{
			return std::format("({:.{}f},{:.{}f},{:.{}f},{:.{}f})", x, precision, y, precision, z, precision, w, precision);
		}

		void NormalizeQ()
		{
			float lenSq = x * x + y * y + z * z + w * w;
			if (lenSq < kQuatEpsilon) return;
			float i_len = 1.0f / sqrtf(lenSq);
			x *= i_len;
			y *= i_len;
			z *= i_len;
			w *= i_len;
		}


		static Quaternion AngleAxis(float angle, const Vector3f& axis)
		{
			angle = ToRadius(angle);
			Vector3f norm = Normalize(axis);
			float s = sinf(angle * 0.5f);
			return Quaternion(norm.x * s, norm.y * s, norm.z * s, cosf(angle * 0.5f));
		}

		static Quaternion RadiusAxis(float radius, const Vector3f& axis)
		{
			Vector3f norm = Normalize(axis);
			float s = sinf(radius * 0.5f);
			return Quaternion(norm.x * s, norm.y * s, norm.z * s, cosf(radius * 0.5f));
		}

		static Quaternion EulerAngles(float x, float y, float z)
		{
			//return Quaternion::AngleAxis(x, Vector3f::kRight) * Quaternion::AngleAxis(y, Vector3f::kUp) * Quaternion::AngleAxis(z, Vector3f::kForward);
			return Quaternion::AngleAxis(x, Vector3f::kRight) * Quaternion::AngleAxis(z, Vector3f::kForward) * Quaternion::AngleAxis(y, Vector3f::kUp);
		}

		static Quaternion EulerAngles(Vector3f euler)
		{
			euler *= k2Radius;
			double cr = cos(euler.x * 0.5);
			double sr = sin(euler.x * 0.5);
			double cp = cos(euler.y * 0.5);
			double sp = sin(euler.y * 0.5);
			double cy = cos(euler.z * 0.5);
			double sy = sin(euler.z * 0.5);
			Quaternion q;
			q.w = cr * cp * cy + sr * sp * sy;
			q.x = sr * cp * cy - cr * sp * sy;
			q.y = cr * sp * cy + sr * cp * sy;
			q.z = cr * cp * sy - sr * sp * cy;
			return q;
		}

		static Vector3f EulerAngles(const Quaternion& q)
		{
			Vector3f e;
			// pitch (y-axis rotation)
			double sinp = std::sqrt(1 + 2 * (q.w * q.y - q.x * q.z));
			double cosp = std::sqrt(1 - 2 * (q.w * q.y - q.x * q.z));
			e.y = 2 * std::atan2(sinp, cosp) - kHalfPi;

			double sinr_cosp = 2.0 * (q.w * q.x + q.y * q.z);
			double cosr_cosp = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
			e.x = std::atan2(sinr_cosp, cosr_cosp);
			double siny_cosp = 2.0 * (q.w * q.z + q.x * q.y);
			double cosy_cosp = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
			e.z = std::atan2(siny_cosp, cosy_cosp);
			e *= k2Angle;
			return e;
		}

		static Quaternion FromTo(const Vector3f& from, const Vector3f& to)
		{
			Vector3f f = Normalize(from);
			Vector3f t = Normalize(to);
			if (f == t)
				return Quaternion();
			else if (f == t * -1.0f)
			{
				Vector3f ortho = Vector3f(1, 0, 0);
				if (fabsf(f.y) < fabsf(f.x)) {
					ortho = Vector3f(0, 1, 0);
				}
				if (fabsf(f.z) < fabs(f.y) && fabs(f.z) < fabsf(f.x)) {
					ortho = Vector3f(0, 0, 1);
				}
				Vector3f axis = Normalize(CrossProduct(f, ortho));
				return Quaternion(axis.x, axis.y, axis.z, 0);
			}
			Vector3f half = Normalize(f + t);
			Vector3f axis = CrossProduct(f, half);
			return Quaternion(axis.x, axis.y, axis.z, DotProduct(f, half));
		}

		static Vector3f GetAxis(const Quaternion& quat)
		{
			return Normalize(Vector3f(quat.x, quat.y, quat.z));
		}
		static float GetAngle(const Quaternion& quat)
		{
			return 2.0f * acosf(quat.w);
		}

		static float GetAngle(const Quaternion& quat,const Vector3f& axis)
		{
			auto nq = NormalizedQ(quat);
			auto naxis = Normalize(axis);

		}

		static bool IsSameOrientation(const Quaternion& l, const Quaternion& r)
		{
			return (fabsf(l.x - r.x) <= kQuatEpsilon &&
				fabsf(l.y - r.y) <= kQuatEpsilon &&
				fabsf(l.z - r.z) <= kQuatEpsilon &&
				fabsf(l.w - r.w) <= kQuatEpsilon) ||
				(fabsf(l.x + r.x) <= kQuatEpsilon &&
					fabsf(l.y + r.y) <= kQuatEpsilon &&
					fabsf(l.z + r.z) <= kQuatEpsilon &&
					fabsf(l.w + r.w) <= kQuatEpsilon);
		}

		static float Dot(const Quaternion& a, const Quaternion& b)
		{
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		}

		static float LenSq(const Quaternion& q)
		{
			return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
		}

		static float Len(const Quaternion& q)
		{
			float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
			if (lenSq < kQuatEpsilon)
				return 0.0f;
			return sqrtf(lenSq);
		}
		static Quaternion NormalizedQ(const Quaternion& q)
		{
			float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
			if (lenSq < kQuatEpsilon)
				return Quaternion();
			float il = 1.0f / sqrtf(lenSq); // il: inverse length
			return Quaternion(q.x * il, q.y * il, q.z * il, q.w * il);
		}
		//flip axis,replace inverse if quat's len sq is 1
		static Quaternion Conjugate(const Quaternion& q)
		{
			return Quaternion(-q.x, -q.y, -q.z, q.w);
		}

		static Quaternion Inverse(const Quaternion& q)
		{
			float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
			if (lenSq < kQuatEpsilon)
				return Quaternion();
			float recip = 1.0f / lenSq;
			return Quaternion(-q.x * recip, -q.y * recip, -q.z * recip, q.w * recip);
		}

		static Quaternion Identity()
		{
			return Quaternion(0.f, 0.f, 0.f, 1.f);
		}

		static Quaternion Pure(const Quaternion& q)
		{
			return Quaternion(q.v, 0.f);
		}

		//linear blend
		static Quaternion Mix(const Quaternion& from, const Quaternion& to, float t)
		{
			return from * (1.0f - t) + to * t;
		}

		static Quaternion NLerp(const Quaternion& from, const Quaternion& to, float t)
		{
			return NormalizedQ(from + (to - from) * t);
		}

		static Quaternion Slerp(const Quaternion& start, const Quaternion& end, float t)
		{
			if (fabsf(Dot(start, end)) > 1.0f - kQuatEpsilon) {
				return NLerp(start, end, t);
			}
			Quaternion delta = Inverse(start) * end;
			return NormalizedQ((delta ^ t) * start);
		}

		static Quaternion LookRotation(const Vector3f& direction, const Vector3f& up)
		{
			// Find orthonormal basis vectors
			Vector3f f = Normalize(direction); // Object Forward
			Vector3f u = Normalize(up); // Desired Up
			Vector3f r = CrossProduct(u, f); // Object Right
			u = CrossProduct(f, r); // Object Up
			// From world forward to object forward
			Quaternion worldToObject = FromTo(Vector3f(0, 0, 1), f);
			// what direction is the new object up?
			Vector3f objectUp = worldToObject * Vector3f(0, 1, 0);
			// From object up to desired up
			Quaternion u2u = FromTo(objectUp, u);
			// Rotate to forward direction first
			// then twist to correct up
			Quaternion result = worldToObject * u2u;
			// Don't forget to normalize the result
			return NormalizedQ(result);
		}

		Matrix4x4f ToMat4f() const
		{
			Vector3f r = *this * Vector3f(1, 0, 0);
			Vector3f u = *this * Vector3f(0, 1, 0);
			Vector3f f = *this * Vector3f(0, 0, 1);
			return { {{
			{    r.x, r.y, r.z, 0.f},
			{ u.x, u.y, u.z, 0.f},
			{ f.x, f.y, f.z, 0.f},
			{ 0.0f, 0.0f, 0.0f, 1.0f},
			}} };
		}

		static Matrix4x4f ToMat4f(const Quaternion& q)
		{
			Vector3f r = q * Vector3f(1, 0, 0);
			Vector3f u = q * Vector3f(0, 1, 0);
			Vector3f f = q * Vector3f(0, 0, 1);
			return { {{
			{    r.x, r.y, r.z, 0.f},
			{ u.x, u.y, u.z, 0.f},
			{ f.x, f.y, f.z, 0.f},
			{ 0.0f, 0.0f, 0.0f, 1.0f},
			}} };
		}

		static Quaternion FromMat4f(const Matrix4x4f& m)
		{
			Vector3f up = Normalize(Vector3f(m[1][0], m[1][1], m[1][2]));
			Vector3f forward = Normalize(
				Vector3f(m[2][0], m[2][1], m[2][2]));
			Vector3f right = CrossProduct(up, forward);
			up = CrossProduct(forward, right);
			return LookRotation(forward, up);
		}
	};

	static void MatrixRotationQuaternion(Matrix4x4f& matrix, Quaternion q)
	{
		Matrix4x4f rotation = { {{
			{   1.0f - 2.0f * q.y * q.y - 2.0f * q.z * q.z,  2.0f * q.x * q.y + 2.0f * q.w * q.z,   2.0f * q.x * q.z - 2.0f * q.w * q.y,    0.0f    },
			{   2.0f * q.x * q.y - 2.0f * q.w * q.z,    1.0f - 2.0f * q.x * q.x - 2.0f * q.z * q.z, 2.0f * q.y * q.z + 2.0f * q.w * q.x,    0.0f    },
			{   2.0f * q.x * q.z + 2.0f * q.w * q.y,    2.0f * q.y * q.z - 2.0f * q.y * q.z - 2.0f * q.w * q.x, 1.0f - 2.0f * q.x * q.x - 2.0f * q.y * q.y, 0.0f    },
			{   0.0f,   0.0f,   0.0f,   1.0f    }
		}} };
		matrix = rotation;
	}

	template<typename V>
	static V lerp(const V& src, const V& des, const float& weight)
	{
		V res{};
		for (u32 i = 0; i < CountOf(src.data); i++)
		{
			res[i] = (1.f - weight) * src[i] + weight * des[i];
		}
		return res;
	}
	template<float>
	static float lerp(const float& src, const float& des, const float& weight)
	{
		return (1.f - weight) * src + weight * des;
	}

	static float lerpf(const float& src, const float& des, const float& weight)
	{
		return (1.f - weight) * src + weight * des;
	}

	static Matrix<float, 8, 8> DCT8X8(const Matrix<int32_t, 8, 8>& in_mat)
	{

		Matrix<float, 8, 8> out_mat{};
		for (int u = 0; u < 8; ++u)
		{
			for (int v = 0; v < 8; ++v)
			{
				float var = 0.f;
				for (int x = 0; x < 8; ++x)
				{
					for (int y = 0; y < 8; ++y)
					{
						var += in_mat[x][y] * cos((2.f * x + 1.f) * u * Pi_over_sixteen) * cos((2.f * y + 1.f) * v * Pi_over_sixteen);
					}
				}
				out_mat[u][v] = one_over_four * NormalizeScaleFactor(u) * NormalizeScaleFactor(v) * var;
			}
		}
		return out_mat;
	}


	static Matrix<int32_t, 8, 8> IDCT8X8(const Matrix<int32_t, 8, 8>& in_mat)
	{
		Matrix<int32_t, 8, 8> out_mat{};
		for (int x = 0; x < 8; ++x)
		{
			for (int y = 0; y < 8; ++y)
			{
				float var = 0;
				for (int u = 0; u < 8; ++u)
				{
					for (int v = 0; v < 8; ++v)
					{
						var += NormalizeScaleFactor(u) * NormalizeScaleFactor(v) * in_mat[u][v] *
							cos((2.f * x + 1.f) * u * Pi_over_sixteen) * cos((2.f * y + 1.f) * v * Pi_over_sixteen);
					}
					out_mat[x][y] = roundf(one_over_four * var);
				}
			}
		}
		return out_mat;
	}
#pragma warning(pop)
	using Color = Vector4D<float>;
	using Color32 = Vector4D<u8>;
	namespace Colors
	{
		static const Color kBlue = { 0.f,0.f,1.f ,1.0f };
		static const Color kRed = { 1.f,0.f,0.f ,1.0f };
		static const Color kGreen = { 0.f,1.f,0.f ,1.0f };
		static const Color kWhite = { 1.f,1.f,1.f ,1.0f };
		static const Color kBlack = { 0.f,0.f,0.f ,1.0f };
		static const Color kGray = { 0.3f,0.3f,0.3f ,1.0f };
		static const Color kYellow = { 1.0f,1.0f,0.0f ,1.0f };
	}

	namespace ALHash
	{
		struct Vector2fHash
		{
			inline std::size_t operator()(const Vector2f& v) const
			{
				std::size_t h1 = std::hash<float>{}(v.x);
				std::size_t h2 = std::hash<float>{}(v.y);
				return h1 ^ (h2 << 1);
			}
		};

		struct Vector2Equal
		{
			inline bool operator()(const Vector2f& v1, const Vector2f& v2) const
			{
				return v1.x == v2.x && v1.y == v2.y;
			}
		};

		struct Vector3fHash
		{
			inline std::size_t operator()(const Vector3f& v) const
			{
				std::size_t h1 = std::hash<float>{}(v.x);
				std::size_t h2 = std::hash<float>{}(v.y);
				std::size_t h3 = std::hash<float>{}(v.z);
				return h1 ^ (h2 << 1) ^ (h3 << 2);
			}
		};

		struct Vector3Equal
		{
			inline bool operator()(const Vector3f& v1, const Vector3f& v2) const
			{
				return v1.x == v2.x && v1.y == v2.y && v1.z == v2.z;
			}
		};

		struct Vector4fHash
		{
			inline std::size_t operator()(const Vector4f& v) const
			{
				std::size_t h1 = std::hash<float>{}(v.x);
				std::size_t h2 = std::hash<float>{}(v.y);
				std::size_t h3 = std::hash<float>{}(v.z);
				std::size_t h4 = std::hash<float>{}(v.w);
				return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
			}
		};

		struct Vector4Equal
		{
			inline bool operator()(const Vector4f& v1, const Vector4f& v2) const
			{
				return v1.x == v2.x && v1.y == v2.y && v1.z == v2.z && v1.w == v2.w;
			}
		};

		template<template<typename> typename TT, typename T>
		struct VectorHash
		{
			inline std::size_t operator()(const TT<T>& v) const
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
			inline bool operator()(const TT<T>& v1, const TT<T>& v2) const
			{
				bool is_equal = true;
				for (u32 i = 1; i < CountOf(v1.data); i++)
				{
					is_equal &= v1[i] == v2[i];
				}
				return is_equal;
			}
		};

		static inline std::size_t CombineHashes(const std::size_t& hash1, const std::size_t& hash2)
		{
			return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
		}

		template<u8 Size>
		class Hash
		{
		public:
			Hash()
			{
				_hash.reset();
			}
			void Set(u8 pos, u8 size, u32 value)
			{
				if (pos + size > Size)
				{
					throw std::out_of_range("Invalid position and size for Set operation");
				}
				while (size--)
				{
					_hash.set(pos++, value & 1);
					value >>= 1;
				}
			}

			u32 Get(u8 pos, u8 size) const
			{
				if (pos + size > Size)
				{
					throw std::out_of_range("Invalid position and size for Get operation");
				}
				std::bitset<32> result;
				for (u8 i = 0; i < size; ++i)
				{
					result.set(i, _hash.test(pos + i));
				}
				return static_cast<u32>(result.to_ulong());
			}

			bool operator==(const Hash<Size>& other) const
			{
				return _hash == other._hash;
			}
			bool operator<(const Hash<Size>& other) const
			{
				return _hash.to_ullong() < other._hash.to_ullong();
			}
			String ToString() const
			{
				return std::format("{}", _hash.to_ullong());
			}
		private:
			std::bitset<Size> _hash;
		};

		template<typename T>
		static u32 Hasher(const T& obj)
		{
			throw std::runtime_error("Unhandled type whth hasher");
			return 0;
		}

		template<typename T>
		static u32 CommonRuntimeHasher(const T& obj)
		{
			static Vector<T> hashes;
			for (u32 i = 0; i < hashes.size(); i++)
			{
				if (hashes[i] == obj)
					return i;
			}
			hashes.emplace_back(obj);
			return static_cast<u32>(hashes.size() - 1);
		}
	}
}

#endif // __AL_MATH_H__

