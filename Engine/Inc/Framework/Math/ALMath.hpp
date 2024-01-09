#ifndef __AL_MATH_H__
#define __AL_MATH_H__

#include <math.h>
#include <sstream>
#include <string>
#include <limits>
#include <format>
#include <limits>
#include <bitset>
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
		constexpr float kDPi = kPi * 2.f;
		constexpr float one_over_four = 1.f / 4.f;
		constexpr float Pi_over_sixteen = kPi / 16.f;

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
	//	for (uint32_t i = 0; i < len; i++)
	//		sum += pow(var[i], 2.f);
	//	sum = sqrt(sum);
	//	for (uint32_t i = 0; i < len; i++) {
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
		for (uint32_t i = 0; i < len; i++)
		{
			sum += pow(var[i], 2.f);
			temp[i] = var[i];
		}
		sum = sqrt(sum);
		for (uint32_t i = 0; i < len; i++) {
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
		static const Vector2D<T> Zero;
		static const Vector2D<T> One;
		union
		{
			T data[2];
			struct { T x, y; };
			struct { T r, g; };
			struct { T u, v; };
			Swizzle<Vector2D, T, 0, 1> xy;
			Swizzle<Vector2D, T, 1, 0> yx;
		};
		Vector2D<T>() {};
		Vector2D<T>(const T& v) : x(v), y(v) {};
		Vector2D<T>(const T& v, const T& w) : x(v), y(w) {};
		operator T* () { return data; };
		operator const T* () { return static_cast<const T*>(data); };
		T& operator[](uint32_t index) { return data[index];}
		const T& operator[](uint32_t index) const{ return data[index];}

		Vector2D<T>& operator+=(const Vector2D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] += other.data[i];
			}
			return *this;
		}

		Vector2D<T>& operator-=(const Vector2D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] -= other.data[i];
			}
			return *this;
		}

		Vector2D<T>& operator*=(const Vector2D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] *= other.data[i];
			}
			return *this;
		}

		Vector2D<T>& operator/=(const Vector2D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				//AL_ASSERT(other.data[i] == 0.f, "vector divide by zero commpoent!")
					data[i] /= other.data[i];
			}
			return *this;
		}
	};
	template <typename T>
	const Vector2D<T> Vector2D<T>::Zero(0, 0);
	template <typename T>
	const Vector2D<T> Vector2D<T>::One(1, 1);
	using Vector2f = Vector2D<float>;

	template <typename T>
	struct Vector3D
	{
		static const Vector3D<T> Zero;
		static const Vector3D<T> One;
		static const Vector3D<T> kForward;
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

		Vector3D<T>() {};
		Vector3D<T>(const T& _v) : x(_v), y(_v), z(_v) {};
		Vector3D<T>(const T& _x, const T& _y, const T& _z) : x(_x), y(_y), z(_z) {};

		Vector3D<T>& operator=(const Vector3D<T>& other)
		{
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] = other.data[i];
			}
			return *this;
		}

		Vector3D<T>& operator+=(const Vector3D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] += other.data[i];
			}
			return *this;
		}

		Vector3D<T>& operator-=(const Vector3D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] -= other.data[i];
			}
			return *this;
		}

		Vector3D<T>& operator*=(const Vector3D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] *= other.data[i];
			}
			return *this;
		}

		Vector3D<T>& operator*=(const float& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] *= other;
			}
			return *this;
		}

		Vector3D<T>& operator/=(const Vector3D<T>& other) {
			try
			{
				for (uint8_t i = 0; i < CountOf(data); i++) {
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
		friend std::ostream& operator<<(std::ostream& os, const Vector3D<T>& vec)
		{
			os << vec.x << "," << vec.y << "," << vec.z;
			return os;
		}

		operator T* () { return data; };
		operator const T* () const { return static_cast<const T*>(data); };
		T& operator[](uint32_t index) { return data[index]; }
		const T& operator[](uint32_t index) const { return data[index]; }

		std::string ToString(int precision = 2) 
		{
			return std::format("({:.{}f},{:.{}f},{:.{}f})", data[0], precision, data[1], precision, data[2], precision);
		}
	};
	template <typename T>
	const Vector3D<T> Vector3D<T>::Zero(0, 0, 0);
	template <typename T>
	const Vector3D<T> Vector3D<T>::One (1, 1, 1);
	template <typename T>
	const Vector3D<T> Vector3D<T>::kForward(0, 0, -1);

	using Vector3f = Vector3D<float>;

	template <typename T>
	struct Vector4D
	{
		static const Vector4D<T> Zero;
		static const Vector4D<T> One;
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

		Vector4D<T>() { x = 0; y = 0; z = 0; w = 0; };
		Vector4D<T>(const T& _v) : x(_v), y(_v), z(_v), w(_v) {};
		Vector4D<T>(const T& _x, const T& _y, const T& _z, const T& _w) : x(_x), y(_y), z(_z), w(_w) {};
		Vector4D<T>(const Vector3D<T>& v3) : x(v3.x), y(v3.y), z(v3.z), w(1.0f) {};
		Vector4D<T>(const Vector3D<T>& v3, const T& _w) : x(v3.x), y(v3.y), z(v3.z), w(_w) {};

		operator T* () { return data; };
		operator const T* () const { return static_cast<const T*>(data); };
		T& operator[](uint32_t index) { return data[index]; }
		const T& operator[](uint32_t index) const { return data[index]; }

		friend std::ostream& operator<<(std::ostream& os, const Vector4D<T>& vec)
		{
			os << vec.x << "," << vec.y << "," << vec.z << "," << vec.w;
			return os;
		}
		Vector4D<T>& operator+=(const Vector4D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] += other.data[i];
			}
			return *this;
		}

		Vector4D<T>& operator-=(const Vector4D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] -= other.data[i];
			}
			return *this;
		}

		Vector4D<T>& operator*=(const Vector4D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) {
				data[i] *= other.data[i];
			}
			return *this;
		}

		Vector4D<T>& operator/=(const Vector4D<T>& other) {
			for (uint8_t i = 0; i < CountOf(data); i++) 
			{
				if (other.data[i] == 0.f)
				{
					throw(std::runtime_error("vector divide by zero commpoent!"));
				}
				data[i] /= other.data[i];
			}
			return *this;
		}
	};
	template <typename T>
	const Vector4D<T> Vector4D<T>::Zero(0, 0, 0, 0);
	template <typename T>
	const Vector4D<T> Vector4D<T>::One(1, 1, 1, 1);

	using Vector4f = Vector4D<float>;
	using Quaternion = Vector4D<float>;
	using R8G8B8A8Unorm = Vector4D<uint8_t>;
	using R8G8B8Unorm = Vector3D<uint8_t>;
	using R32G32B32Float = Vector3D<float>;
	using R32G32B32A32Float = Vector3D<float>;
	using Vector4i = Vector4D<uint8_t>;

	template<template<typename> class TT, typename T>
	static float Distance(const TT<T>& from, const TT<T>& to)
	{
		float dis = 0;
		for (uint32_t i = 0; i < CountOf(from.data); i++)
		{
			dis += (to.data[i] - from.data[i]) * (to.data[i] - from.data[i]);
		}
		return sqrt(dis);
	}

	template<template<typename> class TT, typename T>
	static bool LoadVector(const char* vec_str, const TT<T>& out_v)
	{
#pragma warning(push)
#pragma warning(disable: 4477)
		int length =  CountOf(out_v.data);
		if (std::is_same<T, float>::value)
		{
			if (length == 3) return sscanf_s(vec_str, "%f,%f,%f", &out_v.x, &out_v.y, &out_v.z) == 3;
			else if (length == 4) return sscanf_s(vec_str, "%f,%f,%f,%f", &out_v.x, &out_v.y, &out_v.z, &out_v.data[3]) == 4;
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
		return sscanf_s(f_str, "%f", &tmp) == 1? tmp : 0.0f;
	}



	inline static float ToRadius(const float& angle) { return angle * kPi / 180.f; }
	inline static float ToAngle(const float& radius) { return 180.f * radius / kPi; }

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
		for (uint32_t i = 0; i < CountOf(v1.data); i++)
		{
			res.data[i] = v1.data[i] + v2.data[i];
		}
		return res;
	}

	template<template<typename> typename TT, typename T>
	TT<T> operator*(const T& scalar, const TT<T>& v)
	{
		TT<T> res;
		for (uint32_t i = 0; i < CountOf(v.data); i++)
		{
			res.data[i] = scalar * v.data[i];
		}
		return res;
	}
	template<template<typename> typename TT, typename T>
	TT<T> operator*(const TT<T>& v, const T& scalar)
	{
		return scalar * v;
	}
	template<template<typename> typename TT, typename T>
	TT<T> operator+(const T& scalar, const TT<T>& v)
	{
		TT<T> res{};
		for (uint32_t i = 0; i < CountOf(v.data); i++)
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
		for (uint32_t i = 0; i < CountOf(v.data); i++)
		{
			v[i] = -v[i];
		}
		return v;
	}
	template<template<typename> typename TT, typename T>
	TT<T> operator-(const TT<T> v1, const TT<T> v2)
	{
		TT<T> res;
		for (uint32_t i = 0; i < CountOf(v1.data); i++)
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
		for (uint32_t i = 0; i < CountOf(first.data); i++)
		{
			res += first.data[i] * second.data[i];
		}
		return res;
	}
	/// <summary>
	/// Only 3D vectors can be input for now
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
		for (uint8_t i = 0; i < CountOf(first.data); i++)
		{
			max.data[i] = first.data[i] > second.data[i] ? first.data[i] : second.data[i];
		}
		return max;
	}

	template<template<typename> typename TT, typename T>
	static TT<T> Min(const TT<T>& first, const TT<T>& second)
	{
		TT<T> min{};
		for (uint8_t i = 0; i < CountOf(first.data); i++)
		{
			min.data[i] = first.data[i] < second.data[i] ? first.data[i] : second.data[i];
		}
		return min;
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
		operator T* () { return &data[0][0]; };
		operator const T* () const { return static_cast<const T*>(&data[0][0]); };
	};
	using Matrix4x4f = Matrix<float, 4, 4>;
	using Matrix3x3f = Matrix<float, 3, 3>;

	template<typename T, int rows, int cols>
	std::string MatrixToString(Matrix<T, rows, cols> mat)
	{
		std::stringstream ss;
		for (uint32_t i = 0; i < rows; i++)
		{
			ss << "[ ";
			for (uint32_t j = 0; j < cols; j++)
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
		for (uint32_t i = 0; i < CountOf(m1.data); i++)
		{
			for (uint32_t j = 0; j < CountOf(m2.data); j++)
			{
				ret[i][j] = m1[i][j] + m2[i][j];
			}
		}
	}

	template<typename T, int rows, int cols>
	Matrix<T, rows, cols> operator+(const Matrix<T, rows, cols>& m1, const Matrix<T, rows, cols>& m2)
	{
		Matrix<T, rows, cols> ret;
		for (uint32_t i = 0; i < CountOf(m1.data); i++)
		{
			for (uint32_t j = 0; j < CountOf(m2.data); j++)
			{
				ret[i][j] = m1[i][j] + m2[i][j];
			}
		}
		return ret;
	}

	template<typename T, int rows, int cols>
	void MatrixSub(Matrix<T, rows, cols>& ret, const Matrix<T, rows, cols>& m1, const Matrix<T, rows, cols>& m2)
	{
		for (uint32_t i = 0; i < CountOf(m1.data); i++)
		{
			for (uint32_t j = 0; j < CountOf(m2.data); j++)
			{
				ret[i][j] = m1[i][j] - m2[i][j];
			}
		}
	}

	template<typename T, int rows, int cols>
	Matrix<T, rows, cols> operator-(const Matrix<T, rows, cols>& m1, const Matrix<T, rows, cols>& m2)
	{
		Matrix<T, rows, cols> ret;
		for (uint32_t i = 0; i < CountOf(m1.data); i++)
		{
			for (uint32_t j = 0; j < CountOf(m2.data); j++)
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
		for (uint32_t i = 0; i < rows; i++)
		{
			for (uint32_t j = 0; j < cols; j++)
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
		for (uint32_t i = 0; i < rows; i++)
		{
			for (uint32_t j = 0; j < cols; j++)
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
		for (uint32_t i = 0; i < Da; i++)
		{
			for (uint32_t j = 0; j < Dc; j++)
			{
				for (uint32_t k = 0; k < Dc; k++)
					ret[i][j] += m1[j][k] * m2[k][i];
			}
		}
		Transpose(ret);
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

	static void TransformVector(Vector4f& vector, const Matrix4x4f& matrix)
	{
		Vector4f temp{};
		//for (uint32_t i = 0; i < 4; i++)
		//{
		//	for (uint32_t j = 0; j < 4; j++)
		//	{
		//		temp[j] += vector[i] * matrix[i][j];
		//	}
		//}
		temp.x = vector.x * matrix[0][0] + vector.y * matrix[1][0] + vector.z* matrix[2][0] +  vector.w* matrix[3][0];
		temp.y = vector.x * matrix[0][1] + vector.y * matrix[1][1] + vector.z* matrix[2][1] +  vector.w* matrix[3][1];
		temp.z = vector.x * matrix[0][2] + vector.y * matrix[1][2] + vector.z* matrix[2][2] +  vector.w* matrix[3][2];
		temp.w = vector.x * matrix[0][3] + vector.y * matrix[1][3] + vector.z* matrix[2][3] +  vector.w* matrix[3][3];

		//temp.x = vector.x * matrix[0][0] + vector.y * matrix[0][1] + vector.z * matrix[0][2] + vector.w * matrix[0][3];
		//temp.y = vector.x * matrix[1][0] + vector.y * matrix[1][1] + vector.z * matrix[1][2] + vector.w * matrix[1][3];
		//temp.z = vector.x * matrix[2][0] + vector.y * matrix[2][1] + vector.z * matrix[2][2] + vector.w * matrix[2][3];
		//temp.w = vector.x * matrix[3][0] + vector.y * matrix[3][1] + vector.z * matrix[3][2] + vector.w * matrix[3][3];
		//for (uint32_t i = 0; i < 4; i++) {
		//	for (uint32_t j = 0; j < 4; j++) {
		//		temp[i] += matrix[i][j] * vector[j];
		//	}
		//}
		vector = temp;
		return;
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
		vector[0] = temp[0];
		vector[1] = temp[1];
		vector[2] = temp[2];
		return;
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
		for (uint32_t i = 0; i < CountOf(src.data); i++)
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
	using Color32 = Vector4D<uint8_t>;
	namespace Colors
	{
		static const Color kBlue = { 0.f,0.f,1.f ,1.0f};
		static const Color kRed = { 1.f,0.f,0.f ,1.0f };
		static const Color kGreen = { 0.f,1.f,0.f ,1.0f };
		static const Color kWhite = { 1.f,1.f,1.f ,1.0f };
		static const Color kBlack = { 0.f,0.f,0.f ,1.0f };
		static const Color kGray = { 0.3f,0.3f,0.3f ,1.0f};
		static const Color kYellow = { 1.0f,1.0f,0.0f ,1.0f};
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

		static inline std::size_t CombineHashes(const std::size_t& hash1, const std::size_t& hash2)
		{
			return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
		}

		template<uint8_t Size>
		class Hash
		{
		public:
			Hash()
			{
				_hash.reset();
			}
			void Set(uint8_t pos, uint8_t size, uint32_t value)
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

			uint32_t Get(uint8_t pos, uint8_t size) const
			{
				if (pos + size > Size)
				{
					throw std::out_of_range("Invalid position and size for Get operation");
				}
				std::bitset<32> result;
				for (uint8_t i = 0; i < size; ++i)
				{
					result.set(i, _hash.test(pos + i));
				}
				return static_cast<uint32_t>(result.to_ulong());
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
				return std::format("{}",_hash.to_ullong());
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

