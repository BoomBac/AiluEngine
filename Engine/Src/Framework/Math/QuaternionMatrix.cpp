#include "Framework/Math/QuaternionMatrix.h"

namespace Ailu::Math
{
    Matrix4x4f Quaternion::ToMat4f() const
    {
        Vector3f r = *this * Vector3f(1, 0, 0);
        Vector3f u = *this * Vector3f(0, 1, 0);
        Vector3f f = *this * Vector3f(0, 0, 1);
        return {{{
                {r.x, r.y, r.z, 0.f},
                {u.x, u.y, u.z, 0.f},
                {f.x, f.y, f.z, 0.f},
                {0.0f, 0.0f, 0.0f, 1.0f},
        }}};
    }

    Matrix4x4f Quaternion::ToMat4f(const Quaternion &q)
    {
        Vector3f r = q * Vector3f(1, 0, 0);
        Vector3f u = q * Vector3f(0, 1, 0);
        Vector3f f = q * Vector3f(0, 0, 1);
        return {{{
                {r.x, r.y, r.z, 0.f},
                {u.x, u.y, u.z, 0.f},
                {f.x, f.y, f.z, 0.f},
                {0.0f, 0.0f, 0.0f, 1.0f},
        }}};
    }

    Quaternion Quaternion::FromMat4f(const Matrix4x4f &mat)
    {
        Quaternion q;
        if (mat[2][2] < 0.0f)
        {
            if (mat[0][0] > mat[1][1])
            {
                const float trace = 1.0f + mat[0][0] - mat[1][1] - mat[2][2];
                float s = 2.0f * sqrtf(trace);
                if (mat[1][2] < mat[2][1])
                {
                    s = -s;
                }
                q.x = 0.25f * s;
                s = 1.0f / s;
                q.w = (mat[1][2] - mat[2][1]) * s;
                q.y = (mat[0][1] + mat[1][0]) * s;
                q.z = (mat[2][0] + mat[0][2]) * s;
                if (UNLIKELY((trace == 1.0f) && (q[0] == 0.0f && q[2] == 0.0f && q[3] == 0.0f)))
                {
                    q.x = 1.0f;
                }
            }
            else
            {
                const float trace = 1.0f - mat[0][0] + mat[1][1] - mat[2][2];
                float s = 2.0f * sqrtf(trace);
                if (mat[2][0] < mat[0][2])
                {
                    s = -s;
                }
                q.y = 0.25f * s;
                s = 1.0f / s;
                q.w = (mat[2][0] - mat[0][2]) * s;
                q.x = (mat[0][1] + mat[1][0]) * s;
                q.z = (mat[1][2] + mat[2][1]) * s;
                if (UNLIKELY((trace == 1.0f) && (q[0] == 0.0f && q[1] == 0.0f && q[3] == 0.0f)))
                {
                    q.y = 1.0f;
                }
            }
        }
        else
        {
            if (mat[0][0] < -mat[1][1])
            {
                const float trace = 1.0f - mat[0][0] - mat[1][1] + mat[2][2];
                float s = 2.0f * sqrtf(trace);
                if (mat[0][1] < mat[1][0])
                {
                    s = -s;
                }
                q.z = 0.25f * s;
                s = 1.0f / s;
                q.w = (mat[0][1] - mat[1][0]) * s;
                q.x = (mat[2][0] + mat[0][2]) * s;
                q.y = (mat[1][2] + mat[2][1]) * s;
                if (UNLIKELY((trace == 1.0f) && (q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f)))
                {
                    q.z = 1.0f;
                }
            }
            else
            {
                const float trace = 1.0f + mat[0][0] + mat[1][1] + mat[2][2];
                float s = 2.0f * sqrtf(trace);
                q.w = 0.25f * s;
                s = 1.0f / s;
                q.x = (mat[1][2] - mat[2][1]) * s;
                q.y = (mat[2][0] - mat[0][2]) * s;
                q.z = (mat[0][1] - mat[1][0]) * s;
                if (UNLIKELY((trace == 1.0f) && (q[1] == 0.0f && q[2] == 0.0f && q[3] == 0.0f)))
                {
                    q.w = 1.0f;
                }
            }
        }
        return q;
    }

    Matrix4x4f QuaternionToMatrix(const Quaternion &quaternion)
    {
        return Quaternion::ToMat4f(quaternion);
    }

    Quaternion MatrixToQuaternion(const Matrix4x4f &matrix)
    {
        return Quaternion::FromMat4f(matrix);
    }

    void DecomposeMatrix(const Matrix4x4f &mat, Vector3f &t, Quaternion &r, Vector3f &s)
    {
        t = mat.GetRow(3).xyz;
        s = mat.LossyScale();
        auto rot_mat = mat;
        rot_mat.SetRow(0, Normalize(rot_mat.GetRow(0)));
        rot_mat.SetRow(1, Normalize(rot_mat.GetRow(1)));
        rot_mat.SetRow(2, Normalize(rot_mat.GetRow(2)));
        r = Quaternion::FromMat4f(rot_mat);
    }

    void MatrixRotationQuaternion(Matrix4x4f &matrix, Quaternion q)
    {
        Matrix4x4f rotation = {{{{1.0f - 2.0f * q.y * q.y - 2.0f * q.z * q.z, 2.0f * q.x * q.y + 2.0f * q.w * q.z, 2.0f * q.x * q.z - 2.0f * q.w * q.y, 0.0f},
                                 {2.0f * q.x * q.y - 2.0f * q.w * q.z, 1.0f - 2.0f * q.x * q.x - 2.0f * q.z * q.z, 2.0f * q.y * q.z + 2.0f * q.w * q.x, 0.0f},
                                 {2.0f * q.x * q.z + 2.0f * q.w * q.y, 2.0f * q.y * q.z - 2.0f * q.y * q.z - 2.0f * q.w * q.x, 1.0f - 2.0f * q.x * q.x - 2.0f * q.y * q.y, 0.0f},
                                 {0.0f, 0.0f, 0.0f, 1.0f}}}};
        matrix = rotation;
    }
}
