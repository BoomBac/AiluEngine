/*
 * @author    : BoomBac
 * @created   : 2024.9
*/
#pragma once
#ifndef __COLLISION_H__
#define __COLLISION_H__

#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Geometry.h"
namespace Ailu
{
    struct ContactData
    {
        Vector3f _point = Vector3f::kZero;
        f32 _penetration = 0.0f;
        Vector3f _normal = Vector3f::kUp;
        bool _is_collision = false;
    };
    namespace DebugDrawer
    {
        void AILU_API DebugWireframe(const ContactData& data, f32 duration_sec = -1.0f);
    }

    namespace CollisionDetection
    {
        // from Real Time Collision Detection
        // Computes closest points C1 and C2 of S1(s)=P1+s*(Q1-P1) and
        // S2(t)=P2+t*(Q2-P2), returning s and t. Function result is squared
        // distance between between S1(s) and S2(t)
        AILU_API f32 ClosestPtSegmentSegment(Vector3f p1, Vector3f q1, Vector3f p2, Vector3f q2, f32 &s, f32 &t, Vector3f &c1, Vector3f &c2);
        AILU_API Vector3f ClosestPtOnSegment(const Vector3f &a, const Vector3f &b, const Vector3f &p);
        AILU_API f32 SqDistVector3fSegment(Vector3f a, Vector3f b, Vector3f c);
        AILU_API f32 SqDistVector3fSegment(const Vector3f &p0, const Vector3f &p1, const Vector3f &point, Vector3f &closestPoint);

        AILU_API ContactData Intersect(const AABB &a, const AABB &b);
        AILU_API ContactData Intersect(const AABB &a, const Sphere &b);
        AILU_API ContactData Intersect(const AABB &a, const Capsule &c);
        AILU_API ContactData Intersect(const Sphere &a, const Sphere &b);
        AILU_API ContactData Intersect(const Sphere &s, const Capsule &c);
        AILU_API ContactData Intersect(const Sphere &s, const OBB &obb);
        AILU_API ContactData Intersect(const Capsule &a, const Capsule &b);
        AILU_API ContactData Intersect(const Capsule &c, const Sphere &s);
        AILU_API ContactData Intersect(const Capsule &a, const OBB &b);
        AILU_API ContactData Intersect(const OBB &obb1, const OBB &obb2);
        AILU_API ContactData Intersect(const OBB &o, const Sphere &b);
        AILU_API ContactData Intersect(const OBB &o, const Capsule &c);
        AILU_API ContactData Intersect(const Ray &ray, const AABB &aabb);
        AILU_API ContactData Intersect(const Ray &ray, const OBB &obb);
        AILU_API ContactData Intersect(const Ray &ray, const Sphere &s);
        AILU_API ContactData Intersect(const Ray &ray, const Capsule &c);
        AILU_API ContactData Intersect(const Ray &ray, const Plane& plane);
    }// namespace CollisionUtils
}
#endif// !COLLISION_H__
