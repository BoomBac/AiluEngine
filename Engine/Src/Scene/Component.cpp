#include "Scene/Component.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Gizmo.h"
#include "Render/2D/Sprite.h"
#include "pch.h"

namespace Ailu::ECS
{
    namespace
    {
        std::mutex s_component_type_mutex;
        std::unordered_map<String, ComponentTypeId> s_component_type_ids;
        // Indexed by ComponentTypeId (ids are assigned sequentially from 0).
        Vector<String> s_component_type_names;
        ComponentTypeId s_next_component_type_id = 0;
    }

    ComponentTypeId RegisterComponentType(StringView stable_name)
    {
        std::lock_guard lock(s_component_type_mutex);
        const auto [it, inserted] = s_component_type_ids.emplace(String(stable_name), s_next_component_type_id);
        if (inserted)
        {
            s_component_type_names.emplace_back(stable_name);
            ++s_next_component_type_id;
        }
        return it->second;
    }

    StringView GetComponentStableName(ComponentTypeId type_id)
    {
        std::lock_guard lock(s_component_type_mutex);
        if (type_id < s_component_type_names.size())
            return s_component_type_names[type_id];
        return {};
    }
}

namespace Ailu::ECS
{
    using namespace Render;
    CLightProbe::CLightProbe()
    {
        _cubemap = RenderTexture::Create(512, "light probe", Ailu::Render::ERenderTargetFormat::kDefaultHDR, true, true, true);
        _pass = MakeRef<CubeMapGenPass>(_cubemap.get());
        _debug_material = nullptr;
    }

    Sphere CCollider::AsShpere(const CCollider &c)
    {
        Sphere s;
        s._center = c._center;
        s._radius = c._param.x;
        return s;
    }
    OBB CCollider::AsBox(const CCollider &c)
    {
        OBB box;
        box._center = c._center;
        box._half_axis_length = c._param;
        return box;
    }
    Capsule CCollider::AsCapsule(const CCollider &c)
    {
        static const Vector3f dir[3] = {Vector3f::kRight, Vector3f::kUp, Vector3f::kForward};
        Capsule s;
        s._height = c._param.y;
        s._top = c._center + dir[(u16) c._param.z] * s._height * 0.5f;
        s._bottom = c._center - dir[(u16) c._param.z] * s._height * 0.5f;
        s._radius = c._param.x;
        return s;
    }
}// namespace Ailu

namespace Ailu
{
    using namespace Render;
    namespace DebugDrawer
    {
        void DebugWireframe(const ECS::CCollider &c, const Matrix4x4f &mat, Color color)
        {
            if (c._type == ECS::EColliderType::kBox)
            {
                Gizmo::DrawOBB(ECS::CCollider::AsBox(c) * mat, color);
            }
            else if (c._type == ECS::EColliderType::kSphere)
            {
                Gizmo::DrawSphere(ECS::CCollider::AsShpere(c) * mat, color);
            }
            else if (c._type == ECS::EColliderType::kCapsule)
            {
                Gizmo::DrawCapsule(ECS::CCollider::AsCapsule(c) * mat, color);
            }
        }
        void DebugWireframe(const ECS::CVXGI &c, const Matrix4x4f &mat, Color color)
        {
            // 计算每个方向上的步长
            float widthX = c._grid_size.x;//(2.0f * c._grid_size.x) / c._grid_num.x;
            float widthY = c._grid_size.y;//(2.0f * c._grid_size.y) / c._grid_num.y;
            float widthZ = c._grid_size.z;//(2.0f * c._grid_size.z) / c._grid_num.z;

            Vector3f start, end;

            // 绘制 X 轴方向的线条（沿着 YZ 平面形成网格）
            for (int y = 0; y <= c._grid_num.y; ++y) {
                for (int z = 0; z <= c._grid_num.z; ++z) {
                    start = c._center + Vector3f(-c._grid_size.x, y * widthY - c._grid_size.y, z * widthZ - c._grid_size.z);
                    end = c._center + Vector3f(c._grid_size.x, y * widthY - c._grid_size.y, z * widthZ - c._grid_size.z);
                    Gizmo::DrawLine(start, end);
                }
            }

            // 绘制 Y 轴方向的线条（沿着 XZ 平面形成网格）
            for (int x = 0; x <= c._grid_num.x; ++x) {
                for (int z = 0; z <= c._grid_num.z; ++z) {
                    start = c._center + Vector3f(x * widthX - c._grid_size.x, -c._grid_size.y, z * widthZ - c._grid_size.z);
                    end = c._center + Vector3f(x * widthX - c._grid_size.x, c._grid_size.y, z * widthZ - c._grid_size.z);
                    Gizmo::DrawLine(start, end);
                }
            }

            // 绘制 Z 轴方向的线条（沿着 XY 平面形成网格）
            for (int x = 0; x <= c._grid_num.x; ++x) {
                for (int y = 0; y <= c._grid_num.y; ++y) {
                    start = c._center + Vector3f(x * widthX - c._grid_size.x, y * widthY - c._grid_size.y, -c._grid_size.z);
                    end = c._center + Vector3f(x * widthX - c._grid_size.x, y * widthY - c._grid_size.y, c._grid_size.z);
                    Gizmo::DrawLine(start, end);
                }
            }
        }
    }
}
