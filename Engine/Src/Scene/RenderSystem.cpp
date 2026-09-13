#include "Scene/RenderSystem.h"
#include "Render/Material.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"
#include "pch.h"
#include <Render/Gizmo.h>
#include <Render/GraphicsContext.h>
#include <Render/RenderPipeline.h>
#include "Framework/Common/Application.h"
#include "DirectXMath.h"
#include "Render/Features/RenderFeature.h"
#include "DirectXPackedVector.h"
#include <array>
#include <cfloat>

using namespace DirectX;
using namespace DirectX::PackedVector;

namespace Ailu
{
    namespace ECS
    {
        using namespace Ailu::Render;
        #pragma region LightingSystem
        struct CascadeShadowInfo
        {
            Matrix4x4f _view;
            Matrix4x4f _projection;
            Vector3f _center;
            f32 _radius = 0.0f;
        };

        static void BuildCascadeFrustumCorners(const Camera &camera, f32 cascade_near, f32 cascade_far,
                                                std::array<Vector3f, 8> &corners)
        {
            const f32 tan_half_fov = tanf(camera.FovH() * k2Radius * 0.5f);
            const f32 near_half_height = tan_half_fov * cascade_near;
            const f32 near_half_width = near_half_height * camera.Aspect();
            const f32 far_half_height = tan_half_fov * cascade_far;
            const f32 far_half_width = far_half_height * camera.Aspect();
            const Vector3f near_center = camera.Position() + camera.Forward() * cascade_near;
            const Vector3f far_center = camera.Position() + camera.Forward() * cascade_far;

            corners[0] = near_center + camera.Up() * near_half_height - camera.Right() * near_half_width;
            corners[1] = near_center + camera.Up() * near_half_height + camera.Right() * near_half_width;
            corners[2] = near_center - camera.Up() * near_half_height - camera.Right() * near_half_width;
            corners[3] = near_center - camera.Up() * near_half_height + camera.Right() * near_half_width;
            corners[4] = far_center + camera.Up() * far_half_height - camera.Right() * far_half_width;
            corners[5] = far_center + camera.Up() * far_half_height + camera.Right() * far_half_width;
            corners[6] = far_center - camera.Up() * far_half_height - camera.Right() * far_half_width;
            corners[7] = far_center - camera.Up() * far_half_height + camera.Right() * far_half_width;
        }

        static CascadeShadowInfo BuildCascadeShadow(const Camera &camera, const Vector3f &light_direction, f32 cascade_near,
                                                     f32 cascade_far, u32 shadow_resolution)
        {
            std::array<Vector3f, 8> corners;
            BuildCascadeFrustumCorners(camera, cascade_near, cascade_far, corners);

            Vector3f center = Vector3f::kZero;
            for (const Vector3f &corner : corners)
                center += corner;
            center /= static_cast<f32>(corners.size());

            f32 radius = 0.0f;
            for (const Vector3f &corner : corners)
                radius = std::max(radius, Distance(center, corner));
            constexpr f32 kRadiusQuantization = 1.0f / 16.0f;
            radius = ceilf(radius / kRadiusQuantization) * kRadiusQuantization;

            Vector3f light_up = Vector3f::kUp;
            if (std::abs(DotProduct(light_direction, light_up)) > 0.99f)
                light_up = Vector3f::kForward;
            const Vector3f light_forward = Normalize(light_direction);
            const Vector3f light_right = Normalize(CrossProduct(light_up, light_forward));
            const Vector3f light_corrected_up = CrossProduct(light_forward, light_right);
            Matrix4x4f light_view;
            BuildViewMatrixLookToLH(light_view, Vector3f::kZero, light_forward, light_corrected_up);

            Vector3f center_ls = TransformCoord(light_view, center);
            const f32 world_units_per_texel = radius * 2.0f / static_cast<f32>(shadow_resolution);
            center_ls.x = std::round(center_ls.x / world_units_per_texel) * world_units_per_texel;
            center_ls.y = std::round(center_ls.y / world_units_per_texel) * world_units_per_texel;

            f32 min_z = FLT_MAX;
            f32 max_z = -FLT_MAX;
            for (const Vector3f &corner : corners)
            {
                const Vector3f corner_ls = TransformCoord(light_view, corner);
                min_z = std::min(min_z, corner_ls.z);
                max_z = std::max(max_z, corner_ls.z);
            }
            constexpr f32 kCasterDepthMargin = 20.0f;
            min_z -= kCasterDepthMargin;
            max_z += kCasterDepthMargin;

            Matrix4x4f projection;
            BuildOrthographicMatrix(projection, center_ls.x - radius, center_ls.x + radius, center_ls.y + radius,
                                    center_ls.y - radius, min_z, max_z);
            CascadeShadowInfo result;
            result._view = light_view;
            result._projection = projection;
            result._center = light_right * center_ls.x + light_corrected_up * center_ls.y + light_forward * center_ls.z;
            result._radius = radius;
            return result;
        }

        Ailu::ECS::LightingSystem::LightingSystem()
        {
            _light_probe_debug_mat = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/cubemap_debug.hlsl"), "lightprobe_debug");
            //TODO: 似乎存在额外的对齐原因导致读写大小不匹配，后续在看看
            //auto [probe_data, data_size] = FileManager::ReadFile(ResourceMgr::GetResSysPath(L"ScreenGrab/light_probe.data"));
            //LightProbeHeadInfo head;
            //memcpy(&head, probe_data, sizeof(LightProbeHeadInfo));
            //u16 w = head._prefilter_size;
            //_test_prefilter_map = CubeMap::Create(w, true, ETextureFormat::kR11G11B10);
            //_test_radiance_map = CubeMap::Create(head._radiance_size, false, ETextureFormat::kR11G11B10);
            //u64 offset = sizeof(LightProbeHeadInfo);
            //for (u16 face = 1; face <= 6; face++)
            //{
            //    for (u16 i = 0; i < _test_prefilter_map->MipmapLevel(); i++)
            //    {
            //        auto [cur_w, cur_h] = Texture::CalculateMipSize(_test_prefilter_map->Width(), _test_prefilter_map->Width(), i);
            //        _test_prefilter_map->SetPixelData((ECubemapFace) face, probe_data + offset, i);
            //        offset += cur_w * cur_h * GetPixelByteSize(_test_prefilter_map->PixelFormat());
            //    }
            //}
            //for (u16 face = 1; face <= 6; face++)
            //{
            //    _test_radiance_map->SetPixelData((ECubemapFace) face, probe_data + offset, 0);
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
        void LightingSystem::Update(Register &r, f32 delta_time)
        {
            PROFILE_BLOCK_CPU("LightingSystem::Update")
            u32 index = 0;
            if (g_pGfxContext->GetFrameCount() > 5)
            {
                for (auto &comp: r.View<CLightProbe>())
                {
                    const u32 probe_index = index++;
                    const Entity entity = r.GetEntity<CLightProbe>(probe_index);
                    if (!r.IsEntityEnabled(entity) || !r.IsComponentEnabled<CLightProbe>(entity))
                        continue;
                    if (comp._is_dirty || comp._is_update_every_tick)
                    {
                        const auto *transform = r.GetComponent<CLightProbe, TransformComponent>(probe_index);
                        Camera cam;
                        cam.Position(transform->GetRenderPosition());
                        cam.Near(1.0f);
                        cam.Far(comp._size);
                        cam._layer_mask = 0;
                        cam._layer_mask |= static_cast<u32>(ERenderLayer::kDefault);
                        cam.TargetTexture(comp._cubemap.get());
                        auto renderer = Render::RenderPipeline::Get().GetRenderer();
                        renderer->_is_render_light_probe = true;
                        renderer->SubmitTaskPass(comp._pass.get());
                        renderer->Render(cam, *SceneManagement::SceneMgr::Get().ActiveScene());
                        renderer->_is_render_light_probe = false;
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
                            auto face = (ECubemapFace) j;
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
                            auto face = (ECubemapFace) j;
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
                    //else if (comp._src_type == 3)
                    //{
                    //    _light_probe_debug_mat->EnableKeyword("DEBUG_REFLECT");
                    //    _light_probe_debug_mat->SetTexture("_SrcCubeMap", _test_prefilter_map.get());
                    //}
                    //else if (comp._src_type == 4)
                    //{
                    //    _light_probe_debug_mat->DisableKeyword("DEBUG_REFLECT");
                    //    _light_probe_debug_mat->SetTexture("_SrcCubeMap", _test_radiance_map.get());
                    //}
                    else {}
                    _light_probe_debug_mat->SetFloat("_mipmap", comp._mipmap);
                    comp._debug_material = _light_probe_debug_mat.get();
                }
            }
            ProcessVXGI(r);
            index = 0;
            static ObjectLayer shadow_cast_layer = Application::Get().NameToLayer("ShadowCaster");
            for (auto &e: _entities)
            {
                if (!r.IsEntityEnabled(e) || !r.IsComponentEnabled<LightComponent>(e))
                    continue;
                const auto *transform = r.GetComponent<TransformComponent>(e);
                const Transform transf(transform->GetRenderPosition(), transform->GetRenderRotation(), transform->GetRenderScale());
                auto comp = r.GetComponent<LightComponent>(e);
                comp->_light._light_pos = transf._position;
                Vector3f light_forward = LightComponent::kDefaultDirectionalLightDir;
                light_forward.xyz = transf._rotation * light_forward.xyz;
                comp->_light._light_dir.xyz = Normalize(Vector3f{light_forward.xyz});

                if (comp->_type == ELightType::kDirectional)
                {
                    if (!comp->_shadow._is_cast_shadow)
                    {
                        comp->_shadow._shaodwcam_num = 0u;
                        continue;
                    }
                    comp->_shadow._shaodwcam_num = QuailtySetting::s_cascade_shadow_map_count;
                    Camera &selected_cam = Camera::sSelected ? *Camera::sSelected : *Camera::sCurrent;
                    f32 cur_level_start = 0, cur_level_end = QuailtySetting::s_main_light_shaodw_distance;
                    for (u16 i = 0; i < QuailtySetting::s_cascade_shadow_map_count; ++i)
                    {
                        cur_level_end = QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[i];
                        if (i > 0)
                        {
                            cur_level_start = QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[i - 1];
                        }
                        const CascadeShadowInfo shadow = BuildCascadeShadow(selected_cam, comp->_light._light_dir.xyz,
                                                                             cur_level_start, cur_level_end,
                                                                             QuailtySetting::s_cascade_shaodw_map_resolution);
                        comp->_shadow_cameras[i].SetViewAndProj(shadow._view, shadow._projection);
                        comp->_shadow_cameras[i]._layer_mask &= shadow_cast_layer._value;
                        comp->_cascade_shadow_data[i].xyz = shadow._center;
                        comp->_cascade_shadow_data[i].w = shadow._radius * shadow._radius;
                    }
                }
                else if (comp->_type == ELightType::kSpot)
                {
                    if (!comp->_shadow._is_cast_shadow)
                    {
                        comp->_shadow._shaodwcam_num = 0u;
                        continue;
                    }
                    comp->_shadow._shaodwcam_num = 1u;
                    f32 n = 0.01f, f = comp->_light._light_param.x * 1.5f;
                    comp->_shadow_cameras[0].SetLens(90, 1, n, f);
                    comp->_shadow_cameras[0].Position(comp->_light._light_pos.xyz);
                    comp->_shadow_cameras[0].LookTo(comp->_light._light_dir.xyz, Vector3f::kUp);
                    comp->_shadow_cameras[0]._layer_mask &= shadow_cast_layer._value;
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
                    if (!comp->_shadow._is_cast_shadow)
                    {
                        comp->_shadow._shaodwcam_num = 0u;
                        continue;
                    }
                    comp->_shadow._shaodwcam_num = 6;
                    f32 n = 0.01f, f = comp->_light._light_param.x * 1.5f;
                    for (int i = 0; i < 6; i++)
                    {
                        comp->_shadow_cameras[i].SetLens(90, 1, n,f);
                        comp->_shadow_cameras[i].Position(comp->_light._light_pos.xyz);
                        comp->_shadow_cameras[i].LookTo(targets[i], ups[i]);
                        comp->_shadow_cameras[i]._layer_mask &= shadow_cast_layer._value;
                    }
                }
                else //Area light
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
                    const auto &mat = transform->GetRenderWorldMatrix();
                    TransformCoord(comp->_light._area_points[0], mat);
                    TransformCoord(comp->_light._area_points[1], mat);
                    TransformCoord(comp->_light._area_points[2], mat);
                    TransformCoord(comp->_light._area_points[3], mat);
                    if (!comp->_shadow._is_cast_shadow)
                    {
                        comp->_shadow._shaodwcam_num = 0u;
                        continue;
                    }
                    comp->_shadow._shaodwcam_num = 1u;
                    f32 n = 0.01f, f = comp->_light._light_param.x * 1.5f;
                    comp->_shadow_cameras[0].SetLens(90, 1, n, f);
                    comp->_shadow_cameras[0].Position(comp->_light._light_pos.xyz);
                    comp->_shadow_cameras[0].LookTo(comp->_light._light_dir.xyz, Vector3f::kUp);
                    comp->_shadow_cameras[0]._layer_mask &= shadow_cast_layer._value;
                }
            }
        }

        void LightingSystem::ProcessVXGI(Register &r)
        {
            for (auto &vxgi: r.View<CVXGI>())
            {
                auto t = r.GetComponent<CVXGI, TransformComponent>(0);
                if (auto cam_comp = r.GetComponent<CVXGI, CCamera>(0); cam_comp != nullptr)
                {
                    auto &cam = cam_comp->_camera;
                    if (cam.Type() == ECameraType::kOrthographic)
                    {
                        LOG_WARNING("VXGI only support perspective camera!");
                    }
                    else
                    {
                        auto aabb = Camera::GetBoundingAABB(cam, 0.f, vxgi._distance);
                        auto box_size = aabb.Size();
                        f32 voxel_size = box_size.x > box_size.y ? box_size.x : box_size.y;
                        voxel_size = box_size.z > voxel_size ? box_size.z : voxel_size;
                        //cam._is_gen_voxel = true;
                        vxgi._center = aabb.Center();
                        vxgi._size = Vector3f(voxel_size);
                        vxgi._grid_size = vxgi._size / Vector3f((f32)vxgi._grid_num.x,(f32) vxgi._grid_num.y,(f32) vxgi._grid_num.z);
                        aabb._min = vxgi._center - vxgi._size * 0.5f;
                        aabb._max = vxgi._center + vxgi._size * 0.5f;
                        Gizmo::DrawAABB(aabb, Colors::kYellow);
                    }
                    if (vxgi._is_draw_grid)
                        DebugDrawer::DebugWireframe(vxgi, t->GetRenderWorldMatrix(), Colors::kGreen);
                    Camera::sCurrent->_is_gen_voxel = true;
                }
                break;
            }
        }
        #pragma endregion
    }// namespace ECS
    //namespace ECS
}// namespace Ailu
