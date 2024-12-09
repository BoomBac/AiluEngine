#include "Render/Renderer.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/Pass/PostprocessPass.h"
#include "Render/Pass/SSAOPass.h"
#include "Render/Pass/TAAPass.h"
#include "Render/Pass/VoxelGI.h"
#include "Render/RenderPipeline.h"
#include "Render/RenderingData.h"
#include "pch.h"


namespace Ailu
{
    Renderer::Renderer()
    {
        _p_context = g_pGfxContext;
        _b_init = true;
        Profiler::g_Profiler.Initialize();
        _p_timemgr = new TimeMgr();
        _p_timemgr->Initialize();
        _rendering_data._gbuffers.resize(5);
        _shadowcast_pass = MakeScope<ShadowCastPass>();
        _gbuffer_pass = MakeScope<DeferredGeometryPass>();
        _coptdepth_pass = MakeScope<CopyDepthPass>();
        _lighting_pass = MakeScope<DeferredLightingPass>();
        _skybox_pass = MakeScope<SkyboxPass>();
        _forward_pass = MakeScope<ForwardPass>();
        _ssao_pass = MakeScope<SSAOPass>();
        _copycolor_pass = MakeScope<CopyColorPass>();
        _postprocess_pass = MakeScope<PostProcessPass>();
        _gizmo_pass = MakeScope<GizmoPass>();
        _wireframe_pass = MakeScope<WireFramePass>();
        _gui_pass = MakeScope<GUIPass>();
        //_owned_features.push_back(std::move(std::unique_ptr<RenderFeature>(new TAAFeature())));
        _owned_features.push_back(std::move(std::unique_ptr<RenderFeature>(new VoxelGI())));
        _vxgi = _owned_features.back().get();
        RegisterEventBeforeTick([]()
                                { GraphicsPipelineStateMgr::UpdateAllPSOObject(); });
        RegisterEventAfterTick([]()
                               { Profiler::g_Profiler.EndFrame(); });
    }

    Renderer::~Renderer()
    {
        _p_timemgr->Finalize();
        DESTORY_PTR(_p_timemgr);
    }

    void Renderer::Render(const Camera &cam, const Scene &s)
    {
        bool is_output_to_camera_tex = cam.TargetTexture() != nullptr;
        RenderingStates::Reset();
        ModuleTimeStatics::RenderDeltatime = _p_timemgr->GetElapsedSinceLastMark();
        _p_timemgr->Mark();
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
                    ProfileBlock b(cmd.get(), cmd->GetName());
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
                    tmp_cam.Rect(cam.TargetTexture()->Width(), cam.TargetTexture()->Height());
                    Cull(s, tmp_cam);
                    DoRender(tmp_cam, s);
                    auto cmd = CommandBufferPool::Get("FinalBlit");
                    {
                        ProfileBlock b(cmd.get(), cmd->GetName());
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
        Vector<RenderFeature *> ret;
        for (auto &f: _features)
            ret.push_back(f);
        for (auto &f: _owned_features)
            ret.push_back(f.get());
        return ret;
    }
    void Renderer::BeginScene(const Camera &cam, const Scene &s)
    {
        _active_camera_hash = cam.HashCode();
        IConstantBuffer *scene_cb = _cur_fs->GetSceneCB(s.HashCode());
        IConstantBuffer *cam_cb = _cur_fs->GetCameraCB(_active_camera_hash);
        cam_cb->Reset();
        scene_cb->Reset();
        auto scene_cb_data = IConstantBuffer::As<CBufferPerSceneData>(scene_cb);
        _rendering_data._shadow_data[0]._shadow_index = -1;
        for (int i = 0; i < RenderConstants::kMaxSpotLightNum; i++)
        {
            scene_cb_data->_SpotLights[i]._ShadowDataIndex = -1;
            _rendering_data._shadow_data[i + RenderConstants::kMaxCascadeShadowMapSplitNum]._shadow_index = -1;
        }
        for (int i = 0; i < RenderConstants::kMaxPointLightNum; i++)
        {
            scene_cb_data->_PointLights[i]._ShadowDataIndex = -1;
            _rendering_data._point_shadow_data[i]._camera_far = 0;//标记不投影
        }
        u32 pixel_width = cam.Rect().x, pixel_height = cam.Rect().y;
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
            _rendering_data._camera_data._camera_color_target_desc = RenderTextureDesc(pixel_width, pixel_height, ERenderTargetFormat::kDefaultHDR);
            //_rendering_data._camera_data._camera_color_target_desc._width = pixel_width;
            _camera_color_handle = RenderTexture::GetTempRT(_rendering_data._camera_data._camera_color_target_desc, "CameraColorAttachment");
            _camera_depth_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraDepthAttachment", ERenderTargetFormat::kDepth);
            _camera_depth_tex_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraDepthTexture", ERenderTargetFormat::kRFloat);
            _rendering_data._camera_opaque_tex_handle = RenderTexture::GetTempRT(pixel_width, pixel_height, "CameraColorOpaqueTex", ERenderTargetFormat::kDefaultHDR);
            _rendering_data._gbuffers[0] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer0", ERenderTargetFormat::kRGHalf);
            _rendering_data._gbuffers[1] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer1", ERenderTargetFormat::kDefault);
            _rendering_data._gbuffers[2] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer2", ERenderTargetFormat::kDefault);
            _rendering_data._gbuffers[3] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer3", ERenderTargetFormat::kRGHalf);
            _rendering_data._gbuffers[4] = RenderTexture::GetTempRT(pixel_width, pixel_height, "GBuffer4", ERenderTargetFormat::kRGBAHalf);
        }
        Cull(*g_pSceneMgr->ActiveScene(), cam);
        PrepareCamera(cam);
        PrepareScene(*g_pSceneMgr->ActiveScene());
        PrepareLight(*g_pSceneMgr->ActiveScene());
        _rendering_data._width = pixel_width;
        _rendering_data._height = pixel_height;
        _rendering_data._viewport = Rect{0, 0, (uint16_t) pixel_width, (uint16_t) pixel_height};
        _rendering_data._scissor_rect = _rendering_data._viewport;
        _rendering_data._camera_color_target_handle = _camera_color_handle;
        _rendering_data._camera_depth_target_handle = _camera_depth_handle;
        _rendering_data._camera_depth_tex_handle = _camera_depth_tex_handle;
        _rendering_data._final_rt_handle = _gameview_rt_handle;
        _vxgi->SetActive(cam._is_gen_voxel);
        if (_mode & EShadingMode::kLit)
        {
            _skybox_pass->Setup(false);
            if (cam._is_render_shadow)
                _render_passes.emplace_back(_shadowcast_pass.get());
            if (!_rendering_data._is_debug_voxel)
            {
                _render_passes.emplace_back(_gbuffer_pass.get());
                _render_passes.emplace_back(_lighting_pass.get());
                _render_passes.emplace_back(_forward_pass.get());
                _render_passes.emplace_back(_coptdepth_pass.get());
                _render_passes.emplace_back(_ssao_pass.get());
                _render_passes.emplace_back(_copycolor_pass.get());
                if (cam._is_enable_postprocess)
                    _render_passes.emplace_back(_postprocess_pass.get());
            }
            else
                _skybox_pass->Setup(true);
            for (auto &feature: _owned_features)
            {
                if (feature->IsActive())
                    feature->AddRenderPasses(*this, _rendering_data);
            }
        }
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
        {
            PROFILE_BLOCK_CPU(WaitForSys)
            for (auto &sys: s.GetRegister().SystemView())
                sys.second->WaitFor();
        }
    }
    void Renderer::EndScene(const Scene &s)
    {
        u16 obj_index = 0;
        for (auto &static_mesh: s.GetRegister().View<StaticMeshComponent>())
        {
            if (static_mesh._p_mesh)
            {
                auto &aabbs = static_mesh._transformed_aabbs;
                Vector3f center;
                u16 submesh_count = static_mesh._p_mesh->SubmeshCount();
                auto &materials = static_mesh._p_mats;
                auto *obj_cb = IConstantBuffer::As<CBufferPerObjectData>(_cur_fs->GetObjCB(obj_index));
                for (int i = 0; i < submesh_count; i++)
                {
                    obj_cb->_MatrixWorldPreTick = obj_cb->_MatrixWorld;
                    ++obj_index;
                }
            }
        }
        _render_passes.clear();
        _rendering_data.Reset();
        Gizmo::EndFrame();
    }
    void Renderer::FrameCleanup()
    {
        //_render_passes.clear();
        //_features.clear();
    }

    float Renderer::GetDeltaTime() const
    {
        return _p_timemgr->GetElapsedSinceLastMark();
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
        for (auto &static_mesh: s.GetRegister().View<StaticMeshComponent>())
        {
            if (static_mesh._p_mesh)
            {
                auto &aabbs = static_mesh._transformed_aabbs;
                Vector3f center;
                u16 submesh_count = static_mesh._p_mesh->SubmeshCount();
                auto &materials = static_mesh._p_mats;
                for (int i = 0; i < submesh_count; i++)
                {
                    auto *obj_cb = IConstantBuffer::As<CBufferPerObjectData>(_cur_fs->GetObjCB(obj_index));
                    const auto &t = r.GetComponent<StaticMeshComponent, TransformComponent>(entity_index)->_transform;
                    obj_cb->_MatrixWorld = t._world_matrix;
                    obj_cb->_ObjectID = r.GetEntity<StaticMeshComponent>(entity_index);
                    ++obj_index;
                }
            }
            ++entity_index;
        }
        entity_index = 0u;
        for (auto &static_mesh: s.GetRegister().View<CSkeletonMesh>())
        {
            if (static_mesh._p_mesh)
            {
                auto &aabbs = static_mesh._transformed_aabbs;
                Vector3f center;
                u16 submesh_count = static_mesh._p_mesh->SubmeshCount();
                auto &materials = static_mesh._p_mats;
                for (int i = 0; i < submesh_count; i++)
                {
                    const auto &t = r.GetComponent<CSkeletonMesh, TransformComponent>(entity_index)->_transform;
                    auto *obj_cb = IConstantBuffer::As<CBufferPerObjectData>(_cur_fs->GetObjCB(obj_index));
                    obj_cb->_MatrixWorld = t._world_matrix;
                    obj_cb->_ObjectID = r.GetEntity<CSkeletonMesh>(entity_index);
                    ++obj_index;
                }
            }
            ++entity_index;
        };
        _rendering_data._p_per_object_cbuf = _cur_fs->GetObjCB();
        _rendering_data._p_per_scene_cbuf = _cur_fs->GetSceneCB(s.HashCode());
        auto scene_data = IConstantBuffer::As<CBufferPerSceneData>(_rendering_data._p_per_scene_cbuf);
        f32 cascade_shaodw_max_dis = (f32) QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[QuailtySetting::s_cascade_shadow_map_count - 1];
        scene_data->_CascadeShadowParams = Vector4f((f32) QuailtySetting::s_cascade_shadow_map_count,
                                                    1.0f / QuailtySetting::s_shadow_fade_out_factor, 1.0f / cascade_shaodw_max_dis, 0.f);
        scene_data->g_IndirectLightingIntensity = s._light_data._indirect_lighting_intensity;
        PrepareLight(s);
        f32 t = g_pTimeMgr->TimeSinceLoad, dt = g_pTimeMgr->DeltaTime, sdt = g_pTimeMgr->s_smooth_delta_time;
        scene_data->_Time = Vector4f(t / 20, t, t * 2, t * 3);
        scene_data->_SinTime = Vector4f(sin(t / 8), sin(t / 4), sin(t / 2), sin(t));
        scene_data->_CosTime = Vector4f(cos(t / 8), cos(t / 4), cos(t / 2), cos(t));
        scene_data->_DeltaTime = Vector4f(dt, 1 / dt, sdt, 1 / sdt);
    }

    void Renderer::PrepareLight(const Scene &s)
    {
        const static f32 s_shadow_bias_factor = 0.01f;
        uint16_t updated_light_num = 0u;
        uint16_t direction_light_index = 0, point_light_index = 0, spot_light_index = 0, area_light_index = 0;
        u16 shadowmap_matrix_index = RenderConstants::kMaxCascadeShadowMapSplitNum, shadowcubemap_matrix_index = 0u;
        auto per_scene_cbuf_data = IConstantBuffer::As<CBufferPerSceneData>(_cur_fs->GetSceneCB(s.HashCode()));
        //u16 total_shadow_matrix_count = RenderConstants::kMaxCascadeShadowMapSplitNum;//阴影绘制和采样时来索引虚拟摄像机的矩阵，对于点光源，其采样不需要该值，只用作标志位确认是否需要处理阴影
        //per_scene_cbuf_data->_MainlightWorldPosition = {-1.91522e-07f,-0.707107f,-0.707107f,0};//默认太阳位置给物理大气使用，防止
        for (const auto &comp: s.GetRegister().View<LightComponent>())
        {
            auto &light_data = comp._light;
            Color color = light_data._light_color;
            color.r *= color.a;
            color.g *= color.a;
            color.b *= color.a;
            bool is_exist_directional_shaodw = false;
            bool is_cur_light_comp_active = true;
            if (comp._type == ELightType::kDirectional)
            {
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._ShadowDataIndex = -1;
                if (!is_cur_light_comp_active)
                {
                    per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightDir = Vector3f::kZero;
                    per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                if (comp._shadow._is_cast_shadow && !is_exist_directional_shaodw)
                {
                    for (int i = 0; i < QuailtySetting::s_cascade_shadow_map_count; i++)
                    {
                        auto &shadow_cam = comp._shadow_cameras[i];
                        Cull(*g_pSceneMgr->ActiveScene(), shadow_cam);
                        u16 shadow_index = i;
                        per_scene_cbuf_data->_DirectionalLights[direction_light_index]._ShadowDataIndex = shadow_index;
                        per_scene_cbuf_data->_DirectionalLights[direction_light_index]._ShadowDistance = QuailtySetting::s_main_light_shaodw_distance;
                        per_scene_cbuf_data->_ShadowMatrix[shadow_index] = shadow_cam.GetView() * shadow_cam.GetProjection();
                        per_scene_cbuf_data->_CascadeShadowSplit[i] = comp._cascade_shadow_data[i];
                        per_scene_cbuf_data->_DirectionalLights[direction_light_index]._constant_bias = comp._shadow._constant_bias * comp._shadow._constant_bias * s_shadow_bias_factor;
                        per_scene_cbuf_data->_DirectionalLights[direction_light_index]._slope_bias = comp._shadow._slope_bias * comp._shadow._slope_bias * s_shadow_bias_factor;
                        _rendering_data._shadow_data[shadow_index]._shadow_index = shadow_index;
                        _rendering_data._shadow_data[shadow_index]._shadow_matrix = &per_scene_cbuf_data->_ShadowMatrix[shadow_index];
                        _rendering_data._shadow_data[shadow_index]._cull_results = &_cull_results[shadow_cam.HashCode()];
                    }
                    is_exist_directional_shaodw = true;
                }
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightColor = color.xyz;
                per_scene_cbuf_data->_DirectionalLights[direction_light_index]._LightDir = light_data._light_dir.xyz;
                per_scene_cbuf_data->_MainlightWorldPosition = light_data._light_dir;//Normalize(light_data._light_dir);
                _rendering_data._mainlight_world_position = per_scene_cbuf_data->_MainlightWorldPosition;
                ++direction_light_index;
            }
            else if (comp._type == ELightType::kPoint)
            {
                per_scene_cbuf_data->_PointLights[point_light_index]._ShadowDataIndex = -1;
                if (!is_cur_light_comp_active)
                {
                    per_scene_cbuf_data->_PointLights[point_light_index]._LightParam0 = 0.0;
                    per_scene_cbuf_data->_PointLights[point_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                if (comp._shadow._is_cast_shadow)
                {
                    //u32 shadow_index = total_shadow_matrix_count;
                    per_scene_cbuf_data->_PointLights[point_light_index]._ShadowDataIndex = point_light_index;//点光源使用这个值来索引cubearray
                    per_scene_cbuf_data->_PointLights[point_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
                    per_scene_cbuf_data->_PointLights[point_light_index]._constant_bias = comp._shadow._constant_bias;
                    per_scene_cbuf_data->_PointLights[point_light_index]._slope_bias = comp._shadow._slope_bias;
                    //_per_frame_cbuf_data._PointLights[point_light_index]._ShadowNear = 10;
                    for (int i = 0; i < 6; i++)
                    {
                        auto &shadow_cam = comp._shadow_cameras[i];
                        Cull(*g_pSceneMgr->ActiveScene(), shadow_cam);
                        //per_scene_cbuf_data->_ShadowMatrix[shadow_index + i] = shadow_cam.GetView() * shadow_cam.GetProjection();
                        _rendering_data._point_shadow_data[point_light_index]._shadow_indices[i] = -1;// 废弃变量，现在生成shadowmp时不使用矩阵数组索引
                        _rendering_data._point_shadow_data[point_light_index]._shadow_matrices[i] = shadow_cam.GetView() * shadow_cam.GetProjection();
                        _rendering_data._point_shadow_data[point_light_index]._cull_results[i] = &_cull_results[shadow_cam.HashCode()];
                    }
                    _rendering_data._point_shadow_data[point_light_index]._light_world_pos = light_data._light_pos.xyz;
                    _rendering_data._point_shadow_data[point_light_index]._camera_near = 0.01;//1 cm
                    _rendering_data._point_shadow_data[point_light_index]._camera_far = light_data._light_param.x * 1.5f;
                    ++_rendering_data._addi_point_shadow_num;
                }
                per_scene_cbuf_data->_PointLights[point_light_index]._LightColor = color.xyz;
                per_scene_cbuf_data->_PointLights[point_light_index]._LightPos = light_data._light_pos.xyz;
                per_scene_cbuf_data->_PointLights[point_light_index]._LightParam0 = light_data._light_param.x;
                per_scene_cbuf_data->_PointLights[point_light_index++]._LightParam1 = light_data._light_param.y;
            }
            else if (comp._type == ELightType::kSpot)
            {
                per_scene_cbuf_data->_SpotLights[spot_light_index]._ShadowDataIndex = -1;
                if (!is_cur_light_comp_active)
                {
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                if (comp._shadow._is_cast_shadow)
                {
                    u32 shadow_index = shadowmap_matrix_index++;
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._ShadowDataIndex = shadow_index;
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._constant_bias = comp._shadow._constant_bias * comp._shadow._constant_bias * s_shadow_bias_factor;
                    per_scene_cbuf_data->_SpotLights[spot_light_index]._slope_bias = comp._shadow._slope_bias * comp._shadow._slope_bias * s_shadow_bias_factor;
                    auto &shadow_cam = comp._shadow_cameras[0];
                    Cull(*g_pSceneMgr->ActiveScene(), shadow_cam);
                    per_scene_cbuf_data->_ShadowMatrix[shadow_index] = shadow_cam.GetView() * shadow_cam.GetProjection();
                    _rendering_data._shadow_data[shadow_index]._shadow_index = shadow_index;
                    _rendering_data._shadow_data[shadow_index]._shadow_matrix = &per_scene_cbuf_data->_ShadowMatrix[shadow_index];
                    _rendering_data._shadow_data[shadow_index]._cull_results = &_cull_results[shadow_cam.HashCode()];
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
                per_scene_cbuf_data->_AreaLights[area_light_index]._ShadowDataIndex = -1;
                if (!is_cur_light_comp_active)
                {
                    per_scene_cbuf_data->_AreaLights[area_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                if (comp._shadow._is_cast_shadow)
                {
                    u32 shadow_index = shadowmap_matrix_index++;
                    per_scene_cbuf_data->_AreaLights[area_light_index]._ShadowDataIndex = shadow_index;
                    per_scene_cbuf_data->_AreaLights[area_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
                    per_scene_cbuf_data->_AreaLights[area_light_index]._constant_bias = comp._shadow._constant_bias * comp._shadow._constant_bias * s_shadow_bias_factor;
                    per_scene_cbuf_data->_AreaLights[area_light_index]._slope_bias = comp._shadow._slope_bias * comp._shadow._slope_bias * s_shadow_bias_factor;
                    auto &shadow_cam = comp._shadow_cameras[0];
                    per_scene_cbuf_data->_ShadowMatrix[shadow_index] = shadow_cam.GetView() * shadow_cam.GetProjection();
                    _rendering_data._shadow_data[shadow_index]._shadow_index = shadow_index;
                    _rendering_data._shadow_data[shadow_index]._shadow_matrix = &per_scene_cbuf_data->_ShadowMatrix[shadow_index];
                    Cull(*g_pSceneMgr->ActiveScene(), shadow_cam);
                    _rendering_data._shadow_data[shadow_index]._cull_results = &_cull_results[shadow_cam.HashCode()];
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
        for (const auto &vxgi: s.GetRegister().View<CVXGI>())
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
        auto cam_cb_data = IConstantBuffer::As<CBufferPerCameraData>(_cur_fs->GetCameraCB(cam.HashCode()));
        u32 pixel_width = cam.Rect().x, pixel_height = cam.Rect().y;
        cam_cb_data->_ScreenParams.x = (f32) pixel_width;
        cam_cb_data->_ScreenParams.y = (f32) pixel_height;
        cam_cb_data->_ScreenParams.z = 1.0f / (f32) pixel_width;
        cam_cb_data->_ScreenParams.w = 1.0f / (f32) pixel_height;
        cam_cb_data->_CameraPos = cam.Position();
        cam_cb_data->_MatrixV = cam.GetView();
        cam_cb_data->_MatrixP = cam.GetProjection();
        cam_cb_data->_MatrixVP = cam_cb_data->_MatrixV * cam_cb_data->_MatrixP;
        cam_cb_data->_MatrixIVP = MatrixInverse(cam_cb_data->_MatrixVP);
        _rendering_data._cull_results = &_cull_results[cam.HashCode()];
        _rendering_data._camera = &cam;
        _rendering_data._p_per_camera_cbuf = _cur_fs->GetCameraCB(cam.HashCode());
    }
    RenderTexture *Renderer::TargetTexture()
    {
        return _target_tex;
    }
    void Renderer::SetViewProjectionMatrix(const Matrix4x4f &view, const Matrix4x4f &proj)
    {
        auto cam_cb_data = IConstantBuffer::As<CBufferPerCameraData>(_cur_fs->GetCameraCB(_active_camera_hash));
        cam_cb_data->_MatrixV = view;
        cam_cb_data->_MatrixP = proj;
        cam_cb_data->_MatrixVP = cam_cb_data->_MatrixV * cam_cb_data->_MatrixP;
        cam_cb_data->_MatrixIVP = MatrixInverse(cam_cb_data->_MatrixVP);
        _rendering_data._p_per_camera_cbuf = _cur_fs->GetCameraCB(_active_camera_hash);
    }
    void Renderer::DoRender(const Camera &cam, const Scene &s)
    {
        BeginScene(cam, s);
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
                        auto e = s.GetRegister().GetEntity<StaticMeshComponent>(entity_index);
                        cur_cam_cull_results[queue_id].emplace_back(RenderableObjectData{scene_render_obj_index, dis,
                                                                                         (u16) i, 1, comp._p_mesh.get(), used_mat, &s.GetRegister().GetComponent<TransformComponent>(e)->_transform._world_matrix});
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
        auto &vf = cam.GetViewFrustum();
        for (auto &comp: s.GetAllStaticRenderable())
        {
            CullObject(comp, vf, cur_cam_cull_results, scene_render_obj_index, entity_index, cam, s);
            ++entity_index;
        }
        entity_index = 0u;
        for (auto &comp: s.GetAllSkinedRenderable())
        {
            CullObject(comp, vf, cur_cam_cull_results, scene_render_obj_index, entity_index, cam, s);
            ++entity_index;
        }
        for (auto &it: cur_cam_cull_results)
        {
            auto &objs = it.second;
            std::sort(objs.begin(), objs.end(), [this](const RenderableObjectData &a, const RenderableObjectData &b)
                      { return a._distance_to_cam < b._distance_to_cam; });
        }
    }
    void Ailu::Renderer::StableSort(Vector<RenderPass *> list)
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
