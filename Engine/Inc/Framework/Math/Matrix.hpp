#pragma once
#include "Framework/Math/VectorMath.hpp"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        template<typename T, int rows, int cols>
        struct Matrix
        {
            static Matrix<T, rows, cols> Identity()
            {
                Matrix<T,rows,cols> m;
                for (int i = 0; i < rows; ++i)
                {
                    for (int j = 0; j < cols; ++j)
                    {
                        m.data[i][j] = (i == j) ? 1.0 : 0.0;
                    }
                }
                return m;
            }

            union
            {
                T data[rows][cols];
            };

            T *operator[](int row_index)
            {
                return data[row_index];
            }
            const T *operator[](int row_index) const
            {
                return data[row_index];
            }
            T &operator=(const T &other)
            {
                return *this;
            }
            bool operator==(const Matrix<T, rows, cols> &other) const
            {
                for (int i = 0; i < rows; ++i)
                {
                    for (int j = 0; j < cols; ++j)
                    {
                        if (data[i][j] != other.data[i][j])
                        {
                            return false;
                        }
                    }
                }
                return true;
            }
            Matrix<T, rows, cols> &operator*(float scale)
            {
                for (int i = 0; i < rows; ++i)
                {
                    for (int j = 0; j < cols; ++j)
                    {
                        data[i][j] *= scale;
                    }
                }
                return *this;
            }
            template<template<typename> typename TT, typename T>
            void SetRow(u16 row, const TT<T> &v)
            {
                memcpy(data[row], &v, sizeof(v));
            }

            Vector4D<T> GetRow(u16 row) const
            {
                Vector4D<T> ret;
                if (rows == 3)
                {
                    memcpy(&ret, data[row], sizeof(T) * 3);
                    ret.w = 1.0;
                }
                else
                {
                    memcpy(&ret, data[row], sizeof(T) * 4);
                }
                return ret;
            }
            Vector3D<T> LossyScale() const
            {
                Vector3D<T> s;
                s[0] = std::sqrt(data[0][0] * data[0][0] + data[0][1] * data[0][1] + data[0][2] * data[0][2]);
                s[1] = std::sqrt(data[1][0] * data[1][0] + data[1][1] * data[1][1] + data[1][2] * data[1][2]);
                s[2] = std::sqrt(data[2][0] * data[2][0] + data[2][1] * data[2][1] + data[2][2] * data[2][2]);
                return s;
            }
            operator T *() { return &data[0][0]; };
            operator const T *() const { return static_cast<const T *>(&data[0][0]); };
            // ToString method
            std::string ToString() const
            {
                std::ostringstream oss;
                for (int i = 0; i < rows; ++i)
                {
                    for (int j = 0; j < cols; ++j)
                    {
                        oss << data[i][j];
                        if (j < cols - 1)
                        {
                            oss << ",";
                        }
                    }
                    if (i < rows - 1)
                    {
                        oss << ";";
                    }
                }
                return oss.str();
            }

            // FromString method
            void FromString(const std::string &str)
            {
                std::istringstream iss(str);
                std::string row;
                for (int i = 0; i < rows; ++i)
                {
                    if (!std::getline(iss, row, ';'))
                    {
                        throw std::runtime_error("Invalid input string format");
                    }
                    std::istringstream rowStream(row);
                    std::string value;
                    for (int j = 0; j < cols; ++j)
                    {
                        if (!std::getline(rowStream, value, ','))
                        {
                            throw std::runtime_error("Invalid input string format");
                        }
                        if constexpr (std::is_same_v<T, float>)
                            data[i][j] = static_cast<T>(std::stof(value));
                        else if constexpr (std::is_same_v<T, double>)
                            data[i][j] = static_cast<T>(std::stod(value));
                        else
                            data[i][j] = static_cast<T>(std::stoi(value));
                    }
                }
            }
        };
        using Matrix4x4f = Matrix<float, 4, 4>;
        using Matrix3x3f = Matrix<float, 3, 3>;

        template<typename T, int rows, int cols>
        std::string MatrixToString(Matrix<T, rows, cols> mat)
        {
            std::stringstream ss;
            for (u32 i = 0; i < rows; i++)
            {
                ss << "[ ";
                for (u32 j = 0; j < cols; j++)
                {
                    if (j != cols - 1) ss << mat[i][j] << ",";
                    else
                        ss << mat[i][j] << " ]"
                           << "\n";
                }
            }
            std::string temp;
            std::string res;
            while (!ss.eof())
            {
                getline(ss, temp);
                temp.append("\n");
                res.append(temp);
            }
            return res;
        }
        template<typename T, int rows, int cols>
        Matrix<T, rows, cols> Normalize(const Matrix<T, rows, cols> &mat)
        {
            Matrix<T, rows, cols> ret;
            Vector3D<T> x = {mat[0][0], mat[1][0], mat[2][0]};
            Vector3D<T> y = {mat[0][1], mat[1][1], mat[2][1]};
            Vector3D<T> z = {mat[0][2], mat[1][2], mat[2][2]};
            // Normalize xAxis
            x = Normalize(x);
            // Make yAxis orthogonal to xAxis and normalize it
            y -= DotProduct(y, x) * x;
            y = Normalize(y);

            // Make zAxis orthogonal to both xAxis and yAxis and normalize it
            z -= DotProduct(z, x) * x;
            z -= DotProduct(z, y) * y;
            z = Normalize(z);

            // Set the normalized rotation part
            ret[0][0] = x.x;
            ret[0][1] = y.x;
            ret[0][2] = z.x;
            ret[0][3] = 0.0f;
            ret[1][0] = x.y;
            ret[1][1] = y.y;
            ret[1][2] = z.y;
            ret[1][3] = 0.0f;
            ret[2][0] = x.z;
            ret[2][1] = y.z;
            ret[2][2] = z.z;
            ret[2][3] = 0.0f;
            ret[3][0] = mat[3][0];
            ret[3][1] = mat[3][1];
            ret[3][2] = mat[3][2];
            ret[3][3] = mat[3][3];
            return ret;
        }

        template<typename T, int rows, int cols>
        void MatrixAdd(Matrix<T, rows, cols> &ret, const Matrix<T, rows, cols> &m1, const Matrix<T, rows, cols> &m2)
        {
            for (u32 i = 0; i < CountOf(m1.data); i++)
            {
                for (u32 j = 0; j < CountOf(m2.data); j++)
                {
                    ret[i][j] = m1[i][j] + m2[i][j];
                }
            }
        }

        template<typename T, int rows, int cols>
        Matrix<T, rows, cols> operator+(const Matrix<T, rows, cols> &m1, const Matrix<T, rows, cols> &m2)
        {
            Matrix<T, rows, cols> ret;
            for (u32 i = 0; i < CountOf(m1.data); i++)
            {
                for (u32 j = 0; j < CountOf(m2.data); j++)
                {
                    ret[i][j] = m1[i][j] + m2[i][j];
                }
            }
            return ret;
        }

        template<typename T, int rows, int cols>
        void MatrixSub(Matrix<T, rows, cols> &ret, const Matrix<T, rows, cols> &m1, const Matrix<T, rows, cols> &m2)
        {
            for (u32 i = 0; i < CountOf(m1.data); i++)
            {
                for (u32 j = 0; j < CountOf(m2.data); j++)
                {
                    ret[i][j] = m1[i][j] - m2[i][j];
                }
            }
        }

        template<typename T, int rows, int cols>
        Matrix<T, rows, cols> operator-(const Matrix<T, rows, cols> &m1, const Matrix<T, rows, cols> &m2)
        {
            Matrix<T, rows, cols> ret;
            for (u32 i = 0; i < CountOf(m1.data); i++)
            {
                for (u32 j = 0; j < CountOf(m2.data); j++)
                {
                    ret[i][j] = m1[i][j] - m2[i][j];
                }
            }
            return ret;
        }

        template<typename T, int rows, int cols>
        void MatrixTransposeInPlace(Matrix<T, rows, cols> &mat)
        {
            Matrix<T, cols, rows> ret;
            for (u32 i = 0; i < rows; i++)
            {
                for (u32 j = 0; j < cols; j++)
                {
                    ret[j][i] = mat[i][j];
                }
            }
            mat = ret;
        }
        template<typename T, int rows, int cols>
        [[nodiscard]] Matrix<T, cols, rows> MatrixTranspose(const Matrix<T, rows, cols> &mat)
        {
            Matrix<T, cols, rows> ret = mat;
            MatrixTransposeInPlace(ret);
            return ret;
        }

        template<typename T, int Da, int Db, int Dc>
        void MatrixMultipy(Matrix<T, Da, Dc> &ret, const Matrix<T, Da, Db> &m1, const Matrix<T, Dc, Db> &m2)
        {
            //Matrix<T, Db, Dc> m2_t;
            //m2_t = MatrixTranspose(m2);
            for (u32 i = 0; i < Da; i++)
            {
                for (u32 j = 0; j < Dc; j++)
                {
                    for (u32 k = 0; k < Dc; k++)
                        ret[j][i] += m1[j][k] * m2[k][i];
                }
            }
            //MatrixTranspose(ret);
        }

        template<template<typename, int, int> class M, template<typename> class V, typename T, int ROWS, int COLS>
        inline void GetOrigin(V<T> &result, const M<T, ROWS, COLS> &matrix)
        {
            static_assert(ROWS >= 3, "[Error] Only 3x3 and above matrix can be passed to this method!");
            static_assert(COLS >= 3, "[Error] Only 3x3 and above matrix can be passed to this method!");
            result = {matrix[3][0], matrix[3][1], matrix[3][2]};
        }

        template<typename T, int row, int col>
        Matrix<T, row, col> operator*(const Matrix<T, row, col> &m1, const Matrix<T, row, col> &m2)
        {
            Matrix<T, row, col> ret{};
            MatrixMultipy(ret, m1, m2);
            return ret;
        }


    }
#pragma warning(pop)
}
