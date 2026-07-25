#pragma once

#include "Framework/Math/Quaternion.h"
#include "Framework/Math/MatrixMath.h"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        AILU_API Matrix4x4f QuaternionToMatrix(const Quaternion &quaternion);
        AILU_API Quaternion MatrixToQuaternion(const Matrix4x4f &matrix);
        AILU_API void DecomposeMatrix(const Matrix4x4f &mat, Vector3f &t, Quaternion &r, Vector3f &s);
        AILU_API void MatrixRotationQuaternion(Matrix4x4f &matrix, Quaternion q);
    }
#pragma warning(pop)
}
