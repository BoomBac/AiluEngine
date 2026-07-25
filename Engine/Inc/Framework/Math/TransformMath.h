#pragma once

#include "Framework/Math/MatrixMath.h"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        AILU_API void TransformVector(Vector4f &vector, const Matrix4x4f &matrix);
        AILU_API Vector4f TransformVector(const Matrix4x4f &matrix, const Vector4f &v);
        AILU_API Vector3f MultipyVector(const Vector3f &v, const Matrix4x4f &mat);

        AILU_API void TransformCoord(Vector3f &vector, const Matrix4x4f &matrix);
        AILU_API Vector3f TransformCoord(const Matrix4x4f &matrix, const Vector3f &vector);

        AILU_API void TransformNormal(Vector3f &vector, const Matrix4x4f &matrix);
        [[nodiscard]] AILU_API Vector3f TransformNormal(const Matrix4x4f &matrix, const Vector3f &vector);

        AILU_API void MatrixTranslation(Matrix4x4f &matrix, float x, float y, float z);
        AILU_API Matrix4x4f MatrixTranslation(float x, float y, float z);
        AILU_API Matrix4x4f MatrixTranslation(const Vector3f &translation);

        AILU_API void MatrixRotationX(Matrix4x4f &matrix, float radius);
        AILU_API Matrix4x4f MatrixRotationX(const float &radius);
        AILU_API void MatrixRotationY(Matrix4x4f &matrix, float radius);
        AILU_API Matrix4x4f MatrixRotationY(const float &radius);
        AILU_API void MatrixRotationZ(Matrix4x4f &matrix, const float &radius);
        AILU_API Matrix4x4f MatrixRotationZ(const float &radius);
        AILU_API void MatrixRotationAxis(Matrix4x4f &matrix, const Vector3f &axis, float radius);
        AILU_API void MatrixRotationYawPitchRoll(Matrix4x4f &matrix, const float &yaw, const float &pitch, const float &roll);
        AILU_API Matrix4x4f MatrixRotationYawPitchRoll(const float &yaw, const float &pitch, const float &roll);
        AILU_API Matrix4x4f MatrixRotationYawPitchRoll(const Vector3f &rotation);

        AILU_API void MatrixScale(Matrix4x4f &matrix, float x, float y, float z);
        AILU_API Matrix4x4f MatrixScale(float x, float y, float z);
        AILU_API Matrix4x4f MatrixScale(const Vector3f &scale);
    }
#pragma warning(pop)
}
