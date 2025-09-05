#ifndef __TRANSFORM2D_H__
#define __TRANSFORM2D_H__
#include "ALMath.hpp"
#include "generated/Transform2D.gen.h"
namespace Ailu
{
    namespace Math
    {
        ASTRUCT()
        struct AILU_API Transform2D
        {
            GENERATED_BODY()
        public:
            Transform2D() = default;
            Transform2D(Vector2f p, f32 r, Vector2f s) : _position(p), _rotation(r), _scale(s){}
             
            // --- 工具函数 ---

            static Matrix4x4f ToMatrix(const Transform2D &t)
            {
                float c = cosf(t._rotation);
                float s = sinf(t._rotation);

                Matrix4x4f m{};
                // 2D 旋转缩放嵌入到 3x3（左上角）
                m[0][0] = c * t._scale.x;
                m[0][1] = -s * t._scale.y;
                m[0][2] = 0.0f;

                m[1][0] = s * t._scale.x;
                m[1][1] = c * t._scale.y;
                m[1][2] = 0.0f;

                m[2][0] = 0.0f;
                m[2][1] = 0.0f;
                m[2][2] = 1.0f; // z 轴单位缩放

                // 与 3D Transform 保持一致：平移写在第 4 行（索引 [3][0~2]）
                m[3][0] = t._position.x;
                m[3][1] = t._position.y;
                m[3][2] = 0.0f;

                // 最后一列置零，齐次为1
                m[0][3] = 0.0f;
                m[1][3] = 0.0f;
                m[2][3] = 0.0f;
                m[3][3] = 1.0f;

                return m;
            }

            static void ToMatrix(const Transform2D &t, Matrix4x4f &out_matrix)
            {
                out_matrix = ToMatrix(t);
            }

            static Transform2D FromMatrix(const Matrix4x4f &m)
            {
                Transform2D out;
                // 与 3D Transform 一致，从第 4 行读取平移
                out._position = {m[3][0], m[3][1]};
                // 从左上 2x2 提取缩放
                out._scale.x = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0]);
                out._scale.y = std::sqrt(m[0][1] * m[0][1] + m[1][1] * m[1][1]);
                // 从左上 2x2 提取旋转
                out._rotation = atan2f(m[1][0], m[0][0]);
                return out;
            }

            // --- 父子组合 ---

            static Transform2D Combine(const Transform2D &a, const Transform2D &b)
            {
                Transform2D out;
                out._scale = a._scale * b._scale;
                out._rotation = a._rotation + b._rotation;

                // 先缩放再旋转
                float c = cosf(a._rotation);
                float s = sinf(a._rotation);
                Vector2f rotated{
                        b._position.x * a._scale.x * c - b._position.y * a._scale.y * s,
                        b._position.x * a._scale.x * s + b._position.y * a._scale.y * c};
                out._position = a._position + rotated;
                return out;
            }

            static Transform2D Inverse(const Transform2D &t)
            {
                Transform2D inv;
                inv._rotation = -t._rotation;
                inv._scale.x = fabs(t._scale.x) < kFloatEpsilon ? 0.0f : 1.0f / t._scale.x;
                inv._scale.y = fabs(t._scale.y) < kFloatEpsilon ? 0.0f : 1.0f / t._scale.y;

                float c = cosf(inv._rotation);
                float s = sinf(inv._rotation);

                Vector2f invTrans = -t._position;
                inv._position = {
                        (invTrans.x * c - invTrans.y * s) * inv._scale.x,
                        (invTrans.x * s + invTrans.y * c) * inv._scale.y};
                return inv;
            }

            static Transform2D Mix(const Transform2D &a, const Transform2D &b, float t)
            {
                return {
                        Lerp(a._position, b._position, t),
                        Lerp(a._rotation, b._rotation, t),
                        Lerp(a._scale, b._scale, t)};
            }

            static Vector2f TransformPoint(const Transform2D &a, const Vector2f &p)
            {
                float c = cosf(a._rotation);
                float s = sinf(a._rotation);
                return {
                        a._position.x + (p.x * a._scale.x * c - p.y * a._scale.y * s),
                        a._position.y + (p.x * a._scale.x * s + p.y * a._scale.y * c)};
            }

            static Vector2f TransformVector(const Transform2D &a, const Vector2f &v)
            {
                float c = cosf(a._rotation);
                float s = sinf(a._rotation);
                return {
                        v.x * a._scale.x * c - v.y * a._scale.y * s,
                        v.x * a._scale.x * s + v.y * a._scale.y * c};
            }

            static Transform2D GetWorldTransform(const Transform2D &t)
            {
                Transform2D world = t;
                if (t._p_parent)
                {
                    Transform2D parent = GetWorldTransform(*t._p_parent);
                    world = Combine(parent, world);
                }
                return world;
            }

            static Matrix4x4f GetWorldMatrix(const Transform2D &t)
            {
                Transform2D world = GetWorldTransform(t);
                return ToMatrix(world);
            }

            static Vector2f GetWorldPosition(const Transform2D &t)
            {
                return GetWorldTransform(t)._position;
            }

            static float GetWorldRotation(const Transform2D &t)
            {
                return GetWorldTransform(t)._rotation;
            }

            static Vector2f GetWorldScale(const Transform2D &t)
            {
                return GetWorldTransform(t)._scale;
            }

        public:
            Transform2D *_p_parent = nullptr;
            APROPERTY()
            Vector2f _position = Vector2f::kZero;
            APROPERTY()
            f32 _rotation = 0.0f;// 弧度制
            APROPERTY()
            Vector2f _scale = Vector2f::kOne;
            // 缓存矩阵
            mutable bool _dirty = true;
            mutable Matrix4x4f _local_matrix;// 2D 仿射矩阵（以 4x4 表示）
        };
    }
}
#endif// !__TRANSFORM2D_H__
