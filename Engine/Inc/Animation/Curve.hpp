//
// Created by 22292 on 2024/10/12.
//

#ifndef AILU_CURVE_HPP
#define AILU_CURVE_HPP
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
namespace Ailu
{
    DECLARE_ENUM(EInterpolationType,kConstant,kLinear,kCubic)
    template<typename T>
    class Bezier
    {
    public:
        T P1;// Point 1
        T C1;// Control 1
        T P2;// Point 2
        T C2;// Control 2
        template<typename T>
        static T Interpolate(const Bezier<T> &curve, f32 t)
        {
            //乘数2被称为基函数
            return curve.P1 * ((1 - t) * (1 - t) * (1 - t)) +
                   curve.C1 * (3.0f * ((1 - t) * (1 - t)) * t) +
                   curve.C2 * (3.0f * (1 - t) * (t * t)) +
                   curve.P2 * (t * t * t);
        }
    };
    //埃尔米特样条线使用两个点+对应的切线
    template<typename T>
    struct Hermite
    {
    public:
        T _p1;// Point 1
        T _s1;// Tangent 1
        T _p2;// Point 2
        T _s2;// Tangent 2
     template<typename T>
     static T Interpolate(const Hermite<T> &curve ,f32 t)
     {
         return curve._p1 * ((1.0f + 2.0f * t) * ((1.0f - t) * (1.0f - t))) +
                curve._s1 * (t * ((1.0f - t) * (1.0f - t))) +
                curve._p2 * ((t * t) * (3.0f - 2.0f * t)) +
                curve._s2 * ((t * t) * (t - 1.0f));
     }
    };
}// namespace Ailu
#endif//AILU_CURVE_HPP
