#include "Objects/LightComponent.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Math/Random.h"
#include "Objects/SceneActor.h"
#include "Render/Gizmo.h"
#include "pch.h"

#include "DirectXMath.h"
#include "DirectXPackedVector.h"

using namespace DirectX;
using namespace DirectX::PackedVector;

namespace Ailu
{
    LightComponent::LightComponent()
    {
        IMPLEMENT_REFLECT_FIELD(LightComponent)
        _light._light_color = Colors::kWhite;
        _light._light_pos = {0.0f, 0.0f, 0.0f, 0.0f};
        _light._light_dir = kDefaultDirectionalLightDir;
        _intensity = 1.0f;
        _b_cast_shadow = false;
        _light_type = ELightType::kDirectional;
        _shadow_cameras.resize(QuailtySetting::s_cascade_shadow_map_count);
    }
    void Ailu::LightComponent::Tick(const float &delta_time)
    {
        if (!_b_enable) return;
        auto &transf = static_cast<SceneActor *>(_p_onwer)->GetTransform();
        _light._light_color.a = _intensity;
        _light._light_pos = transf._position;
        Vector4f light_forward = kDefaultDirectionalLightDir;
        light_forward.xyz = transf._rotation * light_forward.xyz;
        _light._light_dir.xyz = Normalize(Vector3f{light_forward.xyz});

        Clamp(_light._light_param.z, 0.0f, 180.0f);
        Clamp(_light._light_param.y, 0.0f, _light._light_param.z - 0.1f);
        if (_b_cast_shadow)
        {
            UpdateShadowCamera();
        }
    }
    void LightComponent::Serialize(std::basic_ostream<char, std::char_traits<char>> &os, String indent)
    {
        Component::Serialize(os, indent);
        using namespace std;
        String prop_indent = indent.append("  ");
        os << prop_indent << "Type: " << ELightTypeStr(_light_type) << endl;
        Vector3f color = _light._light_color.xyz;
        os << prop_indent << "Color: " << color << endl;
        os << prop_indent << "Intensity: " << _light._light_color.a << endl;
        if (_light_type != ELightType::kDirectional)
            os << prop_indent << "Radius: " << _light._light_param.x << endl;
        if (_light_type == ELightType::kSpot)
        {
            os << prop_indent << "Inner: " << _light._light_param.y << endl;
            os << prop_indent << "Outer: " << _light._light_param.z << endl;
        }
        os << prop_indent << "CastShadow: " << (_b_cast_shadow ? "true" : "false") << endl;
        os << prop_indent << "ConstantBias: " << _shadow._constant_bias << endl;
        os << prop_indent << "SlopeBias: " << _shadow._slope_bias << endl;
    }
    void LightComponent::OnGizmo()
    {
        DrawLightGizmo();
    }

    void LightComponent::LightType(ELightType type)
    {
        if (type == _light_type)
            return;
        if (type == ELightType::kPoint && _shadow_cameras.size() != 6)
        {
            _shadow_cameras.resize(6);
        }
        _light_type = type;
    }

    void *LightComponent::DeserializeImpl(Queue<std::tuple<String, String>> &formated_str)
    {
        formated_str.pop();
        auto type = TP_ONE(formated_str.front());
        formated_str.pop();
        LightComponent *comp = new LightComponent();
        Vector3f vec{};
        float f{};
        LoadVector(TP_ONE(formated_str.front()).c_str(), vec);
        formated_str.pop();
        comp->_light._light_color = {vec, LoadFloat(TP_ONE(formated_str.front()).c_str())};
        formated_str.pop();
        comp->_intensity = comp->_light._light_color.a;
        if (type == ELightTypeStr(ELightType::kPoint))
        {
            comp->_light_type = ELightType::kPoint;
            comp->_light._light_param.x = LoadFloat(TP_ONE(formated_str.front()).c_str());
            comp->_shadow_cameras.resize(6);
            formated_str.pop();
        }
        else if (type == ELightTypeStr(ELightType::kSpot))
        {
            comp->_light_type = ELightType::kSpot;
            comp->_light._light_param.x = LoadFloat(TP_ONE(formated_str.front()).c_str());
            formated_str.pop();
            comp->_light._light_param.y = LoadFloat(TP_ONE(formated_str.front()).c_str());
            formated_str.pop();
            comp->_light._light_param.z = LoadFloat(TP_ONE(formated_str.front()).c_str());
            formated_str.pop();
        }
        if (String{TP_ONE(formated_str.front()).c_str()} == "true")
            comp->_b_cast_shadow = true;
        else
            comp->_b_cast_shadow = false;
        formated_str.pop();
        comp->_shadow._constant_bias = LoadFloat(TP_ONE(formated_str.front()).c_str());
        formated_str.pop();
        comp->_shadow._slope_bias = LoadFloat(TP_ONE(formated_str.front()).c_str());
        formated_str.pop();
        return comp;
    }

    void LightComponent::DrawLightGizmo()
    {
        auto &transf = static_cast<SceneActor *>(_p_onwer)->GetTransform();
        switch (_light_type)
        {
            case Ailu::ELightType::kDirectional:
            {
                Vector3f light_to = _light._light_dir.xyz;
                light_to.x *= 2.0f;
                light_to.y *= 2.0f;
                light_to.z *= 2.0f;
                Gizmo::DrawLine(transf._position, transf._position + light_to, _light._light_color);
                Gizmo::DrawCircle(transf._position, 2.0f, 24, _light._light_color, Quaternion::ToMat4f(transf._rotation));
                if (_b_cast_shadow)
                {
                    //Camera::DrawGizmo(&_shadow_cameras[0]);
                }
                return;
            }
            case Ailu::ELightType::kPoint:
            {
                Gizmo::DrawCircle(transf._position, _light._light_param.x, 24, _light._light_color, MatrixRotationX(ToRadius(90.0f)));
                Gizmo::DrawCircle(transf._position, _light._light_param.x, 24, _light._light_color, MatrixRotationZ(ToRadius(90.0f)));
                Gizmo::DrawCircle(transf._position, _light._light_param.x, 24, _light._light_color);
                return;
            }
            case Ailu::ELightType::kSpot:
            {
                float angleIncrement = 90.0;
                auto rot_mat = Quaternion::ToMat4f(transf._rotation);
                Vector3f light_to = _light._light_dir.xyz;
                light_to *= _light._light_param.x;
                {
                    Vector3f inner_center = light_to + transf._position;
                    float inner_radius = tan(ToRadius(_light._light_param.y / 2.0f)) * _light._light_param.x;
                    Gizmo::DrawLine(transf._position, transf._position + light_to * 10.f, Colors::kYellow);
                    Gizmo::DrawCircle(inner_center, inner_radius, 24, _light._light_color, rot_mat);
                    for (int i = 0; i < 4; ++i)
                    {
                        float angle1 = ToRadius(angleIncrement * static_cast<float>(i));
                        float angle2 = ToRadius(angleIncrement * static_cast<float>(i + 1));
                        Vector3f point1(inner_center.x + inner_radius * cos(angle1), inner_center.y, inner_center.z + inner_radius * sin(angle1));
                        Vector3f point2(inner_center.x + inner_radius * cos(angle2), inner_center.y, inner_center.z + inner_radius * sin(angle2));
                        point1 -= inner_center;
                        point2 -= inner_center;
                        point1 = transf._rotation * point1;
                        point2 = transf._rotation * point2;
                        //TransformCoord(point1, rot_mat);
                        //TransformCoord(point2, rot_mat);
                        point1 += inner_center;
                        point2 += inner_center;
                        Gizmo::DrawLine(transf._position, point2, _light._light_color);
                    }
                }
                light_to *= 0.9f;
                {
                    Vector3f outer_center = light_to + transf._position;
                    float outer_radius = tan(ToRadius(_light._light_param.z / 2.0f)) * _light._light_param.x;
                    Gizmo::DrawCircle(outer_center, outer_radius, 24, _light._light_color, rot_mat);
                    for (int i = 0; i < 4; ++i)
                    {
                        float angle1 = ToRadius(angleIncrement * static_cast<float>(i));
                        float angle2 = ToRadius(angleIncrement * static_cast<float>(i + 1));
                        Vector3f point1(outer_center.x + outer_radius * cos(angle1), outer_center.y, outer_center.z + outer_radius * sin(angle1));
                        Vector3f point2(outer_center.x + outer_radius * cos(angle2), outer_center.y, outer_center.z + outer_radius * sin(angle2));
                        point1 -= outer_center;
                        point2 -= outer_center;
                        TransformCoord(point1, rot_mat);
                        TransformCoord(point2, rot_mat);
                        point1 += outer_center;
                        point2 += outer_center;
                        Gizmo::DrawLine(transf._position, point2, _light._light_color);
                    }
                }
            }
        }
    }

    // 计算射线与 ZX 平面的交点
    bool FindIntersectionWithZXPlane(const Vector3f &origin, const Vector3f &direction, Vector3f &intersection)
    {
        // 如果射线方向向量在 y 方向上的分量为 0，则射线平行于 ZX 平面
        if (direction.y == 0.0f)
        {
            if (origin.y == 0.0f)
            {
                // 如果射线起点在 ZX 平面上，则认为与平面处处相交，这里返回起点坐标
                intersection = origin;
                return true;
            }
            // 否则射线与平面平行且不相交
            return false;
        }

        // 计算 t = -y0 / dy
        float t = -origin.y / direction.y;

        // 计算交点坐标
        intersection = origin + direction * t;

        return true;
    }
    Vector3f XMVecToVec3(const XMVECTOR &vec)
    {
        Vector3f ret;
        memcpy(ret, vec.m128_f32, sizeof(Vector3f));
        return ret;
    }
    void LightComponent::UpdateShadowCamera()
    {
        if (_light_type == ELightType::kDirectional)
        {
            auto &transf = static_cast<SceneActor *>(_p_onwer)->GetTransform();
            Camera &selected_cam = Camera::sSelected ? *Camera::sSelected : *Camera::sCurrent;
            //Camera& selected_cam = *Camera::sCurrent;//Camera::sSelected ? *Camera::sSelected : *Camera::sCurrent;
            f32 cur_level_start = 0, cur_level_end = QuailtySetting::s_main_light_shaodw_distance;
            for (u16 i = 0; i < QuailtySetting::s_cascade_shadow_map_count; ++i)
            {
                cur_level_end = QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[i];
                if (i > 0)
                {
                    cur_level_start = QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[i - 1];
                }
                Sphere cascade_sphere = Camera::GetShadowCascadeSphere(selected_cam, 0, 10);
                //cascade_sphere._center.y = 10.0f;
                f32 world_unit_per_pixel = (f32) QuailtySetting::s_cascade_shaodw_map_resolution / cascade_sphere._radius * 2;
                float height = cascade_sphere._radius * 2.5f;
                Vector3f shadow_cam_pos = cascade_sphere._center + -_light._light_dir.xyz * height;
                Vector3f light_dir = _light._light_dir.xyz;
                light_dir = Normalize(light_dir);
                f32 radius_align_down = acosf(DotProduct(light_dir, -Vector3f::kUp));
                bool m1 = true;
                if (m1)
                {
                    XMFLOAT4 rot;
                    memcpy(&rot, &transf._rotation, sizeof(XMFLOAT4));
                    const XMMATRIX lightRotation = XMMatrixRotationQuaternion(XMLoadFloat4(&rot));
                    const XMVECTOR to = XMVector3TransformNormal(XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f), lightRotation);
                    const XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), lightRotation);
                    const XMMATRIX lightView = XMMatrixLookToLH(XMVectorZero(), to, up);// important to not move (zero out eye vector) the light view matrix itself because texel snapping must be done on projection matrix!
                    const XMMATRIX lightViewInv = XMMatrixInverse(nullptr, lightView);
                    const float farPlane = selected_cam.Far();
                    // Unproject main frustum corners into world space (notice the reversed Z projection!):
                    auto unproj_mat = MatrixInverse(selected_cam.GetView() * selected_cam.GetProjection());
                    XMMATRIX unproj;
                    memcpy(unproj.r, unproj_mat, sizeof(XMMATRIX));
                    XMVECTOR frustum_corners[] =
                            {
                                    XMVector3TransformCoord(XMVectorSet(-1, -1, 0, 1), unproj),// near
                                    XMVector3TransformCoord(XMVectorSet(-1, -1, 1, 1), unproj),// far
                                    XMVector3TransformCoord(XMVectorSet(-1, 1, 0, 1), unproj), // near
                                    XMVector3TransformCoord(XMVectorSet(-1, 1, 1, 1), unproj), // far
                                    XMVector3TransformCoord(XMVectorSet(1, -1, 0, 1), unproj), // near
                                    XMVector3TransformCoord(XMVectorSet(1, -1, 1, 1), unproj), // far
                                    XMVector3TransformCoord(XMVectorSet(1, 1, 0, 1), unproj),  // near
                                    XMVector3TransformCoord(XMVectorSet(1, 1, 1, 1), unproj),  // far
                            };
                    // Compute cascade bounds in light-view-space from the main frustum corners:
                    float scalar_factor = QuailtySetting::s_main_light_shaodw_distance / farPlane;
                    frustum_corners[1] = XMVectorLerp(frustum_corners[0], frustum_corners[1], scalar_factor);
                    frustum_corners[3] = XMVectorLerp(frustum_corners[2], frustum_corners[3], scalar_factor);
                    frustum_corners[5] = XMVectorLerp(frustum_corners[4], frustum_corners[5], scalar_factor);
                    frustum_corners[7] = XMVectorLerp(frustum_corners[6], frustum_corners[7], scalar_factor);
                    const float split_near = i == 0 ? 0.0f : QuailtySetting::s_cascade_shadow_map_split[i - 1];
                    const float split_far = QuailtySetting::s_cascade_shadow_map_split[i];
                    const XMVECTOR corners[] =
                            {
                                    XMVector3Transform(XMVectorLerp(frustum_corners[0], frustum_corners[1], split_near), lightView),
                                    XMVector3Transform(XMVectorLerp(frustum_corners[0], frustum_corners[1], split_far), lightView),
                                    XMVector3Transform(XMVectorLerp(frustum_corners[2], frustum_corners[3], split_near), lightView),
                                    XMVector3Transform(XMVectorLerp(frustum_corners[2], frustum_corners[3], split_far), lightView),
                                    XMVector3Transform(XMVectorLerp(frustum_corners[4], frustum_corners[5], split_near), lightView),
                                    XMVector3Transform(XMVectorLerp(frustum_corners[4], frustum_corners[5], split_far), lightView),
                                    XMVector3Transform(XMVectorLerp(frustum_corners[6], frustum_corners[7], split_near), lightView),
                                    XMVector3Transform(XMVectorLerp(frustum_corners[6], frustum_corners[7], split_far), lightView),
                            };

                    // Compute cascade bounding sphere center:
                    XMVECTOR center = XMVectorZero();
                    for (int j = 0; j < 8; ++j)
                    {
                        center = XMVectorAdd(center, corners[j]);
                    }
                    center = center / 8.0f;
                    //Gizmo::DrawLine(Vector3f::kZero, XMVecToVec3(center));

                    // Compute cascade bounding sphere radius:
                    float radius = 0;
                    for (int j = 0; j < 8; ++j)
                    {
                        radius = std::max(radius, XMVectorGetX(XMVector3Length(XMVectorSubtract(corners[j], center))));
                    }
                    //Sphere s;


                    // Fit AABB onto bounding sphere:
                    XMVECTOR vRadius = XMVectorReplicate(radius);
                    XMVECTOR vMin = XMVectorSubtract(center, vRadius);
                    XMVECTOR vMax = XMVectorAdd(center, vRadius);
                    // Snap cascade to texel grid:
                    const XMVECTOR extent = XMVectorSubtract(vMax, vMin);
                    const XMVECTOR texelSize = extent / (f32) QuailtySetting::s_cascade_shaodw_map_resolution;
                    vMin = XMVectorFloor(vMin / texelSize) * texelSize;
                    vMax = XMVectorFloor(vMax / texelSize) * texelSize;
                    center = (vMin + vMax) * 0.5f;


                    XMFLOAT3 _center;
                    XMFLOAT3 _min;
                    XMFLOAT3 _max;
                    XMStoreFloat3(&_center, center);
                    XMStoreFloat3(&_min, vMin);
                    XMStoreFloat3(&_max, vMax);
                    center = XMVector3TransformCoord(center, lightViewInv);
                    cascade_sphere._center.x = center.m128_f32[0];
                    cascade_sphere._center.y = center.m128_f32[1];
                    cascade_sphere._center.z = center.m128_f32[2];
                    cascade_sphere._radius = radius;
                    if (i == 1)
                    {
                        Gizmo::DrawLine(XMVecToVec3(frustum_corners[0]), XMVecToVec3(frustum_corners[1]), Colors::kRed);
                        Gizmo::DrawLine(XMVecToVec3(frustum_corners[2]), XMVecToVec3(frustum_corners[3]), Colors::kRed);
                        Gizmo::DrawLine(XMVecToVec3(frustum_corners[4]), XMVecToVec3(frustum_corners[5]), Colors::kRed);
                        Gizmo::DrawLine(XMVecToVec3(frustum_corners[6]), XMVecToVec3(frustum_corners[7]), Colors::kRed);
                        Gizmo::DrawSphere(cascade_sphere, Random::RandomColor32(i));
                    }
                    // Extrude bounds to avoid early shadow clipping:
                    float ext = abs(_center.z - _min.z);
                    ext = std::max(ext, std::min(1500.0f, farPlane) * 0.5f);
                    _min.z = _center.z - ext;
                    _max.z = _center.z + ext;
                    const XMMATRIX lightProjection = XMMatrixOrthographicOffCenterLH(_min.x, _max.x, _min.y, _max.y, _min.z, _max.z);// notice reversed Z!
                    Matrix4x4f view, proj;
                    memcpy(view, lightView.r, sizeof(XMMATRIX));
                    memcpy(proj, lightProjection.r, sizeof(XMMATRIX));
                    _shadow_cameras[i].SetViewAndProj(view, proj);
                }
                else
                {
                }
                //https://alextardif.com/shadowmapping.html
                //shadow_cam_pos.y = 50.0f;
                _shadow_cameras[i].Type(ECameraType::kOrthographic);
                _shadow_cameras[i].SetLens(90, 1, 0.1f, height * 4.0f);
                _shadow_cameras[i].Size(cascade_sphere._radius * 2.0f);
                _shadow_cameras[i].Position(shadow_cam_pos);
                //_shadow_cameras[i].LookTo(_light._light_dir.xyz, Vector3f::kUp);

                _shadow_cameras[i].Cull(g_pSceneMgr->_p_current);
                _cascade_shadow_data[i].xyz = cascade_sphere._center;
                _cascade_shadow_data[i].w = cascade_sphere._radius * cascade_sphere._radius;
            }
        }
        else if (_light_type == ELightType::kSpot)
        {
            _shadow_cameras[0].SetLens(90, 1, 100, 250);
            _shadow_cameras[0].Position(_light._light_pos.xyz);
            _shadow_cameras[0].LookTo(_light._light_dir.xyz, Vector3f::kUp);
            _shadow_cameras[0].Cull(g_pSceneMgr->_p_current);
            Camera::DrawGizmo(&_shadow_cameras[0], Random::RandomColor32(0));
        }
        else
        {
            const static Vector3f targets[] =
                    {
                            {1.f, 0, 0}, //+x
                            {-1.f, 0, 0},//-x
                            {0, 1.0f, 0},//+y
                            {0, -1.f, 0},//-y
                            {0, 0, +1.f},//+z
                            {0, 0, -1.f} //-z
                    };
            const static Vector3f ups[] =
                    {
                            {0.f, 1.f, 0.f}, //+x
                            {0.f, 1.f, 0.f}, //-x
                            {0.f, 0.f, -1.f},//+y
                            {0.f, 0.f, 1.f}, //-y
                            {0.f, 1.f, 0.f}, //+z
                            {0.f, 1.f, 0.f}  //-z
                    };
            for (int i = 0; i < 6; i++)
            {
                _shadow_cameras[i].SetLens(90, 1, 10, _light._light_param.x * 1.5f);
                _shadow_cameras[i].Position(_light._light_pos.xyz);
                _shadow_cameras[i].LookTo(targets[i], ups[i]);
                _shadow_cameras[i].Cull(g_pSceneMgr->_p_current);
            }
        }
    }
}// namespace Ailu
