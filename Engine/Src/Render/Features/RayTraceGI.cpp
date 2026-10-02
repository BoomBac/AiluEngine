#include "Render/Features/RayTraceGI.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Framework/Common/TimeMgr.h"
#include "Scene/Scene.h"
#include "Render/Renderer.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Animation/SkinningSystem.h"
#include <type_traits>

namespace Ailu
{
    namespace Render
    {
        static u32 s_global_frame_counter = 0u;
        static u32 s_debug_collect_begin_frame = 0u;
        static bool s_is_debug_collecting = false;
        static u32 kCollectDebugFrameNum = 1u;
        namespace
        {
            class PathTracerHistoryHasher
            {
            public:
                void Add(const Vector3f &value)
                {
                    Add(value.x);
                    Add(value.y);
                    Add(value.z);
                }

                void Add(const Vector4f &value)
                {
                    Add(value.x);
                    Add(value.y);
                    Add(value.z);
                    Add(value.w);
                }

                void Add(const Color &value)
                {
                    Add(value.r);
                    Add(value.g);
                    Add(value.b);
                    Add(value.a);
                }

                template<typename T>
                void Add(const T &value)
                {
                    static_assert(std::is_trivially_copyable_v<T>);
                    const auto *bytes = reinterpret_cast<const u8 *>(&value);
                    for (size_t index = 0u; index < sizeof(T); ++index)
                    {
                        _value ^= bytes[index];
                        _value *= 1099511628211ull;
                    }
                }

                template<typename T>
                void AddPointer(const T *value)
                {
                    const uintptr_t address = reinterpret_cast<uintptr_t>(value);
                    Add(address);
                }

                u64 Value() const { return _value; }

            private:
                u64 _value = 14695981039346656037ull;
            };

            void AddMaterialHistory(PathTracerHistoryHasher &hasher, const Material *material)
            {
                hasher.AddPointer(material);
                if (material == nullptr)
                    return;

                hasher.Add(material->PropertyVersion());
                const auto assets = material->CaptureAssetSnapshots();
                hasher.Add(assets._shader_revision);
                for (const auto &[property_id, revision] : assets._texture_revisions)
                {
                    hasher.Add(property_id);
                    hasher.Add(revision);
                }
                for (const auto &[property_id, texture] : material->BoundTextures())
                {
                    hasher.Add(property_id);
                    hasher.AddPointer(texture);
                    const u64 texture_hash = texture != nullptr ? texture->HashCode() : 0u;
                    hasher.Add(texture_hash);
                }
            }

            template<typename TRenderable>
            void AddRenderableHistory(PathTracerHistoryHasher &hasher, const SceneManagement::Scene &scene)
            {
                const auto &reg = scene.GetRegister();
                const auto view = reg.View<TRenderable>();
                const u64 count = view.size();
                hasher.Add(count);
                for (u64 index = 0u; index < view.size(); ++index)
                {
                    const auto &renderable = view[index];
                    const ECS::Entity entity = reg.GetEntity<TRenderable>(index);
                    hasher.Add(entity);
                    const bool enabled = scene.IsEntityEnabled(entity) && reg.IsComponentEnabled<TRenderable>(entity);
                    hasher.Add(enabled);
                    hasher.AddPointer(renderable._p_mesh.get());
                    const u64 material_count = renderable._p_mats.size();
                    hasher.Add(material_count);
                    for (const auto &material : renderable._p_mats)
                        AddMaterialHistory(hasher, material.get());
                    if constexpr (std::is_same_v<TRenderable, ECS::CSkeletonMesh>)
                    {
                        hasher.Add(ECS::SkinningSystem::ResolveSkinVersion(renderable._p_mesh.get(),
                                                                           static_cast<u32>(entity)));
                    }
                }
            }

            u64 BuildPathTracerHistorySignature(const RenderingData &rendering_data, bool enable_mnee,
                                                bool enable_power_weighted_light_sampling, bool enable_ris,
                                                bool enable_resampling, bool enable_environment_nee,
                                                EPathTracerAccumulationMode accumulation_mode)
            {
                PathTracerHistoryHasher hasher;
                hasher.Add(enable_mnee);
                hasher.Add(enable_power_weighted_light_sampling);
                hasher.Add(enable_ris);
                hasher.Add(enable_resampling);
                hasher.Add(enable_environment_nee);
                hasher.Add(accumulation_mode);
                hasher.Add(rendering_data._width);
                hasher.Add(rendering_data._height);

                if (rendering_data._camera != nullptr)
                {
                    const Camera &camera = *rendering_data._camera;
                    hasher.Add(camera.Type());
                    hasher.Add(camera.Aspect());
                    hasher.Add(camera.Near());
                    hasher.Add(camera.Far());
                    hasher.Add(camera.FovH());
                    hasher.Add(camera.Size());
                    const Matrix4x4f &projection = camera.GetProjNoJitter();
                    for (u32 row = 0u; row < 4u; ++row)
                        for (u32 column = 0u; column < 4u; ++column)
                            hasher.Add(projection[row][column]);
                }

                if (rendering_data._scene != nullptr)
                {
                    const auto &scene = *rendering_data._scene;
                    hasher.Add(scene.StructureRevision());
                    AddRenderableHistory<ECS::StaticMeshComponent>(hasher, scene);
                    AddRenderableHistory<ECS::CSkeletonMesh>(hasher, scene);

                    const auto &reg = scene.GetRegister();
                    const auto lights = reg.View<ECS::LightComponent>();
                    const u64 light_count = lights.size();
                    hasher.Add(light_count);
                    for (u64 index = 0u; index < lights.size(); ++index)
                    {
                        const ECS::Entity entity = reg.GetEntity<ECS::LightComponent>(index);
                        const bool enabled = scene.IsEntityEnabled(entity) &&
                                             reg.IsComponentEnabled<ECS::LightComponent>(entity);
                        hasher.Add(entity);
                        hasher.Add(enabled);
                        hasher.Add(lights[index]._type);
                        const auto &light = lights[index]._light;
                        hasher.Add(light._light_pos);
                        hasher.Add(light._light_dir);
                        hasher.Add(light._light_color);
                        hasher.Add(light._light_param);
                        for (const Vector3f &point : light._area_points)
                            hasher.Add(point);
                        hasher.Add(light._is_two_side);
                        const auto &shadow = lights[index]._shadow;
                        hasher.Add(shadow._is_cast_shadow);
                        hasher.Add(shadow._constant_bias);
                        hasher.Add(shadow._slope_bias);
                        hasher.Add(shadow._padding);
                    }
                }

                if (rendering_data._environment != nullptr)
                {
                    const auto &environment = *rendering_data._environment;
                    hasher.AddPointer(environment._source);
                    hasher.Add(environment._source_revision);
                    hasher.Add(environment._intensity);
                    hasher.Add(environment._rotation);
                    hasher.Add(environment._render_sky);
                    hasher.Add(environment._enable_ibl);
                    hasher.Add(environment._is_texture_source);
                    hasher.Add(environment._sky_color);
                    hasher.Add(environment._ground_color);
                }

                if (rendering_data._primitive_data != nullptr)
                {
                    const u64 primitive_count = rendering_data._primitive_data->size();
                    hasher.Add(primitive_count);
                    for (const PrimitiveData &primitive : *rendering_data._primitive_data)
                    {
                        for (u32 row = 0u; row < 4u; ++row)
                            for (u32 column = 0u; column < 4u; ++column)
                                hasher.Add(primitive._local_to_world[row][column]);
                        hasher.Add(primitive._entity_id);
                        hasher.Add(primitive._material_id);
                        hasher.Add(primitive._submesh_id);
                        hasher.Add(primitive._flags);
                        hasher.Add(primitive._global_triangle_offset);
                        hasher.Add(primitive._submesh_triangle_count);
                    }
                }

                if (rendering_data._scene_primitives != nullptr)
                {
                    const u64 primitive_count = rendering_data._scene_primitives->size();
                    hasher.Add(primitive_count);
                    for (const ScenePrimitive &primitive : *rendering_data._scene_primitives)
                    {
                        hasher.Add(primitive._entity_id);
                        hasher.Add(primitive._submesh_index);
                        hasher.Add(primitive._flags);
                        hasher.AddPointer(primitive._mesh);
                        hasher.AddPointer(primitive._vertex_buffer);
                        hasher.AddPointer(primitive._index_buffer);
                        hasher.Add(primitive._vertex_buffer != nullptr ? primitive._vertex_buffer->GetViewVersion() : 0u);
                        hasher.Add(primitive._index_buffer != nullptr ? primitive._index_buffer->GetViewVersion() : 0u);
                    }
                }
                return hasher.Value();
            }
        }
#pragma region RayTraceGI
        RayTraceGI::RayTraceGI() : RenderFeature("RayTraceGI")
        {
            _use_hardware_ray_tracing = GraphicsContext::Get().IsHardwareRayTracingSupported();
            if (_use_hardware_ray_tracing)
                _gi_raytracing_shader = ResourceMgr::Get().LoadAssetRef<RayTracingShader>(
                    L"Shaders/hlsl/DXR/raytracing.alasset");
            _gi_compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/ray_trace/raytrace_gi.alasset");
            _gi_pass = MakeScope<GIPass>(_gi_compute_shader, _gi_raytracing_shader);
            _gi_compute_shader->SetInts("_PickPixel",{200,200});
            _gi_compute_shader->SetBool("_enable_ris", _enable_ris);
            _gi_compute_shader->SetBool("_enable_resampling", _enable_resampling);
        }
        void RayTraceGI::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
        {
            if (_gi_raytracing_shader != nullptr)
            {
                if (_enable_mnee)
                    _gi_raytracing_shader->EnableKeyword("PATH_TRACER_MNEE");
                else
                    _gi_raytracing_shader->DisableKeyword("PATH_TRACER_MNEE");
            }
            if (_enable_mnee)
                _gi_compute_shader->EnableKeyword("PATH_TRACER_MNEE");
            else
                _gi_compute_shader->DisableKeyword("PATH_TRACER_MNEE");
            if (_use_power_weighted_light_sampling)
            {
                _gi_compute_shader->EnableKeyword("PATH_TRACER_POWER_LIGHT_SAMPLING");
                if (_gi_raytracing_shader != nullptr)
                    _gi_raytracing_shader->EnableKeyword("PATH_TRACER_POWER_LIGHT_SAMPLING");
            }
            else
            {
                _gi_compute_shader->DisableKeyword("PATH_TRACER_POWER_LIGHT_SAMPLING");
                if (_gi_raytracing_shader != nullptr)
                    _gi_raytracing_shader->DisableKeyword("PATH_TRACER_POWER_LIGHT_SAMPLING");
            }
            if (s_is_debug_collecting && s_global_frame_counter - s_debug_collect_begin_frame >= kCollectDebugFrameNum)
            {
                s_is_debug_collecting = false;
                _debug_pos = {-1.0f, -1.0f};
            }
            _gi_pass->_debug_pos = _debug_pos;
            _gi_pass->_accumulation_mode = _accumulation_mode;
            _gi_pass->_use_hardware_ray_tracing = _use_hardware_ray_tracing;
            _gi_pass->_enable_mnee = _enable_mnee;
            _gi_pass->_use_power_weighted_light_sampling = _use_power_weighted_light_sampling;
            _gi_pass->_enable_ris = _enable_ris;
            _gi_pass->_enable_resampling = _enable_resampling;
            _gi_pass->_enable_environment_nee = _enable_environment_nee;
            _gi_pass->_debug_temporal_target = _debug_temporal_target;
            _gi_compute_shader->SetInts("_PickPixel", {(i32)_debug_pos.x, (i32)_debug_pos.y});
            if (_debug_pos.x >= 0 && _debug_pos.y >= 0)
            {
                if (s_debug_collect_begin_frame == 0u)
                    s_debug_collect_begin_frame = s_global_frame_counter;
                s_is_debug_collecting = true;
            }
            else
            {
                s_debug_collect_begin_frame = 0u;
                s_is_debug_collecting = false;
                _debug_pos = {-1.0f, -1.0f};
            }
            _gi_compute_shader->SetInt("_debug_hit_box_idx", _debug_hit_box);

            if (_gi_pass->CanUseHardwareRayTracing())
            {
                _gi_pass->PrepareHardwareRayTracingScene(rendering_data);
            }

            _gi_pass->_debug_line_mat->SetInt("_debug_hit_box_idx", _debug_hit_box);
            renderer.EnqueuePass(_gi_pass.get());
            ++s_global_frame_counter;
        }

        void Ailu::Render::RayTraceGI::OnPropertyChanged(const PropertyInfo &prop)
        {
            if (prop.Name() == "_is_show_debug")
            {
                _gi_compute_shader->SetBool("_show_debug", _is_show_debug);
            }
            else if (prop.Name() == "_enable_ris")
            {
                _gi_compute_shader->SetBool("_enable_ris", _enable_ris);
            }
            else if (prop.Name() == "_enable_resampling")
            {
                _gi_compute_shader->SetBool("_enable_resampling", _enable_resampling);
            }
            else {}
        }

#pragma endregion

#pragma region GIPass

        Ref<ConstantBuffer> g_perCamData;
        CBufferPerCameraData g_camData;

        GIPass::GIPass(const AssetRef<ComputeShader> &cs, const AssetRef<RayTracingShader> &rt_shader) :
            _gi_compute_shader(cs), _gi_raytracing_shader(rt_shader), RenderPass("GIPass")
        {
            g_perCamData = ConstantBuffer::Create(sizeof(CBufferPerCameraData), "PerCamData");


            _debug_line_mat = MakeRef<Material>(ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/ray_trace/raytrace_debug_draw.alasset").get(), "RayDebugLineMat");
            _kernel_primary_ray = cs->FindKernel("PrimaryRay");
            _kernel_denoise = cs->FindKernel("Denoise");
            _kernel_build_environment_weights = cs->FindKernel("BuildEnvironmentWeights");
            _kernel_build_environment_cdf = cs->FindKernel("BuildEnvironmentCDF");
            // Produce opaque RT color before TAAOpaqueSnapshot456 and Sprite460.
            _event = ERenderPassEvent::kAfterMotionVector;
            _scene_rt_proxy = MakeScope<SceneRayTracingProxy>();
            _raygen_data = ConstantBuffer::Create(sizeof(RayGenConstantBuffer), "RayTraceGI_RayGenData");
            BufferDesc desc;
            desc._is_random_write = true;
            desc._is_readable = false;
            desc._format = EALGFormat::kALGFormatUNKOWN;
            desc._element_num = 5000;
            desc._element_size = 32u;//rt_common.hlsli  sizeof(DebugRay)
            desc._size = desc._element_size * desc._element_num;
            desc._target = EGPUBufferTarget::kAppend;
            _debug_buffer = GPUBuffer::Create(desc);
            desc._format = EALGFormat::kALGFormatR32_UINT;
            desc._element_size = sizeof(u32);
            _debug_index_buffer = GPUBuffer::Create(desc);
            _arg_buffer = GPUBuffer::Create(EGPUBufferTarget::kIndirectArguments, sizeof(DrawArguments), 1u, "RayDebugArgsBuffer");
            DrawArguments draw_arg{0u, 1u, 0, 0u};
            _arg_buffer->SetData((const u8 *) &draw_arg, sizeof(DrawArguments));
            _gi_compute_shader->SetBuffer("_debug_rays", _debug_buffer.get());
            _gi_compute_shader->SetBuffer("_debug_ray_indices", &*_debug_index_buffer);
        }

        bool GIPass::CanUseHardwareRayTracing() const
        {
            return _use_hardware_ray_tracing && g_pGfxContext != nullptr && g_pGfxContext->IsHardwareRayTracingSupported() && _gi_raytracing_shader != nullptr;
        }

        void GIPass::PrepareHardwareRayTracingScene(const RenderingData &rendering_data)
        {
            auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
            if (scene == nullptr || _scene_rt_proxy == nullptr)
                return;

            _scene_rt_proxy->Sync(scene, rendering_data._primitive_data, rendering_data._scene_primitive_buffer,
                                  rendering_data._scene_primitives);
        }

        void GIPass::UpdateRayGenData(const RenderingData &rendering_data)
        {
            _raygen_cb.viewport = {-1.0f, -1.0f, 1.0f, 1.0f};
            const f32 border = 0.1f;
            const f32 aspect_ratio = rendering_data._height == 0u ? 1.0f : static_cast<f32>(rendering_data._width) / static_cast<f32>(rendering_data._height);
            _raygen_cb.stencil = {
                -1.0f + border,
                -1.0f + border * aspect_ratio,
                1.0f - border,
                1.0f - border * aspect_ratio};
            _raygen_data->SetData(reinterpret_cast<const u8 *>(&_raygen_cb), sizeof(RayGenConstantBuffer));
        }

        void GIPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
        {
            static Matrix4x4f s_prev_view_proj = Matrix4x4f::Identity();
            static u32 s_frame_counter = 0u;
            const auto &camera = *rendering_data._camera;
            const auto &cur_vp_mat = camera.GetViewProjNoJitter();
            bool is_camera_dirty = false;
            for (u32 i = 0; i < 4; i++)
            {
                for (u32 j = 0; j < 4; j++)
                {
                    if (!NearbyEqual(s_prev_view_proj[i][j], cur_vp_mat[i][j],1e-4f))
                    {
                        is_camera_dirty = true;
                        break;
                    }
                }
                if (is_camera_dirty)
                    break;
            }
            const bool accumulation_enabled = _accumulation_mode != EPathTracerAccumulationMode::kDisabled;
            const bool accumulation_just_enabled = accumulation_enabled &&
                (!_has_previous_accumulation_mode ||
                 _previous_accumulation_mode == EPathTracerAccumulationMode::kDisabled);
            const bool target_recreated = MakesureTarget(rendering_data);
            const u64 history_signature = BuildPathTracerHistorySignature(rendering_data, _enable_mnee,
                                                                           _use_power_weighted_light_sampling,
                                                                           _enable_ris, _enable_resampling,
                                                                           _enable_environment_nee, _accumulation_mode);
            const Vector3f camera_position = rendering_data._camera->Position();
            const Vector3f camera_forward = rendering_data._camera->Forward();
            const Vector3f camera_up = rendering_data._camera->Up();
            // Small camera motion remains reprojected by stage 2; only a discontinuous camera cut resets all pixels.
            const bool camera_changed = _has_previous_camera && _previous_camera != rendering_data._camera;
            const bool explicit_reset = _has_previous_camera &&
                _previous_camera_reset_revision != camera.GetTemporalHistoryResetRevision();
            const bool camera_cut = _has_previous_camera &&
                (Magnitude(camera_position - _previous_camera_position) > 1.0f ||
                 DotProduct(camera_forward, _previous_camera_forward) < 0.9f ||
                 DotProduct(camera_up, _previous_camera_up) < 0.9f);
            const bool reference_camera_changed =
                _accumulation_mode == EPathTracerAccumulationMode::kReference && is_camera_dirty;
            const bool history_inputs_changed = _has_history_signature && history_signature != _last_history_signature;
            const bool reset_history = history_inputs_changed || camera_changed || camera_cut || explicit_reset ||
                                       reference_camera_changed || accumulation_just_enabled || target_recreated;
            const Vector2f current_uv_jitter(camera.GetJitter().z, camera.GetJitter().w);
            const Vector2f previous_uv_jitter = reset_history || !_temporal_history_valid
                ? current_uv_jitter
                : _previous_uv_jitter;
            const Vector4f camera_uv_jitter(previous_uv_jitter.x, previous_uv_jitter.y,
                                             current_uv_jitter.x, current_uv_jitter.y);
            if (is_camera_dirty || reset_history)
            {
                s_frame_counter = 0u;
            }
            else
                ++s_frame_counter;
            _previous_accumulation_mode = _accumulation_mode;
            _has_previous_accumulation_mode = true;
            s_prev_view_proj = camera.GetViewProjNoJitter();
            _last_history_signature = history_signature;
            _has_history_signature = true;
            _previous_camera_position = camera_position;
            _previous_camera_forward = camera_forward;
            _previous_camera_up = camera_up;
            _previous_camera = rendering_data._camera;
            _previous_camera_reset_revision = camera.GetTemporalHistoryResetRevision();
            _has_previous_camera = true;
            _raw_path_trace_handle = graph.Import(_raw_path_trace_texture.get());
            _environment_distribution_handle = graph.Import(_environment_distribution_texture.get());
            Texture *environment_source = rendering_data._environment != nullptr ?
                rendering_data._environment->_sky_cubemap.get() : nullptr;
            const u64 environment_revision = rendering_data._environment != nullptr ?
                rendering_data._environment->_source_revision : 0u;
            if (_environment_distribution_dirty || environment_source != _environment_distribution_source ||
                environment_revision != _environment_distribution_revision)
            {
                _environment_distribution_source = environment_source;
                _environment_distribution_revision = environment_revision;
                _environment_distribution_dirty = false;
                graph.AddPass("Build PathTracer Environment Distribution", RDG::PassDesc(RDG::EPassType::kCompute),
                    [&](RDG::RenderGraphBuilder &builder)
                    {
                        builder.Read(rendering_data._rg_handles._environment_sky);
                        _environment_distribution_handle = builder.Write(_environment_distribution_handle,
                                                                         EResourceUsage::kWriteUAV);
                    },
                    [this](RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &data)
                    {
                        Texture *distribution = pass_graph.Resolve<Texture>(_environment_distribution_handle);
                        _gi_compute_shader->SetTexture(_kernel_build_environment_weights, "_EnvironmentSky",
                            pass_graph.Resolve<Texture>(data._rg_handles._environment_sky));
                        _gi_compute_shader->SetTexture(_kernel_build_environment_weights,
                            "_EnvironmentDistributionOutput", distribution);
                        const u16 resolution = static_cast<u16>(distribution->Height());
                        const auto [groups_x, groups_y, groups_z] =
                            _gi_compute_shader->CalculateDispatchNum(_kernel_build_environment_weights,
                                                                      distribution->Width(), resolution, 1u);
                        cmd->Dispatch(_gi_compute_shader.get(), _kernel_build_environment_weights,
                                      groups_x, groups_y, groups_z);
                        cmd->UavBarrier(distribution);
                        _gi_compute_shader->SetTexture(_kernel_build_environment_cdf,
                            "_EnvironmentDistributionOutput", distribution);
                        cmd->Dispatch(_gi_compute_shader.get(), _kernel_build_environment_cdf, 1u, 1u, 1u);
                    });
            }
            _history_prev_handle = graph.Import(_is_history_a ? _history_texture_b.get() : _history_texture_a.get());
            _history_curr_handle = graph.Import(_is_history_a ? _history_texture_a.get() : _history_texture_b.get());
            _temporal_debug_handle = graph.Import(_temporal_debug_texture.get());
            _gi_compute_shader->SetInt("_frame_index", s_frame_counter);
            if (reset_history)
                _temporal_history_valid = false;
            _gi_compute_shader->SetBool("_temporal_history_valid", _temporal_history_valid);
            _gi_compute_shader->SetInt("_accumulation_mode", static_cast<i32>(_accumulation_mode));
            _gi_compute_shader->SetInt("_debug_temporal_target", (i32)_debug_temporal_target);
            _gi_compute_shader->SetBool("_enable_environment_nee", _enable_environment_nee);
            _gi_compute_shader->SetInt("_prev_surface_buffer_idx", _is_cur_a ? (_surface_buffer_b ? _surface_buffer_b->GetBindlessUAVIndex() : -1) : (_surface_buffer_a ? _surface_buffer_a->GetBindlessUAVIndex() : -1));
            _gi_compute_shader->SetInt("_curr_surface_buffer_idx", _is_cur_a ? (_surface_buffer_a ? _surface_buffer_a->GetBindlessUAVIndex() : -1) : (_surface_buffer_b ? _surface_buffer_b->GetBindlessUAVIndex() : -1));

            const bool use_hardware_ray_tracing = CanUseHardwareRayTracing();
            if (!use_hardware_ray_tracing && _scene_rt_proxy != nullptr)
                _scene_rt_proxy->SyncLightCache(SceneManagement::SceneMgr::Get().ActiveScene());
            GPUBuffer *prev_reservoir = _is_cur_a ? _reservoir_b.get() : _reservoir_a.get();
            GPUBuffer *curr_reservoir = _is_cur_a ? _reservoir_a.get() : _reservoir_b.get();
            GPUBuffer *prev_surface_buffer = _is_cur_a ? _surface_buffer_b.get() : _surface_buffer_a.get();
            GPUBuffer *curr_surface_buffer = _is_cur_a ? _surface_buffer_a.get() : _surface_buffer_b.get();
            GPUBuffer *unified_light_data = _scene_rt_proxy != nullptr ? _scene_rt_proxy->GetUnifiedLightData() : nullptr;
            GPUBuffer *unified_light_sampling_data = _scene_rt_proxy != nullptr ?
                _scene_rt_proxy->GetUnifiedLightSamplingData() : nullptr;
            RayTracingScene *rt_scene = use_hardware_ray_tracing && _scene_rt_proxy != nullptr ? _scene_rt_proxy->GetScene() : nullptr;
            if (use_hardware_ray_tracing && _scene_rt_proxy != nullptr && rt_scene != nullptr)
            {
                _scene_rt_proxy->AddBLASUpdatePass(graph);
                _scene_rt_proxy->AddTLASUpdatePass(graph);
            }
            if (!use_hardware_ray_tracing && _scene_rt_proxy != nullptr)
            {
                auto light_data = _scene_rt_proxy->GetUnifiedLightData();
                if (!light_data->IsReady())
                    return;
                _gi_compute_shader->SetBuffer("_UnifiedLights", _scene_rt_proxy->GetUnifiedLightData());
                if (_use_power_weighted_light_sampling)
                    _gi_compute_shader->SetBuffer(_kernel_primary_ray, "_UnifiedLightSamplingTable", unified_light_sampling_data);
            }
            const auto raw_executed = std::make_shared<bool>(false);
            {
                graph.AddPass(use_hardware_ray_tracing ? "RayTrace GI DXR" : "RayTrace GI", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
                    {
                        builder.Read(rendering_data._rg_handles._gbuffers[0]);
                        builder.Read(rendering_data._rg_handles._gbuffers[1]);
                        builder.Read(rendering_data._rg_handles._gbuffers[2]);
                        builder.Read(rendering_data._rg_handles._gbuffers[3]);
                        builder.Read(rendering_data._rg_handles._depth_tex);
                        builder.Read(rendering_data._rg_handles._motion_vector_tex);
                        builder.Read(rendering_data._rg_handles._environment_sky);
                        builder.Read(_environment_distribution_handle);
                        if (prev_reservoir != nullptr)
                            builder.Read(builder.Import(prev_reservoir));
                        if (curr_reservoir != nullptr)
                            (void) builder.Write(builder.Import(curr_reservoir), EResourceUsage::kWriteUAV);
                        if (prev_surface_buffer != nullptr)
                            builder.Read(builder.Import(prev_surface_buffer), EResourceUsage::kWriteUAV);
                        if (curr_surface_buffer != nullptr)
                            (void) builder.Write(builder.Import(curr_surface_buffer), EResourceUsage::kWriteUAV);
                        if (unified_light_data != nullptr)
                            builder.Read(builder.Import(unified_light_data));
                        if (_use_power_weighted_light_sampling && unified_light_sampling_data != nullptr)
                            builder.Read(builder.Import(unified_light_sampling_data));
                        if (rt_scene != nullptr)
                            builder.Read(builder.Import(rt_scene), EResourceUsage::kAccelStructRead);
                        if (use_hardware_ray_tracing && _scene_rt_proxy != nullptr)
                        {
                            if (auto *instance_data = _scene_rt_proxy->GetPrimitiveData(); instance_data != nullptr)
                                builder.Read(builder.Import(instance_data));
                            if (auto *material_data = _scene_rt_proxy->GetMaterialData(); material_data != nullptr)
                                builder.Read(builder.Import(material_data));
                            _scene_rt_proxy->AddRayTracingResourceReads(builder);
                        }
                        if (_debug_buffer != nullptr)
                            (void) builder.Write(builder.Import(_debug_buffer.get()), EResourceUsage::kWriteUAV);
                        if (_debug_index_buffer != nullptr)
                            (void) builder.Write(builder.Import(_debug_index_buffer.get()), EResourceUsage::kWriteUAV);
                        _raw_path_trace_handle = builder.Write(_raw_path_trace_handle, EResourceUsage::kWriteUAV);
                    },
                    [this, use_hardware_ray_tracing, prev_reservoir, curr_reservoir, camera_uv_jitter,
                     raw_executed, accumulation_enabled,
                     camera_ptr = rendering_data._camera](RDG::RenderGraph &graph, CommandBuffer *cmd,
                                                          const RenderingData &data)
                    {
                    auto *output = graph.Resolve<Texture>(_raw_path_trace_handle);
                    // A newly resized Raw texture may still be awaiting GPU creation. Skipping the dispatch
                    // prevents a missing UAV binding from retaining the distribution builder's UAV.
                    if (output == nullptr || !output->IsReady())
                        return;
                    if (use_hardware_ray_tracing)
                    {
                        auto *rt_scene = _scene_rt_proxy->GetScene();
                        if (rt_scene == nullptr || rt_scene->NeedsUpdate() || !_scene_rt_proxy->HasRenderableScene() ||
                            output == nullptr || !rt_scene->IsReady())
                            return;

                        const Camera &cam = *camera_ptr;
                        f32 f = cam.Far(), n = cam.Near();
                        u32 pixel_width = cam.OutputSize().x, pixel_height = cam.OutputSize().y;
                        g_camData._ScreenParams = Vector4f(1.0f / (f32) pixel_width, 1.0f / (f32) pixel_height, (f32) pixel_width, (f32) pixel_height);
                        Camera::CalculateZBUfferAndProjParams(cam, g_camData._ZBufferParams, g_camData._ProjectionParams);
                        g_camData._CameraPos = cam.Position();
                        g_camData._MatrixV = cam.GetView();
                        g_camData._MatrixP = cam.GetProj();
                        g_camData._MatrixVP = cam.GetViewProjJittered();
                        g_camData._MatrixVP_NoJitter = cam.GetViewProjNoJitter();
                        g_camData._MatrixIVP = MatrixInverse(g_camData._MatrixVP);
                        g_camData._MatrixVP_Pre = cam.GetPrevViewProj();
                        const auto &jitter = cam.GetJitter();
                        g_camData._matrix_jitter_x = jitter.x;
                        g_camData._matrix_jitter_y = jitter.y;
                        g_camData._uv_jitter_x = jitter.z;
                        g_camData._uv_jitter_y = jitter.w;
                        g_perCamData->SetData(reinterpret_cast<const u8 *>(&g_camData), sizeof(CBufferPerCameraData));
            
                        _gi_raytracing_shader->SetScene(rt_scene);
                        _gi_raytracing_shader->SetInt("_enable_environment_nee", _enable_environment_nee ? 1 : 0);
                        _gi_raytracing_shader->SetBuffer("g_primitive_data", _scene_rt_proxy->GetPrimitiveData());
                        _gi_raytracing_shader->SetBuffer("g_rayGenCB", _raygen_data.get());
                        _gi_raytracing_shader->SetBuffer("g_perSceneData", data._p_per_scene_cbuf);
                        _gi_raytracing_shader->SetBuffer("g_perCamData", g_perCamData.get());
                        _gi_raytracing_shader->SetBuffer("g_material_data", _scene_rt_proxy->GetMaterialData());
                        _gi_raytracing_shader->SetBuffer("_UnifiedLights", _scene_rt_proxy->GetUnifiedLightData());
                        _gi_raytracing_shader->SetBuffer("_UnifiedLightConfig", _scene_rt_proxy->GetUnifiedLightConfig());
                        if (_use_power_weighted_light_sampling)
                            _gi_raytracing_shader->SetBuffer("_UnifiedLightSamplingTable",
                                                             _scene_rt_proxy->GetUnifiedLightSamplingData());
                         _gi_raytracing_shader->SetTexture("_EnvironmentSky",
                                                          graph.Resolve<Texture>(data._rg_handles._environment_sky));
                        _gi_raytracing_shader->SetTexture("_EnvironmentDistribution",
                                                          graph.Resolve<Texture>(_environment_distribution_handle));
                        _gi_raytracing_shader->SetTexture("RenderTarget", output);
                        cmd->DispatchRays(_gi_raytracing_shader.get(), rt_scene, static_cast<u16>(data._width), static_cast<u16>(data._height), 1u);
                        *raw_executed = true;
                        if (!accumulation_enabled) _is_cur_a = !_is_cur_a;
                        return;
                    }

                    Vector4f params = {1.0f, 1.0f, (f32) (data._width), (f32) (data._height)};
                    params.x /= params.z;
                    params.y /= params.w;
                    u16 w = (u16) params.z, h = (u16) params.w;
                    {
                        constexpr u32 kTileCountX = 1u;
                        constexpr u32 kTileCountY = 1u;
                        const u16 tile_w = (u16) ((w + kTileCountX - 1u) / kTileCountX);
                        const u16 tile_h = (u16) ((h + kTileCountY - 1u) / kTileCountY);
                        const u32 tile_index = _tile_frame_counter % (kTileCountX * kTileCountY);
                        const u32 tile_x = tile_index % kTileCountX;
                        const u32 tile_y = tile_index / kTileCountX;
                        const u16 tile_offset_x = (u16) (tile_x * tile_w);
                        const u16 tile_offset_y = (u16) (tile_y * tile_h);
                        const u16 tile_size_x = (u16) ((tile_offset_x + tile_w > w) ? (w - tile_offset_x) : tile_w);
                        const u16 tile_size_y = (u16) ((tile_offset_y + tile_h > h) ? (h - tile_offset_y) : tile_h);
                        if (s_is_debug_collecting && s_debug_collect_begin_frame == s_global_frame_counter - 1)
                            _debug_buffer->SetCounter(0u);
                        auto [x, y, z] = _gi_compute_shader->CalculateDispatchNum(_kernel_primary_ray, tile_size_x, tile_size_y, 1);
                        _gi_compute_shader->SetTexture(_kernel_primary_ray, "_GBuffer0",
                                                       graph.Resolve<Texture>(data._rg_handles._gbuffers[0]));
                        _gi_compute_shader->SetTexture(_kernel_primary_ray, "_GBuffer1",
                                                       graph.Resolve<Texture>(data._rg_handles._gbuffers[1]));
                        _gi_compute_shader->SetTexture(_kernel_primary_ray, "_GBuffer2",
                                                       graph.Resolve<Texture>(data._rg_handles._gbuffers[2]));
                        _gi_compute_shader->SetTexture(_kernel_primary_ray, "_GBuffer3",
                                                       graph.Resolve<Texture>(data._rg_handles._gbuffers[3]));
                        _gi_compute_shader->SetTexture(_kernel_primary_ray, "_CameraDepthTexture",
                                                       graph.Resolve<Texture>(data._rg_handles._depth_tex));
                        _gi_compute_shader->SetTexture(_kernel_primary_ray, "_MotionVectorTexture",
                                                       graph.Resolve<Texture>(data._rg_handles._motion_vector_tex));
                        _gi_compute_shader->SetVector("_CameraUvJitter", camera_uv_jitter);
                        Texture *environment_sky = graph.Resolve<Texture>(data._rg_handles._environment_sky);
                        _gi_compute_shader->SetTexture(_kernel_primary_ray, "_EnvironmentSky", environment_sky);
                        _gi_compute_shader->SetTexture(_kernel_primary_ray, "_EnvironmentDistribution",
                                                       graph.Resolve<Texture>(_environment_distribution_handle));
                        //_gi_compute_shader->SetBuffer("_debug_rays", _debug_buffer.get());
                        //_gi_compute_shader->SetBuffer("_debug_ray_indices", &*_debug_index_buffer);
                        auto light_count = _scene_rt_proxy->GetUnifiedLightCount();
                        _gi_compute_shader->SetInt("_light_count", light_count);
                        _gi_compute_shader->SetInts("_GI_TileOffset", {(i32) tile_offset_x, (i32) tile_offset_y});
                        _gi_compute_shader->SetInts("_GI_TileSize", {(i32) tile_size_x, (i32) tile_size_y});
                        _gi_compute_shader->SetTexture(_kernel_primary_ray, "_GI_Texture",
                                                       graph.Resolve<Texture>(_raw_path_trace_handle));
                        _gi_compute_shader->SetBuffer(_kernel_primary_ray, "g_prev_reservoir",
                                                      prev_reservoir);
                        _gi_compute_shader->SetBuffer(_kernel_primary_ray, "g_curr_reservoir",
                                                      curr_reservoir);
                        cmd->Dispatch(_gi_compute_shader.get(), _kernel_primary_ray, x, y, 1);
                        *raw_executed = true;
                        if (!accumulation_enabled) _is_cur_a = !_is_cur_a;
                    } });
            }
            ++_tile_frame_counter;
            if (_accumulation_mode != EPathTracerAccumulationMode::kDisabled)
            {
                graph.AddPass("Denoise", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(_raw_path_trace_handle);
                    builder.Read(_history_prev_handle);
                    builder.Read(rendering_data._rg_handles._motion_vector_tex);
                    builder.Read(rendering_data._rg_handles._gbuffers[0]);
                    builder.Read(rendering_data._rg_handles._depth_tex);
                    if (prev_surface_buffer != nullptr)
                        builder.Read(builder.Import(prev_surface_buffer), EResourceUsage::kWriteUAV);
                    if (curr_surface_buffer != nullptr)
                        (void) builder.Write(builder.Import(curr_surface_buffer), EResourceUsage::kWriteUAV);
                    _history_curr_handle = builder.Write(_history_curr_handle, EResourceUsage::kWriteUAV);
                    _temporal_debug_handle = builder.Write(_temporal_debug_handle, EResourceUsage::kWriteUAV);
                },
                [this, prev_surface_buffer, curr_surface_buffer, camera_uv_jitter, current_uv_jitter, raw_executed](
                    RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                {
                    if (!*raw_executed)
                    {
                        _temporal_history_valid = false;
                        return;
                    }
                // Surface history stays in UAV state across frames; order its reads and writes explicitly.
                if (prev_surface_buffer != nullptr)
                    cmd->UavBarrier(prev_surface_buffer);
                if (curr_surface_buffer != nullptr)
                    cmd->UavBarrier(curr_surface_buffer);
                Vector4f params = {1.0f, 1.0f, (f32) (data._width), (f32) (data._height)};
                params.x /= params.z;
                params.y /= params.w;
                u16 w = (u16) params.z, h = (u16) params.w;
                {
                    auto [x, y, z] = _gi_compute_shader->CalculateDispatchNum(_kernel_denoise, w, h, 1);
                    _gi_compute_shader->SetTexture(_kernel_denoise, "_PathTraceRaw",
                                                   graph.Resolve<Texture>(_raw_path_trace_handle));
                    _gi_compute_shader->SetTexture(_kernel_denoise, "_HistoryPrev",
                                                   graph.Resolve<Texture>(_history_prev_handle));
                    _gi_compute_shader->SetTexture(_kernel_denoise, "_HistoryCurr",
                                                   graph.Resolve<Texture>(_history_curr_handle));
                    _gi_compute_shader->SetTexture(_kernel_denoise, "_MotionVectorTexture",
                                                   graph.Resolve<Texture>(data._rg_handles._motion_vector_tex));
                    _gi_compute_shader->SetTexture(_kernel_denoise, "_GBuffer0",
                                                   graph.Resolve<Texture>(data._rg_handles._gbuffers[0]));
                    _gi_compute_shader->SetTexture(_kernel_denoise, "_CameraDepthTexture",
                                                   graph.Resolve<Texture>(data._rg_handles._depth_tex));
                    _gi_compute_shader->SetVector("_CameraUvJitter", camera_uv_jitter);
                    _gi_compute_shader->SetTexture(_kernel_denoise, "_TemporalDebug",
                                                   graph.Resolve<Texture>(_temporal_debug_handle));
                    _gi_compute_shader->SetInt("_prev_surface_buffer_idx",
                                               prev_surface_buffer != nullptr ? prev_surface_buffer->GetBindlessUAVIndex() : -1);
                    _gi_compute_shader->SetInt("_curr_surface_buffer_idx",
                                               curr_surface_buffer != nullptr ? curr_surface_buffer->GetBindlessUAVIndex() : -1);
                    cmd->Dispatch(_gi_compute_shader.get(), _kernel_denoise, x, y, 1);
                    _temporal_history_valid = true;
                    _previous_uv_jitter = current_uv_jitter;
                    _is_history_a = !_is_history_a;
                    _is_cur_a = !_is_cur_a;
                } });
            }
            RDG::RGHandle final_target_handle = accumulation_enabled ? _history_curr_handle : _raw_path_trace_handle;
            graph.AddPass("Debug GI", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                          {
                    builder.Read(final_target_handle);
                    builder.Read(_raw_path_trace_handle);
                    builder.Read(_history_prev_handle);
                    builder.Read(_history_curr_handle);
                    if (accumulation_enabled && _debug_temporal_target >= 4u)
                        builder.Read(_temporal_debug_handle);
                    rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target); 
                    }, [this, accumulation_enabled](RDG::RenderGraph &graph, CommandBuffer *cmd,
                                                     const RenderingData &data)
                { 
                    RDG::RGHandle display_target = accumulation_enabled ? _history_curr_handle : _raw_path_trace_handle;
                    if (_debug_temporal_target == 1u)
                        display_target = _raw_path_trace_handle;
                    else if (_debug_temporal_target == 2u)
                        display_target = _history_prev_handle;
                    else if (_debug_temporal_target == 3u)
                        display_target = _history_curr_handle;
                    else if (accumulation_enabled && _debug_temporal_target >= 4u)
                        display_target = _temporal_debug_handle;
                    cmd->Blit(display_target, data._rg_handles._color_target);
                });
            if (!use_hardware_ray_tracing)
            {
                RDG::RGHandle debug_arg_handle;
                graph.AddPass("Debug Ray CopyCounter", RDG::PassDesc(RDG::EPassType::kCopy), [&, this](RDG::RenderGraphBuilder &builder)
                              {
                        builder.Read(builder.Import(_debug_buffer.get()), EResourceUsage::kCopySrc);
                        debug_arg_handle = builder.Write(builder.Import(_arg_buffer.get()), EResourceUsage::kCopyDst);
                    },
                    [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                              {
                        cmd->CopyCounterValue(&*_debug_buffer, &*_arg_buffer, offsetof(DrawArguments, DrawArguments::_vertex_count_per_instance));
                    });
                graph.AddPass("Debug Ray", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                              {
                        builder.Read(builder.Import(_debug_buffer.get()));
                        builder.Read(debug_arg_handle, EResourceUsage::kIndirectArgument);
                        builder.Read(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
                        rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                        rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target,EResourceUsage::kDSV);
                    }, 
                    [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                              { 
                        _debug_line_mat->SetBuffer("_ray_list", _debug_buffer.get());
                        cmd->SetRenderTargetLoadAction(data._rg_handles._color_target, ELoadStoreAction::kLoad);
                        cmd->SetRenderTargetLoadAction(data._rg_handles._depth_target, ELoadStoreAction::kLoad);
                        cmd->SetRenderTarget(data._rg_handles._color_target,data._rg_handles._depth_target);
                        cmd->DrawProceduralIndirect(&*_debug_line_mat,0,&*_arg_buffer);
                    });
            }
        }

        bool GIPass::MakesureTarget(const RenderingData &rendering_data)
        {
            bool recreated = false;
            if (_environment_distribution_texture == nullptr)
            {
                constexpr u16 kEnvironmentDistributionResolution = 64u;
                _environment_distribution_texture = RenderTexture::Create(kEnvironmentDistributionResolution * 6u,
                    kEnvironmentDistributionResolution, "RayTraceGI_EnvironmentDistribution",
                    ERenderTargetFormat::kRGBAFloat, false, true, true);
                _environment_distribution_dirty = true;
            }
            if (_raw_path_trace_texture == nullptr || _raw_path_trace_texture->Width() != rendering_data._width || _raw_path_trace_texture->Height() != rendering_data._height)
            {
                TextureDesc desc;
                desc._width = rendering_data._width;
                desc._height = rendering_data._height;
                desc._format = EALGFormat::kALGFormatR32G32B32A32_FLOAT;
                desc._is_random_access = true;
                desc._mip_num = 1;
                _raw_path_trace_texture = RenderTexture::Create(desc, "RayTraceGI_PathTraceRaw");
                _history_texture_a = RenderTexture::Create(desc, "RayTraceGI_HistoryA");
                _history_texture_b = RenderTexture::Create(desc, "RayTraceGI_HistoryB");
                _temporal_debug_texture = RenderTexture::Create(desc, "RayTraceGI_TemporalDebug");
                _is_history_a = true;
                _temporal_history_valid = false;
                recreated = true;
                BufferDesc buf_desc;
                buf_desc._element_num = rendering_data._width * rendering_data._height;
                buf_desc._element_size = 40u;//sizeof(Reservoir) restir_di.hlsli
                buf_desc._size = buf_desc._element_num * buf_desc._element_size;
                buf_desc._is_random_write = true;
                buf_desc._format = EALGFormat::kALGFormatUNKOWN;
                buf_desc._target = EGPUBufferTarget::kStructured;
                _reservoir_a = GPUBuffer::CreateSync(buf_desc);
                _reservoir_b = GPUBuffer::CreateSync(buf_desc);
                _reservoir_a->Name("RayTraceGI_ReservoirA");
                _reservoir_b->Name("RayTraceGI_ReservoirB");
                // Vector<u8> reservoir_zero(buf_desc._size, 0u);
                // _reservoir_a->SetData(reservoir_zero);
                // _reservoir_b->SetData(reservoir_zero);
                buf_desc._element_size = 64u;//sizeof(Surface) restir_di.hlsli
                buf_desc._size = buf_desc._element_num * buf_desc._element_size;
                _surface_buffer_a = GPUBuffer::CreateSync(buf_desc);
                _surface_buffer_b = GPUBuffer::CreateSync(buf_desc);
                _surface_buffer_a->Name("RayTraceGI_SurfaceBufferA");
                _surface_buffer_b->Name("RayTraceGI_SurfaceBufferB");
                // Vector<u8> surface_zero(buf_desc._size, 0u);
                // _surface_buffer_a->SetData(surface_zero);
                // _surface_buffer_b->SetData(surface_zero);
            }
            return recreated;
        }
#pragma endregion
    }// namespace Render
} // namespace Ailu
