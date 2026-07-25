#pragma once
#include "Framework/Math/Matrix.hpp"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        template<typename T, int n>
        static auto SubMatrix(const Matrix<T, n, n> &mat, const int &r, const int &c) -> Matrix<T, n - 1, n - 1>
        {
            int i = 0, j = 0;
            Matrix<T, n - 1, n - 1> temp{};
            for (int row = 0; row < n; row++)
            {
                for (int col = 0; col < n; col++)
                {
                    if (row != r && col != c)
                    {
                        temp[i][j++] = mat[row][col];
                        if (j == n - 1)
                        {
                            j = 0;
                            i++;
                        }
                    }
                }
            }
            return temp;
        }

        AILU_API float MatrixDeterminat(Matrix3x3f matrix);
        //The floating point type will have a small deviation from the library implementation,
        //in addition there is a +-0 problem
        AILU_API float MatrixDeterminat(const Matrix4x4f &matrix);
        AILU_API const Matrix4x4f &BuildIdentityMatrix();
        AILU_API void BuildIdentityMatrix(Matrix4x4f &matrix);
        [[nodiscard]] AILU_API Matrix4x4f MatrixInverse(const Matrix4x4f &mat);
        //The matrix is first inverted and then transposed to obtain the matrix with the correct transformation normals
        AILU_API Matrix4x4f MatrixInverseTanspose(const Matrix4x4f &mat);
    }
#pragma warning(pop)
}
