#pragma once

#include "Framework/Math/MatrixMath.h"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        AILU_API Matrix4x4f BuildViewMatrixLookToLH(Matrix4x4f &result, Vector3f position, Vector3f lookTo, Vector3f up);
        AILU_API Matrix4x4f BuildViewMatrixLookAtLH(Matrix4x4f &result, Vector3f position, Vector3f look_at, Vector3f up);
        AILU_API Matrix4x4f BuildViewRHMatrix(Matrix4x4f &result, Vector3f position, Vector3f lookAt, Vector3f up);

        AILU_API void BuildOrthographicMatrix(Matrix4x4f &matrix, f32 left, f32 right, f32 top, f32 bottom, f32 near_plane, f32 far_plane);
        AILU_API void BuildPerspectiveFovLHMatrix(Matrix4x4f &matrix, f32 fieldOfView, f32 screenAspect, f32 near_plane, f32 far_plane);
        AILU_API void BuildPerspectiveFovRHMatrix(Matrix4x4f &matrix, f32 fieldOfView, f32 screenAspect, f32 near_plane, f32 far_plane);

        AILU_API Matrix4x4f MatrixReverseZ(const Matrix4x4f proj);
    }
#pragma warning(pop)
}
