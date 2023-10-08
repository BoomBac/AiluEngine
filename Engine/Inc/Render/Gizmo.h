#pragma once
#ifndef __GIZMO_H__
#define __GIZMO_H__
#include "Framework/Math/ALMath.hpp"
#include "RHI/DX12/D3DContext.h"
#include "RenderCommand.h"
#include "Buffer.h"
#include "Camera.h"

namespace Ailu
{
    class Gizmo
    {
    public:
        static void Init()
        {
            p_buf = DynamicVertexBuffer::Create();
            s_color.a = 0.75f;
        }

        static void DrawLine(const Vector3f& from, const Vector3f& to, Color color = Gizmo::s_color)
        {
            if (Gizmo::s_color.a < 0.1f) return;
            DrawLine(from, to, color, color);
        }

        static void DrawCircle(const Vector3f& center, float radius, int num_segments, Color color = Gizmo::s_color, Matrix4x4f mat = BuildIdentityMatrix())
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

        static void DrawAABB(const Vector3f& minPoint, const Vector3f& maxPoint, Color color = Gizmo::s_color)
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

        static void DrawLine(const Vector3f& from, const Vector3f& to, const Color& color_from, const Color& color_to)
        {
            if (Gizmo::s_color.a < 0.1f) return;
            static float vbuf[6];
            static float cbuf[8];
            vbuf[0] = from.x;
            vbuf[1] = from.y;
            vbuf[2] = from.z;
            vbuf[3] = to.x;
            vbuf[4] = to.y;
            vbuf[5] = to.z;
            memcpy(cbuf, color_from.data, 16);
            memcpy(cbuf + 4, color_to.data, 16);
            cbuf[3] *= s_color.a;
            cbuf[7] *= s_color.a;
            p_buf->AppendData(vbuf, 6, cbuf, 8);
            _vertex_num += 2;
        }

        static void DrawGrid(const int& grid_size, const int& grid_spacing, const Vector3f& center, Color color)
        {
            if (Gizmo::s_color.a < 0.1f) return;
            float halfWidth = static_cast<float>(grid_size * grid_spacing) * 0.5f;
            float halfHeight = static_cast<float>(grid_size * grid_spacing) * 0.5f;
            static Color lineColor = color;

            for (int i = -grid_size / 2; i <= grid_size / 2; ++i)
            {
                float xPos = static_cast<float>(i * grid_spacing) + center.x;
                lineColor.a = color.a * s_color.a;
                lineColor.a *= lerpf(1.0f, 0.0f, abs(xPos - center.x) / halfWidth);
                auto color_start = lineColor;
                auto color_end = lineColor;
                if (DotProduct(Camera::sCurrent->GetForward(), { 0,0,1 }) > 0)
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
                lineColor.a *= lerpf(1.0f, 0.0f, abs(zPos - center.z) / halfWidth);
                color_start = lineColor;
                color_end = lineColor;
                auto right = Camera::sCurrent->GetRight();
                if (DotProduct(Camera::sCurrent->GetForward(), { 1,0,0 }) > 0)
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

        static void DrawCube(const Vector3f& center, const Vector3f& size, Vector4f color = Colors::kGray)
        {
            if (Gizmo::s_color.a < 0.1f) return;
            static float vbuf[72];
            static float cbuf[96];
            float halfX = size.x * 0.5f;
            float halfY = size.y * 0.5f;
            float halfZ = size.z * 0.5f;
            float verticesData[72] = {
                center.x - halfX, center.y - halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z - halfZ,
                center.x + halfX, center.y - halfY, center.z - halfZ,
                center.x - halfX, center.y - halfY, center.z - halfZ,
                center.x - halfX, center.y - halfY, center.z - halfZ,
                center.x - halfX, center.y - halfY, center.z + halfZ,
                center.x - halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y + halfY, center.z - halfZ,
                center.x + halfX, center.y + halfY, center.z - halfZ,
                center.x - halfX, center.y + halfY, center.z - halfZ,
                center.x - halfX, center.y + halfY, center.z - halfZ,
                center.x - halfX, center.y + halfY, center.z + halfZ,
                center.x - halfX, center.y - halfY, center.z + halfZ,
                center.x - halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z + halfZ,
                center.x + halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z - halfZ,
                center.x + halfX, center.y + halfY, center.z - halfZ,
                center.x - halfX, center.y - halfY, center.z - halfZ,
                center.x - halfX, center.y + halfY, center.z - halfZ,
            };
            float colorData[96];
            for (int i = 0; i < 96; i += 4)
            {
                colorData[i] = color.x;
                colorData[i + 1] = color.y;
                colorData[i + 2] = color.z;
                colorData[i + 3] = color.w;
            }
            memcpy(vbuf, verticesData, sizeof(verticesData));
            memcpy(cbuf, colorData, sizeof(colorData));
            p_buf->AppendData(vbuf, 72, cbuf, 96);
            _vertex_num += 24;
        }

        static void Submit()
        {
            if (_vertex_num > RenderConstants::KMaxDynamicVertexNum)
            {
                LOG_WARNING("[Gizmo] vertex num > KMaxDynamicVertexNum {},draw nothing", RenderConstants::KMaxDynamicVertexNum);
                _vertex_num = 0;
                return;
            }
            if (_vertex_num > 0)
            {
                p_buf->UploadData();
                p_buf->Bind();
                D3DContext::GetInstance()->DrawInstanced(_vertex_num, 1);
            }
            _vertex_num = 0;
        }

    public:
        inline static Color s_color = Colors::kGray;
    private:
        inline static uint32_t _vertex_num = 0u;
        inline static Ref<DynamicVertexBuffer> p_buf = nullptr;
    };
}

#endif // !GIZMO_H__

