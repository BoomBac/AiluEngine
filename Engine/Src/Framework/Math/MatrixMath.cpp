#include "Framework/Common/Assert.h"
#include "Framework/Math/MatrixMath.h"

namespace Ailu::Math
{
    float MatrixDeterminat(Matrix3x3f matrix)
    {
        float ret = 0.f, sign = 1.f;
        for (int i = 0; i < 3; ++i)
        {
            Matrix<float, 2, 2> mat2 = SubMatrix(matrix, 0, i);
            ret += sign * matrix[0][i] * (mat2[0][0] * mat2[1][1] - mat2[0][1] * mat2[1][0]);
            sign = -sign;
        }
        return ret;
    }

    float MatrixDeterminat(const Matrix4x4f &matrix)
    {
        float ret = 0.F, sign = 1.F;
        for (int i = 0; i < 4; ++i)
        {
            ret += sign * matrix[0][i] * MatrixDeterminat(SubMatrix(matrix, 0, i));
            sign = -sign;
        }
        return ret;
    }

    const Matrix4x4f &BuildIdentityMatrix()
    {
        static Matrix4x4f identity = {{{{1.0f, 0.0f, 0.0f, 0.0f},
                                        {0.0f, 1.0f, 0.0f, 0.0f},
                                        {0.0f, 0.0f, 1.0f, 0.0f},
                                        {0.0f, 0.0f, 0.0f, 1.0f}}}};
        return identity;
    }

    void BuildIdentityMatrix(Matrix4x4f &matrix)
    {
        Matrix4x4f identity = {{{{1.0f, 0.0f, 0.0f, 0.0f},
                                 {0.0f, 1.0f, 0.0f, 0.0f},
                                 {0.0f, 0.0f, 1.0f, 0.0f},
                                 {0.0f, 0.0f, 0.0f, 1.0f}}}};
        matrix = identity;
        return;
    }

    namespace
    {
        bool InvertM4M4(float inverse[4][4], const float mat[4][4])
        {
            int i, j, k;
            double temp;
            float tempmat[4][4];
            float max;
            int maxj;

            AL_ASSERT(inverse != mat);

            for (i = 0; i < 4; i++)
            {
                for (j = 0; j < 4; j++)
                {
                    inverse[i][j] = 0;
                }
            }
            for (i = 0; i < 4; i++)
            {
                inverse[i][i] = 1;
            }

            for (i = 0; i < 4; i++)
            {
                for (j = 0; j < 4; j++)
                {
                    tempmat[i][j] = mat[i][j];
                }
            }

            for (i = 0; i < 4; i++)
            {
                max = fabsf(tempmat[i][i]);
                maxj = i;
                for (j = i + 1; j < 4; j++)
                {
                    if (fabsf(tempmat[j][i]) > max)
                    {
                        max = fabsf(tempmat[j][i]);
                        maxj = j;
                    }
                }
                if (maxj != i)
                {
                    for (k = 0; k < 4; k++)
                    {
                        std::swap(tempmat[i][k], tempmat[maxj][k]);
                        std::swap(inverse[i][k], inverse[maxj][k]);
                    }
                }

                if (UNLIKELY(tempmat[i][i] == 0.0f))
                {
                    return false;
                }
                temp = (double) tempmat[i][i];
                for (k = 0; k < 4; k++)
                {
                    tempmat[i][k] = (float) ((double) tempmat[i][k] / temp);
                    inverse[i][k] = (float) ((double) inverse[i][k] / temp);
                }
                for (j = 0; j < 4; j++)
                {
                    if (j != i)
                    {
                        temp = tempmat[j][i];
                        for (k = 0; k < 4; k++)
                        {
                            tempmat[j][k] -= (float) ((double) tempmat[i][k] * temp);
                            inverse[j][k] -= (float) ((double) inverse[i][k] * temp);
                        }
                    }
                }
            }
            return true;
        }
    }

    Matrix4x4f MatrixInverse(const Matrix4x4f &mat)
    {
        Matrix4x4f ret;
        InvertM4M4(ret.data, mat.data);
        return ret;
    }

    Matrix4x4f MatrixInverseTanspose(const Matrix4x4f &mat)
    {
        auto inver = MatrixInverse(mat);
        return MatrixTranspose(inver);
    }
}
