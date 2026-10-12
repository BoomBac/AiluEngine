#include "Render/Renderer.h"
#include "Render/GraphicsContext.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Render/DrawSubmission.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Application.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/Features/PostprocessStack.h"
#include "Render/Features/SSAO.h"
#include "Render/Features/TemporalAA.h"
#include "Render/Features/VolumetricClouds.h"
#include "Render/Features/VoxelGI.h"
#include "Render/Features/GpuTerrain.h"
#include "Render/Features/RayTraceGI.h"
#include "Render/Features/RTXDI.h"
#include "Render/Features/VolumetricFog.h"
#include "Render/Features/DDGI.h"
#include "Render/Features/SurfelGI.h"
#include "Render/2D/SpriteRenderFeature.h"
#include "Render/RenderPipeline.h"
#include "Render/RenderingData.h"
#include "Render/CommandBuffer.h"
#include "Render/FrameResource.h"
#include "Animation/AnimationSystem.h"
#include "Animation/SkinningSystem.h"
#include "Framework/Common/JobSystem.h"

#include "Render/RenderGraph/RenderGraph.h"

#include "pch.h"



namespace Ailu::Render
{
    using SceneManagement::SceneMgr;
    Renderer::Renderer() : _cur_fs(nullptr)
    {
        _p_context = g_pGfxContext;
        _target_tex = nullptr;
        _b_init = true;
        Profiler::Initialize();
        _rd_graph = nullptr;
        _rd_graph = AL_NEW(RDG::RenderGraph);
        _rendering_data._gbuffers.resize(4);
        _rendering_data._rg_handles._gbuffers.resize(4);
        _shadowcast_pass = MakeScope<ShadowCastPass>(&_shadow_system);
        _gbuffer_pass = MakeScope<DeferredGeometryPass>();
        _coptdepth_pass = MakeScope<CopyDepthPass>();
        _lighting_pass = MakeScope<DeferredLightingPass>();
        _sky_pass = MakeScope<SkyPass>();
        _motion_vector_pass = MakeScope<MotionVectorPass>();
        _forward_pass = MakeScope<ForwardPass>();
        _copycolor_pass = MakeScope<CopyColorPass>();
        _postprocess_stack = MakeScope<PostprocessStack>();
        _gizmo_pass = MakeScope<GizmoPass>();
        _wireframe_pass = MakeScope<WireFramePass>();
        _gui_pass = MakeScope<GUIPass>();
        _hzb_pass = MakeScope<HZBPass>();
        _depth_only_pass = MakeScope<DepthOnlyPass>();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, TemporalAA));
        _taa = _owned_features.back();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, VoxelGI));
        _vxgi = _owned_features.back();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, VolumetricClouds));
        _cloud = _owned_features.back();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, SSAO));
        _ssao = _owned_features.back();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, GpuTerrain));
        _gpu_terrain = _owned_features.back();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, RayTraceGI));
        _raytrace_gi = _owned_features.back();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, RTXDI));
        _rtxdi = _owned_features.back();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, VolumetricFog));
        _fog = _owned_features.back();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, DDGIFeature));
        _ddgi = _owned_features.back();
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, SurfelGIFeature));
        _surfel_gi = _owned_features.back();
        _lighting_pass->SetSurfelGI(static_cast<SurfelGIFeature *>(_surfel_gi));
        _owned_features.push_back(AL_NEW_TAG(EMemoryTag::kRenderer, SpriteRenderFeature));
        _sprite = _owned_features.back();
        //_features.push_back(_vxgi);
        //_features.push_back(_cloud);
        _features.push_back(_taa);
        _features.push_back(_ssao);
        _features.push_back(_raytrace_gi);
        _raytrace_gi->SetActive(false);
        _features.push_back(_rtxdi);
        _rtxdi->SetActive(false);
        _features.push_back(_fog);
        _fog->SetActive(true);
        _features.push_back(_ddgi);
        _features.push_back(_surfel_gi);
        _features.push_back(_sprite);
        _material_data_lut[0] = 0; //default material
        MaterialData miss_mat{};
        miss_mat._base_color = float3(1.0f, 0.0f, 1.0f);//洋红色
        miss_mat._base_color_tex = RenderConstants::kInvalidBindlessHandle;
        miss_mat._normal_tex = RenderConstants::kInvalidBindlessHandle;
        miss_mat._emission = float3(0.0f, 0.0f, 0.0f);
        miss_mat._emission_tex = RenderConstants::kInvalidBindlessHandle;
        miss_mat._metallic = 0.0f;
        miss_mat._roughness = 1.0f;
        miss_mat._roughness_tex = RenderConstants::kInvalidBindlessHandle;
        miss_mat._metallic_tex = RenderConstants::kInvalidBindlessHandle;
        _material_data_cache.push_back(miss_mat);
        //_features.push_back(_gpu_terrain);
        //_features.push_back(_taa);
    }

    Renderer::~Renderer()
    {
        for (auto *feature: _owned_features)
            AL_DELETE(feature);
        _owned_features.clear();
        AL_DELETE(_rd_graph);
        Profiler::Shutdown();
    }

    void Renderer::SetupFrameResource(FrameResource *prev_fr, FrameResource *cur_fr)
    {
        _prev_fs = prev_fr;
        _cur_fs = cur_fr;
        _scene_render_data_valid = false;
        _scene_render_data = {};
        _view_data.clear();
        _view_data.reserve(128u);
        _active_view_data = nullptr;
        FrameResource::SetActive(cur_fr);
    }

    void Renderer::Render(const Camera &cam, const Scene &s)
    {
        Render(cam, s, MakeCameraRenderRequest());
    }

    void Renderer::Render(const Camera &cam, const Scene &s, const RenderRequest &request)
    {
        if (_cur_fs == nullptr)
            return;
        _render_request = request;
        if (!_scene_render_data_valid)
            PrepareSceneRenderData(s);

        ShadowSet shadow_set;
        const ShadowSet *shadow_set_ptr = nullptr;
        if (request._enable_shadows)
        {
            _shadow_system.Prepare(cam, s, request, shadow_set);
            CullShadowSet(s, shadow_set);
            shadow_set_ptr = &shadow_set;
        }
        ApplyShadowSetToLightData(s, shadow_set_ptr);
        bool is_output_to_camera_tex = cam.TargetTexture() != nullptr;
        for (auto &e: _events_before_tick)
        {
            e();
        }
        if (is_output_to_camera_tex)
        {
            if (cam.TargetTexture()->Dimension() == ETextureDimension::kTex2D)
            {
                DoRender(cam, s, request, shadow_set_ptr, request._enable_shadows,
                         _is_use_render_graph ? cam.TargetTexture() : nullptr);
                if (!_is_use_render_graph)
                {
                    auto cmd = CommandBufferPool::Get("FinalBlit");
                    {
                        PROFILE_BLOCK_GPU(cmd.get(), cmd->Name())
                        auto blit_material = ResourceMgr::Get().Get<Material>(L"Runtime/Material/Blit");
                        const u16 pass_index = cam.UpsamplingFilter() == EUpsamplingFilter::kPoint ? 1u : 0u;
                        cmd->Blit(_rendering_data._camera_color_target_handle, cam.TargetTexture(), blit_material, pass_index);
                    }
                    _p_context->ExecuteCommandBuffer(cmd);
                    CommandBufferPool::Release(cmd);
                }
            }
            else if (cam.TargetTexture()->Dimension() == ETextureDimension::kCube)
            {
                for (u16 i = 1; i <= 6; i++)
                {
                    _is_rendering_cubemap = true;
                    auto face = (ECubemapFace) i;
                    Camera &tmp_cam = Camera::GetCubemapGenCamera(cam, face);
                    tmp_cam._is_scene_camera = false;
                    tmp_cam._postprocess_enabled = false;
                    tmp_cam.OutputSize(cam.TargetTexture()->Width(), cam.TargetTexture()->Height());
                    tmp_cam.PrepareRenderFrame(_p_context->GetFrameCount());
                    const u16 dst_view_index = cam.TargetTexture()->CalculateViewIndex(Texture::ETextureViewType::kRTV, face, 0, 0);
                    DoRender(tmp_cam, s, request, shadow_set_ptr, i == 1u && request._enable_shadows,
                             _is_use_render_graph ? cam.TargetTexture() : nullptr, dst_view_index);
                    _is_rendering_cubemap = false;
                    if (!_is_use_render_graph)
                    {
                        auto cmd = CommandBufferPool::Get("FinalBlit");
                        {
                            PROFILE_BLOCK_GPU(cmd.get(), cmd->Name())
                            auto blit_material = ResourceMgr::Get().Get<Material>(L"Runtime/Material/Blit");
                            const u16 pass_index = cam.UpsamplingFilter() == EUpsamplingFilter::kPoint ? 1u : 0u;
                            cmd->Blit(g_pRenderTexturePool->Get(_rendering_data._camera_color_target_handle), cam.TargetTexture(), 0,
                                      dst_view_index, blit_material, pass_index);
                        }
                        _p_context->ExecuteCommandBuffer(cmd);
                        CommandBufferPool::Release(cmd);
                    }
                    //CommandBufferPool::WaitForAllCommand();
                }
            }
        }
        else
        {
            DoRender(cam, s, request, shadow_set_ptr, request._enable_shadows);
        }

        for (auto &e: _events_after_tick)
        {
            e();
        }

        if (!_p_task_render_passes.empty())
        {
            for (auto task_pass: _p_task_render_passes)
            {
                task_pass->BeginPass(_p_context);
                task_pass->Execute(_p_context, _rendering_data);
            }
            _p_task_render_passes.clear();
        }
    }

    void Renderer::EnqueuePass(RenderPass *pass)
    {
        _render_passes.emplace_back(pass);
    }

    Vector<RenderFeature *> Renderer::GetFeatures()
    {
        return _features;
    }
    void Renderer::BeginScene(const Camera &cam, const Scene &s)
    {
        _active_camera_hash = cam.HashCode();
        const Vector2D<u32> render_size = cam.RenderSize();
        u32 pixel_width = render_size.x, pixel_height = render_size.y;
        _rendering_data._width = pixel_width;
        _rendering_data._height = pixel_height;
        if (_is_use_render_graph)
        {
            const Vector2D<u32> output_size = cam.OutputSize();
            if (_presentation_texture == nullptr || _presentation_texture->Width() != output_size.x ||
                _presentation_texture->Height() != output_size.y)
            {
                Ref<RenderTexture> matching_texture = nullptr;
                for (auto &texture: _presentation_texture_cache)
                {
                    if (texture->Width() == output_size.x && texture->Height() == output_size.y)
                    {
                        matching_texture = texture;
                        break;
                    }
                }
                if (matching_texture != nullptr)
                {
                    _presentation_texture = matching_texture;
                }
                else
                {
                    _presentation_texture = RenderTexture::Create(output_size.x, output_size.y, "RendererPresentation",
                                                                  ERenderTargetFormat::kDefaultHDR);
                    _presentation_texture_cache.emplace_back(_presentation_texture);
                    LOG_WARNING("Expand _presentation_texture_cache to {}",_presentation_texture_cache.size());
                }
            }
            _rendering_data._rg_handles._gbuffers[0] = _rd_graph->CreateResource(TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kRGHalf),RenderResourceName::kGBuffer0);
            _rendering_data._rg_handles._gbuffers[1] = _rd_graph->CreateResource(TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDefault), RenderResourceName::kGBuffer1);
            _rendering_data._rg_handles._gbuffers[2] = _rd_graph->CreateResource(TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDefault), RenderResourceName::kGBuffer2);
            _rendering_data._rg_handles._gbuffers[3] = _rd_graph->CreateResource(TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kRGBAHalf), RenderResourceName::kGBuffer3);
            auto cam_color_desc = _rendering_data._camera_data._camera_color_target_desc = TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDefaultHDR);
            _rendering_data._rg_handles._color_target = _rd_graph->CreateResource(cam_color_desc, RenderResourceName::kCameraColorA);
            auto cam_depth_desc = _rendering_data._camera_data._camera_depth_target_desc = TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDepth);
            cam_depth_desc._load = ELoadStoreAction::kNotCare;
            _rendering_data._rg_handles._depth_target = _rd_graph->CreateResource(cam_depth_desc, RenderResourceName::kCameraDepth);
            TextureDesc desc = TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDefaultHDR);
            desc._load = ELoadStoreAction::kNotCare;
            _rendering_data._rg_handles._color_tex = _rd_graph->CreateResource(desc, RenderResourceName::kCameraColorTex);
            desc._format = ConvertRenderTextureFormatToPixelFormat(ERenderTargetFormat::kRFloat);
            _rendering_data._rg_handles._depth_tex = _rd_graph->CreateResource(desc, RenderResourceName::kCameraDepthTex);

            if (_rendering_data._shadow_set != nullptr)
            {
                const auto directional_mode = _rendering_data._shadow_set->_directional._mode;
                _rendering_data._rg_handles._main_light_shadow_map =
                    _rd_graph->Import(_shadow_system.GetDirectionalShadowMap(directional_mode));
                _rendering_data._rg_handles._addi_shadow_maps =
                    _rd_graph->Import(_shadow_system.GetAdditionalShadowMap());
                _rendering_data._rg_handles._point_light_shadow_maps =
                    _rd_graph->Import(_shadow_system.GetPointShadowMap());
            }
            else
            {
                _rendering_data._rg_handles._main_light_shadow_map = {};
                _rendering_data._rg_handles._addi_shadow_maps = {};
                _rendering_data._rg_handles._point_light_shadow_maps = {};
            }

            auto mv_desc = TextureDesc(_rendering_data._width, _rendering_data._height, ERenderTargetFormat::kRGHalf);
            mv_desc._format = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
            _rendering_data._rg_handles._motion_vector_tex = _rd_graph->CreateResource(mv_desc, RenderResourceName::kMotionVectorTex);
            auto hzb_desc = TextureDesc((_rendering_data._width + 1) >> 1, (_rendering_data._height + 1) >> 1, ERenderTargetFormat::kRFloat);
            hzb_desc._is_random_access = true;
            hzb_desc._mip_num = Texture::MaxMipmapCount(hzb_desc._width, hzb_desc._height);
            _rendering_data._rg_handles._hzb = _rd_graph->CreateResource(hzb_desc, RenderResourceName::kHZB);
            _rendering_data._rg_handles._ao_tex = RDG::RGHandle{0u, 0u};
            _rendering_data._rg_handles._surfel_coverage = RDG::RGHandle{0u, 0u};
            // 由 PostprocessStack 在注册阶段填充；未启用后处理时 FinalBlit 回退到 _color_target。
            _rendering_data._rg_handles._final_color = RDG::RGHandle{};
        }
        else
        {
            RenderTexture::ReleaseTempRT(_camera_color_handle);//不释放color rt，防止多摄像机复用同一张color
            RenderTexture::ReleaseTempRT(_camera_depth_handle);
            RenderTexture::ReleaseTempRT(_gameview_rt_handle);
            RenderTexture::ReleaseTempRT(_rendering_data._camera_opaque_tex_handle);
            RenderTexture::ReleaseTempRT(_camera_depth_tex_handle);
            for (u16 i = 0; i < _rendering_data._gbuffers.size(); i++)
            {
                RenderTexture::ReleaseTempRT(_rendering_data._gbuffers[i]);
            }
            _rendering_data._camera_data._camera_color_target_desc._width = pixel_width;
            _rendering_data._camera_data._camera_color_target_desc._height = pixel_height;
            _rendering_data._camera_data._camera_color_target_desc._format = ConvertRenderTextureFormatToPixelFormat(ERenderTargetFormat::kDefaultHDR);
            _rendering_data._camera_data._camera_color_target_desc._load = ELoadStoreAction::kNotCare;
            _camera_color_handle = RenderTexture::GetTempRT(_rendering_data._camera_data._camera_color_target_desc, "CameraColorAttachment");
            _camera_depth_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraDepthAttachment", ERenderTargetFormat::kDepth);
            _camera_depth_tex_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraDepthTexture", ERenderTargetFormat::kRFloat,ELoadStoreAction::kNotCare);
            _rendering_data._camera_opaque_tex_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraColorOpaqueTex", ERenderTargetFormat::kDefaultHDR,ELoadStoreAction::kNotCare);
            _rendering_data._gbuffers[0] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer0", ERenderTargetFormat::kRGHalf);
            _rendering_data._gbuffers[1] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer1", ERenderTargetFormat::kDefault);
            _rendering_data._gbuffers[2] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer2", ERenderTargetFormat::kDefault);
            _rendering_data._gbuffers[3] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer3", ERenderTargetFormat::kRGBAHalf);
        }
        
        _rendering_data._scene = &s;
        _rendering_data._environment = &_environment_system.Runtime();
        _rendering_data._postprocess_settings = ResolvePostprocessSettings(s);
        if (!_scene_render_data_valid)
            PrepareSceneRenderData(s);
        PrepareViews(s, cam);
        PrepareCamera(cam);
        if (_rendering_data._pre_width != pixel_width || _rendering_data._pre_height != pixel_height)
        {
            _rendering_data._is_res_changed = true;
            _rendering_data._pre_width = pixel_width;
            _rendering_data._pre_height = pixel_height;
        }
        else
        {
            _rendering_data._is_res_changed = false;
        }
        _rendering_data._width = pixel_width;
        _rendering_data._height = pixel_height;
        _rendering_data._viewport = Rect{0, 0, (uint16_t) pixel_width, (uint16_t) pixel_height};
        _rendering_data._scissor_rect = _rendering_data._viewport;
        _rendering_data._camera_color_target_handle = _camera_color_handle;
        _rendering_data._camera_depth_target_handle = _camera_depth_handle;
        _rendering_data._camera_depth_tex_handle = _camera_depth_tex_handle;
        _rendering_data._final_rt_handle = _gameview_rt_handle;
        _vxgi->SetActive(!IsRenderingProbe() && (cam._is_gen_voxel || _vxgi->IsActive()));
        if (IsRenderingProbe())
        {
            _ssao->SetActive(false);
            _fog->SetActive(false);
            _raytrace_gi->SetActive(false);
            _rtxdi->SetActive(false);
            if (static_cast<u32>(_mode) & static_cast<u32>(EShadingMode::kLit))
            {
                _sky_pass->Setup(false);
                if (_rendering_data._shadow_set != nullptr)
                    _render_passes.emplace_back(_shadowcast_pass.get());
                _render_passes.emplace_back(_gbuffer_pass.get());
                _render_passes.emplace_back(_lighting_pass.get());
                _render_passes.emplace_back(_forward_pass.get());
            }
            if (cam._is_render_sky_box)
                _render_passes.emplace_back(_sky_pass.get());
        }
        else if (_is_use_raytracing)
        {
            _raytrace_gi->SetActive(true);
            //_rtxdi->SetActive(true);
            _fog->SetActive(false);
            _ssao->SetActive(false);
            _render_passes.emplace_back(_gbuffer_pass.get());
            _render_passes.emplace_back(_motion_vector_pass.get());
            _render_passes.emplace_back(_coptdepth_pass.get());
            //_render_passes.emplace_back(_depth_only_pass.get());
        }
        else
        {
            _ssao->SetActive(true);
            _fog->SetActive(true);
            _raytrace_gi->SetActive(false);
            _rtxdi->SetActive(false);
            if (static_cast<u32>(_mode) & static_cast<u32>(EShadingMode::kLit))
            {
                _sky_pass->Setup(false);
                if (_rendering_data._shadow_set != nullptr)
                    _render_passes.emplace_back(_shadowcast_pass.get());
                if (!_rendering_data._is_debug_voxel)
                {
                    _render_passes.emplace_back(_gbuffer_pass.get());
                    _render_passes.emplace_back(_lighting_pass.get());
                    _render_passes.emplace_back(_motion_vector_pass.get());
                    _render_passes.emplace_back(_forward_pass.get());
                    _render_passes.emplace_back(_coptdepth_pass.get());
                }
                else
                    _sky_pass->Setup(true);
            }
            if (_is_hiz_active)
                _render_passes.emplace_back(_hzb_pass.get());
            //_render_passes.emplace_back(_gui_pass.get());
            if (static_cast<u32>(_mode) & static_cast<u32>(EShadingMode::kWireframe))
            {
                _sky_pass->Setup(!(static_cast<u32>(_mode) & static_cast<u32>(EShadingMode::kLit)));
                _render_passes.emplace_back(_wireframe_pass.get());
            }
            if (cam._is_render_sky_box)
                _render_passes.emplace_back(_sky_pass.get());
        }
        if (cam._is_scene_camera)
            _render_passes.emplace_back(_gizmo_pass.get());
        if (cam._postprocess_enabled)
        {
            _render_passes.emplace_back(_copycolor_pass.get());
            _render_passes.emplace_back(_postprocess_stack.get());
        }
        for (auto &feature: _features)
        {
            if (feature->IsActive())
                feature->AddRenderPasses(*this, _rendering_data);
        }
        if (_is_use_render_graph)
        {
            if (auto *ddgi = dynamic_cast<DDGIFeature *>(_ddgi); ddgi != nullptr)
                ddgi->PrepareDeferredLighting(*_rd_graph, _rendering_data);
        }
        _rendering_data._rg_handles._volumetric_fog_accum = {};
        auto *volumetric_fog = dynamic_cast<VolumetricFog *>(_fog);
        if (_is_use_render_graph && volumetric_fog != nullptr && volumetric_fog->GetAccTexture() != nullptr)
            _rendering_data._rg_handles._volumetric_fog_accum = _rd_graph->Import(volumetric_fog->GetAccTexture());
        std::stable_sort(_render_passes.begin(), _render_passes.end(), [](RenderPass *a, RenderPass *b) -> bool
                         { return *a < *b; });
        _target_tex = g_pRenderTexturePool->Get(_rendering_data._camera_color_target_handle);
        Shader::SetGlobalBuffer(RenderConstants::kCBufNamePerScene, _cur_fs->GetSceneCB(s.HashCode()));
        Shader::SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _cur_fs->GetCameraCB(cam.HashCode()));
        Shader::SetGlobalTexture("_LTC_Lut1", ResourceMgr::Get().Get<Texture2D>(L"Runtime/ltc_lut1"));
        Shader::SetGlobalTexture("_LTC_Lut2", ResourceMgr::Get().Get<Texture2D>(L"Runtime/ltc_lut2"));
        ComputeShader::SetGlobalBuffer(RenderConstants::kCBufNamePerScene, _cur_fs->GetSceneCB(s.HashCode()));
        ComputeShader::SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _cur_fs->GetCameraCB(cam.HashCode()));
        {
            PROFILE_BLOCK_CPU("WaitForSys")
            for (auto &sys: s.GetRegister().SystemView())
            {
                if (sys)
                    sys->WaitFor();
            }
        }
        //RENDER GRAPH
        if (_is_use_render_graph)
        {
            _rendering_data._postprocess_input = nullptr;//taa关闭时，这个不赋值会导致bloom输入为空
            _environment_system.RecordRenderGraph(*_rd_graph, _rendering_data);
            if (auto *animation_system = const_cast<Scene &>(s).GetRegister().GetSystem<ECS::AnimationSystem>();
                animation_system != nullptr)
                animation_system->RecordSkinningRenderGraph(*_rd_graph, _rendering_data);
            {
                for (auto *pass: _render_passes)
                {
                    PROFILE_BLOCK_CPU(std::format("Record {}", pass->Name()).c_str())
                    pass->OnRecordRenderGraph(*_rd_graph, _rendering_data);
                }
            }
            if (auto *animation_system = const_cast<Scene &>(s).GetRegister().GetSystem<ECS::AnimationSystem>();
                animation_system != nullptr)
                _rd_graph->AddVertexBufferReads(animation_system->GetSkinningOutputHandles());
        }
    }
    void Renderer::EndScene(const Scene &s)
    {
        _render_passes.clear();
        _rendering_data.Reset();
        //RENDER GRAPH
        if (_is_use_render_graph)
        {
            _rd_graph->EndFrame();
        }
        Gizmo::EndFrame();
    }
    void Renderer::FrameCleanup()
    {
        //_render_passes.clear();
        //_features.clear();
    }

    void Renderer::SubmitTaskPass(RenderPass *task)
    {
        _p_task_render_passes.emplace_back(task);
    }
    void Renderer::RemoveTaskPass(RenderPass *task)
    {
        _p_task_render_passes.remove(task);
    }
    void Renderer::ResizeBuffer(u32 width, u32 height)
    {
        _resize_events.emplace(Vector2f{(f32) width, (f32) height});
    }
    void Renderer::RegisterEventBeforeTick(BeforeTickEvent e)
    {
        _events_before_tick.emplace_back(e);
    }
    void Renderer::RegisterEventAfterTick(AfterTickEvent e)
    {
        _events_after_tick.emplace_back(e);
    }
    //void Renderer::RegisterEventAfterTick(AfterTickEvent e)
    //{
    //	events_after_tick.emplace_back(e);
    //}
    //void Renderer::UnRegisterEventBeforeTick(BeforeTickEvent e)
    //{
    //	_events_before_tick.remove(e);
    //}
    //void Renderer::UnRegisterEventAfterTick(AfterTickEvent e)
    //{
    //	_events_after_tick.remove(e);
    //}

    void Renderer::BuildScenePrimitives(const Scene &s)
    {
        PROFILE_BLOCK_CPU("RenderPrepare.BuildPrimitives")
        _scene_primitives.clear();
        _primitive_data.clear();
        _primitive_gi_data.clear();
        auto &r = s.GetRegister();
        _static_probe_system.Update(s);
        _static_probe_system.BuildGPUData(_static_diffuse_probe_groups, _static_diffuse_probe_data);
        AL_ASSERT(_static_diffuse_probe_groups.size() <= RenderConstants::kMaxStaticDiffuseProbeGroupCount);
        AL_ASSERT(_static_diffuse_probe_data.size() <= RenderConstants::kMaxStaticDiffuseProbeCount);
        HashMap<ECS::Entity, u32> gi_data_indices;
        auto collect_gi_data = [this, &r, &gi_data_indices](auto &renderables)
        {
            using ComponentType = std::remove_cvref_t<decltype(*renderables.begin())>;
            for (u32 entity_index = 0u; entity_index < renderables.size(); ++entity_index)
            {
                const ECS::Entity entity = r.GetEntity<ComponentType>(entity_index);
                if (!r.IsComponentEnabled<ComponentType>(entity))
                    continue;
                const auto *transform = r.GetComponent<ComponentType, ECS::TransformComponent>(entity_index);
                if (transform == nullptr)
                    continue;
                Vector3f anchor = transform->GetRenderPosition();
                if (!renderables[entity_index]._transformed_aabbs.empty())
                    anchor = renderables[entity_index]._transformed_aabbs[0].Center();
                SphericalHarmonicsL2 sh;
                f32 weight = 0.0f;
                auto &cache = _gi_cache[entity];
                if (cache._valid && cache._revision == _static_probe_system.Revision() && cache._anchor == anchor)
                {
                    sh = cache._sh;
                    weight = 1.0f;
                }
                else if (_static_probe_system.Sample(anchor, sh, weight) && weight > 0.0f)
                {
                    cache._sh = sh;
                    cache._anchor = anchor;
                    cache._revision = _static_probe_system.Revision();
                    cache._valid = true;
                }
                else
                {
                    cache._valid = false;
                    continue;
                }
                PrimitiveGIData gi_data{};
                for (u32 coefficient = 0u; coefficient < SphericalHarmonicsL2::kCoefficientCount; ++coefficient)
                    gi_data._sh[coefficient] = sh._coefficients[coefficient];
                gi_data_indices[entity] = static_cast<u32>(_primitive_gi_data.size());
                _primitive_gi_data.emplace_back(gi_data);
            }
        };
        collect_gi_data(r.View<ECS::StaticMeshComponent>());
        collect_gi_data(r.View<ECS::CSkeletonMesh>());

        struct PrimitiveBuildChunk
        {
            Vector<ScenePrimitive> _primitives;
            Vector<PrimitiveData> _primitive_data;
        };

        auto build_chunks = [this, &s, &r, &gi_data_indices](auto &renderables, bool is_skinned)
        {
            using ComponentType = std::remove_cvref_t<decltype(*renderables.begin())>;
            constexpr u32 kChunkSize = 32u;
            const u32 renderable_count = static_cast<u32>(renderables.size());
            const u32 chunk_count = (renderable_count + kChunkSize - 1u) / kChunkSize;
            Vector<PrimitiveBuildChunk> chunks(chunk_count);
            JobGroup build_group;
            for (u32 chunk_index = 0u; chunk_index < chunk_count; ++chunk_index)
            {
                const u32 begin = chunk_index * kChunkSize;
                const u32 end = std::min(begin + kChunkSize, renderable_count);
                JobSystem::Get().Dispatch(build_group, [&, begin, end, chunk_index, is_skinned]()
                {
                    auto &chunk = chunks[chunk_index];
                    for (u32 entity_index = begin; entity_index < end; ++entity_index)
                    {
                        auto &renderable = renderables[entity_index];
                        const ECS::Entity entity = r.GetEntity<ComponentType>(entity_index);
                        if (!s.IsEntityEnabled(entity) || !r.IsComponentEnabled<ComponentType>(entity) || !renderable._p_mesh)
                            continue;
                        auto *mesh = renderable._p_mesh.get();
                        if (mesh->GetVertexBuffer() == nullptr)
                            continue;
                        const auto &transform = r.GetComponent<ComponentType, ECS::TransformComponent>(entity_index);
                        const Matrix4x4f &local_to_world = transform->GetRenderWorldMatrix();
                        const Matrix4x4f world_to_local = MatrixInverse(local_to_world);
                        const Vector3f inv_scale = Vector3f::kOne / transform->_local_transform._scale;
                        VertexBuffer *vertex_buffer = is_skinned ?
                            ECS::SkinningSystem::ResolveVertexBuffer(mesh, static_cast<u32>(entity)) : nullptr;
                        if (vertex_buffer == nullptr)
                            vertex_buffer = mesh->GetVertexBuffer().get();
                        for (u16 submesh_index = 0u; submesh_index < mesh->SubmeshCount(); ++submesh_index)
                        {
                            Material *material = renderable._p_mats.size() > submesh_index && renderable._p_mats[submesh_index]
                                ? renderable._p_mats[submesh_index].get() : nullptr;
                            PrimitiveData data{};
                            data._gi_data_index = std::numeric_limits<u32>::max();
                            data._local_to_world = local_to_world;
                            data._world_to_local = world_to_local;
                            data._prev_local_to_world = transform->_prev_render_world_matrix;
                            data._max_inv_scale = std::max(inv_scale.x, std::max(inv_scale.y, inv_scale.z));
                            data._entity_id = static_cast<u32>(entity);
                            const auto gi_data_it = gi_data_indices.find(entity);
                            if (gi_data_it != gi_data_indices.end())
                                data._gi_data_index = gi_data_it->second;
                            if (material != nullptr)
                            {
                                const auto material_it = _material_data_lut.find(material->HashCode());
                                data._material_id = material_it != _material_data_lut.end() ? material_it->second : 0u;
                            }
                            data._submesh_id = submesh_index;
                            data._flags = is_skinned ? kPrimitiveSkinned : kPrimitiveNone;
                            data._flags |= renderable._motion_vector_type == ECS::EMotionVectorType::kPerObject ?
                                kPrimitivePerObjectMotion : 0u;
                            data._flags |= renderable._motion_vector_type == ECS::EMotionVectorType::kForceZero ?
                                kPrimitiveForceZeroMotion : 0u;
                            const auto range = s.GetBVHNodeRange(entity, submesh_index);
                            data._blas_node_start = range.x;
                            data._blas_node_count = range.y;
                            const i32 position_index = mesh->GetBindlessVertexStreamIndex(EVertexSemantic::kPosition, vertex_buffer);
                            const i32 normal_index = mesh->GetBindlessVertexStreamIndex(EVertexSemantic::kNormal, vertex_buffer);
                            const i32 uv_index = mesh->GetBindlessVertexStreamIndex(EVertexSemantic::kTexcoord0, vertex_buffer);
                            const i32 tangent_index = mesh->GetBindlessVertexStreamIndex(EVertexSemantic::kTangent, vertex_buffer);
                            data._position_bindless_idx = position_index >= 0 ? static_cast<u32>(position_index) :
                                RenderConstants::kInvalidBindlessHandle;
                            data._normal_bindless_idx = normal_index >= 0 ? static_cast<u32>(normal_index) :
                                RenderConstants::kInvalidBindlessHandle;
                            data._uv_bindless_idx = uv_index >= 0 ? static_cast<u32>(uv_index) :
                                RenderConstants::kInvalidBindlessHandle;
                            data._tangent_bindless_idx = tangent_index >= 0 ? static_cast<u32>(tangent_index) :
                                RenderConstants::kInvalidBindlessHandle;
                            auto *index_buffer = mesh->GetIndexBuffer(submesh_index).get();
                            data._index_bindless_idx = index_buffer != nullptr ?
                                static_cast<u32>(index_buffer->GetBindlessSRVIndex()) : RenderConstants::kInvalidBindlessHandle;
                            data._submesh_triangle_count = mesh->GetTriangleCount(submesh_index);
                            chunk._primitives.emplace_back(ScenePrimitive{static_cast<u32>(chunk._primitives.size()),
                                static_cast<u32>(entity), mesh, material, vertex_buffer, index_buffer,
                                0u,
                                renderable._transformed_aabbs[submesh_index + 1u], submesh_index, static_cast<u16>(data._flags)});
                            chunk._primitive_data.emplace_back(data);
                        }
                    }
                });
            }
            JobSystem::Get().Wait(build_group);
            for (auto &chunk : chunks)
            {
                const u32 primitive_offset = static_cast<u32>(_primitive_data.size());
                for (auto &primitive : chunk._primitives)
                {
                    primitive._primitive_index += primitive_offset;
                    _scene_primitives.emplace_back(primitive);
                }
                _primitive_data.insert(_primitive_data.end(), chunk._primitive_data.begin(), chunk._primitive_data.end());
            }
        };

        build_chunks(s.GetRegister().View<ECS::StaticMeshComponent>(), false);
        build_chunks(s.GetRegister().View<ECS::CSkeletonMesh>(), true);
        for (auto &primitive : _scene_primitives)
        {
            DrawGeometryKey geometry_key{};
            geometry_key._vertex_buffer = primitive._vertex_buffer != nullptr
                ? primitive._vertex_buffer->TypedHandle<BufferTag>() : BufferHandle{};
            geometry_key._index_buffer = primitive._index_buffer != nullptr
                ? primitive._index_buffer->TypedHandle<BufferTag>() : BufferHandle{};
            geometry_key._vertex_buffer_id = primitive._vertex_buffer != nullptr ? primitive._vertex_buffer->ID() : 0u;
            geometry_key._index_buffer_id = primitive._index_buffer != nullptr ? primitive._index_buffer->ID() : 0u;
            geometry_key._index_count = primitive._index_buffer != nullptr ? primitive._index_buffer->GetCount() : 0u;
            geometry_key._submesh_index = primitive._submesh_index;
            primitive._geometry_id = DrawSubmissionRegistry::Get().InternGeometry(geometry_key);
        }
        AL_ASSERT(_primitive_data.size() <= RenderConstants::kMaxRenderObjectCount);
        auto *primitive_buffer = _cur_fs->GetScenePrimitiveBuffer(s.HashCode());
        auto *primitive_gi_buffer = _cur_fs->GetScenePrimitiveGIDataBuffer(s.HashCode());
        auto *static_probe_group_buffer = _cur_fs->GetStaticDiffuseProbeGroupBuffer(s.HashCode());
        auto *static_probe_data_buffer = _cur_fs->GetStaticDiffuseProbeDataBuffer(s.HashCode());
        primitive_buffer->SetData(_primitive_data);
        if (!_primitive_gi_data.empty())
            primitive_gi_buffer->SetData(_primitive_gi_data);
        if (!_static_diffuse_probe_groups.empty())
            static_probe_group_buffer->SetData(_static_diffuse_probe_groups);
        if (!_static_diffuse_probe_data.empty())
            static_probe_data_buffer->SetData(_static_diffuse_probe_data);
        _rendering_data._scene_primitive_buffer = primitive_buffer;
        _rendering_data._material_buffer = _cur_fs->GetMaterialBuffer();
        _rendering_data._primitive_data = &_primitive_data;
        _rendering_data._scene_primitives = &_scene_primitives;
        Shader::SetGlobalBuffer("g_primitive_data", primitive_buffer);
        ComputeShader::SetGlobalBuffer("g_primitive_data", primitive_buffer);
        Shader::SetGlobalBuffer("g_primitive_gi_data", primitive_gi_buffer);
        ComputeShader::SetGlobalBuffer("g_primitive_gi_data", primitive_gi_buffer);
        Shader::SetGlobalBuffer("g_static_diffuse_probe_groups", static_probe_group_buffer);
        Shader::SetGlobalBuffer("g_static_diffuse_probe_data", static_probe_data_buffer);
        ComputeShader::SetGlobalBuffer("g_static_diffuse_probe_groups", static_probe_group_buffer);
        ComputeShader::SetGlobalBuffer("g_static_diffuse_probe_data", static_probe_data_buffer);
        _cur_fs->ResetPrimitiveIndices();
        Shader::SetGlobalBuffer("g_instance_primitive_indices", _cur_fs->GetPrimitiveIndexBuffer());
    }

    void Renderer::RefreshScenePrimitiveVertexBuffers()
    {
        for (auto &primitive : _scene_primitives)
        {
            if ((primitive._flags & kPrimitiveSkinned) == 0u)
                continue;
            if (auto *vertex_buffer = ECS::SkinningSystem::ResolveVertexBuffer(primitive._mesh, primitive._entity_id);
                vertex_buffer != nullptr && vertex_buffer != primitive._vertex_buffer)
            {
                primitive._vertex_buffer = vertex_buffer;
                DrawGeometryKey geometry_key{};
                geometry_key._vertex_buffer = vertex_buffer->TypedHandle<BufferTag>();
                geometry_key._index_buffer = primitive._index_buffer != nullptr
                    ? primitive._index_buffer->TypedHandle<BufferTag>() : BufferHandle{};
                geometry_key._vertex_buffer_id = vertex_buffer->ID();
                geometry_key._index_buffer_id = primitive._index_buffer != nullptr ? primitive._index_buffer->ID() : 0u;
                geometry_key._index_count = primitive._index_buffer != nullptr ? primitive._index_buffer->GetCount() : 0u;
                geometry_key._submesh_index = primitive._submesh_index;
                primitive._geometry_id = DrawSubmissionRegistry::Get().InternGeometry(geometry_key);
            }
        }
        for (auto &view : _view_data)
        {
            for (auto &[queue, objects] : view._cull_result)
            {
                (void)queue;
                for (auto &object : objects)
                {
                    if (object._primitive_index < _scene_primitives.size())
                    {
                        object._vertex_buffer = _scene_primitives[object._primitive_index]._vertex_buffer;
                        object._geometry_id = _scene_primitives[object._primitive_index]._geometry_id;
                    }
                }
            }
        }
    }

    void Renderer::PrepareScene(const Scene &s)
    {
        auto scene_data_buffer = s.GetSceneMeshDataBuffer();
        ComputeShader::SetGlobalBuffer("g_scene", scene_data_buffer);
        ComputeShader::SetGlobalBuffer("g_tlas_buffer", s.GetTLASBuffer());
        ComputeShader::SetGlobalBuffer("g_blas_buffer", s.GetBLASBuffer());
        ComputeShader::SetGlobalBuffer("g_material_buffer", _cur_fs->GetMaterialBuffer());
        ComputeShader::SetGlobalInt("_scene_bindless_idx", scene_data_buffer ? scene_data_buffer->GetBindlessSRVIndex() : RenderConstants::kInvalidBindlessHandle);
        ComputeShader::SetGlobalInt("_tlas_count", s.GetTLASNodeCount());
        ComputeShader::SetGlobalInt("_blas_count", s.GetBLASNodeCount());
        ComputeShader::SetGlobalInt("_inst_count", static_cast<u32>(_primitive_data.size()));
        ComputeShader::SetGlobalInt("_tri_count", s.TriangleCount());
        _rendering_data._p_per_scene_cbuf = _cur_fs->GetSceneCB(s.HashCode());
        auto scene_data = ConstantBuffer::As<CBufferPerSceneData>(_rendering_data._p_per_scene_cbuf);
        scene_data->g_IndirectLightingIntensity = s._light_data._indirect_lighting_intensity;
        scene_data->g_EnvironmentIntensity = _rendering_data._environment != nullptr ? _rendering_data._environment->_intensity : 1.0f;
        scene_data->g_EnvironmentRotation = _rendering_data._environment != nullptr ? _rendering_data._environment->_rotation : 0.0f;
        scene_data->g_EnvironmentEnabled = _rendering_data._environment != nullptr &&
                                            _rendering_data._environment->_enable_ibl &&
                                            (_render_request._feature_flags & kRenderFeatureEnvironmentIbl) != 0u
            ? 1.0f : 0.0f;
        f32 t = TimeMgr::Get().TickTimeSinceLoad, dt = TimeMgr::Get().s_delta_time, sdt = TimeMgr::Get().s_smooth_delta_time;
        scene_data->_Time = Vector4f(t / 20, t, t * 2, (f32) g_pGfxContext->GetFrameCount());
        scene_data->_SinTime = Vector4f(sin(t / 8), sin(t / 4), sin(t / 2), sin(t));
        scene_data->_CosTime = Vector4f(cos(t / 8), cos(t / 4), cos(t / 2), cos(t));
        scene_data->_DeltaTime = Vector4f(dt, 1 / dt, sdt, 1 / sdt);
        scene_data->_FrameIndex = (u32)g_pGfxContext->GetFrameCount();
    }

    void Renderer::PrepareLight(const Scene &s)
    {
        PROFILE_BLOCK_CPU("RenderPrepare.Light")
        uint16_t updated_light_num = 0u;
        uint16_t direction_light_index = 0, point_light_index = 0, spot_light_index = 0, area_light_index = 0;
        auto per_scene_cbuf_data = ConstantBuffer::As<CBufferPerSceneData>(_cur_fs->GetSceneCB(s.HashCode()));
        for (const auto &comp: s.GetRegister().View<ECS::LightComponent>())
        {
            auto &light_data = comp._light;
            Color color = light_data._light_color;
            color.r *= color.a;
            color.g *= color.a;
            color.b *= color.a;
            bool is_cur_light_comp_active = true;
            if (comp._type == ECS::ELightType::kDirectional)
            {
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._shadowmap_index = -1;
                if (!is_cur_light_comp_active)
                {
                    per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightDir = Vector3f::kZero;
                    per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightParam0 = 0.0f;
                    per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightColor = color.xyz * kPi;
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightDir = light_data._light_dir.xyz;
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightParam0 = light_data._light_param.w;
                per_scene_cbuf_data->_MainlightWorldPosition = light_data._light_dir.xyz;
                per_scene_cbuf_data->_MainlightColor = color.xyz * kPi;
                _rendering_data._mainlight_world_position = per_scene_cbuf_data->_MainlightWorldPosition;
                ++direction_light_index;
            }
            else if (comp._type == ECS::ELightType::kPoint)
            {
                per_scene_cbuf_data->_PointLights[point_light_index]._shadowmap_index = -1;
                if (!is_cur_light_comp_active)
                {
                    per_scene_cbuf_data->_PointLights[point_light_index]._LightParam0 = 0.0;
                    per_scene_cbuf_data->_PointLights[point_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                per_scene_cbuf_data->_PointLights[point_light_index]._LightColor = color.xyz * kPi;
                per_scene_cbuf_data->_PointLights[point_light_index]._LightPos = light_data._light_pos.xyz;
                per_scene_cbuf_data->_PointLights[point_light_index]._LightParam0 = light_data._light_param.x;
                per_scene_cbuf_data->_PointLights[point_light_index++]._LightParam1 = light_data._light_param.y;
            }
            else if (comp._type == ECS::ELightType::kSpot)
            {
                per_scene_cbuf_data->_SpotLights[spot_light_index]._shadowmap_index = -1;
                if (!is_cur_light_comp_active)
                {
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                per_scene_cbuf_data->_SpotLights[spot_light_index]._LightColor = color.xyz * kPi;
                per_scene_cbuf_data->_SpotLights[spot_light_index]._LightPos = light_data._light_pos.xyz;
                per_scene_cbuf_data->_SpotLights[spot_light_index]._LightDir = light_data._light_dir.xyz;
                per_scene_cbuf_data->_SpotLights[spot_light_index]._Rdius = light_data._light_param.x;
                float inner_cos = cos(light_data._light_param.y / 2 * k2Radius);
                float outer_cos = cos(light_data._light_param.z / 2 * k2Radius);
                per_scene_cbuf_data->_SpotLights[spot_light_index]._LightAngleScale = 1.0f / std::max(0.001f, (inner_cos - outer_cos));
                per_scene_cbuf_data->_SpotLights[spot_light_index]._LightAngleOffset = -outer_cos * per_scene_cbuf_data->_SpotLights[spot_light_index]._LightAngleScale;
                ++spot_light_index;
            }
            else//area_light
            {
                per_scene_cbuf_data->_AreaLights[area_light_index]._shadowmap_index = -1;
                if (!is_cur_light_comp_active)
                {
                    per_scene_cbuf_data->_AreaLights[area_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                per_scene_cbuf_data->_AreaLights[area_light_index]._LightColor = color.xyz * kPi;
                memset(per_scene_cbuf_data->_AreaLights[area_light_index]._points, 0, sizeof(per_scene_cbuf_data->_AreaLights[area_light_index]._points));
                per_scene_cbuf_data->_AreaLights[area_light_index]._points[0].xyz = light_data._area_points[0];
                per_scene_cbuf_data->_AreaLights[area_light_index]._points[1].xyz = light_data._area_points[1];
                per_scene_cbuf_data->_AreaLights[area_light_index]._points[2].xyz = light_data._area_points[2];
                per_scene_cbuf_data->_AreaLights[area_light_index]._points[3].xyz = light_data._area_points[3];
                per_scene_cbuf_data->_AreaLights[area_light_index]._is_twosided = light_data._is_two_side;
                ++area_light_index;
            }
            ++updated_light_num;
        }
        per_scene_cbuf_data->_ActiveLightCount.x = direction_light_index;
        per_scene_cbuf_data->_ActiveLightCount.y = point_light_index;
        per_scene_cbuf_data->_ActiveLightCount.z = spot_light_index;
        per_scene_cbuf_data->_ActiveLightCount.w = area_light_index;
        if (updated_light_num == 0)
        {
            per_scene_cbuf_data->_DirectionalLights[0]._LightColor = Colors::kBlack.xyz;
            per_scene_cbuf_data->_DirectionalLights[0]._LightDir = {0.0f, 0.0f, 0.0f};
        }
        // 没有方向光的场景（纯自发光 / 纯 GI 测试）也必须给 _MainlightWorldPosition 一个合法朝向：
        // 体积光对它做了 normalize，零向量会产出 NaN，写进整张体积光 3D 贴图后整屏发黑；大气/天空也读同一个量。
        // 这里退化成一盏正上方的虚拟太阳：朝向合法、颜色为黑，不会照亮任何东西。
        if (direction_light_index == 0u)
        {
            per_scene_cbuf_data->_MainlightWorldPosition = Vector3f(0.0f, 1.0f, 0.0f);
            per_scene_cbuf_data->_MainlightColor = Colors::kBlack.xyz;
            _rendering_data._mainlight_world_position = per_scene_cbuf_data->_MainlightWorldPosition;
        }
        for (const auto &vxgi: s.GetRegister().View<ECS::CVXGI>())
        {
            _rendering_data._vxgi_data._center = vxgi._center;
            _rendering_data._vxgi_data._grid_num = vxgi._grid_num;
            _rendering_data._vxgi_data._grid_size = vxgi._grid_size;
            _rendering_data._vxgi_data._size = vxgi._size;
            _rendering_data._is_debug_voxel = vxgi._is_draw_voxel;
            _rendering_data._vxgi_debug_mipmap = vxgi._mipmap;
            dynamic_cast<VoxelGI *>(_vxgi)->_is_draw_debug_voxel = vxgi._is_draw_voxel;
            per_scene_cbuf_data->g_VXGI_Center.xyz = vxgi._center;
            per_scene_cbuf_data->g_VXGI_GridNum.xyz = vxgi._grid_num;
            per_scene_cbuf_data->g_VXGI_GridSize.xyz = vxgi._grid_size;
            per_scene_cbuf_data->g_VXGI_Size.xyz = vxgi._size;
            per_scene_cbuf_data->g_VXGI_Center.w = vxgi._max_distance;
            per_scene_cbuf_data->g_VXGI_GridSize.w = vxgi._min_distance;
            per_scene_cbuf_data->g_VXGI_Size.w = tanf(vxgi._diffuse_cone_angle * k2Radius * 0.5f);
            break;
        }
    }

    void Renderer::PrepareSceneRenderData(const Scene &s)
    {
        PROFILE_BLOCK_CPU("RenderPrepare.Scene")
        _environment_system.Update(s);
        _rendering_data._environment = &_environment_system.Runtime();
        _scene_render_data._scene = &s;
        PrepareMaterial(s);
        BuildScenePrimitives(s);
        PrepareScene(s);
        PrepareLight(s);
        _scene_render_data._primitives = &_scene_primitives;
        _scene_render_data._primitive_data = &_primitive_data;
        _scene_render_data._material_data = &_material_data_cache;
        _scene_render_data_valid = true;

    }

    void Renderer::PrepareViews(const Scene &s, const Camera &cam)
    {
        PROFILE_BLOCK_CPU("RenderPrepare.BuildViews")
        for (auto &view : _view_data)
        {
            if (view._camera == &cam)
            {
                _active_view_data = &view;
                return;
            }
        }
        const u32 first_view_index = static_cast<u32>(_view_data.size());
        ViewRenderData main_view;
        main_view._camera = &cam;
        main_view._view_type = cam._is_scene_camera ? ERenderViewType::kCamera : ERenderViewType::kPreview;
        main_view._frame_index = g_pGfxContext->GetFrameCount();
        main_view._input._frustum = cam.GetViewFrustum();
        main_view._input._camera_position = cam.Position();
        main_view._input._layer_mask = cam._layer_mask;
        main_view._input._is_custom_vp = cam.IsCustomVP();
        _view_data.emplace_back(std::move(main_view));
        _active_view_data = &_view_data.back();
        DispatchViewCulls(s, first_view_index);
    }

    void Renderer::PrepareFrameViews(const Scene &s, const Vector<Camera *> &cameras)
    {
        if (cameras.empty())
            return;
        if (!_scene_render_data_valid)
        {
            _rendering_data._scene = &s;
            _rendering_data._postprocess_settings = ResolvePostprocessSettings(s);
            PrepareSceneRenderData(s);
        }
        if (!_view_data.empty())
            return;

        for (auto *camera : cameras)
        {
            if (camera == nullptr)
                continue;
            ViewRenderData view;
            view._camera = camera;
            view._view_type = camera->_is_scene_camera ? ERenderViewType::kCamera : ERenderViewType::kPreview;
            view._frame_index = g_pGfxContext->GetFrameCount();
            view._input._frustum = camera->GetViewFrustum();
            view._input._camera_position = camera->Position();
            view._input._layer_mask = camera->_layer_mask;
            view._input._is_custom_vp = camera->IsCustomVP();
            _view_data.emplace_back(std::move(view));
        }
        _active_view_data = _view_data.empty() ? nullptr : &_view_data.back();
        DispatchViewCulls(s, 0u);
    }

    void Renderer::DispatchViewCulls(const Scene &s, u32 first_view_index)
    {
        JobGroup cull_group;
        PROFILE_BLOCK_CPU("RenderPrepare.Cull")
        for (u32 view_index = first_view_index; view_index < _view_data.size(); ++view_index)
        {
            JobSystem::Get().Dispatch(cull_group, [this, &s, view_index]()
            {
                auto &view = _view_data[view_index];
                PROFILE_BLOCK_CPU("RenderPrepare.Cull.Main")
                Cull(_scene_render_data, view._input, view._cull_result);
                for (const auto &[queue, objects] : view._cull_result)
                {
                    (void)queue;
                    for (const auto &object : objects)
                    {
                        if ((object._flags & kPrimitiveSkinned) != 0u)
                            view._visible_skinned_primitives.emplace_back(object._primitive_index);
                    }
                }
            });
        }
        JobSystem::Get().Wait(cull_group);

        if (auto *animation_system = const_cast<Scene &>(s).GetRegister().GetSystem<ECS::AnimationSystem>(); animation_system != nullptr)
        {
            PROFILE_BLOCK_CPU("RenderPrepare.Skinning")
            for (auto &view : _view_data)
                animation_system->PrepareVisibleSkinning(view._cull_result);
        }
        RefreshScenePrimitiveVertexBuffers();
    }


    void Renderer::PrepareCamera(const Camera &cam)
    {
        if (cam.ConsumeTemporalDebugFrame())
        {
            const Vector4f jitter = cam._anti_aliasing == EAntiAliasing::kTAA ? cam.GetJitter() : Vector4f::kZero;
            const Matrix4x4f &jittered_vp = cam.GetViewProjJittered();
            const Matrix4x4f &no_jitter_vp = cam.GetViewProjNoJitter();
            const Vector3f probe_point = cam.Position() + cam.Forward() * std::max(cam.Near() * 2.0f, 1.0f);
            const Vector4f no_jitter_clip = TransformVector(no_jitter_vp, Vector4f(probe_point, 1.0f));
            const Vector4f jittered_clip = TransformVector(jittered_vp, Vector4f(probe_point, 1.0f));
            const Vector2f uv_delta(
                jittered_clip.x / jittered_clip.w * 0.5f - no_jitter_clip.x / no_jitter_clip.w * 0.5f,
                no_jitter_clip.y / no_jitter_clip.w * 0.5f - jittered_clip.y / jittered_clip.w * 0.5f);
            LOG_INFO("Camera temporal debug frame={} hash={} aa={} matrix_jitter=({}, {}) uv_jitter=({}, {}) "
                     "projected_uv_delta=({}, {}) vp_offset=({}, {}) vp_no_jitter_offset=({}, {})",
                     g_pGfxContext->GetFrameCount(), cam.HashCode(), static_cast<i32>(cam._anti_aliasing), jitter.x,
                     jitter.y, jitter.z, jitter.w, uv_delta.x, uv_delta.y, jittered_vp[2][0], jittered_vp[2][1],
                     no_jitter_vp[2][0], no_jitter_vp[2][1]);
        }
        //auto cam_hash = cam.HashCode();
        auto cam_cb_data = ConstantBuffer::As<CBufferPerCameraData>(_cur_fs->GetCameraCB(cam.HashCode()));
        *cam_cb_data = {};
        f32 f = cam.Far(), n = cam.Near();
        const Vector2D<u32> render_size = cam.RenderSize();
        u32 pixel_width = render_size.x, pixel_height = render_size.y;
        cam_cb_data->_ScreenParams = Vector4f(1.0f / (f32) pixel_width,1.0f / (f32) pixel_height,(f32)pixel_width,(f32)pixel_height);
        Camera::CalculateZBUfferAndProjParams(cam,cam_cb_data->_ZBufferParams,cam_cb_data->_ProjectionParams);
        cam_cb_data->_CameraPos = cam.Position();
        cam_cb_data->_MatrixV = cam.GetView();
        cam_cb_data->_MatrixP = cam.GetProj();
        cam_cb_data->_MatrixVP = cam.GetViewProjJittered();
        cam_cb_data->_MatrixVP_NoJitter = cam.GetViewProjNoJitter();
        cam_cb_data->_MatrixIVP = MatrixInverse(cam_cb_data->_MatrixVP);
        cam_cb_data->_MatrixVP_Pre = cam.GetPrevViewProj();
        auto& jitter = cam.GetJitter();
        cam_cb_data->_matrix_jitter_x = jitter.x;
        cam_cb_data->_matrix_jitter_y = jitter.y;
        cam_cb_data->_uv_jitter_x = jitter.z;
        cam_cb_data->_uv_jitter_y = jitter.w;
        cam.GetCornerInWorld(cam_cb_data->_LT, cam_cb_data->_LB, cam_cb_data->_RT, cam_cb_data->_RB);
        cam_cb_data->_RenderFeatureFlags = _render_request._feature_flags;
        ApplyShadowSetToCameraData(*cam_cb_data, _rendering_data._shadow_set);
        _rendering_data._cull_results = _active_view_data != nullptr ? &_active_view_data->_cull_result : nullptr;
        _rendering_data._camera = &cam;
        _rendering_data._p_per_camera_cbuf = _cur_fs->GetCameraCB(cam.HashCode());
    }

    void Renderer::ApplyShadowSetToCameraData(CBufferPerCameraData &camera_data, const ShadowSet *shadow_set)
    {
        if (shadow_set == nullptr || !shadow_set->_valid || shadow_set->_directional._view_count == 0u)
            return;
        const u16 cascade_count = std::min<u16>(shadow_set->_directional._view_count,
                                                RenderConstants::kMaxCascadeShadowMapSplitNum);
        const u16 view_begin = shadow_set->_directional._view_begin;
        for (u16 cascade_index = 0u; cascade_index < cascade_count; ++cascade_index)
        {
            const u32 view_index = static_cast<u32>(view_begin) + cascade_index;
            if (view_index >= shadow_set->_views.size())
                break;
            const ShadowView &view = shadow_set->_views[view_index];
            camera_data._DirectionalShadowMatrix[cascade_index] = view._shadow_matrix;
            camera_data._DirectionalShadowSplit[cascade_index] = shadow_set->_directional._splits[cascade_index];
        }
        const f32 shadow_distance = std::max(shadow_set->_directional._shadow_distance, 0.1f);
        camera_data._DirectionalShadowParams = Vector4f(static_cast<f32>(cascade_count),
                                                        1.0f / QuailtySetting::s_shadow_fade_out_factor,
                                                        1.0f / shadow_distance, 1.0f);
        const u32 shadow_resolution = shadow_set->_directional._mode == EDirectionalShadowMode::kSingleOrthographic
            ? QuailtySetting::s_probe_directional_shadow_map_resolution
            : QuailtySetting::s_cascade_shaodw_map_resolution;
        const f32 texel_size = 1.0f / static_cast<f32>(std::max(shadow_resolution, 1u));
        camera_data._DirectionalShadowMapParams = Vector4f(texel_size, texel_size, 1.25f, 0.0f);
    }

    static void FillMaterialData(Material* src, MaterialData& dst)
    {
        dst._base_color_tex         = RenderConstants::kInvalidBindlessHandle;
        dst._normal_tex             = RenderConstants::kInvalidBindlessHandle;
        dst._roughness_tex = RenderConstants::kInvalidBindlessHandle;
        dst._metallic_tex = RenderConstants::kInvalidBindlessHandle;
        dst._emission_tex           = RenderConstants::kInvalidBindlessHandle;
        dst._emission_strength      = 0.0f;
        dst._sigma_a                = Vector3f::kZero;
        if (src != nullptr && src->IsStandardLit())
        {
            // Mirror the raster standard lit path: only the checker id is forwarded, everything else is standard.
            dst._material_id = src->MaterialID() == EMaterialID::kChecker ? AL_MATERIAL_ID_CHECKER : AL_MATERIAL_ID_STANDARD;
            const auto *albedo_prop = src->MainProperty(ETextureUsage::kAlbedo);
            if (albedo_prop != nullptr)
                dst._base_color = albedo_prop->GetValue<Vector4f>().xyz;
            if (auto base_color_tex = src->MainTex(ETextureUsage::kAlbedo); base_color_tex)
            {
                dst._base_color_tex = base_color_tex->GetBindlessSRVIndex();
            }
            const auto *metallic_prop = src->MainProperty(ETextureUsage::kMetallic);
            if (metallic_prop != nullptr)
                dst._metallic = metallic_prop->GetValue<f32>();
            const auto *roughness_prop = src->MainProperty(ETextureUsage::kRoughness);
            if (roughness_prop != nullptr)
                dst._roughness = std::max(0.0f, roughness_prop->GetValue<f32>());
            if (auto normal_tex = src->MainTex(ETextureUsage::kNormal); normal_tex)
            {
                dst._normal_tex = normal_tex->GetBindlessSRVIndex();
            }
            if (auto roughness_tex = src->MainTex(ETextureUsage::kRoughness); roughness_tex)
                dst._roughness_tex = roughness_tex->GetBindlessSRVIndex();
            if (auto metallic_tex = src->MainTex(ETextureUsage::kMetallic); metallic_tex)
                dst._metallic_tex = metallic_tex->GetBindlessSRVIndex();
            const auto *emission_prop = src->MainProperty(ETextureUsage::kEmission);
            if (emission_prop != nullptr)
            {
                dst._emission = emission_prop->GetValue<Color>().xyz;
                dst._emission_strength = 1.0f;
            }
            if (auto emission_tex = src->MainTex(ETextureUsage::kEmission); emission_tex)
            {
                dst._emission_tex = emission_tex->GetBindlessSRVIndex();
                dst._emission_strength = 1.0f;
            }
            dst._ior = src->GetFloat("_IOR");
            dst._transmission = src->GetFloat("_Transmission");
            dst._sigma_a = src->GetPathTracerSigmaA();
        }
        //dst._base_color        = Vector3f::kOne;
        //dst._metallic          = src.metallic;
        //dst._roughness         = src.roughness;
        //dst._specular          = src.specular;
        //dst._ior               = src.ior;
        //dst._opacity           = src.opacity;

        // dst._emission          = src.emission_color;
        // dst._emission_strength = src.emission_intensity;

        // dst._base_color_tex    = src.base_color_tex ? src.base_color_tex->BindlessIndex() : kInvalidTex;
        // dst._normal_tex        = src.normal_tex     ? src.normal_tex->BindlessIndex()     : kInvalidTex;
        // dst._emission_tex      = src.emission_tex ? src.emission_tex->BindlessIndex() : kInvalidTex;

        // dst._flags = 0;
        // if (src.is_metallic)   dst._flags |= kMaterial_Metallic;
        // if (src.is_emissive)   dst._flags |= kMaterial_Emissive;
        // if (src.is_thin)       dst._flags |= kMaterial_Thin;
    }


    void Renderer::PrepareMaterial(const Scene &s)
    {
        PROFILE_BLOCK_CPU("RenderPrepare.Material")

        auto collect_materials = [this](auto &renderables)
        {
            for (auto &renderable : renderables)
            {
                for (auto &material : renderable._p_mats)
                {
                    if (material == nullptr)
                        continue;
                    if (!_material_data_lut.contains(material->HashCode()))
                    {
                        const u32 material_index = static_cast<u32>(_material_data_cache.size());
                        _material_data_lut[material->HashCode()] = material_index;
                        _material_data_cache.push_back(MaterialData());
                    }
                    FillMaterialData(material.get(), _material_data_cache[_material_data_lut[material->HashCode()]]);
                }
            }
        };
        collect_materials(s.GetRegister().View<ECS::StaticMeshComponent>());
        collect_materials(s.GetRegister().View<ECS::CSkeletonMesh>());
        _cur_fs->GetMaterialBuffer()->SetData(
            (u8*)_material_data_cache.data(),
            (u32)(_material_data_cache.size() * sizeof(MaterialData)));
    }

    RenderTexture *Renderer::TargetTexture()
    {
        return _target_tex;
    }
    void Renderer::DoRender(const Camera &cam, const Scene &s, const RenderRequest &request,
                            const ShadowSet *shadow_set, bool render_shadow_maps,
                            RenderTexture *output_target, i32 output_view_index)
    {
        _render_request = request;
        _rendering_data._shadow_set = shadow_set;
        _rendering_data._render_shadow_maps = render_shadow_maps;
        {
            PROFILE_BLOCK_CPU("BeginScene")
            BeginScene(cam, s);
        }
        if (_is_use_render_graph)
        {
            RenderTexture *final_target = output_target != nullptr ? output_target : _presentation_texture.get();
            if (final_target != nullptr)
            {
                const RDG::RGHandle source_handle = _rendering_data.FinalColorHandle();
                const RDG::RGHandle target_handle = _rd_graph->Import(final_target);
                RDG::RGHandle output_handle;
                const String final_blit_name = output_view_index >= 0
                    ? std::format("CubemapFinalBlit_Face{}", output_view_index % 10000 + 1)
                    : "FinalBlit";
                _rd_graph->AddPass(final_blit_name, RDG::PassDesc(), [source_handle, target_handle, &output_handle](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(source_handle);
                    output_handle = builder.Write(target_handle);
                }, [source_handle, target_handle, output_view_index,
                 point_sampling = cam.UpsamplingFilter() == EUpsamplingFilter::kPoint](RDG::RenderGraph &graph, CommandBuffer *cmd,
                                                                       const RenderingData &)
                {
                    auto *src = graph.Resolve<Texture>(source_handle);
                    auto *dst = graph.Resolve<RenderTexture>(target_handle);
                    if (src == nullptr || dst == nullptr)
                        return;
                    auto blit_material = ResourceMgr::Get().Get<Material>(L"Runtime/Material/Blit");
                    const u16 pass_index = point_sampling ? 1u : 0u;
                    if (output_view_index >= 0)
                        cmd->Blit(src, dst, 0, static_cast<u16>(output_view_index), blit_material, pass_index);
                    else
                        cmd->Blit(src, dst, blit_material, pass_index);
                });
                const RDG::RGHandle presentation_read_handle = output_handle;
                _rd_graph->AddPass("PreparePresentation", RDG::PassDesc(), [presentation_read_handle](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(presentation_read_handle, EResourceUsage::kReadSRV);
                }, [](RDG::RenderGraph &, CommandBuffer *, const RenderingData &) {});
                _target_tex = final_target;
            }
            _rd_graph->Execute(*_p_context,_rendering_data);
        }
        else
        {
            _environment_system.Execute(_p_context, _rendering_data);
            if (cam._layer_mask & static_cast<u32>(ERenderLayer::kDefault))
            {
                for (auto &pass: _render_passes)
                {
                    if (pass->IsActive())
                    {
                        PROFILE_BLOCK_CPU(pass->GetName())
                        pass->BeginPass(_p_context);
                        pass->Execute(_p_context, _rendering_data);
                        pass->EndPass(_p_context);
                    }
                }
            }
            else if (cam._layer_mask & static_cast<u32>(ERenderLayer::kSkyBox))
            {
                PROFILE_BLOCK_CPU(_sky_pass->GetName())
                _sky_pass->BeginPass(_p_context);
                _sky_pass->Execute(_p_context, _rendering_data);
                _sky_pass->EndPass(_p_context);
            }
        }
        EndScene(s);
    }

    void Renderer::CullShadowSet(const Scene &s, ShadowSet &shadow_set)
    {
        JobGroup cull_group;
        for (u32 view_index = 0u; view_index < shadow_set._views.size(); ++view_index)
        {
            JobSystem::Get().Dispatch(cull_group, [this, &shadow_set, view_index]()
            {
                ShadowView &view = shadow_set._views[view_index];
                ViewCullInput input;
                input._frustum = view._camera.GetViewFrustum();
                input._camera_position = view._camera.Position();
                input._layer_mask = view._camera._layer_mask;
                input._is_custom_vp = view._camera.IsCustomVP();
                Cull(_scene_render_data, input, view._cull_result);
            });
        }
        JobSystem::Get().Wait(cull_group);
        RefreshScenePrimitiveVertexBuffers();
        (void)s;
    }

    void Renderer::ApplyShadowSetToLightData(const Scene &s, const ShadowSet *shadow_set)
    {
        auto *scene_cb_data = ConstantBuffer::As<CBufferPerSceneData>(_cur_fs->GetSceneCB(s.HashCode()));
        for (u16 i = 0u; i < RenderConstants::kMaxDirectionalLightNum; ++i)
            scene_cb_data->_DirectionalLights[i]._shadowmap_index = -1;
        for (u16 i = 0u; i < RenderConstants::kMaxPointLightNum; ++i)
            scene_cb_data->_PointLights[i]._shadowmap_index = -1;
        for (u16 i = 0u; i < RenderConstants::kMaxSpotLightNum; ++i)
            scene_cb_data->_SpotLights[i]._shadowmap_index = -1;
        for (u16 i = 0u; i < RenderConstants::kMaxAreaLightNum; ++i)
            scene_cb_data->_AreaLights[i]._shadowmap_index = -1;
        if (shadow_set == nullptr || !shadow_set->_valid)
            return;

        if (shadow_set->_directional._view_count > 0u)
        {
            scene_cb_data->_DirectionalLights[0]._shadowmap_index = 0;
            scene_cb_data->_DirectionalLights[0]._ShadowDistance = shadow_set->_directional._shadow_distance;
        }
        for (const ShadowView &view : shadow_set->_views)
        {
            switch (view._type)
            {
            case EShadowViewType::kDirectional:
                if (view._light_index < RenderConstants::kMaxDirectionalLightNum)
                {
                    auto &light = scene_cb_data->_DirectionalLights[view._light_index];
                    light._constant_bias = view._constant_bias;
                    light._slope_bias = view._slope_bias;
                }
                break;
            case EShadowViewType::kSpot:
                if (view._light_index < RenderConstants::kMaxSpotLightNum)
                {
                    auto &light = scene_cb_data->_SpotLights[view._light_index];
                    light._shadowmap_index = view._target_slice;
                    light._ShadowDistance = view._camera_far;
                    light._shadow_matrix = view._shadow_matrix;
                    light._constant_bias = view._constant_bias;
                    light._slope_bias = view._slope_bias;
                }
                break;
            case EShadowViewType::kArea:
                if (view._light_index < RenderConstants::kMaxAreaLightNum)
                {
                    auto &light = scene_cb_data->_AreaLights[view._light_index];
                    light._shadowmap_index = view._target_slice;
                    light._ShadowDistance = view._camera_far;
                    light._shadow_matrix = view._shadow_matrix;
                    light._constant_bias = view._constant_bias;
                    light._slope_bias = view._slope_bias;
                }
                break;
            case EShadowViewType::kPoint:
                if (view._light_index < RenderConstants::kMaxPointLightNum)
                {
                    auto &light = scene_cb_data->_PointLights[view._light_index];
                    light._shadowmap_index = view._shadow_map_index;
                    light._ShadowDistance = view._camera_far;
                    light._constant_bias = view._constant_bias;
                    light._slope_bias = view._slope_bias;
                }
                break;
            default:
                break;
            }
        }
    }

    void Renderer::Cull(const SceneRenderData &scene_data, const ViewCullInput &input, CullResult &output)
    {
        output.clear();
        if (scene_data._primitives == nullptr)
            return;
        const auto &vf = input._frustum;
        for (const ScenePrimitive &primitive : *scene_data._primitives)
        {
            if ((!input._is_custom_vp && !ViewFrustum::Conatin(vf, primitive._world_bounds)) || primitive._material == nullptr)
                continue;
            const u32 queue_id = primitive._material->RenderQueue();
            output[queue_id].emplace_back(RenderableObjectData{primitive._primitive_index,
                Distance(primitive._world_bounds.Center(), input._camera_position), primitive._submesh_index, primitive._mesh,
                primitive._material, primitive._vertex_buffer, primitive._index_buffer, primitive._geometry_id,
                primitive._flags, primitive._entity_id});
        }
        for (auto &it: output)
        {
            auto &objs = it.second;
            const bool is_transparent = it.first >= Shader::kRenderQueueTransparent;
            std::sort(objs.begin(), objs.end(), [is_transparent](const RenderableObjectData &a, const RenderableObjectData &b)
            {
                return is_transparent ? a._distance_to_cam > b._distance_to_cam : a._distance_to_cam < b._distance_to_cam;
            });
        }
    }
    void Renderer::StableSort(Vector<RenderPass *> list)
    {
        for (int i = 0; i < list.size() - 1; i++)
        {
            for (int j = 0; j < list.size() - 1 - i; j++)
            {
                if (list[j] > list[j + 1])
                {
                    RenderPass *temp = list[j];
                    list[j] = list[j + 1];
                    list[j + 1] = temp;
                }
            }
        }
    }
}// namespace Ailu
