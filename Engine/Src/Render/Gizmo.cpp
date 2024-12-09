#include "pch.h"
#include "Render/Gizmo.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu
{
    void Gizmo::Init()
    {
        VertexBufferLayout layout = {
                {"POSITION",EShaderDateType::kFloat3,0},
                {"COLOR",EShaderDateType::kFloat4,1},
        };
        s_world_vbuf.reset(IVertexBuffer::Create(layout));
        s_screen_vbuf.reset(IVertexBuffer::Create(layout));
        s_world_vbuf->SetStream(nullptr, kMaxVertexNum * sizeof(Vector3f), 0, true);
        s_world_vbuf->SetStream(nullptr, kMaxVertexNum * sizeof(Vector4f), 1, true);
        s_screen_vbuf->SetStream(nullptr, kMaxVertexNum * sizeof(Vector3f), 0, true);
        s_screen_vbuf->SetStream(nullptr, kMaxVertexNum * sizeof(Vector4f), 1, true);
        s_world_pos.resize(kMaxVertexNum);
        s_world_color.resize(kMaxVertexNum);
        s_screen_pos.resize(kMaxVertexNum);
        s_screen_color.resize(kMaxVertexNum);
        s_color.a = 0.75f;
        s_line_drawer = g_pResourceMgr->Get<Material>(L"Runtime/Material/Gizmo");
    }

    void Gizmo::DrawLine(const Vector3f &from, const Vector3f &to, Color color)
    {
        if (Gizmo::s_color.a < 0.1f) return;
        DrawLine(from, to, color, color);
    }

    void Gizmo::DrawLine(const Vector3f &from, const Vector3f &to, f32 duration_sec, Color color)
    {
        s_geometry_draw_list.emplace_back(std::make_tuple([=]()
                                                          { DrawLine(from, to, color); }, std::chrono::system_clock::now(), (u32)duration_sec));
    }

    void Gizmo::DrawCircle(const Vector3f &center, float radius, u16 num_segments, Color color, Matrix4x4f mat)
    {
        if (Gizmo::s_color.a < 0.1f) return;
        float angleIncrement = 360.0f / static_cast<float>(num_segments);

        for (int i = 0; i < num_segments; ++i)
        {
            float angle1 = ToRadius(angleIncrement * static_cast<float>(i));
            float angle2 = ToRadius(angleIncrement * static_cast<float>(i + 1));
            Vector3f point1(center.x + radius * cos(angle1), center.y, center.z + radius * sin(angle1));
            Vector3f point2(center.x + radius * cos(angle2), center.y, center.z + radius * sin(angle2));
            point1 -= center;
            point2 -= center;
            TransformCoord(point1, mat);
            TransformCoord(point2, mat);
            point1 += center;
            point2 += center;
            DrawLine(point1, point2, color);
        }
    }
    void Gizmo::DrawCircle(const Vector3f &center, float radius, u16 num_segments, f32 duration_sec, Color color, Matrix4x4f mat)
    {
        s_geometry_draw_list.emplace_back(std::make_tuple([=]()
                                                          { DrawCircle(center, radius, num_segments, color, mat); }, std::chrono::system_clock::now(), (u32) duration_sec));
    }
    void Gizmo::DrawSphere(const Sphere &s, Color color)
    {
        Gizmo::DrawCircle(s._center, s._radius, 24, color, MatrixRotationX(ToRadius(90.0f)));
        Gizmo::DrawCircle(s._center, s._radius, 24, color, MatrixRotationZ(ToRadius(90.0f)));
        Gizmo::DrawCircle(s._center, s._radius, 24, color);
    }
    void Gizmo::DrawSphere(const Sphere &s, f32 duration_sec, Color color)
    {
        DrawCircle(s._center, s._radius, 24, duration_sec,color, MatrixRotationX(ToRadius(90.0f)));
        DrawCircle(s._center, s._radius, 24, duration_sec,color, MatrixRotationZ(ToRadius(90.0f)));
        DrawCircle(s._center, s._radius, 24, duration_sec,color);
    }
    void Gizmo::DrawAABB(const AABB &aabb, Color color)
    {
        DrawAABB(aabb._min, aabb._max, color);
    }

    void Gizmo::DrawAABB(const Vector3f &minPoint, const Vector3f &maxPoint, Color color)
    {
        if (Gizmo::s_color.a < 0.1f) return;
        Vector3f vertices[8];
        vertices[0] = minPoint;
        vertices[1] = Vector3f(minPoint.x, minPoint.y, maxPoint.z);
        vertices[2] = Vector3f(minPoint.x, maxPoint.y, minPoint.z);
        vertices[3] = Vector3f(minPoint.x, maxPoint.y, maxPoint.z);
        vertices[4] = Vector3f(maxPoint.x, minPoint.y, minPoint.z);
        vertices[5] = Vector3f(maxPoint.x, minPoint.y, maxPoint.z);
        vertices[6] = Vector3f(maxPoint.x, maxPoint.y, minPoint.z);
        vertices[7] = maxPoint;

        DrawLine(vertices[0], vertices[1], color);
        DrawLine(vertices[0], vertices[2], color);
        DrawLine(vertices[0], vertices[4], color);
        DrawLine(vertices[1], vertices[3], color);
        DrawLine(vertices[1], vertices[5], color);
        DrawLine(vertices[2], vertices[3], color);
        DrawLine(vertices[2], vertices[6], color);
        DrawLine(vertices[3], vertices[7], color);
        DrawLine(vertices[4], vertices[5], color);
        DrawLine(vertices[4], vertices[6], color);
        DrawLine(vertices[5], vertices[7], color);
        DrawLine(vertices[6], vertices[7], color);
    }

    void Gizmo::DrawOBB(const OBB &obb, Color color)
    {
        Vector3f vertices[8];
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
        DrawLine(vertices[0], vertices[1], color);
        DrawLine(vertices[1], vertices[3], color);
        DrawLine(vertices[3], vertices[2], color);
        DrawLine(vertices[2], vertices[0], color);
        DrawLine(vertices[4], vertices[5], color);
        DrawLine(vertices[5], vertices[7], color);
        DrawLine(vertices[7], vertices[6], color);
        DrawLine(vertices[6], vertices[4], color);
        DrawLine(vertices[0], vertices[4], color);
        DrawLine(vertices[1], vertices[5], color);
        DrawLine(vertices[2], vertices[6], color);
        DrawLine(vertices[3], vertices[7], color);
    }

    void Gizmo::DrawCube(const Vector3f &center, const Vector3f &size, u32 sec_duration, Color color)
    {
        s_geometry_draw_list.emplace_back(std::make_tuple([=]()
                                                          { DrawCube(center, size, color); }, std::chrono::system_clock::now(), std::chrono::seconds(sec_duration)));
    }
    void Gizmo::DrawLine(const Vector3f &from, const Vector3f &to, const Color &color_from, const Color &color_to)
    {
        if (Gizmo::s_color.a < 0.1f) return;
        if (s_world_vertex_num + 2 >= RenderConstants::KMaxDynamicVertexNum)
        {
            LOG_WARNING("Gizmo vertex buffer is full, some lines may not be drawn. Please increase the size of the vertex buffer.")
            return;
        }
        s_world_pos[s_world_vertex_num] = from;
        s_world_pos[s_world_vertex_num+1] = to;
        s_world_color[s_world_vertex_num] = color_from * s_color.a;
        s_world_color[s_world_vertex_num+1] = color_to * s_color.a;
        s_world_vertex_num += 2;
    }

    void Gizmo::DrawGrid(const int &grid_size, const int &grid_spacing, const Vector3f &center, Color color)
    {
        if (Gizmo::s_color.a < 0.1f) return;
        float halfWidth = static_cast<float>(grid_size * grid_spacing) * 0.5f;
        float halfHeight = static_cast<float>(grid_size * grid_spacing) * 0.5f;
        static Color lineColor = color;

        for (int i = -grid_size / 2; i <= grid_size / 2; ++i)
        {
            float xPos = static_cast<float>(i * grid_spacing) + center.x;
            lineColor.a = color.a * s_color.a;
            lineColor.a *= Lerp(1.0f, 0.0f, abs(xPos - center.x) / halfWidth);
            auto color_start = lineColor;
            auto color_end = lineColor;
            if (DotProduct(Camera::sCurrent->Forward(), {0, 0, 1}) > 0)
            {
                color_start.a *= 0.8f;
                color_end.a *= 0.2f;
            }
            else
            {
                color_start.a *= 0.2f;
                color_end.a *= 0.8f;
            }
            Gizmo::DrawLine(Vector3f(xPos, center.y, -halfHeight + center.z),
                            Vector3f(xPos, center.y, halfHeight + center.z), color_start, color_end);

            float zPos = static_cast<float>(i * grid_spacing) + center.z;
            lineColor.a = color.a * s_color.a;
            lineColor.a *= Lerp(1.0f, 0.0f, abs(zPos - center.z) / halfWidth);
            color_start = lineColor;
            color_end = lineColor;
            auto right = Camera::sCurrent->Right();
            if (DotProduct(Camera::sCurrent->Forward(), {1, 0, 0}) > 0)
            {
                color_start.a *= 0.8f;
                color_end.a *= 0.2f;
            }
            else
            {
                color_start.a *= 0.2f;
                color_end.a *= 0.8f;
            }
            Gizmo::DrawLine(Vector3f(-halfWidth + center.x, center.y, zPos),
                            Vector3f(halfWidth + center.x, center.y, zPos), color_start, color_end);
        }
    }

    void Gizmo::DrawCube(const Vector3f &center, const Vector3f &size, Vector4f color)
    {
        if (Gizmo::s_color.a < 0.1f) return;
        // 计算立方体的 8 个顶点
        Vector3f v0 = center + Vector3f(-size.x, -size.y, -size.z);
        Vector3f v1 = center + Vector3f(size.x, -size.y, -size.z);
        Vector3f v2 = center + Vector3f(size.x, size.y, -size.z);
        Vector3f v3 = center + Vector3f(-size.x, size.y, -size.z);
        Vector3f v4 = center + Vector3f(-size.x, -size.y, size.z);
        Vector3f v5 = center + Vector3f(size.x, -size.y, size.z);
        Vector3f v6 = center + Vector3f(size.x, size.y, size.z);
        Vector3f v7 = center + Vector3f(-size.x, size.y, size.z);

        // 绘制底面四条边
        DrawLine(v0, v1, color);
        DrawLine(v1, v2, color);
        DrawLine(v2, v3, color);
        DrawLine(v3, v0, color);

        // 绘制顶面四条边
        DrawLine(v4, v5, color);
        DrawLine(v5, v6, color);
        DrawLine(v6, v7, color);
        DrawLine(v7, v4, color);

        // 绘制连接顶面和底面的四条边
        DrawLine(v0, v4, color);
        DrawLine(v1, v5, color);
        DrawLine(v2, v6, color);
        DrawLine(v3, v7, color);
    }

    void Gizmo::DrawCapsule(const Capsule &capsule, Color color)
    {
        DrawCylinder(capsule._top, capsule._bottom, capsule._radius,4u,color);
        DrawHemisphere(capsule._top, capsule._bottom, capsule._radius, color);
        DrawHemisphere(capsule._bottom, capsule._top, capsule._radius, color);
    }
    void Gizmo::DrawCylinder(const Vector3f &start, const Vector3f end, f32 radius, u16 segments,Color color)
    {
        //Vector3f forward = Vector3.Slerp(up, -up, 0.5f).normalized * radius;// 找到轴向垂直方向的一个向量
        //Vector3f right = Vector3.Cross(up, forward).normalized * radius;    // 找到另一个垂直方向的向量
        Vector3f up =  Normalize(end - start);
        Vector3f forward_base = Vector3f::kForward;
        if (forward_base == up || forward_base == (-up))
            forward_base = Vector3f::kRight;
        Vector3f forward = CrossProduct(forward_base, up);
        Vector3f right = Normalize(CrossProduct(up, forward));
        forward = Normalize(CrossProduct(right, up));
        forward *= radius;
        right *= radius;
        up *= radius;

        DrawCircle(start, forward, right, segments*4, color);
        DrawCircle(end, forward, right, segments*4, color);

        for (int i = 0; i < segments; i++)
        {
            float theta = (float) i / segments * kPi * 2;
            float nextTheta = (float) (i + 1) / segments * kPi * 2;
            Vector3f offset1 = right * std::cos(theta) +     forward * std::sin(theta);
            Vector3f offset2 = right * std::cos(nextTheta) + forward * std::sin(nextTheta);
            DrawLine(start + offset1, end + offset1, color);
            DrawLine(start + offset2, end + offset2, color);
        }
    }
    void Gizmo::DrawHemisphere(const Vector3f &center, const Vector3f &axis, f32 radius, Color color)
    {
        Vector3f up = Normalize(axis - center);
        Vector3f forward_base = Vector3f::kForward;
        if (forward_base == up || forward_base == (-up))
            forward_base = Vector3f::kRight;
        Vector3f forward = Normalize(CrossProduct(forward_base, up));
        Vector3f right = Normalize(CrossProduct(up, forward));
        Vector3f lastPoint;
        radius *= -1;
        for (int i = 0; i <= kSegments; i++)
        {
            float theta = (float) i / kSegments * kPi;// 从 0 到 π，表示半球的上半部分
            Vector3f point = center + forward * radius * std::cos(theta) + up * radius * std::sin(theta);
            if (i > 0)
                DrawLine(lastPoint, point, color);
            lastPoint = point;
        }
        lastPoint = center + right * radius;// 左右弧线的起始点
        for (int i = 0; i <= kSegments; i++)
        {
            float theta = (float) i / kSegments * kPi;// 从 0 到 π
            Vector3f point = center + right * radius * std::cos(theta) + up * radius * std::sin(theta);
            if (i > 0)
                DrawLine(lastPoint, point, color);
            lastPoint = point;
        }
    }

    void Gizmo::DrawCircle(const Vector3f &center, const Vector3f &forward, const Vector3f &right, u16 num_segments,Color color)
    {
        u16 sgem = num_segments;
        Vector3f lastPoint = center + right * std::cos(0) + forward * std::sin(0);
        for (int i = 1; i <= sgem; i++)
        {
            float theta = (float) i / sgem * kPi * 2;
            Vector3f nextPoint = center + right * std::cos(theta) + forward * std::sin(theta);
            DrawLine(lastPoint, nextPoint, color);
            lastPoint = nextPoint;
        }
    }
    void Gizmo::DrawLine(const Vector2f &from, const Vector2f &to, Color color)
    {
        if (Gizmo::s_color.a < 0.1f) return;
        if (s_screen_vertex_num + 2 >= RenderConstants::KMaxDynamicVertexNum)
        {
            LOG_WARNING("Gizmo vertex buffer is full, some lines may not be drawn. Please increase the size of the vertex buffer.")
            return;
        }
        s_screen_pos[s_screen_vertex_num] = Vector3f{from,-48.5f}; //default camera pos at z is -50,near is 1
        s_screen_pos[s_screen_vertex_num+1] = Vector3f{to,-48.5f};
        s_screen_color[s_screen_vertex_num] = color * s_color.a;
        s_screen_color[s_screen_vertex_num+1] = color * s_color.a;
        s_screen_vertex_num += 2;
    }
    void Gizmo::DrawRect(const Vector2f &top_left, const Vector2f &bottom_right, Color color)
    {
        if (Gizmo::s_color.a < 0.1f) return;
        Vector2f top_right = {bottom_right.x, top_left.y};
        Vector2f bottom_left = {top_left.x, bottom_right.y};
        DrawLine(top_left, top_right, color);
        DrawLine(top_right, bottom_right, color);
        DrawLine(bottom_right, bottom_left, color);
        DrawLine(bottom_left, top_left, color);
    }

    void Gizmo::Submit(CommandBuffer *cmd, const RenderingData &data)
    {
        if (s_world_vertex_num > RenderConstants::KMaxDynamicVertexNum)
        {
            LOG_WARNING("[Gizmo] vertex num > KMaxDynamicVertexNum {},draw nothing", RenderConstants::KMaxDynamicVertexNum);
            s_world_vertex_num = 0;
            return;
        }
        if (s_world_vertex_num > 0)
        {
            auto cur_time = std::chrono::system_clock::now();
            s_geometry_draw_list.erase(std::remove_if(s_geometry_draw_list.begin(), s_geometry_draw_list.end(),
                                                      [&](const auto &it) -> bool
                                                      {
                                                          return cur_time - std::get<2>(it) > std::get<1>(it);
                                                      }),
                                       s_geometry_draw_list.end());
            for (auto &it: s_geometry_draw_list)
            {
                auto &f = std::get<0>(it);
                f();
            }
            s_world_vbuf->SetData((u8*)s_world_pos.data(), s_world_vertex_num * sizeof(Vector3f),0u,0);
            s_world_vbuf->SetData((u8*)s_world_color.data(), s_world_vertex_num * sizeof(Color), 1u, 0);
            s_screen_vbuf->SetData((u8*)s_screen_pos.data(), s_screen_vertex_num * sizeof(Vector3f), 0u, 0);
            s_screen_vbuf->SetData((u8*)s_screen_color.data(), s_screen_vertex_num * sizeof(Color), 1u, 0);
            cmd->DrawInstanced(s_world_vbuf.get(), nullptr,s_line_drawer,0,1);
            _screen_camera_cb._MatrixVP = Camera::GetDefaultOrthogonalViewProj(data._width, data._height);
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera,&_screen_camera_cb, RenderConstants::kPerCameraDataSize);
            cmd->DrawInstanced(s_screen_vbuf.get(), nullptr,s_line_drawer,0,1);
        }
        s_world_vertex_num = 0u;
        s_screen_vertex_num = 0u;
    }
    //当GizmoPass未激活时，实际可能还会有数据在填充，所以将顶点偏移置空。一定要调用
    //当激活时，实际就不用调用
    void Gizmo::EndFrame()
    {

    }
}
