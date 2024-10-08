#include "pch.h"
#include "Render/Gizmo.h"
#include "Framework/Math/Geometry.h"

namespace Ailu
{
    AABB AABB::operator*(const Matrix4x4f &mat) const
    {
        Vector3f vertices[8];
        vertices[0] = _min;
        vertices[1] = Vector3f(_min.x, _min.y, _max.z);
        vertices[2] = Vector3f(_min.x, _max.y, _min.z);
        vertices[3] = Vector3f(_min.x, _max.y, _max.z);
        vertices[4] = Vector3f(_max.x, _min.y, _min.z);
        vertices[5] = Vector3f(_max.x, _min.y, _max.z);
        vertices[6] = Vector3f(_max.x, _max.y, _min.z);
        vertices[7] = _max;

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

    Capsule Capsule::operator*(const Matrix4x4f &m) const
    {
        Capsule s(*this);
        auto&& scale = m.LossyScale();
        s._radius *= (scale.x + scale.x) * 0.5f;
        s._height *= scale.y;
        TransformCoord(s._bottom,m);
        TransformCoord(s._top,m);
        return s;
    }
    
    Sphere Sphere::operator*(const Matrix4x4f &m) const
    {
        Sphere s(*this);
        s._radius *= std::max<f32>(std::max<f32>(m[0][0], m[2][2]), m[1][1]);
        TransformCoord(s._center, m);
        return s;
    }

    bool ViewFrustum::Conatin(const ViewFrustum& vf, const AABB& aabb)
    {
        static Array<Vector3f, 8> points;
        Vector3f bl = aabb._min;
        Vector3f tr = aabb._max;
        bool edge_in_vf = true;
        for (int i = 0; i < 6; i++)
        {
            if (DotProduct(vf._planes[i]._normal, bl) + vf._planes[i]._distance > 0 && DotProduct(vf._planes[i]._normal, tr) + vf._planes[i]._distance > 0)
            {
                edge_in_vf = false;
            }
        }
        if (edge_in_vf)
            return true;
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
                //LOG_INFO("Outside {}", i);
                return false;
            }
        }
        return true;
    };

    OBB OBB::operator *(const Matrix4x4f &m) const
    {
        OBB box(*this);
        TransformCoord(box._center, m);
        TransformNormal(box._local_axis[0],m);
        TransformNormal(box._local_axis[1],m);
        TransformNormal(box._local_axis[2],m);
        box._half_axis_length.x *= Magnitude(box._local_axis[0]);
        box._half_axis_length.y *= Magnitude(box._local_axis[1]);
        box._half_axis_length.z *= Magnitude(box._local_axis[2]);
        box._local_axis[0] = Normalize(box._local_axis[0]);
        box._local_axis[1] = Normalize(box._local_axis[1]);
        box._local_axis[2] = Normalize(box._local_axis[2]);
        return box;
    }
    void OBB::ConstructVertices(const OBB &obb, Vector3f* vertices)
    {
        const Vector3f &center = obb._center;
        const Vector3f &half = obb._half_axis_length;
        const Vector3f *axis = obb._local_axis;
        vertices[0] = center + axis[0] * half.x + axis[1] * half.y + axis[2] * half.z;// +X +Y +Z
        vertices[1] = center - axis[0] * half.x + axis[1] * half.y + axis[2] * half.z;// -X +Y +Z
        vertices[2] = center + axis[0] * half.x - axis[1] * half.y + axis[2] * half.z;// +X -Y +Z
        vertices[3] = center - axis[0] * half.x - axis[1] * half.y + axis[2] * half.z;// -X -Y +Z
        vertices[4] = center + axis[0] * half.x + axis[1] * half.y - axis[2] * half.z;// +X +Y -Z
        vertices[5] = center - axis[0] * half.x + axis[1] * half.y - axis[2] * half.z;// -X +Y -Z
        vertices[6] = center + axis[0] * half.x - axis[1] * half.y - axis[2] * half.z;// +X -Y -Z
        vertices[7] = center - axis[0] * half.x - axis[1] * half.y - axis[2] * half.z;// -X -Y -Z
    }
    void OBB::ConstructEdges(const OBB &obb, Vector3f vertices[8], Vector3f *start, Vector3f *end)
    {
        // Define pairs of vertex indices that form the edges
        static const std::array<std::array<int, 2>, 12> edgeIndices = {{
                {0, 1},
                {1, 3},
                {3, 2},
                {2, 0},// bottom face
                {4, 5},
                {5, 7},
                {7, 6},
                {6, 4},// top face
                {0, 4},
                {1, 5},
                {2, 6},
                {3, 7}// vertical edges
        }};

        // Construct edges
        for (u16 i = 0; i < 12; ++i)
        {
            auto& indices = edgeIndices[i]; 
            Vector3f start_p = vertices[indices[0]];
            Vector3f end_p = vertices[indices[1]];
            //Vector3f dir = end - start;
            start[i] = start_p;
            end[i] = end_p;
        }
    }
}
