#pragma once
#ifndef __GIZMO_H__
#define __GIZMO_H__
#include "Framework/Math/Geometry.h"
#include "Buffer.h"
#include "Camera.h"
#include "CommandBuffer.h"

namespace Ailu
{
    class AILU_API Gizmo
    {
    private:
        inline static List<std::tuple<std::function<void()>, std::chrono::system_clock::time_point, std::chrono::seconds>> s_geometry_draw_list;
    public:
        static void Init();
        static void DrawLine(const Vector3f &from, const Vector3f &to, Color color = Gizmo::s_color);
        static void DrawLine(const Vector3f &from, const Vector3f &to, f32 duration_sec, Color color = Gizmo::s_color);
        static void DrawCircle(const Vector3f &center, float radius, u16 num_segments, Color color = Gizmo::s_color, Matrix4x4f mat = BuildIdentityMatrix());
        static void DrawCircle(const Vector3f &center, float radius, u16 num_segments, f32 duration_sec, Color color = Gizmo::s_color, Matrix4x4f mat = BuildIdentityMatrix());
        static void DrawCircle(const Vector3f &center, const Vector3f &forward, const Vector3f &right, u16 num_segments = kSegments, Color color = Gizmo::s_color);
        static void DrawSphere(const Sphere &s, Color color = Gizmo::s_color);
        static void DrawSphere(const Sphere &s, f32 duration_sec,Color color = Gizmo::s_color);
        static void DrawAABB(const AABB &aabb, Color color = Gizmo::s_color);
        static void DrawAABB(const Vector3f &minPoint, const Vector3f &maxPoint, Color color = Gizmo::s_color);
        static void DrawOBB(const OBB &obb, Color color = Gizmo::s_color);
        static void DrawCube(const Vector3f &center, const Vector3f &size, u32 sec_duration, Color color = Gizmo::s_color);
        static void DrawLine(const Vector3f &from, const Vector3f &to, const Color &color_from, const Color &color_to);
        static void DrawGrid(const int &grid_size, const int &grid_spacing, const Vector3f &center, Color color);
        static void DrawCube(const Vector3f &center, const Vector3f &size, Vector4f color = Colors::kGray);
        static void DrawCapsule(const Capsule &capsule, Color color = Gizmo::s_color);
        static void DrawCylinder(const Vector3f &start, const Vector3f end, f32 radius, u16 segments = kSegments,Color color = Gizmo::s_color);

        static void Submit(CommandBuffer *cmd);

        //当GizmoPass未激活时，实际可能还会有数据在填充，所以将顶点偏移置空。一定要调用
        //当激活时，实际就不用调用
        static void EndFrame();

    public:
        inline static Color s_color = Colors::kGray;
    private:
        static void DrawHemisphere(const Vector3f &center, const Vector3f &axis, f32 radius, Color color = Gizmo::s_color);
    private:
        inline static u32 _vertex_num = 0u;
        inline static const int kSegments = 24;
        inline static Ref<IDynamicVertexBuffer> p_buf = nullptr;
    };
};// namespace Ailu

#endif // !GIZMO_H__

