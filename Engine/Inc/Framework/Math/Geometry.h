#pragma once
#ifndef __GEOMETRY_H__
#define __GEOMETRY_H__
#include "ALMath.hpp"

namespace Ailu
{
#undef max
	struct Plane
	{
		Plane() : _normal(Vector3f::kForward), _distance(0.f) {}
		Plane(Vector3f n, f32 d) : _normal(n), _distance(d) {}
		Vector3f _normal;
		f32 _distance;
        Vector3f _point;
        bool Intersect(const Vector3f ray_start,const Vector3f ray_dir,Vector3f& intersect_point)
        {
            float denom = DotProduct(_normal,ray_dir);
            if (std::abs(denom) > 1e-6)
            { // Check if the line is not parallel to the plane
                float t = (_distance - DotProduct(_normal,ray_start)) / denom;
                intersect_point = ray_start + t * ray_dir;
                return true;
            }
            return false; // The line is parallel to the plane
        }
    private:
        f32 _padding = 0.0f;
	};

    struct Ray
    {
    public:
        Vector3f _start;
        Vector3f _dir;
        Ray(Vector3f s, Vector3f d) : _start(s), _dir(d){};
    private:
        Vector2f _padding;
    };

    struct Capsule
    {
        Vector3f _top;
        f32 _radius;
        Vector3f _bottom;
        f32 _height;
        Capsule operator*(const Matrix4x4f &m) const;
    };

    struct AABB
    {
    public:
        static float DistanceFromRayToAABB(const Vector3f& rayOrigin, const Vector3f& rayDirection, const Vector3f& aabbMin, const Vector3f& aabbMax)
        {
            Vector3f p = 0.5f * (aabbMin + aabbMax);
            return DistanceToRay(rayOrigin, rayDirection, p);
        }
        //https://tavianator.com/2022/ray_box_boundary.html, slob method
        // max(t_enter) < min(t_exit)
        bool static Intersect(const AABB& aabb, Vector3f origin, Vector3f dir)
        {
            Vector3f invDir;
            invDir.x = 1.0f / dir.x;
            invDir.y = 1.0f / dir.y;
            invDir.z = 1.0f / dir.z;
            f32 t_enter = 0.0f, t_exit = INFINITY;
            for (int i = 0; i < 3; i++)
            {
                float t1 = (aabb._min.data[i] - origin.data[i]) * invDir.data[i];
                float t2 = (aabb._max.data[i] - origin.data[i]) * invDir.data[i];
                t_enter = std::max<f32>(std::min<f32>(t1, t2), t_enter);
                t_exit = std::min<f32>(std::max<f32>(t1, t2), t_exit);
            }
            return t_enter < t_exit;
        }
        bool static Intersect(const AABB &aabb, const Ray& ray)
        {
            return Intersect(aabb,ray._start,ray._dir);
        }
        AABB operator*(const Matrix4x4f &mat) const;

        static Vector3f MaxAABB()
        {
            return Vector3f(std::numeric_limits<float>::lowest());
        }

        static Vector3f MinAABB()
        {
            return Vector3f(std::numeric_limits<float>::max());
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

        inline f32 Diagon() const
        {
            return Distance(_min, _max);
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
        static bool Conatin(const ViewFrustum& vf, const AABB& aabb);
        Array<Plane, 8> _planes;
    };

    struct OBB
    {
        Vector3f _center = Vector3f::kZero;
        Vector3f _half_axis_length = Vector3f::kOne;
        Vector3f _local_axis[3] = {Vector3f::kRight, Vector3f::kUp, Vector3f::kForward};
        OBB operator*(const Matrix4x4f &m) const;
        static void ConstructVertices(const OBB &obb, Vector3f* vertices);
        static void ConstructEdges(const OBB &obb, Vector3f vertices[8],Vector3f* start,Vector3f* end);
    };

    struct Sphere
    {
        Vector3f _center;
        f32 _radius;
        Sphere operator*(const Matrix4x4f &m) const;
    };


}


#endif // !GEOMETRY_H__
