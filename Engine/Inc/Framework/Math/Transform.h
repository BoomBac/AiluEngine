#pragma once
#ifndef __TRANSFORM_H__
#define __TRANSFORM_H__
#include "ALMath.hpp"
#include "Framework/Common/Utils.h"
#include "GlobalMarco.h"
#include "Objects/Serialize.h"


namespace Ailu
{
    //https://www.andre-gaschler.com/rotationconverter/
    struct Transform
    {
    public:
        Vector3f _position;
        Quaternion _rotation;
        Vector3f _scale;

        static Matrix4x4f ToMatrix(const Transform &t);

        static void ToMatrix(const Transform &transform, Matrix4x4f &out_matrix);

        //expensive! don't use realtime!
        static Transform FromMatrix(const Matrix4x4f &m);
        //transform b then a
        static Transform Combine(const Transform &a, const Transform &b);

        static Transform Inverse(const Transform &t);

        static Transform Mix(const Transform &a, const Transform &b, float t);

        static Vector3f TransformPoint(const Transform &a, const Vector3f &b);

        static Vector3f TransformVector(const Transform &a, const Vector3f &b);

        static Transform LocalInverse(const Transform &t);

    public:
        Transform() : _position(Vector3f::kZero), _rotation(Quaternion()), _scale(Vector3f::kOne) {}
        Transform(const Vector3f &p, const Quaternion &r, const Vector3f &s) : _position(p), _rotation(r), _scale(s) {}
    };

    static Archive &operator<<(Archive &ar, Transform &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_position:" << c._position.ToString(9) << std::endl;
        ar.InsertIndent();
        ar << "_rotation:" << c._rotation.ToString(9) << std::endl;
        ar.InsertIndent();
        ar << "_scale:" << c._scale.ToString(9);
        ar.DecreaseIndent();
        ar.NewLine();
        return ar;
    }
    static Archive &operator>>(Archive &ar, Transform &c)
    {
        Array<String, 3> bufs;
        ar >> bufs[0] >> bufs[1] >> bufs[2];
        AL_ASSERT(su::BeginWith(bufs[0], "_position"));
        c._position.FromString(su::Split(bufs[0], ":")[1]);
        c._rotation.FromString(su::Split(bufs[1], ":")[1]);
        c._scale.FromString(su::Split(bufs[2], ":")[1]);
        return ar;
    }

}// namespace Ailu


#endif// !TRANSFORM_H__
