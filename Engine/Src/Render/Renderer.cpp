#include "Render/Renderer.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Application.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/Features/PostprocessPass.h"
#include "Render/Features/SSAO.h"
#include "Render/Features/TemporalAA.h"
#include "Render/Features/VolumetricClouds.h"
#include "Render/Features/VoxelGI.h"
#include "Render/Features/GpuTerrain.h"
#include "Render/Features/RayTraceGI.h"
#include "Render/RenderPipeline.h"
#include "Render/RenderingData.h"

#include "Render/RenderGraph/RenderGraph.h"

#include "pch.h"



namespace Ailu::Render
{
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
        _shadowcast_pass = MakeScope<ShadowCastPass>();
        _gbuffer_pass = MakeScope<DeferredGeometryPass>();
        _coptdepth_pass = MakeScope<CopyDepthPass>();
        _lighting_pass = MakeScope<DeferredLightingPass>();
        _skybox_pass = MakeScope<SkyboxPass>();
        _motion_vector_pass = MakeScope<MotionVectorPass>();
        _forward_pass = MakeScope<ForwardPass>();
        _copycolor_pass = MakeScope<CopyColorPass>();
        _postprocess_pass = MakeScope<PostProcessPass>();
        _gizmo_pass = MakeScope<GizmoPass>();
        _wireframe_pass = MakeScope<WireFramePass>();
        _gui_pass = MakeScope<GUIPass>();
        _hzb_pass = MakeScope<HZBPass>();
        _owned_features.push_back(std::move(std::unique_ptr<RenderFeature>(new TemporalAA())));
        _taa = _owned_features.back().get();
        _owned_features.push_back(std::move(std::unique_ptr<RenderFeature>(new VoxelGI())));
        _vxgi = _owned_features.back().get();
        _owned_features.push_back(std::move(std::unique_ptr<RenderFeature>(new VolumetricClouds())));
        _cloud = _owned_features.back().get();
        _owned_features.push_back(std::move(std::unique_ptr<RenderFeature>(new SSAO())));
        _ssao = _owned_features.back().get();
        _owned_features.push_back(std::move(std::unique_ptr<RenderFeature>(new GpuTerrain())));
        _gpu_terrain = _owned_features.back().get();
        _owned_features.push_back(std::move(std::unique_ptr<RenderFeature>(new RayTraceGI())));
        _raytrace_gi = _owned_features.back().get();
        //_features.push_back(_vxgi);
        //_features.push_back(_cloud);
        //_features.push_back(_taa);
        _features.push_back(_ssao);
        _features.push_back(_raytrace_gi);
        //_features.push_back(_gpu_terrain);
        //_features.push_back(_taa);

    }

    Renderer::~Renderer()
    {
        _owned_features.clear();
        AL_DELETE(_rd_graph);
        Profiler::Shutdown();
    }

    void Renderer::Render(const Camera &cam, const Scene &s)
    {
        if (_cur_fs == nullptr)
            return;
        bool is_output_to_camera_tex = cam.TargetTexture() != nullptr;
        for (auto &e: _events_before_tick)
        {
            e();
        }
        if (is_output_to_camera_tex)
        {
            if (cam.TargetTexture()->Dimension() == ETextureDimension::kTex2D)
            {
                DoRender(cam, s);
                auto cmd = CommandBufferPool::Get("FinalBlit");
                {
                    GpuProfileBlock b(cmd.get(), cmd->Name());
                    cmd->Blit(_rendering_data._camera_color_target_handle, cam.TargetTexture());
                }
                _p_context->ExecuteCommandBuffer(cmd);
                CommandBufferPool::Release(cmd);
            }
            else if (cam.TargetTexture()->Dimension() == ETextureDimension::kCube)
            {
                for (u16 i = 1; i <= 6; i++)
                {
                    auto face = (ECubemapFace::ECubemapFace) i;
                    Camera &tmp_cam = Camera::GetCubemapGenCamera(cam, face);
                    tmp_cam._is_scene_camera = false;
                    tmp_cam._is_render_shadow = false;
                    tmp_cam.OutputSize(cam.TargetTexture()->Width(), cam.TargetTexture()->Height());
                    Cull(s, tmp_cam);
                    DoRender(tmp_cam, s);
                    auto cmd = CommandBufferPool::Get("FinalBlit");
                    {
                        GpuProfileBlock b(cmd.get(), cmd->Name());
                        cmd->Blit(g_pRenderTexturePool->Get(_rendering_data._camera_color_target_handle), cam.TargetTexture(), 0,
                                  cam.TargetTexture()->CalculateViewIndex(Texture::ETextureViewType::kRTV, face, 0, 0), nullptr);
                    }
                    _p_context->ExecuteCommandBuffer(cmd);
                    CommandBufferPool::Release(cmd);
                    //CommandBufferPool::WaitForAllCommand();
                }
            }
        }
        else
        {
            DoRender(cam, s);
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
        ConstantBuffer *scene_cb = _cur_fs->GetSceneCB(s.HashCode());
        ConstantBuffer *cam_cb = _cur_fs->GetCameraCB(_active_camera_hash);
        //cam_cb->Reset();
        //scene_cb->Reset();
        auto scene_cb_data = ConstantBuffer::As<CBufferPerSceneData>(scene_cb);
        for (u16 i = 0; i < RenderConstants::kMaxCascadeShadowMapSplitNum; i++)
        {
            _rendering_data._cascade_shadow_data[i]._shadowmap_index = -1;
        }
        for (u16 i = 0; i < RenderConstants::kMaxSpotLightNum; i++)
        {
            scene_cb_data->_SpotLights[i]._shadowmap_index = -1;
            _rendering_data._spot_shadow_data[i]._shadowmap_index = -1;
        }
        for (u16 i = 0; i < RenderConstants::kMaxPointLightNum; i++)
        {
            scene_cb_data->_PointLights[i]._shadowmap_index = -1;
            _rendering_data._point_shadow_data[i]._shadowmap_index = -1;
        }
        for (u16 i = 0; i < RenderConstants::kMaxAreaLightNum; i++)
        {
            scene_cb_data->_AreaLights[i]._shadowmap_index = -1;
            _rendering_data._area_shadow_data[i]._shadowmap_index = -1;
        }
        u32 pixel_width = cam.OutputSize().x, pixel_height = cam.OutputSize().y;
        _rendering_data._width = pixel_width;
        _rendering_data._height = pixel_height;
        if (_is_use_render_graph)
        {
            _rendering_data._rg_handles._gbuffers[0] = _rd_graph->CreateResource(TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kRGHalf),RenderResourceName::kGBuffer0);
            _rendering_data._rg_handles._gbuffers[1] = _rd_graph->CreateResource(TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDefault), RenderResourceName::kGBuffer1);
            _rendering_data._rg_handles._gbuffers[2] = _rd_graph->CreateResource(TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDefault), RenderResourceName::kGBuffer2);
            _rendering_data._rg_handles._gbuffers[3] = _rd_graph->CreateResource(TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kRGBAHalf), RenderResourceName::kGBuffer3);
            auto cam_color_desc = _rendering_data._camera_data._camera_color_target_desc = TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDefaultHDR);
            _rendering_data._rg_handles._color_target = _rd_graph->CreateResource(cam_color_desc, RenderResourceName::kCameraColorA);
            auto cam_depth_desc = _rendering_data._camera_data._camera_depth_target_desc = TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDepth);
            _rendering_data._rg_handles._depth_target = _rd_graph->CreateResource(cam_depth_desc, RenderResourceName::kCameraDepth);
            TextureDesc desc = TextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDefaultHDR);
            desc._load = ELoadStoreAction::kNotCare;
            _rendering_data._rg_handles._color_tex = _rd_graph->CreateResource(desc, RenderResourceName::kCameraColorTex);
            desc._format = ConvertRenderTextureFormatToPixelFormat(ERenderTargetFormat::kRFloat);
            _rendering_data._rg_handles._depth_tex = _rd_graph->CreateResource(desc, RenderResourceName::kCameraDepthTex);

            auto shadow_map_size = QuailtySetting::s_cascade_shaodw_map_resolution;
            TextureDesc sm_desc = TextureDesc(shadow_map_size, shadow_map_size, ERenderTargetFormat::kShadowMap);
            sm_desc._array_size = RenderConstants::kMaxCascadeShadowMapSplitNum;
            sm_desc._store = ELoadStoreAction::kClear;
            sm_desc._dimension = ETextureDimension::kTex2DArray;
            _rendering_data._rg_handles._main_light_shadow_map = _rd_graph->CreateResource(sm_desc, RenderResourceName::kMainLightShadowMap);
            sm_desc._width = shadow_map_size >> 1;
            sm_desc._height = shadow_map_size >> 1;
            sm_desc._array_size = RenderConstants::kMaxSpotLightNum + RenderConstants::kMaxAreaLightNum;
            _rendering_data._rg_handles._addi_shadow_maps = _rd_graph->CreateResource(sm_desc, RenderResourceName::kAddLightShadowMap);
            sm_desc._array_size = RenderConstants::kMaxPointLightNum;
            sm_desc._dimension = ETextureDimension::kCubeArray;
            _rendering_data._rg_handles._point_light_shadow_maps = _rd_graph->CreateResource(sm_desc, RenderResourceName::kPointLightShadowMap);

            auto mv_desc = TextureDesc(_rendering_data._width, _rendering_data._height, ERenderTargetFormat::kRGHalf);
            _rendering_data._rg_handles._motion_vector_tex = _rd_graph->CreateResource(mv_desc, RenderResourceName::kMotionVectorTex);
            mv_desc = TextureDesc(_rendering_data._width, _rendering_data._height, ERenderTargetFormat::kDepth);
            _rendering_data._rg_handles._motion_vector_depth = _rd_graph->CreateResource(mv_desc, RenderResourceName::kMotionVectorDepth);
            auto hzb_desc = TextureDesc((_rendering_data._width + 1) >> 1, (_rendering_data._height + 1) >> 1, ERenderTargetFormat::kRFloat);
            hzb_desc._is_random_access = true;
            hzb_desc._mip_num = Texture::MaxMipmapCount(hzb_desc._width, hzb_desc._height);
            _rendering_data._rg_handles._hzb = _rd_graph->CreateResource(hzb_desc, RenderResourceName::kHZB);
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
        
        Cull(*g_pSceneMgr->ActiveScene(),cam);
        PrepareCamera(cam);
        PrepareScene(*g_pSceneMgr->ActiveScene());
        PrepareLight(*g_pSceneMgr->ActiveScene());
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
        _vxgi->SetActive(cam._is_gen_voxel || _vxgi->IsActive());
        if (_mode & EShadingMode::kLit)
        {
            _skybox_pass->Setup(false);
            if (cam._is_render_shadow)
                _render_passes.emplace_back(_shadowcast_pass.get());
            if (!_rendering_data._is_debug_voxel)
            {
                _render_passes.emplace_back(_gbuffer_pass.get());
                _render_passes.emplace_back(_lighting_pass.get());
                _render_passes.emplace_back(_motion_vector_pass.get());
                _render_passes.emplace_back(_forward_pass.get());
                _render_passes.emplace_back(_coptdepth_pass.get());
                _render_passes.emplace_back(_copycolor_pass.get());
                if (cam._is_enable_postprocess)
                    _render_passes.emplace_back(_postprocess_pass.get());
            }
            else
                _skybox_pass->Setup(true);
        }
        if (_is_hiz_active)
            _render_passes.emplace_back(_hzb_pass.get());
        _render_passes.emplace_back(_gui_pass.get());
        for (auto &feature: _features)
        {
            if (feature->IsActive())
                feature->AddRenderPasses(*this, _rendering_data);
        }
        if (_mode & EShadingMode::kWireframe)
        {
            _skybox_pass->Setup(!(_mode & EShadingMode::kLit));
            _render_passes.emplace_back(_wireframe_pass.get());
        }
        if (cam._is_scene_camera)
            _render_passes.emplace_back(_gizmo_pass.get());
        if (cam._is_render_sky_box)
            _render_passes.emplace_back(_skybox_pass.get());
        std::stable_sort(_render_passes.begin(), _render_passes.end(), [](RenderPass *a, RenderPass *b) -> bool
                         { return *a < *b; });
        _target_tex = g_pRenderTexturePool->Get(_rendering_data._camera_color_target_handle);
        Shader::SetGlobalBuffer(RenderConstants::kCBufNamePerScene, _cur_fs->GetSceneCB(s.HashCode()));
        Shader::SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _cur_fs->GetCameraCB(cam.HashCode()));
        Shader::SetGlobalTexture("_LTC_Lut1", g_pResourceMgr->Get<Texture2D>(L"Runtime/ltc_lut1"));
        Shader::SetGlobalTexture("_LTC_Lut2", g_pResourceMgr->Get<Texture2D>(L"Runtime/ltc_lut2"));
        ComputeShader::SetGlobalBuffer(RenderConstants::kCBufNamePerScene, _cur_fs->GetSceneCB(s.HashCode()));
        ComputeShader::SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _cur_fs->GetCameraCB(cam.HashCode()));
        {
            PROFILE_BLOCK_CPU(WaitForSys)
            for (auto &sys: s.GetRegister().SystemView())
                sys.second->WaitFor();
        }
        //RENDER GRAPH
        if (_is_use_render_graph)
        {
            _rendering_data._postprocess_input = nullptr;//taa关闭时，这个不赋值会导致bloom输入为空
            {
                for (auto *pass: _render_passes)
                    pass->OnRecordRenderGraph(*_rd_graph, _rendering_data);
                _rd_graph->Compile();
            }
        }
    }
    void Renderer::EndScene(const Scene &s)
    {
        u16 obj_index = 0;
        for (auto &static_mesh: s.GetRegister().View<ECS::StaticMeshComponent>())
        {
            if (static_mesh._p_mesh)
            {
                auto &aabbs = static_mesh._transformed_aabbs;
                Vector3f center;
                u16 submesh_count = static_mesh._p_mesh->SubmeshCount();
                auto &materials = static_mesh._p_mats;
                auto *obj_cb = ConstantBuffer::As<CBufferPerObjectData>(_cur_fs->GetObjCB(obj_index));
                for (int i = 0; i < submesh_count; i++)
                {
                    obj_cb->_MatrixWorld_Pre = obj_cb->_MatrixWorld;
                    ++obj_index;
                }
            }
        }
        _render_passes.clear();
        _rendering_data.Reset();
        //RENDER GRAPH
        if (_is_use_render_graph)
        {
            _target_tex = static_cast<RenderTexture *>(_rd_graph->Export(_rendering_data._rg_handles._color_target));
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

    void Renderer::PrepareScene(const Scene &s)
    {
        u16 obj_index = 0;
        u64 entity_index = 0;
        auto &r = s.GetRegister();
        for (auto &static_mesh: s.GetRegister().View<ECS::StaticMeshComponent>())
        {
            if (static_mesh._p_mesh)
            {
                auto &aabbs = static_mesh._transformed_aabbs;
                Vector3f center;
                u16 submesh_count = static_mesh._p_mesh->SubmeshCount();
                auto &materials = static_mesh._p_mats;
                for (int i = 0; i < submesh_count; i++)
                {
                    auto *obj_cb = ConstantBuffer::As<CBufferPerObjectData>(_cur_fs->GetObjCB(obj_index));
                    const auto &t = r.GetComponent<ECS::StaticMeshComponent, ECS::TransformComponent>(entity_index)->_transform;
                    obj_cb->_MatrixWorld = t._world_matrix;
                    obj_cb->_MatrixInvWorld = MatrixInverse(t._world_matrix);
                    obj_cb->_ObjectID = (i32)r.GetEntity<ECS::StaticMeshComponent>(entity_index);
                    obj_cb->_MotionVectorParam.x = static_mesh._motion_vector_type == ECS::EMotionVectorType::kPerObject? 1.0f : 0.0f; //dynamic object
                    obj_cb->_MotionVectorParam.y = static_mesh._motion_vector_type == ECS::EMotionVectorType::kForceZero? 1.0f : 0.0f; //force off
                    ++obj_index;
                }
            }
            ++entity_index;
        }
        entity_index = 0u;
        for (auto &static_mesh: s.GetRegister().View<ECS::CSkeletonMesh>())
        {
            if (static_mesh._p_mesh)
            {
                auto &aabbs = static_mesh._transformed_aabbs;
                Vector3f center;
                u16 submesh_count = static_mesh._p_mesh->SubmeshCount();
                auto &materials = static_mesh._p_mats;
                for (int i = 0; i < submesh_count; i++)
                {
                    const auto &t = r.GetComponent<ECS::CSkeletonMesh, ECS::TransformComponent>(entity_index)->_transform;
                    auto *obj_cb = ConstantBuffer::As<CBufferPerObjectData>(_cur_fs->GetObjCB(obj_index));
                    obj_cb->_MatrixWorld = t._world_matrix;
                    obj_cb->_ObjectID = (i32)r.GetEntity<ECS::CSkeletonMesh>(entity_index);
                    ++obj_index;
                }
            }
            ++entity_index;
        };
        _rendering_data._p_per_object_cbuf = _cur_fs->GetObjCB();
        _rendering_data._p_per_scene_cbuf = _cur_fs->GetSceneCB(s.HashCode());
        auto scene_data = ConstantBuffer::As<CBufferPerSceneData>(_rendering_data._p_per_scene_cbuf);
        f32 cascade_shaodw_max_dis = (f32) QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[QuailtySetting::s_cascade_shadow_map_count - 1];
        scene_data->_CascadeShadowParams = Vector4f((f32) QuailtySetting::s_cascade_shadow_map_count,
                                                    1.0f / QuailtySetting::s_shadow_fade_out_factor, 1.0f / cascade_shaodw_max_dis, 0.f);
        scene_data->g_IndirectLightingIntensity = s._light_data._indirect_lighting_intensity;
        PrepareLight(s);
        f32 t = g_pTimeMgr->TickTimeSinceLoad, dt = g_pTimeMgr->s_delta_time, sdt = g_pTimeMgr->s_smooth_delta_time;
        scene_data->_Time = Vector4f(t / 20, t, t * 2, (f32) g_pGfxContext->GetFrameCount());
        scene_data->_SinTime = Vector4f(sin(t / 8), sin(t / 4), sin(t / 2), sin(t));
        scene_data->_CosTime = Vector4f(cos(t / 8), cos(t / 4), cos(t / 2), cos(t));
        scene_data->_DeltaTime = Vector4f(dt, 1 / dt, sdt, 1 / sdt);
        scene_data->_FrameIndex = (u32)g_pGfxContext->GetFrameCount();
    }

    void Renderer::PrepareLight(const Scene &s)
    {
        const static f32 s_shadow_bias_factor = 0.01f;
        uint16_t updated_light_num = 0u;
        uint16_t direction_light_index = 0, point_light_index = 0, spot_light_index = 0, area_light_index = 0;
        u16 addi_shadow_map_index = 0u;
        auto per_scene_cbuf_data = ConstantBuffer::As<CBufferPerSceneData>(_cur_fs->GetSceneCB(s.HashCode()));
        for (const auto &comp: s.GetRegister().View<ECS::LightComponent>())
        {
            auto &light_data = comp._light;
            Color color = light_data._light_color;
            color.r *= color.a;
            color.g *= color.a;
            color.b *= color.a;
            bool is_exist_directional_shaodw = false;
            bool is_cur_light_comp_active = true;
            if (comp._type == ECS::ELightType::kDirectional)
            {
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._shadowmap_index = -1;
                if (!is_cur_light_comp_active)
                {
                    per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightDir = Vector3f::kZero;
                    per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                if (comp._shadow._is_cast_shadow && !is_exist_directional_shaodw)
                {
                    for (int cascade_index = 0; cascade_index < QuailtySetting::s_cascade_shadow_map_count; cascade_index++)
                    {
                        auto &shadow_cam = comp._shadow_cameras[cascade_index];
                        Cull(*g_pSceneMgr->ActiveScene(), shadow_cam);
                        per_scene_cbuf_data->_DirectionalLights[direction_light_index]._shadowmap_index = 0;
                        per_scene_cbuf_data->_DirectionalLights[direction_light_index]._ShadowDistance = QuailtySetting::s_main_light_shaodw_distance;
                        per_scene_cbuf_data->_CascadeShadowMatrix[cascade_index] = shadow_cam.GetView() * shadow_cam.GetProj();
                        per_scene_cbuf_data->_CascadeShadowSplit[cascade_index] = comp._cascade_shadow_data[cascade_index];
                        per_scene_cbuf_data->_DirectionalLights[direction_light_index]._constant_bias = comp._shadow._constant_bias * comp._shadow._constant_bias * s_shadow_bias_factor;
                        per_scene_cbuf_data->_DirectionalLights[direction_light_index]._slope_bias = comp._shadow._slope_bias * comp._shadow._slope_bias * s_shadow_bias_factor;
                        _rendering_data._cascade_shadow_data[cascade_index]._shadowmap_index = 0;
                        _rendering_data._cascade_shadow_data[cascade_index]._shadow_matrix = per_scene_cbuf_data->_CascadeShadowMatrix[cascade_index];
                        _rendering_data._cascade_shadow_data[cascade_index]._cull_results = &_cull_results[shadow_cam.HashCode()];
                    }
                    is_exist_directional_shaodw = true;
                }
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightColor = color.xyz;
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightDir = light_data._light_dir.xyz;
                per_scene_cbuf_data->_MainlightWorldPosition = light_data._light_dir.xyz;
                per_scene_cbuf_data->_MainlightColor = color.xyz;
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
                if (comp._shadow._is_cast_shadow)
                {
                    per_scene_cbuf_data->_PointLights[point_light_index]._shadowmap_index = point_light_index;//点光源使用这个值来索引cubearray
                    per_scene_cbuf_data->_PointLights[point_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
                    per_scene_cbuf_data->_PointLights[point_light_index]._constant_bias = comp._shadow._constant_bias;
                    per_scene_cbuf_data->_PointLights[point_light_index]._slope_bias = comp._shadow._slope_bias;
                    for (int i = 0; i < 6; i++)
                    {
                        auto &shadow_cam = comp._shadow_cameras[i];
                        Cull(*g_pSceneMgr->ActiveScene(), shadow_cam);
                        _rendering_data._point_shadow_data[point_light_index]._shadowmap_index = point_light_index;
                        _rendering_data._point_shadow_data[point_light_index]._shadow_matrices[i] = shadow_cam.GetView() * shadow_cam.GetProj();
                        _rendering_data._point_shadow_data[point_light_index]._cull_results[i] = &_cull_results[shadow_cam.HashCode()];
                    }
                    _rendering_data._point_shadow_data[point_light_index]._light_world_pos = light_data._light_pos.xyz;
                    _rendering_data._point_shadow_data[point_light_index]._camera_near = 0.01f;//1 cm
                    _rendering_data._point_shadow_data[point_light_index]._camera_far = light_data._light_param.x * 1.5f;
                    ++_rendering_data._addi_point_shadow_num;
                }
                per_scene_cbuf_data->_PointLights[point_light_index]._LightColor = color.xyz;
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
                if (comp._shadow._is_cast_shadow)
                {
                    auto &shadow_cam = comp._shadow_cameras[0];
                    Cull(*g_pSceneMgr->ActiveScene(), shadow_cam);
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._shadowmap_index = _rendering_data._addi_shadow_num;
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._constant_bias = comp._shadow._constant_bias * comp._shadow._constant_bias * s_shadow_bias_factor;
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._slope_bias = comp._shadow._slope_bias * comp._shadow._slope_bias * s_shadow_bias_factor;
                    Matrix4x4f shaodw_matrix = shadow_cam.GetView() * shadow_cam.GetProj();
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._shadow_matrix = shaodw_matrix;
                    _rendering_data._spot_shadow_data[spot_light_index]._shadowmap_index = _rendering_data._addi_shadow_num;
                    _rendering_data._spot_shadow_data[spot_light_index]._shadow_matrix = shaodw_matrix;
                    _rendering_data._spot_shadow_data[spot_light_index]._cull_results = &_cull_results[shadow_cam.HashCode()];
                    ++_rendering_data._addi_shadow_num;
                }
                per_scene_cbuf_data->_SpotLights[spot_light_index]._LightColor = color.xyz;
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
                if (comp._shadow._is_cast_shadow)
                {
                    auto &shadow_cam = comp._shadow_cameras[0];
                    Matrix4x4f shaodw_matrix = shadow_cam.GetView() * shadow_cam.GetProj();
                    Cull(*g_pSceneMgr->ActiveScene(), shadow_cam);
                    per_scene_cbuf_data->_AreaLights[area_light_index]._shadowmap_index = _rendering_data._addi_shadow_num;
                    per_scene_cbuf_data->_AreaLights[area_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
                    per_scene_cbuf_data->_AreaLights[area_light_index]._constant_bias = comp._shadow._constant_bias * comp._shadow._constant_bias * s_shadow_bias_factor;
                    per_scene_cbuf_data->_AreaLights[area_light_index]._slope_bias = comp._shadow._slope_bias * comp._shadow._slope_bias * s_shadow_bias_factor;
                    per_scene_cbuf_data->_AreaLights[area_light_index]._shadow_matrix = shaodw_matrix;
                    _rendering_data._area_shadow_data[area_light_index]._shadowmap_index = _rendering_data._addi_shadow_num;
                    _rendering_data._area_shadow_data[area_light_index]._shadow_matrix = shaodw_matrix;
                    _rendering_data._area_shadow_data[area_light_index]._cull_results = &_cull_results[shadow_cam.HashCode()];
                    ++_rendering_data._addi_shadow_num;
                }
                per_scene_cbuf_data->_AreaLights[area_light_index]._LightColor = color.xyz;
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

    void Renderer::PrepareCamera(const Camera &cam)
    {
        //auto cam_hash = cam.HashCode();
        auto cam_cb_data = ConstantBuffer::As<CBufferPerCameraData>(_cur_fs->GetCameraCB(cam.HashCode()));
        f32 f = cam.Far(), n = cam.Near();
        u32 pixel_width = cam.OutputSize().x, pixel_height = cam.OutputSize().y;
        cam_cb_data->_ScreenParams = Vector4f(1.0f / (f32) pixel_width,1.0f / (f32) pixel_height,(f32)pixel_width,(f32)pixel_height);
        Camera::CalculateZBUfferAndProjParams(cam,cam_cb_data->_ZBufferParams,cam_cb_data->_ProjectionParams);
        cam_cb_data->_CameraPos = cam.Position();
        cam_cb_data->_MatrixV = cam.GetView();
        cam_cb_data->_MatrixP = cam.GetProj();
        cam_cb_data->_MatrixVP = cam_cb_data->_MatrixV * cam_cb_data->_MatrixP;
        cam_cb_data->_MatrixVP_NoJitter = cam.GetViewProj();
        cam_cb_data->_MatrixIVP = MatrixInverse(cam_cb_data->_MatrixVP);
        auto prev_cam_cb_data = ConstantBuffer::As<CBufferPerCameraData>(_prev_fs->GetCameraCB(_active_camera_hash));
        cam_cb_data->_MatrixVP_Pre = prev_cam_cb_data->_MatrixVP_NoJitter;
        auto& jitter = cam.GetJitter();
        cam_cb_data->_matrix_jitter_x = jitter.x;
        cam_cb_data->_matrix_jitter_y = jitter.y;
        cam_cb_data->_uv_jitter_x = jitter.z;
        cam_cb_data->_uv_jitter_y = jitter.w;
        cam.GetCornerInWorld(cam_cb_data->_LT, cam_cb_data->_LB, cam_cb_data->_RT, cam_cb_data->_RB);
        _rendering_data._cull_results = &_cull_results[cam.HashCode()];
        _rendering_data._camera = &cam;
        _rendering_data._p_per_camera_cbuf = _cur_fs->GetCameraCB(cam.HashCode());
    }
    RenderTexture *Renderer::TargetTexture()
    {
        return _target_tex;
    }
    void Renderer::DoRender(const Camera &cam, const Scene &s)
    {
        if (Application::Get()._is_multi_thread_rendering && _p_cur_pipeline->_is_need_wait_for_render_thread)
        {
            Application::Get().NotifyRender();
            Application::Get().WaitForRender();
            _p_cur_pipeline->_is_need_wait_for_render_thread = false;
        }
        {
            PROFILE_BLOCK_CPU(BeginScene)
            BeginScene(cam, s);
        }
        if (_is_use_render_graph)
        {
            _rd_graph->Execute(*_p_context,_rendering_data);
        }
        else
        {
            if (cam._layer_mask & ERenderLayer::kDefault)
            {
                for (auto &pass: _render_passes)
                {
                    if (pass->IsActive())
                    {
                        CPUProfileBlock cblock(pass->GetName());
                        pass->BeginPass(_p_context);
                        pass->Execute(_p_context, _rendering_data);
                        pass->EndPass(_p_context);
                    }
                }
            }
            else if (cam._layer_mask & ERenderLayer::kSkyBox)
            {
                CPUProfileBlock cblock(_skybox_pass->GetName());
                _skybox_pass->BeginPass(_p_context);
                _skybox_pass->Execute(_p_context, _rendering_data);
                _skybox_pass->EndPass(_p_context);
            }
        }
        EndScene(s);
    }

    template<typename T>
    void static CullObject(T &comp, const ViewFrustum &vf, CullResult &cur_cam_cull_results, u16 &scene_render_obj_index, const u64 &entity_index, const Camera &cam, const Scene &s)
    {
        if (comp._p_mesh)
        {
            auto &aabbs = comp._transformed_aabbs;
            Vector3f center;
            u16 submesh_count = comp._p_mesh->SubmeshCount();
            auto &materials = comp._p_mats;
            if (!cam.IsCustomVP() && !ViewFrustum::Conatin(vf, aabbs[0]))
            {
                ++scene_render_obj_index;
                return;
            }
            for (int i = 0; i < submesh_count; i++)
            {
                if (ViewFrustum::Conatin(vf, aabbs[i + 1]) || cam.IsCustomVP())
                {
                    f32 dis = Distance(aabbs[i + 1].Center(), cam.Position());
                    u32 queue_id;
                    Material *used_mat = nullptr;
                    if (!materials.empty())
                    {
                        used_mat = i < materials.size() && materials[i] != nullptr ? materials[i].get() : materials[0].get();
                        if (used_mat)
                        {
                            queue_id = used_mat->RenderQueue();
                            if (!cur_cam_cull_results.contains(queue_id))
                            {
                                cur_cam_cull_results.insert(std::make_pair(queue_id, Vector<RenderableObjectData>()));
                            }
                        }
                        auto e = s.GetRegister().GetEntity<ECS::StaticMeshComponent>(entity_index);
                        cur_cam_cull_results[queue_id].emplace_back(RenderableObjectData{scene_render_obj_index, dis,
                                                                                         (u16) i, 1, comp._p_mesh.get(), used_mat, &s.GetRegister().GetComponent<ECS::TransformComponent>(e)->_transform._world_matrix});
                    }
                }
                ++scene_render_obj_index;
            }
        }
    }
    void Renderer::Cull(const Scene &s, const Camera &cam)
    {
        if (_cull_results.contains(cam.HashCode()))
            _cull_results[cam.HashCode()].clear();
        else
            _cull_results.insert(std::make_pair(cam.HashCode(), CullResult()));
        u16 scene_render_obj_index = 0u;
        u64 entity_index = 0u;
        auto &cur_cam_cull_results = _cull_results[cam.HashCode()];
        const Camera& cull_cam = Camera::sSelected? *Camera::sSelected : cam;
        auto &vf = cull_cam.GetViewFrustum();
        for (auto &comp: s.GetAllStaticRenderable())
        {
            CullObject(comp, vf, cur_cam_cull_results, scene_render_obj_index, entity_index, cull_cam, s);
            ++entity_index;
        }
        entity_index = 0u;
        for (auto &comp: s.GetAllSkinedRenderable())
        {
            CullObject(comp, vf, cur_cam_cull_results, scene_render_obj_index, entity_index, cull_cam, s);
            ++entity_index;
        }
        for (auto &it: cur_cam_cull_results)
        {
            auto &objs = it.second;
            std::sort(objs.begin(), objs.end(), [this](const RenderableObjectData &a, const RenderableObjectData &b)
                      { return a._distance_to_cam < b._distance_to_cam; });
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
