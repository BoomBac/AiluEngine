export module core.math.aabb;
import <string>;

#include "ALMath.hpp"
export namespace Ailu
{
    export class AABB
    {
    public:
        static std::pair<Vector3f, Vector3f> CaclulateBoundBox(const AABB& aabb, const Matrix4x4f& mat)
        {
            Vector3f vertices[8];
            vertices[0] = aabb._min;
            vertices[1] = Vector3f(aabb._min.x, aabb._min.y, aabb._max.z);
            vertices[2] = Vector3f(aabb._min.x, aabb._max.y, aabb._min.z);
            vertices[3] = Vector3f(aabb._min.x, aabb._max.y, aabb._max.z);
            vertices[4] = Vector3f(aabb._max.x, aabb._min.y, aabb._min.z);
            vertices[5] = Vector3f(aabb._max.x, aabb._min.y, aabb._max.z);
            vertices[6] = Vector3f(aabb._max.x, aabb._max.y, aabb._min.z);
            vertices[7] = aabb._max;
#undef max
            Vector3f new_min = Vector3f(std::numeric_limits<float>::max());
            Vector3f new_max = Vector3f(std::numeric_limits<float>::lowest());
            for (int i = 1; i < 8; ++i)
            {
                Vector3f new_vert = MultipyVector(vertices[i], mat);
                new_min = Min(new_min, new_vert);
                new_max = Max(new_max, new_vert);
            }
            return std::make_pair(new_min, new_max);
        }

        AABB() : _min(Vector3f(0.0f)), _max(Vector3f(0.0f)) {}
        AABB(const Vector3f& minPoint, const Vector3f& maxPoint) : _min(minPoint), _max(maxPoint) {}

        Vector3f GetHalfAxisLength() const
        {
            auto center = 0.5f * (_min + _max);
            Vector3f axis{};
            axis.x = _max.x - center.x;
            axis.y = _max.y - center.y;
            axis.z = _max.z - center.z;
            return axis;
        };

        inline Vector3f Center() const
        {
            return 0.5f * (_min + _max);
        }

        inline Vector3f Size() const
        {
            return _max - _min;
        }

        bool Intersects(const AABB& other) const
        {
            return (_min.x <= other._max.x && _max.x >= other._min.x) &&
                (_min.y <= other._max.y && _max.y >= other._min.y) &&
                (_min.z <= other._max.z && _max.z >= other._min.z);
        }

        bool Contains(Vector3f& point) const
        {
            return (point.x >= _min.x && point.x <= _max.x) &&
                (point.y >= _min.y && point.y <= _max.y) &&
                (point.z >= _min.z && point.z <= _max.z);
        }
    public:
        Vector3f _min;
        Vector3f _max;
    };
}