#pragma once
#include "Framework/Platform/Api.h"
#include "Framework/Math/VectorMath.hpp"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        template<typename T, int rows, int cols>
        struct Matrix;
        using Matrix4x4f = Matrix<float, 4, 4>;

        //https://github.com/apachecn/apachecn-c-cpp-zh/blob/master/docs/handson-cpp-game-ani-prog/04.md
        struct AILU_API Quaternion
        {
        public:
            inline static float kQuatEpsilon = kFloatEpsilon;
            union
            {
                Vector4f _quat;
                struct
                {
                    float x, y, z, w;
                };
                struct
                {
                    Vector3f v;
                    float s;
                };
            };

        public:
            Quaternion() : _quat(0.0f, 0.0f, 0.0f, 1.0f) {};
            Quaternion(const Vector4f &quat) : _quat(quat) {};
            Quaternion(float x, float y, float z, float w) : _quat(Vector4f(x, y, z, w)) {};
            Quaternion(Vector3f v, float s) : _quat(Vector4f(v, w)) {};
            friend std::ostream &operator<<(std::ostream &os, const Quaternion &q)
            {
                os << q.x << "," << q.y << "," << q.z << "," << q.w;
                return os;
            }
            template<typename Archive>
            void serialize(Archive &ar, u32 version)
            {
                ar &ToString();
            }
            Quaternion &operator=(const Quaternion &other)
            {
                memcpy(this, &other, sizeof(Quaternion));
                return *this;
            }
            Quaternion &operator=(const Vector4f &other)
            {
                memcpy(this, &other, sizeof(Quaternion));
                return *this;
            }
            f32 &operator[](u16 index)
            {
                return _quat[index];
            }
            Quaternion operator+(const Quaternion &other) const { return Quaternion(_quat + other._quat); };
            Quaternion operator-(const Quaternion &other) const { return Quaternion(_quat - other._quat); };
            Quaternion operator*(float scale) const { return Quaternion(_quat * scale); };
            // left mulipty: other * current
            Quaternion operator*(const Quaternion &other) const
            {
                return Quaternion(
                        other.x * w + other.y * z - other.z * y + other.w * x,
                        -other.x * z + other.y * w + other.z * x + other.w * y,
                        other.x * y - other.y * x + other.z * w + other.w * z,
                        -other.x * x - other.y * y - other.z * z + other.w * w);
            }

            Vector3f operator*(const Vector3f &v) const
            {
                return this->v * 2.0f * DotProduct(this->v, v) +
                       v * (this->s * this->s - DotProduct(this->v, this->v)) +
                       CrossProduct(this->v, v) * 2.0f * this->s;
            }

            // right mulipty: current * other
            Quaternion operator^(const Quaternion &other) const
            {
                return Quaternion(
                        w * other.w - x * other.x - y * other.y - z * other.z,
                        w * other.x + x * other.w - y * other.z + z * other.y,
                        w * other.y + x * other.z + y * other.w - z * other.x,
                        w * other.z - x * other.y + y * other.x + z * other.w);
            }

            // right mulipty: current * other
            Quaternion operator^(float f) const
            {
                float angle = 2.0f * acosf(s);
                Vector3f axis = Normalize(v);
                float halfCos = cosf(f * angle * 0.5f);
                float halfSin = sinf(f * angle * 0.5f);
                return Quaternion(axis.x * halfSin, axis.y * halfSin, axis.z * halfSin, halfCos);
            }

            bool operator==(const Quaternion &other) const
            {
                return (fabsf(this->x - other.x) <= kQuatEpsilon &&
                        fabsf(this->y - other.y) <= kQuatEpsilon &&
                        fabsf(this->z - other.z) <= kQuatEpsilon &&
                        fabsf(this->w - other.w) <= kQuatEpsilon);
            }
            bool operator!=(const Quaternion &other) const
            {
                return !(*this == other);
            }

            Quaternion operator-() const
            {
                return Conjugate(*this);
            }

            std::string ToString(int precision = 2) const
            {
                return std::format("{:.{}f},{:.{}f},{:.{}f},{:.{}f}", x, precision, y, precision, z, precision, w, precision);
            }
            bool FromString(const std::string &str)
            {
                std::stringstream ss(str);
                char delimiter;
                if (!(ss >> _quat.data[0] >> delimiter) || delimiter != ',' ||
                    !(ss >> _quat.data[1] >> delimiter) || delimiter != ',' ||
                    !(ss >> _quat.data[2] >> delimiter) || delimiter != ',' ||
                    !(ss >> _quat.data[3]))
                {
                    return false;
                }
                return true;
            }

            void NormalizeQ()
            {
                float lenSq = x * x + y * y + z * z + w * w;
                if (lenSq < kQuatEpsilon) return;
                float i_len = 1.0f / sqrtf(lenSq);
                x *= i_len;
                y *= i_len;
                z *= i_len;
                w *= i_len;
            }


            static Quaternion AngleAxis(float angle, const Vector3f &axis)
            {
                angle = ToRadius(angle);
                Vector3f norm = Normalize(axis);
                float s = sinf(angle * 0.5f);
                return Quaternion(norm.x * s, norm.y * s, norm.z * s, cosf(angle * 0.5f));
            }

            static Quaternion RadiusAxis(float radius, const Vector3f &axis)
            {
                Vector3f norm = Normalize(axis);
                float s = sinf(radius * 0.5f);
                return Quaternion(norm.x * s, norm.y * s, norm.z * s, cosf(radius * 0.5f));
            }

            static Quaternion EulerAngles(float x, float y, float z)
            {
                //return Quaternion::AngleAxis(x, Vector3f::kRight) * Quaternion::AngleAxis(y, Vector3f::kUp) * Quaternion::AngleAxis(z, Vector3f::kForward);
                return Quaternion::AngleAxis(x, Vector3f::kRight) * Quaternion::AngleAxis(z, Vector3f::kForward) * Quaternion::AngleAxis(y, Vector3f::kUp);
            }

            static Quaternion EulerAngles(Vector3f euler)
            {
                euler *= k2Radius;
                double cr = cos(euler.x * 0.5);
                double sr = sin(euler.x * 0.5);
                double cp = cos(euler.y * 0.5);
                double sp = sin(euler.y * 0.5);
                double cy = cos(euler.z * 0.5);
                double sy = sin(euler.z * 0.5);
                Quaternion q;
                q.w = cr * cp * cy + sr * sp * sy;
                q.x = sr * cp * cy - cr * sp * sy;
                q.y = cr * sp * cy + sr * cp * sy;
                q.z = cr * cp * sy - sr * sp * cy;
                return q;
            }

            static Vector3f EulerAngles(const Quaternion &q)
            {
                Vector3f e;
                // pitch (y-axis rotation)
                double sinp = std::sqrt(1 + 2 * (q.w * q.y - q.x * q.z));
                double cosp = std::sqrt(1 - 2 * (q.w * q.y - q.x * q.z));
                e.y = 2 * std::atan2(sinp, cosp) - kHalfPi;

                double sinr_cosp = 2.0 * (q.w * q.x + q.y * q.z);
                double cosr_cosp = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
                e.x = std::atan2(sinr_cosp, cosr_cosp);
                double siny_cosp = 2.0 * (q.w * q.z + q.x * q.y);
                double cosy_cosp = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
                e.z = std::atan2(siny_cosp, cosy_cosp);
                e *= k2Angle;
                return e;
            }

            static Quaternion FromTo(const Vector3f &from, const Vector3f &to)
            {
                Vector3f f = Normalize(from);
                Vector3f t = Normalize(to);
                if (f == t)
                    return Quaternion();
                else if (f == t * -1.0f)
                {
                    Vector3f ortho = Vector3f(1, 0, 0);
                    if (fabsf(f.y) < fabsf(f.x))
                    {
                        ortho = Vector3f(0, 1, 0);
                    }
                    if (fabsf(f.z) < fabs(f.y) && fabs(f.z) < fabsf(f.x))
                    {
                        ortho = Vector3f(0, 0, 1);
                    }
                    Vector3f axis = Normalize(CrossProduct(f, ortho));
                    return Quaternion(axis.x, axis.y, axis.z, 0);
                }
                Vector3f half = Normalize(f + t);
                Vector3f axis = CrossProduct(f, half);
                return Quaternion(axis.x, axis.y, axis.z, DotProduct(f, half));
            }

            static Vector3f GetAxis(const Quaternion &quat)
            {
                return Normalize(Vector3f(quat.x, quat.y, quat.z));
            }
            static float GetAngle(const Quaternion &quat)
            {
                return 2.0f * acosf(quat.w);
            }

            static float GetAngle(const Quaternion &quat, const Vector3f &axis)
            {
                auto nq = NormalizedQ(quat);
                auto naxis = Normalize(axis);

                // 虚部
                Vector3f qv{nq.x, nq.y, nq.z};

                // 投影到 axis 上
                float proj = DotProduct(qv, axis);

                // 角度 = 2 * atan2(|虚部在axis上的分量|, w)
                float angle = 2.0f * std::atan2(std::abs(proj), nq.w);

                return angle;
            }


            static bool IsSameOrientation(const Quaternion &l, const Quaternion &r)
            {
                return (fabsf(l.x - r.x) <= kQuatEpsilon &&
                        fabsf(l.y - r.y) <= kQuatEpsilon &&
                        fabsf(l.z - r.z) <= kQuatEpsilon &&
                        fabsf(l.w - r.w) <= kQuatEpsilon) ||
                       (fabsf(l.x + r.x) <= kQuatEpsilon &&
                        fabsf(l.y + r.y) <= kQuatEpsilon &&
                        fabsf(l.z + r.z) <= kQuatEpsilon &&
                        fabsf(l.w + r.w) <= kQuatEpsilon);
            }

            static float Dot(const Quaternion &a, const Quaternion &b)
            {
                return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            }

            static float LenSq(const Quaternion &q)
            {
                return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
            }

            static float Len(const Quaternion &q)
            {
                float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
                if (lenSq < kQuatEpsilon)
                    return 0.0f;
                return sqrtf(lenSq);
            }
            static Quaternion NormalizedQ(const Quaternion &q)
            {
                float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
                //if (lenSq < kQuatEpsilon)
                //    return Quaternion();
                float il = 1.0f / sqrtf(lenSq);// il: inverse length
                return Quaternion(q.x * il, q.y * il, q.z * il, q.w * il);
            }
            //flip axis,replace inverse if quat's len sq is 1
            static Quaternion Conjugate(const Quaternion &q)
            {
                return Quaternion(-q.x, -q.y, -q.z, q.w);
            }

            static Quaternion Inverse(const Quaternion &q)
            {
                float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
                if (lenSq < kQuatEpsilon)
                    return Quaternion();
                float recip = 1.0f / lenSq;
                return Quaternion(-q.x * recip, -q.y * recip, -q.z * recip, q.w * recip);
            }

            static Quaternion Identity()
            {
                return Quaternion(0.f, 0.f, 0.f, 1.f);
            }

            static Quaternion Pure(const Quaternion &q)
            {
                return Quaternion(q.v, 0.f);
            }

            //linear blend
            static Quaternion Mix(const Quaternion &from, const Quaternion &to, float t)
            {
                return from * (1.0f - t) + to * t;
            }

            static Quaternion NLerp(const Quaternion &from, const Quaternion &to, float t)
            {
                return NormalizedQ(from + (to - from) * t);
            }

            static Quaternion SLerp(const Quaternion &start, const Quaternion &end, float t)
            {
                if (fabsf(Dot(start, end)) > 1.0f - kQuatEpsilon)
                {
                    return NLerp(start, end, t);
                }
                Quaternion delta = Inverse(start) * end;
                return NormalizedQ((delta ^ t) * start);
            }

            static Quaternion LookRotation(const Vector3f &direction, const Vector3f &up = Vector3f(0.0f, 1.0f, 0.0f))
            {
                // Find orthonormal basis vectors
                Vector3f f = Normalize(direction);// Object Forward
                Vector3f u = Normalize(up);       // Desired Up
                Vector3f r = CrossProduct(u, f);  // Object Right
                u = CrossProduct(f, r);           // Object Up
                // From world forward to object forward
                Quaternion worldToObject = FromTo(Vector3f(0, 0, 1), f);
                // what direction is the new object up?
                Vector3f objectUp = worldToObject * Vector3f(0, 1, 0);
                // From object up to desired up
                Quaternion u2u = FromTo(objectUp, u);
                // Rotate to forward direction first
                // then twist to correct up
                Quaternion result = worldToObject * u2u;
                // Don't forget to normalize the result
                return NormalizedQ(result);
            }

            Matrix4x4f ToMat4f() const;
            static Matrix4x4f ToMat4f(const Quaternion &q);
            //https://github.com/blender/blender/blob/756538b4a117cb51a15e848fa6170143b6aafcd8/source/blender/blenlib/intern/math_rotation.c#L272
            /* hints for branch prediction, only use in code that runs a _lot_ */
            static Quaternion FromMat4f(const Matrix4x4f &mat);
        };

        template<>
        static Quaternion Lerp(const Quaternion &src, const Quaternion &des, const float &weight)
        {
            return Quaternion::NLerp(src, des, weight);
        }

    }
#pragma warning(pop)
}
