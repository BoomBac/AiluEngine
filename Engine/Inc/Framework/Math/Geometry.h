#pragma once
#ifndef __GEOMETRY_H__
#define __GEOMETRY_H__
#include "ALMath.hpp"

namespace Ailu
{
	struct Plane
	{
		Vector3f _normal;
		f32 _distance;
		Plane() : _normal(Vector3f::kForward), _distance(0.f) {}
		Plane(Vector3f n, f32 d) : _normal(n), _distance(d) {}
	};

    class AABB
    {
    public:
        static float DistanceFromRayToAABB(const Vector3f& rayOrigin, const Vector3f& rayDirection, const Vector3f& aabbMin, const Vector3f& aabbMax)
        {
            Vector3f p = 0.5f * (aabbMin + aabbMax);
            return DistanceToRay(rayOrigin, rayDirection, p);
        }

        static AABB CaclulateBoundBox(const AABB& aabb, const Matrix4x4f& mat)
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
            return AABB(new_min, new_max);
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

    struct ViewFrustum
    {
        static bool Conatin(const ViewFrustum& vf,const AABB& aabb)
        {
            static Array<Vector3f, 8> points;
            Vector3f bl = aabb._min;
            Vector3f tr = aabb._max;
            Vector3f center = (bl + tr) / 2.0f;
            Vector3f extent = tr - bl;
            float l = abs(extent.x), w = abs(extent.y), h = abs(extent.z);
            points[0] = (center + Vector3f(l / 2, w / 2, h / 2));
            points[1] = (center + Vector3f(l / 2, w / 2, -h / 2));
            points[2] = (center + Vector3f(l / 2, -w / 2, h / 2));
            points[3] = (center + Vector3f(l / 2, -w / 2, -h / 2));
            points[4] = (center + Vector3f(-l / 2, w / 2, h / 2));
            points[5] = (center + Vector3f(-l / 2, w / 2, -h / 2));
            points[6] = (center + Vector3f(-l / 2, -w / 2, h / 2));
            points[7] = (center + Vector3f(-l / 2, -w / 2, -h / 2));
            for (int i = 0; i < 6; i++)
            {
                if (DotProduct(vf._planes[i]._normal, points[0]) + vf._planes[i]._distance > 0
                    && DotProduct(vf._planes[i]._normal, points[1]) + vf._planes[i]._distance > 0
                    && DotProduct(vf._planes[i]._normal, points[2]) + vf._planes[i]._distance > 0
                    && DotProduct(vf._planes[i]._normal, points[3]) + vf._planes[i]._distance > 0
                    && DotProduct(vf._planes[i]._normal, points[4]) + vf._planes[i]._distance > 0
                    && DotProduct(vf._planes[i]._normal, points[5]) + vf._planes[i]._distance > 0
                    && DotProduct(vf._planes[i]._normal, points[6]) + vf._planes[i]._distance > 0
                    && DotProduct(vf._planes[i]._normal, points[7]) + vf._planes[i]._distance > 0)
                {
                    LOG_INFO("Outside {}",i);
                    return false;
                }
            }
            return true;
        };
        Array<Plane, 8> _planes;
    };
}


#endif // !GEOMETRY_H__
