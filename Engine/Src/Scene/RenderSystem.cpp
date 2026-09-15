#include "Scene/RenderSystem.h"
#include "Render/Material.h"
#include "Render/CommandBuffer.h"
#include "Render/StaticDiffuseProbeData.h"
#include "Render/Texture.h"
#if AILU_ENABLE_FRAME_DEBUGGER
#include "Render/FrameDebugger/FrameCaptureService.h"
#endif
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"
#include "pch.h"
#include <Render/Gizmo.h>
#include <Render/GraphicsContext.h>
#include <Render/RenderPipeline.h>
#include "Framework/Common/Application.h"
#include "Render/Features/RenderFeature.h"
#include "DirectXPackedVector.h"

namespace Ailu
{
    namespace ECS
    {
        using namespace Ailu::Render;
        #pragma region LightingSystem
        // 每次烘焙（含多遍烘焙的每一遍）都取一个单调递增的 revision：
        // StaticDiffuseProbeSystem::Revision() 依赖它，否则重烘后 Renderer 侧的 GI 采样缓存不会失效。
        static u64 NextProbeBakeRevision()
        {
            static std::atomic<u64> s_probe_bake_revision{0u};
            return s_probe_bake_revision.fetch_add(1u, std::memory_order_relaxed) + 1u;
        }

        Ailu::ECS::LightingSystem::LightingSystem()
        {
            _light_probe_debug_mat = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/cubemap_debug.alasset"), "lightprobe_debug");
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

        RenderTexture *LightingSystem::GetStaticProbePreviewCubemap(Entity entity, u32 probe_index) const
        {
            if (entity != _static_probe_preview_group || probe_index >= _static_probe_preview_cubemaps.size())
                return nullptr;
            return _static_probe_preview_cubemaps[probe_index].get();
        }

        void LightingSystem::UpdateLightWorldState(Register &r)
        {
            for (auto &e : _entities)
            {
                if (!r.IsEntityEnabled(e) || !r.IsComponentEnabled<LightComponent>(e))
                    continue;
                const auto *transform_component = r.GetComponent<TransformComponent>(e);
                auto *light_component = r.GetComponent<LightComponent>(e);
                if (transform_component == nullptr || light_component == nullptr)
                    continue;

                const Transform transform(transform_component->GetRenderPosition(),
                                          transform_component->GetRenderRotation(),
                                          transform_component->GetRenderScale());
                light_component->_light._light_pos = transform._position;
                Vector3f light_forward = LightComponent::kDefaultDirectionalLightDir;
                light_forward.xyz = transform._rotation * light_forward.xyz;
                light_component->_light._light_dir.xyz = Normalize(Vector3f{light_forward.xyz});

                if (light_component->_type != ELightType::kArea)
                    continue;
                memset(light_component->_light._area_points, 0, sizeof(light_component->_light._area_points));
                const f32 half_width = light_component->_light._light_param.y * 0.5f;
                const f32 half_height = light_component->_light._light_param.z * 0.5f;
                light_component->_light._area_points[0].x = -half_width;
                light_component->_light._area_points[0].z = half_height;
                light_component->_light._area_points[1].x = half_width;
                light_component->_light._area_points[1].z = half_height;
                light_component->_light._area_points[2].x = half_width;
                light_component->_light._area_points[2].z = -half_height;
                light_component->_light._area_points[3].x = -half_width;
                light_component->_light._area_points[3].z = -half_height;
                const auto &world_matrix = transform_component->GetRenderWorldMatrix();
                for (auto &point : light_component->_light._area_points)
                    TransformCoord(point, world_matrix);
            }
        }

        void LightingSystem::Update(Register &r, f32 delta_time)
        {
            PROFILE_BLOCK_CPU("LightingSystem::Update")
            UpdateLightWorldState(r);
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
                        renderer->SubmitTaskPass(comp._pass.get());
                        const RenderRequest request = MakeReflectionProbeRenderRequest(
                            std::min(comp._size, QuailtySetting::s_probe_shadow_distance));
                        renderer->Render(cam, *SceneManagement::SceneMgr::Get().ActiveScene(), request);
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
            UpdateStaticDiffuseProbeBake(r);
            ProcessVXGI(r);
        }

        void LightingSystem::UpdateStaticDiffuseProbeBake(Register &r)
        {
            if (!_static_probe_bake_active)
            {
                // 多遍烘焙：上一遍结束后自动用同一组再跑一遍，上一遍烘出的 GI 会作为间接光参与本次 capture。
                // 单遍烘焙等于零弹射（capture 里只有直接光 + 天空 IBL），室内间接光会明显偏弱。
                bool continue_pass = _static_probe_bake_next_pass;
                _static_probe_bake_next_pass = false;
                if (continue_pass && (_static_probe_bake_pass + 1u >= _static_probe_bake_pass_count ||
                                      _static_probe_bake_group == kInvalidEntity))
                    continue_pass = false;
                if (continue_pass)
                    ++_static_probe_bake_pass;

                u32 group_index = 0u;
                for (auto &group : r.View<CLightProbeGroup>())
                {
                    const Entity entity = r.GetEntity<CLightProbeGroup>(group_index++);
                    if (continue_pass)
                    {
                        if (entity != _static_probe_bake_group)
                            continue;
                    }
                    else if (!group._bake_requested || !r.IsEntityEnabled(entity) ||
                             !r.IsComponentEnabled<CLightProbeGroup>(entity))
                        continue;

                    if (!continue_pass)
                    {
                        group._bake_requested = false;
                        _static_probe_capture_requested = group._capture_bake_requested;
                        group._capture_bake_requested = false;
                        _static_probe_bake_pass = 0u;
                        _static_probe_bake_pass_count = std::max(1u, QuailtySetting::s_static_probe_bake_pass_count);
                    }

                    const auto *transform = r.GetComponent<TransformComponent>(entity);
                    if (transform == nullptr)
                        continue;

                    _static_probe_bake_group = entity;
                    _static_probe_bake_index = 0u;
                    _static_probe_bake_readback_count.store(0u, std::memory_order_relaxed);
                    _static_probe_bake_data = MakeRef<Render::StaticDiffuseProbeData>("StaticDiffuseProbeData");
                    _static_probe_bake_data->_revision = NextProbeBakeRevision();
                    _static_probe_bake_data->_grid._probe_count = group._probe_count;
                    _static_probe_bake_data->_grid._origin = Vector3f(
                        group._probe_count.x > 1 ? -group._size.x * 0.5f : 0.0f,
                        group._probe_count.y > 1 ? -group._size.y * 0.5f : 0.0f,
                        group._probe_count.z > 1 ? -group._size.z * 0.5f : 0.0f);
                    _static_probe_bake_data->_grid._spacing = Vector3f(
                        group._probe_count.x > 1 ? group._size.x / static_cast<f32>(group._probe_count.x - 1) : 1.0f,
                        group._probe_count.y > 1 ? group._size.y / static_cast<f32>(group._probe_count.y - 1) : 1.0f,
                        group._probe_count.z > 1 ? group._size.z / static_cast<f32>(group._probe_count.z - 1) : 1.0f);
                    _static_probe_bake_data->_probes.resize(_static_probe_bake_data->_grid.ProbeCount());
                    _static_probe_preview_group = entity;
                    _static_probe_preview_cubemaps.clear();
                    _static_probe_preview_cubemaps.resize(_static_probe_bake_data->_probes.size());
                    _static_probe_bake_cubemap = nullptr;
                    _static_probe_bake_active = !_static_probe_bake_data->_probes.empty();
                    if (!_static_probe_bake_active)
                    {
                        LOG_ERROR("Failed to initialize static diffuse probe bake resources");
                        _static_probe_bake_data = nullptr;
                        _static_probe_preview_cubemaps.clear();
                        _static_probe_preview_group = kInvalidEntity;
                    }
                    break;
                }
            }

            if (!_static_probe_bake_active)
                return;

            auto *group = r.GetComponent<CLightProbeGroup>(_static_probe_bake_group);
            const auto *transform = r.GetComponent<TransformComponent>(_static_probe_bake_group);
            auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
            auto *renderer = Render::RenderPipeline::Get().GetRenderer();
            if (group == nullptr || transform == nullptr || scene == nullptr || renderer == nullptr ||
                _static_probe_bake_index >= _static_probe_bake_data->_probes.size())
            {
                _static_probe_bake_active = false;
                return;
            }

            const auto &grid = _static_probe_bake_data->_grid;
            const u32 probe_index = _static_probe_bake_index++;
            const i32 x = static_cast<i32>(probe_index % static_cast<u32>(grid._probe_count.x));
            const u32 yz = probe_index / static_cast<u32>(grid._probe_count.x);
            const i32 y = static_cast<i32>(yz % static_cast<u32>(grid._probe_count.y));
            const i32 z = static_cast<i32>(yz / static_cast<u32>(grid._probe_count.y));
            const Vector3f local_position = grid._origin + Vector3f(
                static_cast<f32>(x) * grid._spacing.x, static_cast<f32>(y) * grid._spacing.y,
                static_cast<f32>(z) * grid._spacing.z);
            Ref<Render::RenderTexture> &cubemap = _static_probe_preview_cubemaps[probe_index];
            cubemap = Render::RenderTexture::Create(64u, std::format("StaticDiffuseProbeBake_{}", probe_index),
                                                     Render::ERenderTargetFormat::kDefaultHDR, false, true, false);
            if (cubemap == nullptr)
            {
                LOG_ERROR("Failed to create static diffuse probe cubemap {}", probe_index);
                _static_probe_bake_active = false;
                _static_probe_bake_data = nullptr;
                _static_probe_bake_cubemap = nullptr;
                _static_probe_preview_cubemaps.clear();
                _static_probe_preview_group = kInvalidEntity;
                return;
            }
            _static_probe_bake_cubemap = cubemap;
            Camera camera;
            camera.Position(transform->GetRenderPosition() + local_position);
            camera.Near(0.01f);
            camera.Far(std::max(group->_size.x, std::max(group->_size.y, group->_size.z)) * 2.0f);
            camera._layer_mask = static_cast<u32>(ERenderLayer::kDefault);
            camera.TargetTexture(cubemap.get());
            if (_static_probe_capture_requested)
            {
                g_pGfxContext->TakePixCapture();
#if AILU_ENABLE_FRAME_DEBUGGER
                Render::FrameDebugger::FrameCaptureOptions capture_options;
                capture_options._output_mode = Render::FrameDebugger::EFrameCaptureOutputMode::kMetadataOnly;
                Render::FrameDebugger::FrameCaptureService::RequestCapture(capture_options);
#endif
                LOG_INFO("Static diffuse probe PIX capture requested; the next probe frame contains all six cubemap faces.");
                _static_probe_capture_requested = false;
            }
            RenderRequest request = MakeStaticDiffuseProbeRenderRequest(QuailtySetting::s_probe_shadow_distance);
            if (_static_probe_bake_pass > 0u)
            {
                // 第 2 遍起：把上一遍烘出的 GI 一起算进 capture，补上单遍烘焙缺失的弹射光能
                request._feature_flags |= kRenderFeatureStaticDiffuseGi;
            }
            renderer->Render(camera, *scene, request);

            auto command_buffer = Render::CommandBufferPool::Get("StaticDiffuseProbeSH");
            command_buffer->Clear();
            // 探针 SH 不允许占用全局 g_environment_sh（该槽位由 EnvironmentSystem 独占）。
            // 否则下一颗探针 capture 会把本颗探针的 SH 当成环境光，烘焙结果依赖烘焙顺序。
            _static_probe_projector.Execute(command_buffer.get(), cubemap.get(),
                                            _static_probe_projector.Output().get());
            const auto bake_data = _static_probe_bake_data;
            const Entity bake_group = _static_probe_bake_group;
            command_buffer->ReadbackBuffer(_static_probe_projector.Output().get(), false,
                                           sizeof(Render::SphericalHarmonicsL2),
                                           [this, bake_data, bake_group, probe_index](const u8 *bytes, u32 size)
            {
                if (bytes == nullptr || size < sizeof(Render::SphericalHarmonicsL2) ||
                    probe_index >= bake_data->_probes.size())
                    return;
                memcpy(bake_data->_probes[probe_index]._coefficients, bytes,
                       sizeof(Render::SphericalHarmonicsL2));
                const u32 readback_count = _static_probe_bake_readback_count.fetch_add(
                    1u, std::memory_order_acq_rel) + 1u;
                if (readback_count != bake_data->_probes.size())
                    return;

                bake_data->_revision++;
                const WString asset_path = ToWChar(std::format("project://StaticDiffuseProbeData_{}.alasset", bake_group));
                Asset *asset = ResourceMgr::Get().CreateAsset(asset_path, bake_data, true);
                if (asset != nullptr)
                {
                    if (auto *active_scene = SceneManagement::SceneMgr::Get().ActiveScene(); active_scene != nullptr)
                    {
                        if (auto *active_group = active_scene->GetRegister().GetComponent<CLightProbeGroup>(bake_group);
                            active_group != nullptr)
                            active_group->_baked_data = asset->GetGuid();
                        active_scene->MarkEdited();
                    }
                    ResourceMgr::Get().SaveAllUnsavedAssets();
                    LOG_INFO(L"Baked {} static diffuse probes (pass {}/{}) to {}", bake_data->_probes.size(),
                             _static_probe_bake_pass + 1u, _static_probe_bake_pass_count, asset_path);
                }
                _static_probe_bake_active = false;
                _static_probe_bake_data = nullptr;
                _static_probe_bake_cubemap = nullptr;
                _static_probe_bake_next_pass = _static_probe_bake_pass + 1u < _static_probe_bake_pass_count;
                if (_static_probe_bake_next_pass)
                    LOG_INFO("Static diffuse probe bake pass {}/{} finished; the next pass bakes with this GI",
                             _static_probe_bake_pass + 1u, _static_probe_bake_pass_count);
            });
            g_pGfxContext->ExecuteCommandBuffer(command_buffer);
            Render::CommandBufferPool::Release(command_buffer);
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
