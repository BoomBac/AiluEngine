#include "Framework/Math/Projection.h"

namespace Ailu::Math
{
    Matrix4x4f BuildViewMatrixLookToLH(Matrix4x4f &result, Vector3f position, Vector3f lookTo, Vector3f up)
    {
        Vector3f zAxis, xAxis, yAxis;
        zAxis = lookTo;
        Normalize(zAxis);
        xAxis = CrossProduct(up, zAxis);
        Normalize(xAxis);
        yAxis = CrossProduct(zAxis, xAxis);
        result = {{{{xAxis.x, yAxis.x, zAxis.x, 0.0f},
                    {xAxis.y, yAxis.y, zAxis.y, 0.0f},
                    {xAxis.z, yAxis.z, zAxis.z, 0.0f},
                    {-DotProduct(xAxis, position), -DotProduct(yAxis, position), -DotProduct(zAxis, position), 1.0f}}}};
        return result;
    }

    Matrix4x4f BuildViewMatrixLookAtLH(Matrix4x4f &result, Vector3f position, Vector3f look_at, Vector3f up)
    {
        Vector3f zAxis, xAxis, yAxis;
        zAxis = look_at - position;
        return BuildViewMatrixLookToLH(result, position, zAxis, up);
    }

    Matrix4x4f BuildViewRHMatrix(Matrix4x4f &result, Vector3f position, Vector3f lookAt, Vector3f up)
    {
        Vector3f zAxis, xAxis, yAxis;
        zAxis = lookAt - position;
        Normalize(zAxis);
        xAxis = CrossProduct(up, zAxis);
        Normalize(xAxis);
        yAxis = CrossProduct(zAxis, xAxis);
        result = {{{{xAxis.x, yAxis.x, zAxis.x, 0.0f},
                    {xAxis.y, yAxis.y, zAxis.y, 0.0f},
                    {xAxis.z, yAxis.z, zAxis.z, 0.0f},
                    {DotProduct(xAxis, -position), DotProduct(yAxis, -position), DotProduct(zAxis, -position), 1.0f}}}};
        return result;
    }

    void BuildOrthographicMatrix(Matrix4x4f &matrix, f32 left, f32 right, f32 top, f32 bottom, f32 near_plane, f32 far_plane)
    {
#if defined(_REVERSED_Z)
        std::swap(near_plane, far_plane);
#endif
        const float width = right - left;
        const float height = top - bottom;
        const float depth = far_plane - near_plane;
        matrix = {{{{2.0f / width, 0.0f, 0.0f, 0.0f},
                    {0.0f, 2.0f / height, 0.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f / depth, 0.0f},
                    {-(right + left) / width, -(top + bottom) / height, -near_plane / depth, 1.0f}}}};
        return;
    }

    void BuildPerspectiveFovLHMatrix(Matrix4x4f &matrix, f32 fieldOfView, f32 screenAspect, f32 near_plane, f32 far_plane)
    {
#if defined(_REVERSED_Z)
        std::swap(near_plane, far_plane);
#endif
        Matrix4x4f perspective = {{{{1.0f / (screenAspect * tanf(fieldOfView * 0.5f)), 0.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f / tanf(fieldOfView * 0.5f), 0.0f, 0.0f},
                                    {0.0f, 0.0f, far_plane / (far_plane - near_plane), 1.0f},
                                    {0.0f, 0.0f, (-near_plane * far_plane) / (far_plane - near_plane), 0.0f}}}};
        matrix = perspective;
        return;
    }

    void BuildPerspectiveFovRHMatrix(Matrix4x4f &matrix, f32 fieldOfView, f32 screenAspect, f32 near_plane, f32 far_plane)
    {
#if defined(_REVERSED_Z)
        std::swap(near_plane, far_plane);
#endif
        Matrix4x4f perspective = {{{{1.0f / (screenAspect * tanf(fieldOfView * 0.5f)), 0.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f / tanf(fieldOfView * 0.5f), 0.0f, 0.0f},
                                    {0.0f, 0.0f, far_plane / (near_plane - far_plane), -1.0f},
                                    {0.0f, 0.0f, (-near_plane * far_plane) / (far_plane - near_plane), 0.0f}}}};

        matrix = perspective;
        return;
    }

    Matrix4x4f MatrixReverseZ(const Matrix4x4f proj)
    {
#if defined(_REVERSED_Z)
        const Matrix4x4f reverse_z {1.0f, 0.0f,  0.0f, 0.0f,
            0.0f, 1.0f,  0.0f, 0.0f,
            0.0f, 0.0f, -1.0f, 0.0f,
            0.0f, 0.0f,  1.0f, 1.0f};
        return proj * reverse_z;
#else
        return proj;
#endif
    }
}
