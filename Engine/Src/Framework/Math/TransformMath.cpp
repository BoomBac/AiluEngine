#include "Framework/Math/TransformMath.h"

#include <immintrin.h>

namespace Ailu::Math
{
    void TransformVector(Vector4f &vector, const Matrix4x4f &matrix)
    {
#ifdef _SIMD
        __m128 xmm_vector1 = _mm_loadu_ps(&vector.x);

        __m128 xmm_matrix_row1 = _mm_loadu_ps(&matrix[0][0]);
        __m128 xmm_matrix_row2 = _mm_loadu_ps(&matrix[1][0]);
        __m128 xmm_matrix_row3 = _mm_loadu_ps(&matrix[2][0]);
        __m128 xmm_matrix_row4 = _mm_loadu_ps(&matrix[3][0]);

        xmm_vector1 = _mm_add_ps(
                _mm_add_ps(
                        _mm_mul_ps(_mm_shuffle_ps(xmm_vector1, xmm_vector1, _MM_SHUFFLE(0, 0, 0, 0)), xmm_matrix_row1),
                        _mm_mul_ps(_mm_shuffle_ps(xmm_vector1, xmm_vector1, _MM_SHUFFLE(1, 1, 1, 1)), xmm_matrix_row2)),
                _mm_add_ps(
                        _mm_mul_ps(_mm_shuffle_ps(xmm_vector1, xmm_vector1, _MM_SHUFFLE(2, 2, 2, 2)), xmm_matrix_row3),
                        _mm_mul_ps(_mm_shuffle_ps(xmm_vector1, xmm_vector1, _MM_SHUFFLE(3, 3, 3, 3)), xmm_matrix_row4)));
        _mm_storeu_ps(&vector.x, xmm_vector1);
#else
        Vector4f temp{};
        temp.x = vector.x * matrix[0][0] + vector.y * matrix[1][0] + vector.z * matrix[2][0] + vector.w * matrix[3][0];
        temp.y = vector.x * matrix[0][1] + vector.y * matrix[1][1] + vector.z * matrix[2][1] + vector.w * matrix[3][1];
        temp.z = vector.x * matrix[0][2] + vector.y * matrix[1][2] + vector.z * matrix[2][2] + vector.w * matrix[3][2];
        temp.w = vector.x * matrix[0][3] + vector.y * matrix[1][3] + vector.z * matrix[2][3] + vector.w * matrix[3][3];
        vector = temp;
        return;
#endif
    }

    Vector4f TransformVector(const Matrix4x4f &matrix, const Vector4f &v)
    {
        Vector4f temp = v;
        TransformVector(temp, matrix);
        return temp;
    }

    Vector3f MultipyVector(const Vector3f &v, const Matrix4x4f &mat)
    {
        Vector4f temp{v, 1.f};
        TransformVector(temp, mat);
        return temp.xyz;
    }

    void TransformCoord(Vector3f &vector, const Matrix4x4f &matrix)
    {
        Vector4f temp{vector, 1.f};
        TransformVector(temp, matrix);
        memcpy(&vector, temp.Data(), 12);
    }

    Vector3f TransformCoord(const Matrix4x4f &matrix, const Vector3f &vector)
    {
        Vector4f temp{vector, 1.f};
        TransformVector(temp, matrix);
        return temp.xyz;
    }

    void TransformNormal(Vector3f &vector, const Matrix4x4f &matrix)
    {
        Vector4f temp{vector, 0.f};
        TransformVector(temp, matrix);
        vector.xyz = temp.xyz;
    }

    Vector3f TransformNormal(const Matrix4x4f &matrix, const Vector3f &vector)
    {
        Vector4f temp{vector, 0.f};
        TransformVector(temp, matrix);
        return temp.xyz;
    }

    void MatrixTranslation(Matrix4x4f &matrix, float x, float y, float z)
    {
        Matrix4x4f translation = {{{{1.0f, 0.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f, 0.0f},
                                    {0.0f, 0.0f, 1.0f, 0.0f},
                                    {x, y, z, 1.0f}}}};
        matrix = translation;
    }

    Matrix4x4f MatrixTranslation(float x, float y, float z)
    {
        Matrix4x4f translation = {{{{1.0f, 0.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f, 0.0f},
                                    {0.0f, 0.0f, 1.0f, 0.0f},
                                    {x, y, z, 1.0f}}}};
        return translation;
    }

    Matrix4x4f MatrixTranslation(const Vector3f &translation)
    {
        return {{{{1.0f, 0.0f, 0.0f, 0.0f},
                  {0.0f, 1.0f, 0.0f, 0.0f},
                  {0.0f, 0.0f, 1.0f, 0.0f},
                  {translation.x, translation.y, translation.z, 1.0f}}}};
    }

    void MatrixRotationX(Matrix4x4f &matrix, float radius)
    {
        float c = cosf(radius), s = sinf(radius);
        Matrix4x4f rotation = {{{{1.0f, 0.0f, 0.0f, 0.0f},
                                 {0.0f, c, s, 0.0f},
                                 {0.0f, -s, c, 0.0f},
                                 {0.0f, 0.0f, 0.0f, 1.0f}}}};
        matrix = rotation;
        return;
    }

    Matrix4x4f MatrixRotationX(const float &radius)
    {
        float c = cosf(radius), s = sinf(radius);
        return {{{{1.0f, 0.0f, 0.0f, 0.0f},
                  {0.0f, c, s, 0.0f},
                  {0.0f, -s, c, 0.0f},
                  {0.0f, 0.0f, 0.0f, 1.0f}}}};
    }

    void MatrixRotationY(Matrix4x4f &matrix, float radius)
    {
        float c = cosf(radius), s = sinf(radius);
        Matrix4x4f rotation = {{{{c, 0.0f, -s, 0.0f},
                                 {0.0f, 1.0f, 0.0f, 0.0f},
                                 {s, 0.0f, c, 0.0f},
                                 {0.0f, 0.0f, 0.0f, 1.0f}}}};
        matrix = rotation;
        return;
    }

    Matrix4x4f MatrixRotationY(const float &radius)
    {
        float c = cosf(radius), s = sinf(radius);
        return {{{{c, 0.0f, -s, 0.0f},
                  {0.0f, 1.0f, 0.0f, 0.0f},
                  {s, 0.0f, c, 0.0f},
                  {0.0f, 0.0f, 0.0f, 1.0f}}}};
    }

    void MatrixRotationZ(Matrix4x4f &matrix, const float &radius)
    {
        float c = cosf(radius), s = sinf(radius);
        Matrix4x4f rotation = {{{{c, s, 0.0f, 0.0f},
                                 {-s, c, 0.0f, 0.0f},
                                 {0.0f, 0.0f, 1.0f, 0.0f},
                                 {0.0f, 0.0f, 0.0f, 1.0f}}}};
        matrix = rotation;
        return;
    }

    Matrix4x4f MatrixRotationZ(const float &radius)
    {
        float c = cosf(radius), s = sinf(radius);
        return {{{{c, s, 0.0f, 0.0f},
                  {-s, c, 0.0f, 0.0f},
                  {0.0f, 0.0f, 1.0f, 0.0f},
                  {0.0f, 0.0f, 0.0f, 1.0f}}}};
    }

    void MatrixRotationAxis(Matrix4x4f &matrix, const Vector3f &axis, float radius)
    {
        float c = cosf(radius), s = sinf(radius), one_minus_c = 1.0f - c;
        Matrix4x4f rotation = {{{{c + axis.x * axis.x * one_minus_c, axis.x * axis.y * one_minus_c + axis.z * s, axis.x * axis.z * one_minus_c - axis.y * s, 0.0f},
                                 {axis.x * axis.y * one_minus_c - axis.z * s, c + axis.y * axis.y * one_minus_c, axis.y * axis.z * one_minus_c + axis.x * s, 0.0f},
                                 {axis.x * axis.z * one_minus_c + axis.y * s, axis.y * axis.z * one_minus_c - axis.x * s, c + axis.z * axis.z * one_minus_c, 0.0f},
                                 {0.0f, 0.0f, 0.0f, 1.0f}}}};
        matrix = rotation;
    }

    void MatrixRotationYawPitchRoll(Matrix4x4f &matrix, const float &yaw, const float &pitch, const float &roll)
    {
        float cYaw, cPitch, cRoll, sYaw, sPitch, sRoll;
        cYaw = cosf(yaw);
        cPitch = cosf(pitch);
        cRoll = cosf(roll);
        sYaw = sinf(yaw);
        sPitch = sinf(pitch);
        sRoll = sinf(roll);
        Matrix4x4f tmp = {{{{(cRoll * cYaw) + (sRoll * sPitch * sYaw), (sRoll * cPitch), (cRoll * -sYaw) + (sRoll * sPitch * cYaw), 0.0f},
                            {(-sRoll * cYaw) + (cRoll * sPitch * sYaw), (cRoll * cPitch), (sRoll * sYaw) + (cRoll * sPitch * cYaw), 0.0f},
                            {(cPitch * sYaw), -sPitch, (cPitch * cYaw), 0.0f},
                            {0.0f, 0.0f, 0.0f, 1.0f}}}};
        matrix = tmp;
        return;
    }

    Matrix4x4f MatrixRotationYawPitchRoll(const float &yaw, const float &pitch, const float &roll)
    {
        float cYaw, cPitch, cRoll, sYaw, sPitch, sRoll;
        cYaw = cosf(yaw);
        cPitch = cosf(pitch);
        cRoll = cosf(roll);
        sYaw = sinf(yaw);
        sPitch = sinf(pitch);
        sRoll = sinf(roll);
        return {{{{(cRoll * cYaw) + (sRoll * sPitch * sYaw), (sRoll * cPitch), (cRoll * -sYaw) + (sRoll * sPitch * cYaw), 0.0f},
                  {(-sRoll * cYaw) + (cRoll * sPitch * sYaw), (cRoll * cPitch), (sRoll * sYaw) + (cRoll * sPitch * cYaw), 0.0f},
                  {(cPitch * sYaw), -sPitch, (cPitch * cYaw), 0.0f},
                  {0.0f, 0.0f, 0.0f, 1.0f}}}};
    }

    Matrix4x4f MatrixRotationYawPitchRoll(const Vector3f &rotation)
    {
        float yaw = ToRadius(rotation.y), pitch = ToRadius(rotation.x), roll = ToRadius(rotation.z);
        float cYaw, cPitch, cRoll, sYaw, sPitch, sRoll;
        cYaw = cosf(yaw);
        cPitch = cosf(pitch);
        cRoll = cosf(roll);
        sYaw = sinf(yaw);
        sPitch = sinf(pitch);
        sRoll = sinf(roll);
        return {{{{(cRoll * cYaw) + (sRoll * sPitch * sYaw), (sRoll * cPitch), (cRoll * -sYaw) + (sRoll * sPitch * cYaw), 0.0f},
                  {(-sRoll * cYaw) + (cRoll * sPitch * sYaw), (cRoll * cPitch), (sRoll * sYaw) + (cRoll * sPitch * cYaw), 0.0f},
                  {(cPitch * sYaw), -sPitch, (cPitch * cYaw), 0.0f},
                  {0.0f, 0.0f, 0.0f, 1.0f}}}};
    }

    void MatrixScale(Matrix4x4f &matrix, float x, float y, float z)
    {
        Matrix4x4f scale = {{{{x, 0.0f, 0.0f, 0.0f},
                              {0.0f, y, 0.0f, 0.0f},
                              {0.0f, 0.0f, z, 0.0f},
                              {0.0f, 0.0f, 0.0f, 1.0f}}}};
        matrix = scale;
    }

    Matrix4x4f MatrixScale(float x, float y, float z)
    {
        Matrix4x4f scale = {{{{x, 0.0f, 0.0f, 0.0f},
                              {0.0f, y, 0.0f, 0.0f},
                              {0.0f, 0.0f, z, 0.0f},
                              {0.0f, 0.0f, 0.0f, 1.0f}}}};
        return scale;
    }

    Matrix4x4f MatrixScale(const Vector3f &scale)
    {
        Matrix4x4f mat = {{{{scale.x, 0.0f, 0.0f, 0.0f},
                            {0.0f, scale.y, 0.0f, 0.0f},
                            {0.0f, 0.0f, scale.z, 0.0f},
                            {0.0f, 0.0f, 0.0f, 1.0f}}}};
        return mat;
    }
}
