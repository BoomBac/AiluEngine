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
        ContactData Intersect(const AABB &a, const AABB &b);
        ContactData Intersect(const AABB &a, const Sphere &b);
        ContactData Intersect(const AABB &a, const Capsule &c);
        ContactData Intersect(const Sphere &a, const Sphere &b);
        ContactData Intersect(const Sphere &s, const Capsule &c);
        ContactData Intersect(const Sphere &s, const OBB &obb);
        ContactData Intersect(const Capsule &a, const Capsule &b);
        ContactData Intersect(const Capsule &c, const Sphere &s);
        ContactData Intersect(const Capsule &a, const OBB &b);
        ContactData Intersect(const OBB &obb1, const OBB &obb2);
        ContactData Intersect(const OBB &o, const Sphere &b);
        ContactData Intersect(const OBB &o, const Capsule &c);
    }// namespace CollisionUtils
}
#endif// !COLLISION_H__
