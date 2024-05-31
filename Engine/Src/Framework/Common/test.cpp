//#include "pch.h"
#define AILU_API __declspec(dllexport)
#include "Framework/Math/ALMath.hpp"
#include <queue>

//template<typename T, size_t size_of_arr>
//constexpr uint32_t CountOf(T(&)[size_of_arr]) { return (uint32_t)size_of_arr; }
//
//template<typename T, size_t row, size_t col>
//constexpr uint32_t CountOf(T(&)[row][col]) { return (uint32_t)(row * col); }
//
//template<template<typename> typename TT, typename T>
//TT<T> operator-(const TT<T> v1, const TT<T> v2)
//{
//	TT<T> res;
//	for (uint32_t i = 0; i < CountOf(v1.data); i++)
//	{
//		res.data[i] = v1.data[i] - v2.data[i];
//	}
//	return res;
//}

class AILU_API BC
{
};
class AILU_API SceneMgr
{
public:
private:
	inline static uint16_t s_scene_index = 0u;
	std::queue<BC*> _pending_delete_actors;
};