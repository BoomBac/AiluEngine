#include "Scene/RenderSystem.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"
#include "pch.h"
#include <Render/Gizmo.h>
#include <Render/GraphicsContext.h>
#include <Render/RenderPipeline.h>

#include "DirectXMath.h"
#include "DirectXPackedVector.h"

using namespace DirectX;
using namespace DirectX::PackedVector;

namespace Ailu
{
    namespace ECS
    {
        void RenderSystem::Update(Register &r, f32 delta_time)
        {
            PROFILE_BLOCK_CPU(RenderSystem_Update)
            for (auto &e: _entities)
            {
                auto comp = r.GetComponent<StaticMeshComponent>(e);
                auto transf = r.GetComponent<TransformComponent>(e)->_transform;
                for (int i = 0; i < comp->_p_mesh->_bound_boxs.size(); i++)
                {
                    comp->_transformed_aabbs[i] = comp->_p_mesh->_bound_boxs[i] * transf._world_matrix;
                }
            }
            u32 index = 0u;
            for (auto& comp: r.View<CSkeletonMesh>())
            {
                auto t = r.GetComponent<CSkeletonMesh, TransformComponent>(index);
                if (!comp._p_mesh)
                    continue;
                for (int i = 0; i < comp._p_mesh->_bound_boxs.size(); i++)
                {
                    comp._transformed_aabbs[i] = comp._p_mesh->_bound_boxs[i] * t->_transform._world_matrix;
                }
            }
        }

        static Vector3f XMVecToVec3(const XMVECTOR &vec)
        {
            Vector3f ret;
            memcpy(ret, vec.m128_f32, sizeof(Vector3f));
            return ret;
        }

        Ailu::ECS::LightingSystem::LightingSystem()
        {
            _light_probe_debug_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/cubemap_debug.hlsl"), "lightprobe_debug");

            //auto [probe_data, data_size] = FileManager::ReadFile(ResourceMgr::GetResSysPath(L"ScreenGrab/light_probe.data"));
            //LightProbeHeadInfo head;
            //memcpy(&head, probe_data, sizeof(LightProbeHeadInfo));
            //u16 w = head._prefilter_size;
            //_test_prefilter_map = CubeMap::Create(w,true,ETextureFormat::kR11G11B10);
            //_test_radiance_map = CubeMap::Create(head._radiance_size, false, ETextureFormat::kR11G11B10);
            //u64 offset = sizeof(LightProbeHeadInfo);
            //for (u16 face = 1; face <= 6; face++)
            //{
            //    for (u16 i = 0; i < _test_prefilter_map->MipmapLevel() + 1; i++)
            //    {
            //        auto [cur_w, cur_h] = _test_prefilter_map->CurMipmapSize(i);
            //        _test_prefilter_map->SetPixelData((ECubemapFace::ECubemapFace) face, probe_data + offset, i);
            //        offset += cur_w * cur_h * GetPixelByteSize(_test_prefilter_map->PixelFormat());
            //    }
            //}
            //for (u16 face = 1; face <= 6; face++)
            //{
            //    _test_radiance_map->SetPixelData((ECubemapFace::ECubemapFace) face, probe_data + offset, 0);
            //    offset += head._radiance_size * head._radiance_size * GetPixelByteSize(_test_radiance_map->PixelFormat());
            //}
            //_test_prefilter_map->Name("_test_prefilter_map");
            //_test_prefilter_map->Apply();
            //_test_radiance_map->Name("_test_radiance_map");
            //_test_radiance_map->Apply();
            //delete[] probe_data;
        }

        void LightingSystem::OnPushEntity(Entity e)
        {
        }
        void Ailu::ECS::LightingSystem::Update(Register &r, f32 delta_time)
        {

            PROFILE_BLOCK_CPU(LightingSystem_Update)
            u32 index = 0;
            if (g_pGfxContext->GetFrameCount() > 5)
            {
                for (auto &comp: r.View<CLightProbe>())
                {
                    if (comp._is_dirty || comp._is_update_every_tick)
                    {
                        auto transf = r.GetComponent<CLightProbe, TransformComponent>(index)->_transform;
                        Camera cam;
                        cam.Position(transf._position);
                        cam.Near(1.0f);
                        cam.Far(comp._size);
                        cam._layer_mask = 0;
                        cam._layer_mask |= ERenderLayer::kDefault;
                        cam.TargetTexture(comp._cubemap.get());
                        g_pGfxContext->GetPipeline()->GetRenderer()->SubmitTaskPass(comp._pass.get());
                        g_pGfxContext->GetPipeline()->GetRenderer()->Render(cam, *g_pSceneMgr->ActiveScene());
                        comp._is_dirty = false;
                        /*
                        RenderTexture *out_tex = comp._pass->_prefilter_cubemap.get();
                        //RenderTexture *out_tex = comp._cubemap.get();
                        u64 light_probe_data_size = sizeof(LightProbeHeadInfo) + out_tex->TotalByteSize() + comp._pass->_radiance_map->TotalByteSize();
                        u8 *probe_data = new u8[light_probe_data_size];
                        LightProbeHeadInfo header{out_tex->Width(), comp._pass->_radiance_map->Width(), out_tex->PixelFormat()};
                        memcpy(probe_data, &header, sizeof(LightProbeHeadInfo));
                        u64 offset = sizeof(LightProbeHeadInfo);
                        for (u16 j = 1; j <= 6; j++)
                        {
                            auto face = (ECubemapFace::ECubemapFace) j;
                            for (u16 i = 0; i < out_tex->MipmapLevel() + 1; i++)
                            {
                                const f32 *data = (f32 *) out_tex->ReadBack(i, 0, face);
                                auto [w, h] = out_tex->CurMipmapSize(i);
                                u64 cur_mip_size = w * h * GetPixelByteSize(out_tex->PixelFormat());
                                memcpy(probe_data + offset, data, cur_mip_size);
                                delete[] data;
                                offset += cur_mip_size;
                            }
                        }
                        for (u16 j = 1; j <= 6; j++)
                        {
                            auto face = (ECubemapFace::ECubemapFace) j;
                            const void *data = comp._pass->_radiance_map->ReadBack(0, 0, face);
                            u64 cur_mip_size = comp._pass->_radiance_map->Width() * comp._pass->_radiance_map->Height() * GetPixelByteSize(comp._pass->_radiance_map->PixelFormat());
                            memcpy(probe_data + offset, data, cur_mip_size);
                            delete[] data;
                            offset += cur_mip_size;
                        }
                        FileManager::CreateFile(ResourceMgr::GetResSysPath(L"ScreenGrab/light_probe.data"));
                        FileManager::WriteFile(ResourceMgr::GetResSysPath(L"ScreenGrab/light_probe.data"), false, (u8 *) probe_data,light_probe_data_size);
                        delete[] probe_data;
                        */
                    }
                    if (comp._src_type == 0)
                    {
                        _light_probe_debug_mat->DisableKeyword("DEBUG_REFLECT");
                        _light_probe_debug_mat->SetTexture("_SrcCubeMap", comp._cubemap.get());
                    }
                    else if (comp._src_type == 1)
                    {
                        _light_probe_debug_mat->DisableKeyword("DEBUG_REFLECT");
                        _light_probe_debug_mat->SetTexture("_SrcCubeMap", comp._pass->_radiance_map.get());
                    }
                    else if (comp._src_type == 2)
                    {
                        _light_probe_debug_mat->EnableKeyword("DEBUG_REFLECT");
                        _light_probe_debug_mat->SetTexture("_SrcCubeMap", comp._pass->_prefilter_cubemap.get());
                    }
                    else if (comp._src_type == 3)
                    {
                        _light_probe_debug_mat->EnableKeyword("DEBUG_REFLECT");
                        _light_probe_debug_mat->SetTexture("_SrcCubeMap", _test_prefilter_map.get());
                    }
                    else if (comp._src_type == 4)
                    {
                        _light_probe_debug_mat->DisableKeyword("DEBUG_REFLECT");
                        _light_probe_debug_mat->SetTexture("_SrcCubeMap", _test_radiance_map.get());
                    }
                    else {}
                    _light_probe_debug_mat->SetFloat("_mipmap", comp._mipmap);
                    Shader::SetGlobalTexture("SkyBox", comp._cubemap.get());
                    Shader::SetGlobalTexture("RadianceTex", comp._pass->_radiance_map.get());
                    Shader::SetGlobalTexture("PrefilterEnvTex", comp._pass->_prefilter_cubemap.get());
                    comp._debug_material = _light_probe_debug_mat.get();
                }
            }

            index = 0;
            for (auto &e: _entities)
            {
                auto transf = r.GetComponent<TransformComponent>(e)->_transform;
                auto comp = r.GetComponent<LightComponent>(e);
                comp->_light._light_pos = transf._position;
                Vector3f light_forward = LightComponent::kDefaultDirectionalLightDir;
                light_forward.xyz = transf._rotation * light_forward.xyz;
                comp->_light._light_dir.xyz = Normalize(Vector3f{light_forward.xyz});

                if (comp->_type == ELightType::kDirectional)
                {
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
                        Vector3f shadow_cam_pos = cascade_sphere._center + -comp->_light._light_dir.xyz * height;
                        Vector3f light_dir = comp->_light._light_dir.xyz;
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
                            auto unproj_mat = MatrixInverse(selected_cam.GetView() * selected_cam.GetProjection()); //这里如果near-clip过小的话，也会造成抖动的情况
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
                            //if (i == 1)
                            //{
                            //    Gizmo::DrawLine(XMVecToVec3(frustum_corners[0]), XMVecToVec3(frustum_corners[1]), Colors::kRed);
                            //    Gizmo::DrawLine(XMVecToVec3(frustum_corners[2]), XMVecToVec3(frustum_corners[3]), Colors::kRed);
                            //    Gizmo::DrawLine(XMVecToVec3(frustum_corners[4]), XMVecToVec3(frustum_corners[5]), Colors::kRed);
                            //    Gizmo::DrawLine(XMVecToVec3(frustum_corners[6]), XMVecToVec3(frustum_corners[7]), Colors::kRed);
                            //    Gizmo::DrawSphere(cascade_sphere, Random::RandomColor32(i));
                            //}
                            // Extrude bounds to avoid early shadow clipping:
                            float ext = abs(_center.z - _min.z);
                            ext = std::max(ext, std::min(1500.0f, farPlane) * 0.5f);
                            _min.z = _center.z - ext;
                            _max.z = _center.z + ext;
                            const XMMATRIX lightProjection = XMMatrixOrthographicOffCenterLH(_min.x, _max.x, _min.y, _max.y, _min.z, _max.z);// notice reversed Z!
                            Matrix4x4f view, proj;
                            memcpy(view, lightView.r, sizeof(XMMATRIX));
                            memcpy(proj, lightProjection.r, sizeof(XMMATRIX));
                            comp->_shadow_cameras[i].SetViewAndProj(view, proj);
                        }
                        else
                        {
                        }
                        //https://alextardif.com/shadowmapping.html
                        //shadow_cam_pos.y = 50.0f;
                        comp->_shadow_cameras[i].Type(ECameraType::kOrthographic);
                        comp->_shadow_cameras[i].SetLens(90, 1, 0.1f, height * 4.0f);
                        comp->_shadow_cameras[i].Size(cascade_sphere._radius * 2.0f);
                        comp->_shadow_cameras[i].Position(shadow_cam_pos);
                        //_shadow_cameras[i].LookTo(_light._light_dir.xyz, Vector3f::kUp);
                        comp->_cascade_shadow_data[i].xyz = cascade_sphere._center;
                        comp->_cascade_shadow_data[i].w = cascade_sphere._radius * cascade_sphere._radius;
                    }
                }
                else if (comp->_type == ELightType::kSpot)
                {
                    comp->_shadow_cameras[0].SetLens(90, 1, 0.01, comp->_light._light_param.x * 1.5f);
                    comp->_shadow_cameras[0].Position(comp->_light._light_pos.xyz);
                    comp->_shadow_cameras[0].LookTo(comp->_light._light_dir.xyz, Vector3f::kUp);
                    //Camera::DrawGizmo(&_shadow_cameras[0], Random::RandomColor32(0));
                }
                else if (comp->_type == ELightType::kPoint)
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
                        comp->_shadow_cameras[i].SetLens(90, 1, 0.01, comp->_light._light_param.x * 1.5f);
                        comp->_shadow_cameras[i].Position(comp->_light._light_pos.xyz);
                        comp->_shadow_cameras[i].LookTo(targets[i], ups[i]);
                    }
                }
                else
                {
                    memset(comp->_light._area_points, 0, sizeof(comp->_light._area_points));
                    f32 w = comp->_light._light_param.y * 0.5f, h = comp->_light._light_param.z * 0.5f;
                    comp->_light._area_points[0].x = -w;
                    comp->_light._area_points[0].z = h;
                    comp->_light._area_points[1].x = w;
                    comp->_light._area_points[1].z = h;
                    comp->_light._area_points[2].x = w;
                    comp->_light._area_points[2].z = -h;
                    comp->_light._area_points[3].x = -w;
                    comp->_light._area_points[3].z = -h;
                    auto mat = Transform::ToMatrix(transf);
                    TransformCoord(comp->_light._area_points[0], mat);
                    TransformCoord(comp->_light._area_points[1], mat);
                    TransformCoord(comp->_light._area_points[2], mat);
                    TransformCoord(comp->_light._area_points[3], mat);
                }
            }
        }

    }// namespace ECS
    //namespace ECS
}// namespace Ailu
