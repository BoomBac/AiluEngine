#include "Physics/Collision.h"
#include "Framework/Math/Random.h"
#include "Render/Gizmo.h"
#include "pch.h"

namespace Ailu
{
    void DebugDrawer::DebugWireframe(const ContactData &data, f32 duration_sec)
    {
        if (data._penetration <= 0)
            return;
        Sphere s{data._point, 0.025f};
        if (duration_sec >= 0.0f)
        {
            Gizmo::DrawSphere(s, duration_sec, Colors::kYellow);
            Gizmo::DrawLine(s._center, s._center + data._normal * 0.1f, duration_sec, Colors::kYellow);
        }
        else
        {
            Gizmo::DrawSphere(s, Colors::kYellow);
            Gizmo::DrawLine(s._center, s._center + data._normal * 0.1f, Colors::kYellow);
        }
    }

    namespace CollisionDetection
    {
        namespace
        {
            // Returns the squared distance between point c and segment ab
            static f32 SqDistVector3fSegment(Vector3f a, Vector3f b, Vector3f c)
            {
                Vector3f ab = b - a, ac = c - a, bc = c - b;
                f32 e = DotProduct(ac, ab);
                // Handle cases where c projects outside ab
                if (e <= 0.0f) return DotProduct(ac, ac);
                f32 f = DotProduct(ab, ab);
                if (e >= f) return DotProduct(bc, bc);
                // Handle cases where c projects onto ab
                return DotProduct(ac, ac) - e * e / f;
            }
            float SqDistVector3fSegment(const Vector3f &p0, const Vector3f &p1, const Vector3f &point, Vector3f &closestPoint)
            {
                Vector3f segment = p1 - p0;
                Vector3f pointToP0 = point - p0;

                // 1. 计算线段的长度平方
                float segmentLengthSq = DotProduct(segment, segment);

                // 2. 如果线段的长度接近零，返回 p0 作为最近点
                if (segmentLengthSq < 0.00001f)
                {
                    closestPoint = p0;
                    return DotProduct(pointToP0, pointToP0);// 距离平方
                }

                // 3. 计算点在线段上的投影
                float t = DotProduct(pointToP0, segment) / segmentLengthSq;

                // 4. 限制 t 在 [0, 1] 之间，确保最近点在线段上
                t = std::clamp(t, 0.0f, 1.0f);

                // 5. 计算线段上的最近点
                closestPoint = p0 + t * segment;

                // 6. 返回最近点与原点之间的距离平方
                Vector3f diff = closestPoint - point;
                return DotProduct(diff, diff);// 距离平方
            }

            // from Real Time Collision Detection
            // Computes closest points C1 and C2 of S1(s)=P1+s*(Q1-P1) and
            // S2(t)=P2+t*(Q2-P2), returning s and t. Function result is squared
            // distance between between S1(s) and S2(t)
            static f32 ClosestPtSegmentSegment(Vector3f p1, Vector3f q1, Vector3f p2, Vector3f q2, float &s, float &t, Vector3f &c1, Vector3f &c2)
            {
                Vector3f d1 = q1 - p1;// Direction vector of segment S1
                Vector3f d2 = q2 - p2;// Direction vector of segment S2
                Vector3f r = p1 - p2;
                float a = DotProduct(d1, d1);// Squared length of segment S1, always nonnegative
                float e = DotProduct(d2, d2);// Squared length of segment S2, always nonnegative
                float f = DotProduct(d2, r);
                // Check if either or both segments degenerate into points
                if (a <= kEpsilon && e <= kEpsilon)
                {
                    // Both segments degenerate into points
                    s = t = 0.0f;
                    c1 = p1;
                    c2 = p2;
                    return DotProduct(c1 - c2, c1 - c2);
                }
                if (a <= kEpsilon)
                {
                    // First segment degenerates into a point
                    s = 0.0f;
                    t = f / e;// s = 0 => t = (b*s + f) / e = f / e
                    t = std::clamp(t, 0.0f, 1.0f);
                }
                else
                {
                    float c = DotProduct(d1, r);
                    if (e <= kEpsilon)
                    {
                        // Second segment degenerates into a point
                        t = 0.0f;
                        s = std::clamp(-c / a, 0.0f, 1.0f);// t = 0 => s = (b*t - c) / a = -c / a
                    }
                    else
                    {
                        // The general nondegenerate case starts here
                        float b = DotProduct(d1, d2);
                        float denom = a * e - b * b;// Always nonnegative
                        // If segments not parallel, compute closest point on L1 to L2 and
                        // clamp to segment S1. Else pick arbitrary s (here 0)
                        if (denom != 0.0f)
                        {
                            s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
                        }
                        else
                            s = 0.0f;
                        // Compute point on L2 closest to S1(s) using
                        // t = DotProduct((P1 + D1*s) - P2,D2) / DotProduct(D2,D2) = (b*s + f) / e
                        t = (b * s + f) / e;
                        // If t in [0,1] done. Else clamp t, recompute s for the new value
                        // of t using s = DotProduct((P2 + D2*t) - P1,D1) / DotProduct(D1,D1)= (t*b - c) / a
                        // and clamp s to [0, 1]
                        if (t < 0.0f)
                        {
                            t = 0.0f;
                            s = std::clamp(-c / a, 0.0f, 1.0f);
                        }
                        else if (t > 1.0f)
                        {
                            t = 1.0f;
                            s = std::clamp((b - c) / a, 0.0f, 1.0f);
                        }
                    }
                }
                c1 = p1 + d1 * s;
                c2 = p2 + d2 * t;
                return DotProduct(c1 - c2, c1 - c2);
            }

            static Vector3f ClosestPtOnSegment(const Vector3f &a, const Vector3f &b, const Vector3f &p)
            {
                Vector3f ab = b - a;
                float t = DotProduct(p - a, ab) / DotProduct(ab, ab);
                t = std::clamp(t, 0.0f, 1.0f);// 保证 t 在 [0, 1] 范围内
                return a + t * ab;
            }

            static Vector3f ToOBBLocalSpace(const Vector3f &point, const OBB &obb)
            {
                Vector3f localPoint = point - obb._center;
                return Vector3f(
                        DotProduct(localPoint, obb._local_axis[0]),
                        DotProduct(localPoint, obb._local_axis[1]),
                        DotProduct(localPoint, obb._local_axis[2]));
            }
            static Vector3f FromOBBLocalSpace(const Vector3f &localPoint, const OBB &obb)
            {
                return obb._center + localPoint.x * obb._local_axis[0] + localPoint.y * obb._local_axis[1] + localPoint.z * obb._local_axis[2];
            }
            static Vector3f TransformNormalFromOBBLocalSpace(const Vector3f &localNormal, const OBB &obb)
            {
                // 只考虑旋转，不考虑平移和缩放
                return localNormal.x * obb._local_axis[0] + localNormal.y * obb._local_axis[1] + localNormal.z * obb._local_axis[2];
            }

            static f32 ProjectOBBOnAxis(const Vector3f &axis, const OBB &obb)
            {
                return abs(DotProduct(axis, obb._local_axis[0]) * obb._half_axis_length.x) +
                       abs(DotProduct(axis, obb._local_axis[1]) * obb._half_axis_length.y) +
                       abs(DotProduct(axis, obb._local_axis[2]) * obb._half_axis_length.z);
            }
            static Vector3f s_shadowest_depth_axis;
            static bool TestAxis(const Vector3f &axis, const OBB &obb1, const OBB &obb2, const Vector3f &distanceVec, f32 &penetration)
            {
                float radius1 = ProjectOBBOnAxis(axis, obb1);
                float radius2 = ProjectOBBOnAxis(axis, obb2);
                float distance = abs(DotProduct(axis, distanceVec));
                bool isColliding = distance <= (radius1 + radius2);
                f32 depth = (radius1 + radius2) - distance;
                if (isColliding && depth < penetration)
                {
                    penetration = depth;
                    s_shadowest_depth_axis = axis;
                }
                return isColliding;
            }
            //输入一个点，找其在obb上的最小穿透深度
            static f32 TestObbAndPoint(const OBB &obb, const Vector3f &point, ContactData &contact)
            {
                Vector3f p = ToOBBLocalSpace(point, obb);
                Vector3f normal;
                f32 min_depth = obb._half_axis_length.x - abs(p.x);
                if (min_depth < 0.0f)
                    return -1.f;
                normal = obb._local_axis[0] * (p.x >= 0.0f ? 1.0f : -1.0f);
                f32 depth = obb._half_axis_length.y - abs(p.y);
                if (depth < 0.0f)
                    return -1.f;
                else if (depth < min_depth)
                {
                    min_depth = depth;
                    normal = obb._local_axis[1] * (p.y >= 0.0f ? 1.0f : -1.0f);
                }
                depth = obb._half_axis_length.z - abs(p.z);
                if (depth < 0.0f)
                    return -1.f;
                else if (depth < min_depth)
                {
                    min_depth = depth;
                    normal = obb._local_axis[2] * (p.z >= 0.0f ? 1.0f : -1.0f);
                }
                contact._normal = Normalize(normal);
                contact._point = point;
                contact._penetration = min_depth;
                return min_depth;
            }
        }// namespace

        ContactData Intersect(const AABB &a, const AABB &b)
        {
            ContactData contact;
            contact._is_collision = (a._min.x <= b._max.x && a._max.x >= b._min.x) &&
                                    (a._min.y <= b._max.y && a._max.y >= b._min.y) &&
                                    (a._min.z <= b._max.z && a._max.z >= b._min.z);
            return contact;
        }

        ContactData Intersect(const AABB &a, const Sphere &b)
        {
            ContactData contact;
            Vector3f P;
            P.x = std::clamp(b._center.x, a._min.x, a._max.x);
            P.y = std::clamp(b._center.y, a._min.y, a._max.y);
            P.z = std::clamp(b._center.z, a._min.z, a._max.z);
            f32 distSquared = SqrMagnitude(P - b._center);
            contact._is_collision = distSquared <= b._radius * b._radius;
            return contact;
        }

        ContactData Intersect(const AABB &a, const Capsule &c)
        {
            ContactData contact;
            Vector3f P;
            P.x = std::clamp(c._top.x, a._min.x, a._max.x);
            P.y = std::clamp(c._top.y, a._min.y, a._max.y);
            P.z = std::clamp(c._top.z, a._min.z, a._max.z);
            float dist2 = SqDistVector3fSegment(c._top, c._bottom, P);
            contact._is_collision = dist2 <= c._radius * c._radius;
            return contact;
        }

        ContactData Intersect(const Sphere &a, const Sphere &b)
        {
            ContactData contact;
            Vector3f ab = b._center - a._center;
            f32 distSquared = SqrMagnitude(ab);
            f32 radiusSum = a._radius + b._radius;
            f32 distance = sqrt(distSquared);
            contact._penetration = radiusSum - distance;
            contact._normal = Normalize(ab);
            contact._point = a._center + contact._normal * a._radius;
            contact._is_collision = distSquared <= radiusSum * radiusSum;
            return contact;
        }

        ContactData Intersect(const Sphere &s, const Capsule &c)
        {
            ContactData contact;
            // 1. 计算球心到胶囊线段之间的最近点距离
            Vector3f closestPointOnCapsule;
            float dist2 = SqDistVector3fSegment(c._top, c._bottom, s._center, closestPointOnCapsule);

            // 2. 判断是否发生碰撞
            float radiusSum = s._radius + c._radius;
            contact._is_collision = dist2 <= radiusSum * radiusSum;

            if (!contact._is_collision)
            {
                return contact;// 没有碰撞，直接返回
            }

            // 3. 计算碰撞法向量（从胶囊体到球心的方向）
            Vector3f delta = s._center - closestPointOnCapsule;
            float dist = sqrt(dist2);// 球心与胶囊线段最近点的实际距离
            Vector3f normal;

            if (dist > 0.0001f)
            {
                normal = delta / dist;// 归一化法向量
            }
            else
            {
                normal = Vector3f::kUp;// 防止法向量为零的情况，给一个默认值
            }

            // 4. 计算碰撞点为胶囊线段上的最近点 closestPointOnCapsule或其和球心的中点
            //contact._point = closestPointOnCapsule + normal * c._radius;
            contact._point = (closestPointOnCapsule + s._center) * 0.5f;

            // 5. 计算穿透深度（半径和减去两者之间的距离）
            contact._penetration = radiusSum - dist;

            // 6. 设置碰撞法向量
            contact._normal = normal;

            return contact;
        }

        ContactData Intersect(const Sphere &s, const OBB &obb)
        {
            ContactData contact;
            // 1. 将球体中心转换到 OBB 局部坐标系
            Vector3f localCenter = s._center - obb._center;

            // 2. 在 OBB 上找到最近点
            Vector3f closestPoint = obb._center;
            for (int i = 0; i < 3; i++)
            {
                // OBB 的第 i 轴
                Vector3f axis = obb._local_axis[i];

                // 计算球体中心在 OBB 第 i 轴上的投影
                float distance = DotProduct(localCenter, axis);

                // 将投影限制在 OBB 的半轴长度范围内
                if (distance > obb._half_axis_length[i]) distance = obb._half_axis_length[i];
                if (distance < -obb._half_axis_length[i]) distance = -obb._half_axis_length[i];

                // 最近点是在 OBB 中的该轴的点
                closestPoint += axis * distance;
            }

            // 3. 判断球体中心与 OBB 最近点的距离是否小于等于球体半径
            Vector3f closestToCenter = s._center - closestPoint;
            f32 distanceSquared = SqrMagnitude(closestToCenter);

            // 如果距离小于球体半径，则发生碰撞
            contact._is_collision = distanceSquared <= s._radius * s._radius;
            if (contact._is_collision)
            {
                contact._point = closestPoint;
                contact._normal = Normalize(closestToCenter);
                contact._penetration = s._radius - sqrt(distanceSquared);
            }
            return contact;
        }

        ContactData Intersect(const Capsule &a, const Capsule &b)
        {
            ContactData contact;
            Vector3f capsule_axis_a = Normalize(a._top - a._bottom);
            Vector3f capsule_axis_b = Normalize(b._top - b._bottom);
            // 1. 计算两个胶囊体内核线段之间的最近点距离和对应点 c1, c2
            f32 s, t;
            Vector3f c1, c2;
            f32 dist2 = ClosestPtSegmentSegment(a._top, a._bottom,b._top, b._bottom, s, t, c1, c2);

            // 2. 判断是否发生碰撞
            f32 radiusSum = a._radius + b._radius;
            contact._is_collision = dist2 <= radiusSum * radiusSum;

            if (!contact._is_collision)
                return contact;
            // 3. 计算碰撞法向量（c1 到 c2 的向量）,也就是从b指向a
            Vector3f delta = c1 - c2;
            float dist = sqrt(dist2);// 最近点之间的距离
            Vector3f normal;
            if (dist > kEpsilon)
                normal = delta / dist;// 归一化法向量
            else
                normal = Vector3f::kUp;// 防止法向量为零的情况，给一个默认值
            // 4. 计算碰撞点c1，c1-c2在a上的交点 (或 c1 和 c2 的中点)
            contact._point = (c1 + c2) * 0.5f;//c1 + -normal * a._radius;
            // 5. 计算穿透深度（胶囊体半径之和减去两最近点之间的距离）
            contact._penetration = radiusSum - dist;
            // 6. 设置碰撞法向量
            contact._normal = normal;
            return contact;
        }


        ContactData Intersect(const Capsule &c, const Sphere &s)
        {
            ContactData contact = Intersect(s, c);
            contact._normal = -contact._normal;
            return contact;
        }

        ContactData Intersect(const Capsule &a, const OBB &b)
        {
            ContactData contact;
            Vector3f capsule_axis = Normalize(a._top - a._bottom);
            // 1. 将胶囊端点转换到 OBB 的局部空间
            //Vector3f top_cbb_space = ToOBBLocalSpace(a._top + capsule_axis * a._radius, b);
            //Vector3f bot_cbb_space = ToOBBLocalSpace(a._bottom - capsule_axis * a._radius, b);
            Vector3f top_cbb_space = ToOBBLocalSpace(a._top, b);
            Vector3f bot_cbb_space = ToOBBLocalSpace(a._bottom, b);

            // 2. 在 OBB 的局部空间上找到最接近 top 端点的点 P（Clamp OBB 表面）
            Vector3f P;
            P.x = std::clamp(top_cbb_space.x, -b._half_axis_length.x, b._half_axis_length.x);
            P.y = std::clamp(top_cbb_space.y, -b._half_axis_length.y, b._half_axis_length.y);
            P.z = std::clamp(top_cbb_space.z, -b._half_axis_length.z, b._half_axis_length.z);

            // 3. 计算胶囊体线段与 OBB 最近点 P 的最短距离
            float dist2 = SqDistVector3fSegment(top_cbb_space, bot_cbb_space, P);

            // 4. 判断是否碰撞
            contact._is_collision = dist2 <= a._radius * a._radius;
            if (!contact._is_collision)
            {
                return contact;// 如果没有碰撞，直接返回
            }

            // 5. 计算胶囊线段上最近的点 Q
            Vector3f Q = ClosestPtOnSegment(top_cbb_space, bot_cbb_space, P);// Q 是胶囊体上最近的点

            // 6. 计算碰撞法向量（从 OBB 点到胶囊线段最近点的向量）
            Vector3f delta = P - Q;
            float dist = sqrt(dist2);
            Vector3f normal;

            if (dist > kEpsilon)
                normal = delta / dist;// 归一化法向量
            else
                normal = Vector3f::kUp;// 防止法向量为零的情况，给一个默认值

            // 7. 计算碰撞点为 OBB 表面点 P 和胶囊体最近点 Q 的中点
            contact._point = (P + Q) * 0.5f;

            // 8. 计算穿透深度（胶囊体半径减去距离）
            contact._penetration = a._radius - dist;

            // 9. 由于 P 和 Q 在 OBB 的局部空间，所以需要将它们转换回世界空间
            contact._point = FromOBBLocalSpace(contact._point, b);
            contact._normal = TransformNormalFromOBBLocalSpace(normal,b);

            return contact;
        }

        //SAT Method
        ContactData Intersect(const OBB &obb1, const OBB &obb2)
        {
            s_shadowest_depth_axis = Vector3f::kZero;
            ContactData contact;
            Vector3f distanceVec = obb2._center - obb1._center;

            // OBB1 的轴
            Vector3f axisA[3] = {obb1._local_axis[0], obb1._local_axis[1], obb1._local_axis[2]};
            // OBB2 的轴
            Vector3f axisB[3] = {obb2._local_axis[0], obb2._local_axis[1], obb2._local_axis[2]};
            f32 axis_penetration = std::numeric_limits<f32>::max();
            // 检查 6 个轴：OBB1 和 OBB2 的每个局部轴
            for (int i = 0; i < 3; ++i)
            {
                if (!TestAxis(axisA[i], obb1, obb2, distanceVec, axis_penetration))
                {
                    contact._is_collision = false;
                    return contact;
                }
                if (!TestAxis(axisB[i], obb1, obb2, distanceVec, axis_penetration))
                {
                    contact._is_collision = false;
                    return contact;
                }
            }

            // 检查 9 个轴：OBB1 和 OBB2 轴的叉积
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    Vector3f crossAxis = CrossProduct(axisA[i], axisB[j]);
                    if (Magnitude(crossAxis) > kEpsilon)
                    {// 叉积结果可能为零向量，需要排除
                        if (!TestAxis(crossAxis, obb1, obb2, distanceVec, axis_penetration))
                        {
                            contact._is_collision = false;
                            return contact;
                        }
                    }
                }
            }

            Vector3f a_face_normals[6] = {
                    Normalize(obb1._local_axis[0]),
                    Normalize(obb1._local_axis[1]),
                    Normalize(obb1._local_axis[2]),
                    Normalize(-obb1._local_axis[0]),
                    Normalize(-obb1._local_axis[1]),
                    Normalize(-obb1._local_axis[2]),
            };
            Vector3f b_face_normals[6] = {
                    Normalize(obb2._local_axis[0]),
                    Normalize(obb2._local_axis[1]),
                    Normalize(obb2._local_axis[2]),
                    Normalize(-obb2._local_axis[0]),
                    Normalize(-obb2._local_axis[1]),
                    Normalize(-obb2._local_axis[2]),
            };
            f32 angle = 100;
            Vector3f contact_face_n_a, contact_face_n_b;
            {
                Vector3f face_a;
                for (u16 i = 0; i < 6; i++)
                {
                    f32 dot = DotProduct(a_face_normals[i], s_shadowest_depth_axis);
                    if (dot < angle)
                    {
                        face_a = a_face_normals[i];
                        angle = dot;
                    }
                }
                Vector3f to_other = obb2._center - obb1._center;
                contact_face_n_a = DotProduct(face_a, to_other) > 0 ? face_a : -face_a;
            }
            angle = 100;
            {
                Vector3f face_a;
                for (u16 i = 0; i < 6; i++)
                {
                    f32 dot = DotProduct(b_face_normals[i], s_shadowest_depth_axis);
                    if (dot < angle)
                    {
                        face_a = b_face_normals[i];
                        angle = dot;
                    }
                }
                Vector3f to_other = obb1._center - obb2._center;
                contact_face_n_b = DotProduct(face_a, to_other) > 0 ? face_a : -face_a;
            }
            // Gizmo::DrawLine(obb1._center, obb1._center - face_a,Colors::kRed);
            // 如果所有轴都通过测试，则 OBB 发生碰撞
            contact._is_collision = true;
            contact._penetration = 0.0f;
            Gizmo::DrawLine(obb1._center, obb1._center + s_shadowest_depth_axis, Colors::kYellow);
            if (contact._is_collision)
            {
                ContactData deepest_contact;
                Vector3f vertices1[8], vertices2[8];
                OBB::ConstructVertices(obb2, vertices2);
                OBB::ConstructVertices(obb1, vertices1);
                f32 depth = -1.0f;
                Vector<Vector3f> a_face_vertices, b_face_vertices;
                a_face_vertices.reserve(4);
                b_face_vertices.reserve(4);
                for (u16 i = 0; i < 8; ++i)
                {
                    if (DotProduct(vertices1[i] - obb1._center, contact_face_n_a) > 0)
                    {
                        a_face_vertices.emplace_back(vertices1[i]);
                        //Gizmo::DrawLine(obb1._center, a_face_vertices.back(), Colors::kBlue);
                    }
                    if (DotProduct(vertices2[i] - obb2._center, contact_face_n_b) > 0)
                    {
                        b_face_vertices.emplace_back(vertices2[i]);
                        //Gizmo::DrawLine(obb2._center, b_face_vertices.back(), Colors::kBlue);
                    }
                }
                //点-面
                //只使用接触面上的4个点进行测试，这样只要测试8次
                for (u16 i = 0; i < 4; ++i)
                {
                    f32 cur_depth = TestObbAndPoint(obb1, b_face_vertices[i], deepest_contact);
                    if (cur_depth > 0 || cur_depth > depth)
                    {
                        depth = cur_depth;
                        contact._point = deepest_contact._point;
                        contact._normal = deepest_contact._normal;
                        contact._penetration = deepest_contact._penetration;
                    }
                }
                for (u16 i = 0; i < 4; ++i)
                {
                    f32 cur_depth = TestObbAndPoint(obb2, a_face_vertices[i], deepest_contact);
                    if (cur_depth > 0 || cur_depth > depth)
                    {
                        depth = cur_depth;
                        contact._point = deepest_contact._point;
                        contact._normal = deepest_contact._normal;
                        contact._penetration = deepest_contact._penetration;
                    }
                }
                if (depth > 0)
                    return contact;
                //边-边
                Vector3f edge_start1[4] = {
                        a_face_vertices[0],
                        a_face_vertices[1],
                        a_face_vertices[2],
                        a_face_vertices[3],
                };
                Vector3f edge_end1[4] = {
                        a_face_vertices[1],
                        a_face_vertices[2],
                        a_face_vertices[3],
                        a_face_vertices[0],
                };
                Vector3f edge_start2[4] = {
                        b_face_vertices[0],
                        b_face_vertices[1],
                        b_face_vertices[2],
                        b_face_vertices[3],
                };
                Vector3f edge_end2[4] = {
                        b_face_vertices[1],
                        b_face_vertices[2],
                        b_face_vertices[3],
                        b_face_vertices[0],
                };
                //OBB::ConstructEdges(obb1, vertices1, edge_start1, edge_end1);
                //OBB::ConstructEdges(obb2, vertices2, edge_start2, edge_end2);
                depth = -1.f;
                ContactData shadowest_contact;
                deepest_contact._penetration = std::numeric_limits<f32>::min();
                for (u16 i = 0; i < 4; i++)
                {
                    f32 cur_depth = 0.0;
                    for (u16 j = 0; j < 4; j++)
                    {
                        f32 s = 1.0f, t = 1.0f;
                        Vector3f c_a, c_b;
                        cur_depth = ClosestPtSegmentSegment(edge_start1[i], edge_end1[i], edge_start2[j], edge_end2[j], s, t, c_a, c_b);
                        if (s == 0 || t == 0)
                            continue;
                        f32 dis_offset = Magnitude(c_a - obb2._center) - Magnitude(c_b - obb2._center);
                        if (dis_offset < 0.f && cur_depth < 0.25f)//两个最近点距离小于0.5m才视作穿透
                        {
                            depth = cur_depth;
                            shadowest_contact._point = (c_a + c_b) * 0.5;
                            shadowest_contact._normal = Normalize(c_a - obb2._center);
                            shadowest_contact._penetration = depth;
                        }
                    }
                    if (shadowest_contact._penetration > deepest_contact._penetration)
                    {
                        deepest_contact._normal = shadowest_contact._normal;
                        deepest_contact._point = shadowest_contact._point;
                        deepest_contact._penetration = shadowest_contact._penetration;
                    }
                }
                contact._normal = deepest_contact._normal;
                contact._point = deepest_contact._point;
                contact._penetration = sqrt(deepest_contact._penetration);
            }
            return contact;
        }

        ContactData Intersect(const OBB &o, const Sphere &b)
        {
            auto contact = Intersect(b, o);
            contact._normal = -contact._normal;
            return contact;
        }

        ContactData Intersect(const OBB &o, const Capsule &c)
        {
            return Intersect(c, o);
        }
    }// namespace CollisionDetection
}// namespace Ailu
