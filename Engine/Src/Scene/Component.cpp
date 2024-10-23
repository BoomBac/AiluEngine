#include "Scene/Component.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Gizmo.h"
#include "pch.h"

namespace Ailu
{
    Archive &operator<<(Archive &ar, const TagComponent &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_name:" << c._name;
        ar.DecreaseIndent();
        ar.NewLine();
        return ar;
    }
    Archive &operator>>(Archive &ar, TagComponent &c)
    {
        String bufs[1];
        ar >> bufs[0];
        AL_ASSERT(su::BeginWith(bufs[0], "_name"));
        c._name = su::Split(bufs[0], ":")[1];
        return ar;
    }

    Archive &operator<<(Archive &ar, TransformComponent &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_transform:";
        ar.NewLine();
        ar << c._transform;
        ar.DecreaseIndent();
        return ar;
    }
    Archive &operator>>(Archive &ar, TransformComponent &c)
    {
        string str;
        ar >> str;
        AL_ASSERT(su::BeginWith(str, "_transform"));
        ar >> c._transform;
        return ar;
    }

    Archive &operator<<(Archive &ar, const LightData &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_light_color:" << c._light_color;
        ar.NewLine();
        ar.InsertIndent();
        ar << "_light_param:" << c._light_param;
        ar.NewLine();
        ar.InsertIndent();
        ar << "_is_two_side:" << c._is_two_side;
        ar.DecreaseIndent();
        ar.NewLine();
        return ar;
    }
    Archive &operator>>(Archive &ar, LightData &c)
    {
        String bufs[3];
        ar >> bufs[0] >> bufs[1] >> bufs[2];
        AL_ASSERT(su::BeginWith(bufs[0], "_light_color"));
        c._light_color.FromString(su::Split(bufs[0], ":")[1]);
        c._light_param.FromString(su::Split(bufs[1], ":")[1]);
        c._is_two_side = static_cast<bool>(std::stoi(su::Split(bufs[2], ":")[1]));
        return ar;
    }

    Archive &operator<<(Archive &ar, const ShadowData &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_is_cast_shadow:" << c._is_cast_shadow;
        ar.NewLine();
        ar.InsertIndent();
        ar << "_constant_bias:" << c._constant_bias;
        ar.NewLine();
        ar.InsertIndent();
        ar << "_slope_bias:" << c._slope_bias;
        ar.DecreaseIndent();
        ar.NewLine();
        return ar;
    }
    Archive &operator>>(Archive &ar, ShadowData &c)
    {
        String bufs[3];
        ar >> bufs[0] >> bufs[1] >> bufs[2];
        AL_ASSERT(su::BeginWith(bufs[0], "_is_cast_shadow"));
        c._is_cast_shadow = std::stoi(su::Split(bufs[0], ":")[1]);
        c._constant_bias = std::stof(su::Split(bufs[1], ":")[1]);
        c._slope_bias = std::stof(su::Split(bufs[2], ":")[1]);
        return ar;
    }

    Archive &operator<<(Archive &ar, const LightComponent &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_type:" << ELightType::ToString(c._type);
        ar.NewLine();
        ar.InsertIndent();
        ar << "_light:";
        ar.NewLine();
        ar << c._light;
        ar.InsertIndent();
        ar << "_shadow:";
        ar.NewLine();
        ar << c._shadow;
        ar.DecreaseIndent();
        return ar;
    }
    Archive &operator>>(Archive &ar, LightComponent &c)
    {
        String str;
        ar >> str;
        AL_ASSERT(su::BeginWith(str, "_type"));
        c._type = ELightType::FromString(su::Split(str, ":")[1]);
        ar >> str;
        AL_ASSERT(su::BeginWith(str, "_light"));
        ar >> c._light;
        ar >> str;
        AL_ASSERT(su::BeginWith(str, "_shadow"));
        ar >> c._shadow;
        return ar;
    }

    Archive &operator<<(Archive &ar, const StaticMeshComponent &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_p_mesh:" << (c._p_mesh ? c._p_mesh->GetGuid().ToString() : Guid::EmptyGuid().ToString());
        ar.NewLine();
        ar.InsertIndent();
        ar << "_material_num:" << c._p_mats.size();
        for (u32 i = 0; i < c._p_mats.size(); i++)
        {
            ar.NewLine();
            ar.InsertIndent();
            ar << std::format("_p_mats[{}]:{}", i, c._p_mats[i]->GetGuid().ToString());
        }
        ar.DecreaseIndent();
        ar.NewLine();
        return ar;
    }
    Archive &operator>>(Archive &ar, StaticMeshComponent &c)
    {
        String bufs[1];
        ar >> bufs[0];
        AL_ASSERT(su::BeginWith(bufs[0], "_p_mesh"));
        Guid asset_guid = Guid(su::Split(bufs[0], ":")[1]);
        if (asset_guid == Guid::EmptyGuid())
        {
            c._p_mesh = nullptr;
        }
        else
        {
            g_pResourceMgr->Load<Mesh>(asset_guid);
            c._p_mesh = g_pResourceMgr->GetRef<Mesh>(asset_guid);
            c._transformed_aabbs.resize(c._p_mesh->SubmeshCount() + 1);
        }
        ar >> bufs[0];
        u16 material_num = std::stoi(su::Split(bufs[0], ":")[1]);

        for (u16 i = 0; i < material_num; i++)
        {
            ar >> bufs[0];
            asset_guid = Guid(su::Split(bufs[0], ":")[1]);
            g_pResourceMgr->Load<Material>(asset_guid);
            if (auto mat = g_pResourceMgr->GetRef<Material>(asset_guid); mat != nullptr)
                c._p_mats.push_back(mat);
            else
                LOG_ERROR("Load material failed:{}", asset_guid.ToString());
        }
        return ar;
    }

    Archive &operator<<(Archive &ar, const CSkeletonMesh &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_p_mesh:" << (c._p_mesh ? c._p_mesh->GetGuid().ToString() : Guid::EmptyGuid().ToString());
        ar.NewLine();
        ar.InsertIndent();
        ar << "_material_num:" << c._p_mats.size();
        for (u32 i = 0; i < c._p_mats.size(); i++)
        {
            ar.NewLine();
            ar.InsertIndent();
            ar << std::format("_p_mats[{}]:{}", i, c._p_mats[i]->GetGuid().ToString());
        }
        ar.NewLine();
        ar.InsertIndent();
        ar << "_anim_clip:" << (c._anim_clip ? c._anim_clip->GetGuid().ToString() : Guid::EmptyGuid().ToString());
        ar.DecreaseIndent();
        ar.NewLine();
        return ar;
    }
    Archive &operator>>(Archive &ar, CSkeletonMesh &c)
    {
        String bufs[1];
        ar >> bufs[0];
        AL_ASSERT(su::BeginWith(bufs[0], "_p_mesh"));
        Guid asset_guid = Guid(su::Split(bufs[0], ":")[1]);
        if (asset_guid == Guid::EmptyGuid())
        {
            c._p_mesh = nullptr;
        }
        else
        {
            g_pResourceMgr->Load<SkeletonMesh>(asset_guid);
            c._p_mesh = g_pResourceMgr->GetRef<SkeletonMesh>(asset_guid);
            c._transformed_aabbs.resize(c._p_mesh->SubmeshCount() + 1);
        }
        ar >> bufs[0];
        u16 material_num = std::stoi(su::Split(bufs[0], ":")[1]);
        c._transformed_aabbs.resize(c._p_mesh->SubmeshCount() + 1);
        for (u16 i = 0; i < material_num; i++)
        {
            ar >> bufs[0];
            asset_guid = Guid(su::Split(bufs[0], ":")[1]);
            g_pResourceMgr->Load<Material>(asset_guid);
            if (auto mat = g_pResourceMgr->GetRef<Material>(asset_guid); mat != nullptr)
                c._p_mats.push_back(mat);
            else
                LOG_ERROR("Load material failed:{}", asset_guid.ToString());
        }
        ar >> bufs[0];
        asset_guid = Guid(su::Split(bufs[0], ":")[1]);
        if (asset_guid == Guid::EmptyGuid())
        {
            c._anim_clip = nullptr;
        }
        else
        {
            g_pResourceMgr->Load<AnimationClip>(asset_guid);
            c._anim_clip = g_pResourceMgr->GetRef<AnimationClip>(asset_guid);
        }
        return ar;
    }
    
    Archive &Ailu::operator<<(Archive &ar, const CHierarchy &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_first_child:" << c._first_child;
        ar.NewLine();
        ar.InsertIndent();
        ar << "_prev_sibling:" << c._prev_sibling;
        ar.NewLine();
        ar.InsertIndent();
        ar << "_next_sibling:" << c._next_sibling;
        ar.NewLine();
        ar.InsertIndent();
        ar << "_parent:" << c._parent;
        ar.NewLine();
        ar.InsertIndent();
        ar << "_children_num:" << c._children_num;
        ar.NewLine();
        ar.InsertIndent();
        ar << "_inv_matrix_attach:" << c._inv_matrix_attach.ToString();
        ar.DecreaseIndent();
        ar.NewLine();
        return ar;
    }
    Archive &Ailu::operator>>(Archive &ar, CHierarchy &c)
    {
        String bufs[1];
        ar >> bufs[0];
        AL_ASSERT(su::BeginWith(bufs[0], "_first_child"));
        c._first_child = std::stoi(su::Split(bufs[0], ":")[1]);
        ar >> bufs[0];
        c._prev_sibling = std::stoi(su::Split(bufs[0], ":")[1]);
        ar >> bufs[0];
        c._next_sibling = std::stoi(su::Split(bufs[0], ":")[1]);
        ar >> bufs[0];
        c._parent = std::stoi(su::Split(bufs[0], ":")[1]);
        ar >> bufs[0];
        c._children_num = std::stoi(su::Split(bufs[0], ":")[1]);
        ar >> bufs[0];
        c._inv_matrix_attach.FromString(su::Split(bufs[0], ":")[1]);
        return ar;
    }

    Archive &operator<<(Archive &ar, const CCamera &c)
    {
        ar.IncreaseIndent();
        ar.InsertIndent();
        ar << "_camera:";
        ar.NewLine();
        ar << c._camera;
        ar.DecreaseIndent();
        return ar;
    }
    Archive &operator>>(Archive &ar, CCamera &c)
    {
        string str;
        ar >> str;
        AL_ASSERT(su::BeginWith(str, "_camera"));
        ar >> c._camera;
        return ar;
    }

    CLightProbe::CLightProbe()
    {
        _cubemap = RenderTexture::Create(512, "light probe", ERenderTargetFormat::kDefaultHDR, true, true, true);
        _cubemap->CreateView();
        _pass = MakeRef<CubeMapGenPass>(_cubemap.get());
        _debug_material = nullptr;
    }

    Archive &Ailu::operator<<(Archive &ar, const CLightProbe &c)
    {
        ar.IncreaseIndent();
        ar << ar.GetIndent() << "_size:" << c._size << std::endl;
        ar << ar.GetIndent() << "_is_update_every_tick:" << c._is_update_every_tick << std::endl;
        //ar << ar.GetIndent() << "_is_dirty:" << c._is_dirty << std::endl;
        ar.DecreaseIndent();
        return ar;
    }

    Archive &Ailu::operator>>(Archive &ar, CLightProbe &c)
    {
        String bufs[1];
        ar >> bufs[0];
        AL_ASSERT(su::BeginWith(bufs[0], "_size"));
        c._size = std::stoi(su::Split(bufs[0], ":")[1]);
        ar >> bufs[0];
        AL_ASSERT(su::BeginWith(bufs[0], "_is_update_every_tick"));
        c._is_dirty = true;
        return ar;
    }

    Archive &Ailu::operator<<(Archive &ar, const CRigidBody &c)
    {
        ar.IncreaseIndent();
        ar << ar.GetIndent() << "_mass:" << c._mass << std::endl;
        ar.DecreaseIndent();
        return ar;
    }

    Archive &Ailu::operator>>(Archive &ar, CRigidBody &c)
    {
        String bufs[1];
        ar >> bufs[0];
        AL_ASSERT(su::BeginWith(bufs[0], "_mass"));
        c._mass = std::stof(su::Split(bufs[0], ":")[1]);
        return ar;
    }
    Archive &operator<<(Archive &ar, const CCollider &c)
    {
        ar.IncreaseIndent();
        ar << ar.GetIndent() << "_type:" << EColliderType::ToString(c._type) << std::endl;
        ar << ar.GetIndent() << "_is_trigger:" << c._is_trigger << std::endl;
        ar << ar.GetIndent() << "_center:" << c._center.ToString() << std::endl;
        ar << ar.GetIndent() << "_param:" << c._param.ToString() << std::endl;
        ar.DecreaseIndent();
        return ar;
    }
    Archive &operator>>(Archive &ar, CCollider &c)
    {
        String buf;
        ar >> buf;
        AL_ASSERT(su::BeginWith(buf, "_type"));
        c._type = EColliderType::FromString(su::Split(buf, ":")[1]);
        ar >> buf;
        c._is_trigger = static_cast<bool>(std::stoi(su::Split(buf, ":")[1]));
        ar >> buf;
        c._center.FromString(su::Split(buf, ":")[1]);
        ar >> buf;
        c._param.FromString(su::Split(buf, ":")[1]);
        return ar;
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

    namespace DebugDrawer
    {
        void DebugWireframe(const CCollider &c, const Transform &t,Color color)
        {
            if (c._type == EColliderType::kBox)
            {
                Gizmo::DrawOBB(CCollider::AsBox(c) * t._world_matrix, color);
            }
            else if (c._type == EColliderType::kSphere)
            {
                Gizmo::DrawSphere(CCollider::AsShpere(c) * t._world_matrix, color);
            }
            else if (c._type == EColliderType::kCapsule)
            {
                Gizmo::DrawCapsule(CCollider::AsCapsule(c) * t._world_matrix, color);
            }
        }
    }
}// namespace Ailu
