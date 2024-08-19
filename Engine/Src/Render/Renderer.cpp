#include "Render/Renderer.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/SceneMgr.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/Pass/PostprocessPass.h"
#include "Render/Pass/SSAOPass.h"
#include "Render/Pass/TAAPass.h"
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

        for (int i = 0; i < RenderConstants::kMaxRenderObjectCount; i++)
            _p_per_object_cbufs[i] = IConstantBuffer::Create(RenderConstants::kPerObjectDataSize);

        _rendering_data._gbuffers.resize(4);
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
        _features.push_back(std::move(std::unique_ptr<RenderFeature>(new TAAFeature())));

        RegisterEventBeforeTick([]()
                                { GraphicsPipelineStateMgr::UpdateAllPSOObject(); });
        RegisterEventAfterTick([]()
                               { Profiler::g_Profiler.EndFrame(); });
    }

    Renderer::~Renderer()
    {
        _p_timemgr->Finalize();
        DESTORY_PTR(_p_timemgr);
        for (int i = 0; i < RenderConstants::kMaxRenderObjectCount; i++)
            DESTORY_PTR(_p_per_object_cbufs[i]);
    }

    void Renderer::Render(const Camera &cam, Scene &s)
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
                    tmp_cam.Cull(&s);
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

    void Renderer::BeginScene(const Camera &cam, const Scene &s)
    {
        _active_camera_hash = cam.HashCode();
        if (cam._is_render_shadow)
            _render_passes.emplace_back(_shadowcast_pass.get());
        _render_passes.emplace_back(_gbuffer_pass.get());
        _render_passes.emplace_back(_coptdepth_pass.get());
        _render_passes.emplace_back(_ssao_pass.get());
        _render_passes.emplace_back(_lighting_pass.get());
        if (cam._is_render_sky_box)
            _render_passes.emplace_back(_skybox_pass.get());
        _render_passes.emplace_back(_forward_pass.get());
        _render_passes.emplace_back(_copycolor_pass.get());
        if (cam._is_enable_postprocess)
            _render_passes.emplace_back(_postprocess_pass.get());
        if (cam._is_scene_camera)
            _render_passes.emplace_back(_gizmo_pass.get());


        memset(reinterpret_cast<void *>(&_per_scene_cbuf_data), 0, sizeof(CBufferPerSceneData));
        memset(reinterpret_cast<void *>(&_per_cam_cbuf_data), 0, sizeof(CBufferPerCameraData));
        _rendering_data._shadow_data[0]._shadow_index = -1;
        for (int i = 0; i < RenderConstants::kMaxSpotLightNum; i++)
        {
            _per_scene_cbuf_data._SpotLights[i]._ShadowDataIndex = -1;
            _rendering_data._shadow_data[i + RenderConstants::kMaxCascadeShadowMapSplitNum]._shadow_index = -1;
        }
        for (int i = 0; i < RenderConstants::kMaxPointLightNum; i++)
        {
            _per_scene_cbuf_data._PointLights[i]._ShadowDataIndex = -1;
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
        }
        PrepareCamera(cam);
        PrepareScene(*g_pSceneMgr->_p_current);
        PrepareLight(*g_pSceneMgr->_p_current);

        _rendering_data._width = pixel_width;
        _rendering_data._height = pixel_height;
        _rendering_data._viewport = Rect{0, 0, (uint16_t) pixel_width, (uint16_t) pixel_height};
        _rendering_data._scissor_rect = _rendering_data._viewport;
        _rendering_data._camera_color_target_handle = _camera_color_handle;
        _rendering_data._camera_depth_target_handle = _camera_depth_handle;
        _rendering_data._camera_depth_tex_handle = _camera_depth_tex_handle;
        _rendering_data._final_rt_handle = _gameview_rt_handle;
        _target_tex = g_pRenderTexturePool->Get(_rendering_data._camera_color_target_handle);
        Shader::SetGlobalBuffer(RenderConstants::kCBufNamePerScene, _p_per_scene_cbuf[s.HashCode()].get());
        Shader::SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _p_per_camera_cbuf[cam.HashCode()].get());
    }
    void Renderer::EndScene(const Scene &s)
    {
        u16 obj_index = 0;
        for (auto static_mesh: s.GetAllStaticRenderable())
        {
            if (static_mesh->GetMesh())
            {
                auto &aabbs = static_mesh->GetAABB();
                Vector3f center;
                u16 submesh_count = static_mesh->GetMesh()->SubmeshCount();
                auto &materials = static_mesh->GetMaterials();
                for (int i = 0; i < submesh_count; i++)
                {
                    _per_object_datas[obj_index]._MatrixWorldPreTick = _per_object_datas[obj_index]._MatrixWorld;
                    memcpy(_p_per_object_cbufs[obj_index]->GetData(), &_per_object_datas[obj_index], RenderConstants::kPerObjectDataSize);
                    ++obj_index;
                }
            }
        }

        _rendering_data.Reset();
        _render_passes.clear();
        Gizmo::EndFrame();
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
        for (auto static_mesh: s.GetAllStaticRenderable())
        {
            if (static_mesh->GetMesh())
            {
                auto &aabbs = static_mesh->GetAABB();
                Vector3f center;
                u16 submesh_count = static_mesh->GetMesh()->SubmeshCount();
                auto &materials = static_mesh->GetMaterials();
                for (int i = 0; i < submesh_count; i++)
                {
                    _per_object_datas[obj_index]._MatrixWorld = static_mesh->GetOwner()->GetComponent<TransformComponent>()->GetMatrix();
                    memcpy(_p_per_object_cbufs[obj_index]->GetData(), &_per_object_datas[obj_index], RenderConstants::kPerObjectDataSize);
                    ++obj_index;
                }
            }
        }
        _rendering_data._p_per_object_cbuf = _p_per_object_cbufs;
        f32 cascade_shaodw_max_dis = (f32) QuailtySetting::s_main_light_shaodw_distance * QuailtySetting::s_cascade_shadow_map_split[QuailtySetting::s_cascade_shadow_map_count - 1];
        _per_scene_cbuf_data._CascadeShadowParams = Vector4f((f32) QuailtySetting::s_cascade_shadow_map_count,
                                                             1.0f / QuailtySetting::s_shadow_fade_out_factor, 1.0f / cascade_shaodw_max_dis, 0.f);
        _per_scene_cbuf_data.g_IndirectLightingIntensity = s._light_data._indirect_lighting_intensity;
        PrepareLight(s);
        auto scene_hash = s.HashCode();
        if (!_p_per_scene_cbuf.contains(scene_hash))
        {
            Scope<IConstantBuffer> cbuf = std::unique_ptr<IConstantBuffer>(IConstantBuffer::Create(RenderConstants::kPerSceneDataSize));
            _p_per_scene_cbuf.insert(std::make_pair(scene_hash, std::move(cbuf)));
        }
        _rendering_data._p_per_scene_cbuf = _p_per_scene_cbuf[scene_hash].get();
        memcpy(_p_per_scene_cbuf[scene_hash]->GetData(), &_per_scene_cbuf_data, sizeof(CBufferPerSceneData));
    }

    void Renderer::PrepareLight(const Scene &s)
    {
        const static f32 s_shadow_bias_factor = 0.01f;
        auto &light_comps = s.GetAllLight();
        uint16_t updated_light_num = 0u;
        uint16_t direction_light_index = 0, point_light_index = 0, spot_light_index = 0;
        u16 total_shadow_matrix_count = RenderConstants::kMaxCascadeShadowMapSplitNum;//阴影绘制和采样时来索引虚拟摄像机的矩阵，对于点光源，其采样不需要该值，只用作标志位确认是否需要处理阴影
        for (auto light: light_comps)
        {
            auto &light_data = light->_light;
            Color32 color = light_data._light_color;
            color.r *= color.a;
            color.g *= color.a;
            color.b *= color.a;
            bool is_exist_directional_shaodw = false;
            if (light->LightType() == ELightType::kDirectional)
            {
                _per_scene_cbuf_data._DirectionalLights[direction_light_index]._ShadowDataIndex = -1;
                if (!light->Active())
                {
                    _per_scene_cbuf_data._DirectionalLights[direction_light_index]._LightDir = Vector3f::kZero;
                    _per_scene_cbuf_data._DirectionalLights[direction_light_index]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                if (light->CastShadow() && !is_exist_directional_shaodw)
                {
                    for (int i = 0; i < QuailtySetting::s_cascade_shadow_map_count; i++)
                    {
                        auto shadow_cam = light->ShadowCamera(i);
                        _per_scene_cbuf_data._DirectionalLights[direction_light_index]._ShadowDataIndex = i;
                        _per_scene_cbuf_data._DirectionalLights[direction_light_index]._ShadowDistance = QuailtySetting::s_main_light_shaodw_distance;
                        _per_scene_cbuf_data._ShadowMatrix[i] = shadow_cam->GetView() * shadow_cam->GetProjection();
                        _rendering_data._shadow_data[i]._shadow_index = i;
                        _rendering_data._shadow_data[i]._shadow_matrix = _per_scene_cbuf_data._ShadowMatrix[i];
                        _rendering_data._shadow_data[i]._cull_results = &shadow_cam->CullResults();
                        _per_scene_cbuf_data._CascadeShadowSplit[i] = light->CascadeShadowMData(i);
                        _per_scene_cbuf_data._DirectionalLights[direction_light_index]._constant_bias = light->_shadow._constant_bias * light->_shadow._constant_bias * s_shadow_bias_factor;
                        _per_scene_cbuf_data._DirectionalLights[direction_light_index]._slope_bias = light->_shadow._slope_bias * light->_shadow._slope_bias * s_shadow_bias_factor;
                    }
                    is_exist_directional_shaodw = true;
                }
                _per_scene_cbuf_data._DirectionalLights[direction_light_index]._LightColor = color.xyz;
                _per_scene_cbuf_data._DirectionalLights[direction_light_index]._LightDir = light_data._light_dir.xyz;
                _per_scene_cbuf_data._MainlightWorldPosition = light_data._light_dir;//Normalize(light_data._light_dir);
                _rendering_data._mainlight_world_position = _per_scene_cbuf_data._MainlightWorldPosition;
                ++direction_light_index;
            }
            else if (light->LightType() == ELightType::kPoint)
            {
                _per_scene_cbuf_data._PointLights[point_light_index]._ShadowDataIndex = -1;
                if (!light->Active())
                {
                    _per_scene_cbuf_data._PointLights[point_light_index]._LightParam0 = 0.0;
                    _per_scene_cbuf_data._PointLights[point_light_index++]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                if (light->CastShadow())
                {
                    u32 shadow_index = total_shadow_matrix_count;
                    _per_scene_cbuf_data._PointLights[point_light_index]._ShadowDataIndex = _rendering_data._addi_point_shadow_num;//点光源使用这个值来索引cubearray
                    _per_scene_cbuf_data._PointLights[point_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
                    _per_scene_cbuf_data._PointLights[point_light_index]._constant_bias = light->_shadow._constant_bias;
                    _per_scene_cbuf_data._PointLights[point_light_index]._slope_bias = light->_shadow._slope_bias;
                    //_per_frame_cbuf_data._PointLights[point_light_index]._ShadowNear = 10;
                    for (int i = 0; i < 6; i++)
                    {
                        Camera *shadow_cam = light->ShadowCamera(i);
                        _per_scene_cbuf_data._ShadowMatrix[shadow_index + i] = shadow_cam->GetView() * shadow_cam->GetProjection();
                        _rendering_data._point_shadow_data[point_light_index]._shadow_indices[i] = shadow_index + i;
                        _rendering_data._point_shadow_data[point_light_index]._cull_results[i] = &shadow_cam->CullResults();
                        ++total_shadow_matrix_count;
                    }
                    _rendering_data._point_shadow_data[point_light_index]._light_world_pos = light_data._light_pos.xyz;
                    _rendering_data._point_shadow_data[point_light_index]._camera_near = 10;
                    _rendering_data._point_shadow_data[point_light_index]._camera_far = light_data._light_param.x * 1.5f;
                    ++_rendering_data._addi_point_shadow_num;
                }
                _per_scene_cbuf_data._PointLights[point_light_index]._LightColor = color.xyz;
                _per_scene_cbuf_data._PointLights[point_light_index]._LightPos = light_data._light_pos.xyz;
                _per_scene_cbuf_data._PointLights[point_light_index]._LightParam0 = light_data._light_param.x;
                _per_scene_cbuf_data._PointLights[point_light_index++]._LightParam1 = light_data._light_param.y;
            }
            else if (light->LightType() == ELightType::kSpot)
            {
                _per_scene_cbuf_data._SpotLights[spot_light_index]._ShadowDataIndex = -1;
                if (!light->Active())
                {
                    _per_scene_cbuf_data._SpotLights[spot_light_index++]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                if (light->CastShadow())
                {
                    u32 shadow_index = total_shadow_matrix_count++;
                    _per_scene_cbuf_data._SpotLights[spot_light_index]._ShadowDataIndex = shadow_index;
                    _per_scene_cbuf_data._SpotLights[spot_light_index]._ShadowDistance = light_data._light_param.x * 1.5f;
                    _per_scene_cbuf_data._SpotLights[spot_light_index]._constant_bias = light->_shadow._constant_bias * light->_shadow._constant_bias * s_shadow_bias_factor;
                    _per_scene_cbuf_data._SpotLights[spot_light_index]._slope_bias = light->_shadow._slope_bias * light->_shadow._slope_bias * s_shadow_bias_factor;
                    Camera *shadow_cam = light->ShadowCamera();
                    _per_scene_cbuf_data._ShadowMatrix[shadow_index] = shadow_cam->GetView() * shadow_cam->GetProjection();
                    _rendering_data._shadow_data[shadow_index]._shadow_index = shadow_index;
                    _rendering_data._shadow_data[shadow_index]._cull_results = &shadow_cam->CullResults();
                    ++_rendering_data._addi_shadow_num;
                }
                _per_scene_cbuf_data._SpotLights[spot_light_index]._LightColor = color.xyz;
                _per_scene_cbuf_data._SpotLights[spot_light_index]._LightPos = light_data._light_pos.xyz;
                _per_scene_cbuf_data._SpotLights[spot_light_index]._LightDir = light_data._light_dir.xyz;
                _per_scene_cbuf_data._SpotLights[spot_light_index]._Rdius = light_data._light_param.x;
                float inner_cos = cos(light_data._light_param.y / 2 * k2Radius);
                float outer_cos = cos(light_data._light_param.z / 2 * k2Radius);
                _per_scene_cbuf_data._SpotLights[spot_light_index]._LightAngleScale = 1.0f / std::max(0.001f, (inner_cos - outer_cos));
                _per_scene_cbuf_data._SpotLights[spot_light_index]._LightAngleOffset = -outer_cos * _per_scene_cbuf_data._SpotLights[spot_light_index]._LightAngleScale;
                ++spot_light_index;
            }
            ++updated_light_num;
        }
        if (updated_light_num == 0)
        {
            _per_scene_cbuf_data._DirectionalLights[0]._LightColor = Colors::kBlack.xyz;
            _per_scene_cbuf_data._DirectionalLights[0]._LightDir = {0.0f, 0.0f, 0.0f};
        }
    }

    void Renderer::PrepareCamera(const Camera &cam)
    {
        auto cam_hash = cam.HashCode();
        u32 pixel_width = cam.Rect().x, pixel_height = cam.Rect().y;
        _per_cam_cbuf_data._ScreenParams.x = (f32) pixel_width;
        _per_cam_cbuf_data._ScreenParams.y = (f32) pixel_height;
        _per_cam_cbuf_data._ScreenParams.z = 1.0f / (f32) pixel_width;
        _per_cam_cbuf_data._ScreenParams.w = 1.0f / (f32) pixel_height;
        _per_cam_cbuf_data._CameraPos = cam.Position();
        _per_cam_cbuf_data._MatrixV = cam.GetView();
        _per_cam_cbuf_data._MatrixP = cam.GetProjection();
        _per_cam_cbuf_data._MatrixVP = _per_cam_cbuf_data._MatrixV * _per_cam_cbuf_data._MatrixP;
        _per_cam_cbuf_data._MatrixIVP = MatrixInverse(_per_cam_cbuf_data._MatrixVP);
        _rendering_data._cull_results = &cam.CullResults();
        _rendering_data._camera = &cam;
        if (!_p_per_camera_cbuf.contains(cam_hash))
        {
            Scope<IConstantBuffer> cbuf = std::unique_ptr<IConstantBuffer>(IConstantBuffer::Create(RenderConstants::kPerCameraDataSize));
            _p_per_camera_cbuf.insert(std::make_pair(cam_hash, std::move(cbuf)));
        }
        _rendering_data._p_per_camera_cbuf = _p_per_camera_cbuf[cam_hash].get();
        memcpy(_p_per_camera_cbuf[cam_hash]->GetData(), &_per_cam_cbuf_data, RenderConstants::kPerCameraDataSize);
    }
    RenderTexture *Renderer::TargetTexture()
    {
        return _target_tex;
    }
    void Renderer::SetViewProjectionMatrix(const Matrix4x4f &view, const Matrix4x4f &proj)
    {
        _per_cam_cbuf_data._MatrixV = view;
        _per_cam_cbuf_data._MatrixP = proj;
        _per_cam_cbuf_data._MatrixVP = _per_cam_cbuf_data._MatrixV * _per_cam_cbuf_data._MatrixP;
        _per_cam_cbuf_data._MatrixIVP = MatrixInverse(_per_cam_cbuf_data._MatrixVP);
        if (!_p_per_camera_cbuf.contains(_active_camera_hash))
        {
            Scope<IConstantBuffer> cbuf = std::unique_ptr<IConstantBuffer>(IConstantBuffer::Create(RenderConstants::kPerCameraDataSize));
            _p_per_camera_cbuf.insert(std::make_pair(_active_camera_hash, std::move(cbuf)));
        }
        _rendering_data._p_per_camera_cbuf = _p_per_camera_cbuf[_active_camera_hash].get();
        memcpy(_p_per_camera_cbuf[_active_camera_hash]->GetData(), &_per_cam_cbuf_data, RenderConstants::kPerCameraDataSize);
    }
    void Renderer::DoRender(const Camera &cam, Scene &s)
    {
        BeginScene(cam, s);
        for (auto &feature: _features)
        {
            feature->AddRenderPasses(this, _rendering_data);
        }
        std::stable_sort(_render_passes.begin(), _render_passes.end(), [](RenderPass *a, RenderPass *b) -> bool
                         { return *a < *b; });
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
