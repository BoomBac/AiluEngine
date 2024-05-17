#include "pch.h"
#include "Render/Gizmo.h"
#include "Framework/Math/Geometry.h"

namespace Ailu
{
	//bool ViewFrustum::Conatin(const ViewFrustum& vf, const AABB& aabb)
 //   {
 //       static Array<Vector3f, 8> points;
 //       static Array<u32, 6> s_innear_point_mask;
 //       memset(s_innear_point_mask.data(), 0, sizeof(u32) * 6);
 //       Vector3f bl = aabb._min;
 //       Vector3f tr = aabb._max;
 //       Vector3f center = (bl + tr) / 2.0f;
 //       Vector3f extent = tr - bl;
 //       float l = abs(extent.x), w = abs(extent.y), h = abs(extent.z);
 //       points[0] = (center + Vector3f(l / 2, w / 2, h / 2));
 //       points[1] = (center + Vector3f(l / 2, w / 2, -h / 2));
 //       points[2] = (center + Vector3f(l / 2, -w / 2, h / 2));
 //       points[3] = (center + Vector3f(l / 2, -w / 2, -h / 2));
 //       points[4] = (center + Vector3f(-l / 2, w / 2, h / 2));
 //       points[5] = (center + Vector3f(-l / 2, w / 2, -h / 2));
 //       points[6] = (center + Vector3f(-l / 2, -w / 2, h / 2));
 //       points[7] = (center + Vector3f(-l / 2, -w / 2, -h / 2));
 //       for (int i = 0; i < 6; i++)
 //       {
 //           int outside_count = 0;
 //           for (int j = 0; j < 8; j++)
 //           {
 //              auto plane_center_to_conner = points[j] - vf._planes[i]._point;
 //              if (DotProduct(vf._planes[i]._normal, plane_center_to_conner) > 0) //点在外侧
 //              {
 //                  Gizmo::DrawLine(vf._planes[i]._point, points[j],Colors::kRed);
 //                  outside_count++;
 //              }
 //              else
 //              {
 //                  s_innear_point_mask[i] |= (1 << j);
 //              }
 //           }
 //           if (outside_count == 8)
 //               return false;
 //       }
 //       //u32 mask = 255;
 //       //for (int i = 0; i < 6; i++)
 //       //{
 //       //    if (s_innear_point_mask[i] != 255)
 //       //    {
 //       //        mask &= s_innear_point_mask[i];
 //       //    }
 //       //}
 //       //if (mask == 0)
 //       //    return false;
 //       return true;
 //   };
    bool ViewFrustum::Conatin(const ViewFrustum& vf, const AABB& aabb)
    {
        static Array<Vector3f, 8> points;
        Vector3f bl = aabb._min;
        Vector3f tr = aabb._max;
        bool edge_in_vf = true;
        for (int i = 0; i < 6; i++)
        {
            if (DotProduct(vf._planes[i]._normal, bl) + vf._planes[i]._distance > 0&& DotProduct(vf._planes[i]._normal, tr) + vf._planes[i]._distance > 0)
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
}
