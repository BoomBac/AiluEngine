#include "Framework/Math/Transform.h"
#include "Framework/Math/QuaternionMatrix.h"
#include "pch.h"

namespace Ailu
{
    Matrix4x4f Transform::ToMatrix(const Transform &t)
    {
        Matrix4x4f m{};
        m[3][0] = t._position.x;
        m[3][1] = t._position.y;
        m[3][2] = t._position.z;
        const f32 x2 = t._rotation.x + t._rotation.x;
        const f32 y2 = t._rotation.y + t._rotation.y;
        const f32 z2 = t._rotation.z + t._rotation.z;
        {
            const f32 xx2 = t._rotation.x * x2;
            const f32 yy2 = t._rotation.y * y2;
            const f32 zz2 = t._rotation.z * z2;
            m[0][0] = (1.0f - (yy2 + zz2)) * t._scale.x;
            m[1][1] = (1.0f - (xx2 + zz2)) * t._scale.y;
            m[2][2] = (1.0f - (xx2 + yy2)) * t._scale.z;
        }
        {
            const f32 yz2 = t._rotation.y * z2;
            const f32 wx2 = t._rotation.w * x2;
            m[2][1] = (yz2 - wx2) * t._scale.z;
            m[1][2] = (yz2 + wx2) * t._scale.y;
        }
        {
            const f32 xy2 = t._rotation.x * y2;
            const f32 wz2 = t._rotation.w * z2;

            m[1][0] = (xy2 - wz2) * t._scale.y;
            m[0][1] = (xy2 + wz2) * t._scale.z;
        }
        {
            const f32 xz2 = t._rotation.x * z2;
            const f32 wy2 = t._rotation.w * y2;

            m[2][0] = (xz2 + wy2) * t._scale.z;
            m[0][2] = (xz2 - wy2) * t._scale.x;
        }
        m[0][3] = 0.0f;
        m[1][3] = 0.0f;
        m[2][3] = 0.0f;
        m[3][3] = 1.0f;
        return m;
        // Vector3f x = transform._rotation * Vector3f(1, 0, 0);
        // Vector3f y = transform._rotation * Vector3f(0, 1, 0);
        // Vector3f z = transform._rotation * Vector3f(0, 0, 1);
        // x = x * transform._scale.x;// Vector * float
        // y = y * transform._scale.y;// Vector * float
        // z = z * transform._scale.z;// Vector * float
        // Vector3f t = transform._position;
        // return {{{
        //         {x.x, x.y, x.z, 0},// X basis (& Scale)
        //         {y.x, y.y, y.z, 0},// Y basis (& scale)
        //         {z.x, z.y, z.z, 0},// Z basis (& scale)
        //         {t.x, t.y, t.z, 1} // Position
        // }}};
    }

    void Transform::ToMatrix(const Transform &transform, Matrix4x4f &out_matrix)
    {
        const auto &mat = ToMatrix(transform);
        out_matrix = mat;
        // Vector3f x = transform._rotation * Vector3f(1, 0, 0);
        // Vector3f y = transform._rotation * Vector3f(0, 1, 0);
        // Vector3f z = transform._rotation * Vector3f(0, 0, 1);
        // x = x * transform._scale.x;// Vector * float
        // y = y * transform._scale.y;// Vector * float
        // z = z * transform._scale.z;// Vector * float
        // Vector3f t = transform._position;
        // out_matrix.SetRow(0, Vector4f{x, 0.f});
        // out_matrix.SetRow(1, Vector4f{y, 0.f});
        // out_matrix.SetRow(2, Vector4f{z, 0.f});
        // out_matrix.SetRow(3, Vector4f{t, 1.f});
    }

    //expensive! don't use realtime!
    Transform Transform::FromMatrix(const Matrix4x4f &m)
    {
        Transform out;
        out._position = Vector3f(m[3][0], m[3][1], m[3][2]);
        out._rotation = Quaternion::FromMat4f(m);
        Matrix4x4f rotScaleMat{{{{m[0][0], m[0][1], m[0][2], 0},
                                 {m[1][0], m[1][1], m[1][2], 0},
                                 {m[2][0], m[2][1], m[2][2], 0},
                                 {0, 0, 0, 1}}}};
        Matrix4x4f invRotMat = Quaternion::ToMat4f(Quaternion::Inverse(out._rotation));
        Matrix4x4f scaleSkewMat = rotScaleMat * invRotMat;
        out._scale = Vector3f(scaleSkewMat[0][0], scaleSkewMat[1][1], scaleSkewMat[2][2]);
        return out;
    }
    //transform b then a
    Transform Transform::Combine(const Transform &a, const Transform &b)
    {
        Transform out;
        out._scale = a._scale * b._scale;
        out._rotation = b._rotation * a._rotation;
        out._position = a._rotation * (a._scale * b._position);
        out._position = a._position + out._position;
        return out;
    }

    Transform Transform::Inverse(const Transform &t)
    {
        Transform inv;
        inv._rotation = Quaternion::Inverse(t._rotation);
        inv._scale.x = fabs(t._scale.x) < Math::kFloatEpsilon ? 0.0f : 1.0f / t._scale.x;
        inv._scale.y = fabs(t._scale.y) < Math::kFloatEpsilon ? 0.0f : 1.0f / t._scale.y;
        inv._scale.z = fabs(t._scale.z) < Math::kFloatEpsilon ? 0.0f : 1.0f / t._scale.z;
        Vector3f invTrans = t._position * -1.0f;
        inv._position = inv._rotation * (inv._scale * invTrans);
        return inv;
    }

    Transform Transform::Mix(const Transform &a, const Transform &b, float t)
    {
        Quaternion bRot = b._rotation;
        if (Quaternion::Dot(a._rotation, bRot) < 0.0f)
        {
            bRot = -bRot;
        }
        return Transform(
                Lerp(a._position, b._position, t),
                Quaternion::NLerp(a._rotation, bRot, t),
                Lerp(a._scale, b._scale, t));
    }

    Vector3f Transform::TransformPoint(const Transform &a, const Vector3f &b)
    {
        Vector3f out;
        out = a._rotation * (a._scale * b);
        out = a._position + out;
        return out;
    }

    Vector3f Transform::TransformVector(const Transform &a, const Vector3f &b)
    {
        Vector3f out;
        out = a._rotation * (a._scale * b);
        return out;
    }

    // Local (SRT-level) inverse.  Note: this is NOT the same as a full matrix inverse
    // when non-uniform scale and rotation are combined; it assumes S * R * T order.
    Transform Transform::LocalInverse(const Transform &t)
    {
        Quaternion invRotation = Quaternion::Inverse(t._rotation);
        Vector3f invScale = Vector3f(0, 0, 0);
        if (t._scale.x != 0.0f)
            invScale.x = 1.0f / t._scale.x;
        if (t._scale.y != 0)
            invScale.y = 1.0f / t._scale.y;
        if (t._scale.z != 0)
            invScale.z = 1.0f / t._scale.z;
        Vector3f invTranslation = invRotation * (invScale * (-1.0f * t._position));
        Transform result;
        result._position = invTranslation;
        result._rotation = invRotation;
        result._scale = invScale;
        return result;
    }

    // void Transform::SetGlobalSRT(Transform &t, Vector3f s, Quaternion r, Vector3f p)
    // {
    //     if (t._p_parent == nullptr)
    //     {
    //         t._rotation = r;
    //         t._position = p;
    //         t._scale = s;
    //         return;
    //     }

    //     const Matrix4x4f desired_world = ToMatrix(Transform(p, r, s));
    //     const Matrix4x4f inv_parent_world = Math::MatrixInverse(GetWorldMatrix(*t._p_parent));
    //     const Transform local_transform = FromMatrix(desired_world * inv_parent_world);
    //     t._position = local_transform._position;
    //     t._rotation = local_transform._rotation;
    //     t._scale = local_transform._scale;
    // }

    // void Transform::SetGlobalRotation(Transform &t, Quaternion rotation)
    // {
    //     const Transform world_transform = GetWorldTransform(t);
    //     SetGlobalSRT(t, world_transform._scale, rotation, world_transform._position);
    // }

    // void Transform::SetGlobalPosition(Transform &t, Vector3f position)
    // {
    //     const Transform world_transform = GetWorldTransform(t);
    //     SetGlobalSRT(t, world_transform._scale, world_transform._rotation, position);
    // }

    // void Transform::SetGlobalScale(Transform &t, Vector3f scale)
    // {
    //     const Transform world_transform = GetWorldTransform(t);
    //     SetGlobalSRT(t, scale, world_transform._rotation, world_transform._position);
    // }
}// namespace Ailu
