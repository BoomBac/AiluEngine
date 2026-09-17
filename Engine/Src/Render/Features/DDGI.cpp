#include "Render/Features/DDGI.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Math/MatrixMath.h"
#include "Framework/Math/Transform.h"
#include "Render/CommandBuffer.h"
#include "Render/Camera.h"
#include "Render/DDGI/DDGIProbeLayout.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/Renderer.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Ailu::Render
{
    namespace
    {
        constexpr f32 kDefaultMaxRayDistance = 50.0f;
        constexpr f32 kDefaultDebugProbeRadius = 0.12f;
        constexpr f32 kDefaultDebugRayLength = 4.0f;
        constexpr f32 kDefaultRandomRayBackfaceThreshold = 0.10f;
        constexpr u32 kDefaultDebugMode = 2u;
        constexpr u32 kMaxDispatchX = std::numeric_limits<u16>::max();

        struct ProbeDispatchData
        {
            DDGIVolumeRenderData _volume;
            Ref<GPUBuffer> _probe_ray_data;
            Ref<GPUBuffer> _probe_data;
            u32 _probe_count = 0u;
        };

        struct RelocationDispatchData
        {
            DDGIVolumeRenderData _volume;
            Ref<GPUBuffer> _probe_ray_data;
            Ref<GPUBuffer> _probe_data;
            u32 _probe_count = 0u;
            RDG::RGHandle _probe_ray_data_handle;
            RDG::RGHandle _probe_data_handle;
        };

        struct RadianceDispatchData
        {
            DDGIVolumeRenderData _volume;
            Ref<GPUBuffer> _probe_ray_data;
            Ref<GPUBuffer> _probe_data;
            Ref<Texture2D> _radiance_texture;
            Ref<Texture2D> _radiance_history_texture;
            bool _history_valid = false;
            u32 _probe_count = 0u;
            RDG::RGHandle _probe_ray_data_handle;
            RDG::RGHandle _probe_data_handle;
            RDG::RGHandle _radiance_texture_handle;
            RDG::RGHandle _radiance_history_texture_handle;
        };

        struct BorderDispatchData
        {
            DDGIVolumeRenderData _volume;
            Ref<Texture2D> _radiance_texture;
            u32 _probe_count = 0u;
            RDG::RGHandle _radiance_texture_handle;
        };

        struct DistanceDispatchData
        {
            DDGIVolumeRenderData _volume;
            Ref<GPUBuffer> _probe_ray_data;
            Ref<GPUBuffer> _probe_data;
            Ref<Texture2D> _distance_texture;
            Ref<Texture2D> _distance_history_texture;
            bool _history_valid = false;
            u32 _probe_count = 0u;
            RDG::RGHandle _probe_ray_data_handle;
            RDG::RGHandle _probe_data_handle;
            RDG::RGHandle _distance_texture_handle;
            RDG::RGHandle _distance_history_texture_handle;
        };

        struct DistanceBorderDispatchData
        {
            DDGIVolumeRenderData _volume;
            Ref<Texture2D> _distance_texture;
            u32 _probe_count = 0u;
            RDG::RGHandle _distance_texture_handle;
        };

        struct DebugDispatchData
        {
            DDGIVolumeRenderData _volume;
            Ref<GPUBuffer> _probe_ray_data;
            Ref<GPUBuffer> _probe_data;
            Ref<Texture2D> _radiance_texture;
            Ref<Texture2D> _distance_texture;
            u32 _probe_count = 0u;
            u32 _ray_count = 0u;
            RDG::RGHandle _probe_ray_data_handle;
            RDG::RGHandle _probe_data_handle;
            RDG::RGHandle _radiance_texture_handle;
            RDG::RGHandle _distance_texture_handle;
        };

        Matrix4x4f BuildDDGIVolumeTransform(const ECS::TransformComponent &transform)
        {
            const Transform world_transform(
                transform.GetRenderPosition(), transform.GetRenderRotation(), transform.GetRenderScale());
            return Transform::ToMatrix(world_transform);
        }

        template<typename T>
        f32 ReadMaxRayDistance(const T &volume)
        {
            if constexpr (requires { volume._max_ray_distance; })
                return volume._max_ray_distance;
            return kDefaultMaxRayDistance;
        }

        template<typename T>
        bool ReadDrawProbeRays(const T &volume)
        {
            if constexpr (requires { volume._draw_probe_rays; })
                return volume._draw_probe_rays;
            return true;
        }

        template<typename T>
        bool ReadFreezeRayRotation(const T &volume)
        {
            if constexpr (requires { volume._freeze_ray_rotation; })
                return volume._freeze_ray_rotation;
            return false;
        }

        template<typename T>
        f32 ReadDebugProbeRadius(const T &volume)
        {
            if constexpr (requires { volume._debug_probe_radius; })
                return volume._debug_probe_radius;
            return kDefaultDebugProbeRadius;
        }

        template<typename T>
        f32 ReadDebugRayLength(const T &volume)
        {
            if constexpr (requires { volume._debug_ray_length; })
                return volume._debug_ray_length;
            return kDefaultDebugRayLength;
        }

        template<typename T>
        f32 ReadRandomRayBackfaceThreshold(const T &volume)
        {
            if constexpr (requires { volume._random_ray_backface_threshold; })
                return volume._random_ray_backface_threshold;
            return kDefaultRandomRayBackfaceThreshold;
        }

        template<typename T>
        u32 ReadDebugMode(const T &volume)
        {
            if constexpr (requires { volume._debug_mode; })
                return static_cast<u32>(volume._debug_mode);
            return kDefaultDebugMode;
        }

        bool IsValidPositiveFinite(f32 value)
        {
            return std::isfinite(value) && value > 0.0f;
        }

        // A DDGI shader that fails to load or expose its kernel silently disables the corresponding probe
        // update step (and, before, the temporal swap), so report it instead of leaving the feature half
        // working with no diagnostics.
        ComputeShaderKernelId FindRequiredKernel(const AssetRef<ComputeShader> &shader, const char *kernel_name,
                                                 const char *asset_name)
        {
            if (shader == nullptr)
            {
                LOG_ERROR("DDGI: failed to load compute shader {}", asset_name);
                return kInvalidComputeShaderKernelId;
            }
            const ComputeShaderKernelId kernel = shader->FindKernel(kernel_name);
            if (kernel == kInvalidComputeShaderKernelId)
                LOG_ERROR("DDGI: kernel {} is missing from {}", kernel_name, asset_name);
            return kernel;
        }

        bool CalculateRadianceAtlasLayout(const Vector3UInt &probe_count, u32 &tiles_x, u32 &tiles_y,
                                          u32 &width, u32 &height)
        {
            if (probe_count.x == 0u || probe_count.y == 0u || probe_count.z == 0u)
                return false;

            const u64 tiles_y64 = static_cast<u64>(probe_count.y) * probe_count.z;
            const u64 width64 = static_cast<u64>(probe_count.x) * DDGI::kRadiancePhysicalResolution;
            const u64 height64 = tiles_y64 * DDGI::kRadiancePhysicalResolution;
            if (tiles_y64 > std::numeric_limits<u32>::max() || width64 > std::numeric_limits<u16>::max() ||
                height64 > std::numeric_limits<u16>::max())
            {
                return false;
            }

            tiles_x = probe_count.x;
            tiles_y = static_cast<u32>(tiles_y64);
            width = static_cast<u32>(width64);
            height = static_cast<u32>(height64);
            return true;
        }

        bool CalculateDistanceAtlasLayout(const Vector3UInt &probe_count, u32 &tiles_x, u32 &tiles_y,
                                          u32 &width, u32 &height)
        {
            if (probe_count.x == 0u || probe_count.y == 0u || probe_count.z == 0u)
                return false;

            const u64 tiles_y64 = static_cast<u64>(probe_count.y) * probe_count.z;
            const u64 width64 = static_cast<u64>(probe_count.x) * DDGI::kDistancePhysicalResolution;
            const u64 height64 = tiles_y64 * DDGI::kDistancePhysicalResolution;
            if (tiles_y64 > std::numeric_limits<u32>::max() || width64 > std::numeric_limits<u16>::max() ||
                height64 > std::numeric_limits<u16>::max())
            {
                return false;
            }

            tiles_x = probe_count.x;
            tiles_y = static_cast<u32>(tiles_y64);
            width = static_cast<u32>(width64);
            height = static_cast<u32>(height64);
            return true;
        }
    }

    DDGIProbeTracePass::DDGIProbeTracePass(DDGIFeature &feature) : RenderPass("DDGI Probe Trace"), _feature(&feature)
    {
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }

    void DDGIProbeTracePass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr || _feature->_compute_shader == nullptr ||
            _feature->_probe_trace_kernel == kInvalidComputeShaderKernelId || _feature->_volume_runtimes.empty())
        {
            return;
        }

        auto *proxy = _feature->_scene_rt_proxy.get();
        const bool use_inline_ray_tracing = _feature->_use_inline_ray_tracing;
        RayTracingScene *rt_scene = use_inline_ray_tracing && proxy != nullptr ? proxy->GetScene() : nullptr;
        if (use_inline_ray_tracing && (proxy == nullptr || rt_scene == nullptr))
            return;

        GPUBuffer *primitive_data = rendering_data._scene_primitive_buffer;
        GPUBuffer *material_data = rendering_data._material_buffer;
        if (primitive_data == nullptr || material_data == nullptr)
            return;

        GPUBuffer *unified_light_data = proxy != nullptr ? proxy->GetUnifiedLightData() : nullptr;
        const u32 unified_light_count = proxy != nullptr ? proxy->GetUnifiedLightCount() : 0u;

        GPUBuffer *scene_triangle_data = nullptr;
        GPUBuffer *scene_tlas_data = nullptr;
        GPUBuffer *scene_blas_data = nullptr;
        if (!use_inline_ray_tracing)
        {
            auto *scene = rendering_data._scene;
            if (scene == nullptr || scene->TriangleCount() == 0u)
                return;

            scene_triangle_data = scene->GetSceneMeshDataBuffer();
            scene_tlas_data = scene->GetTLASBuffer();
            scene_blas_data = scene->GetBLASBuffer();
            if (scene_triangle_data == nullptr || scene_tlas_data == nullptr || scene_blas_data == nullptr)
                return;
        }

        Vector<ProbeDispatchData> dispatches;
        dispatches.reserve(_feature->_volume_runtimes.size());
        for (const auto &[entity, runtime] : _feature->_volume_runtimes)
        {
            (void) entity;
            if (runtime._probe_ray_data == nullptr || runtime._probe_data == nullptr || runtime._probe_count == 0u)
                continue;
            dispatches.emplace_back(ProbeDispatchData{runtime._desc, runtime._probe_ray_data, runtime._probe_data,
                                                       runtime._probe_count});
        }
        if (dispatches.empty())
            return;

        if (use_inline_ray_tracing)
        {
            proxy->AddBLASUpdatePass(graph);
            proxy->AddTLASUpdatePass(graph);
        }

        graph.AddPass("DDGI Probe Trace", RDG::PassDesc(RDG::EPassType::kCompute),
            [dispatches, use_inline_ray_tracing, rt_scene, proxy, primitive_data, material_data,
             scene_triangle_data, scene_tlas_data, scene_blas_data, unified_light_data, unified_light_count]
            (RDG::RenderGraphBuilder &builder)
            {
                if (use_inline_ray_tracing)
                {
                    builder.Read(builder.Import(rt_scene), EResourceUsage::kAccelStructRead);
                    proxy->AddTraceResourceReads(builder);
                }
                else
                {
                    builder.Read(builder.Import(scene_triangle_data), EResourceUsage::kReadSRV);
                    builder.Read(builder.Import(scene_tlas_data), EResourceUsage::kReadSRV);
                    builder.Read(builder.Import(scene_blas_data), EResourceUsage::kReadSRV);
                }
                builder.Read(builder.Import(primitive_data), EResourceUsage::kReadSRV);
                builder.Read(builder.Import(material_data), EResourceUsage::kReadSRV);
                if (unified_light_data != nullptr)
                    builder.Read(builder.Import(unified_light_data), EResourceUsage::kReadSRV);

                for (const auto &dispatch : dispatches)
                {
                    builder.Read(builder.Import(dispatch._probe_data.get()), EResourceUsage::kReadSRV);
                    (void) builder.Write(builder.Import(dispatch._probe_ray_data.get()), EResourceUsage::kWriteUAV);
                }
            },
            [feature = _feature, dispatches, use_inline_ray_tracing, rt_scene, primitive_data, material_data,
             unified_light_data, unified_light_count]
            (RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
            {
                auto *shader = feature->_compute_shader.get();
                auto kernel = feature->_probe_trace_kernel;
                if (shader == nullptr || cmd == nullptr)
                    return;

                if (use_inline_ray_tracing)
                {
                    if (rt_scene == nullptr || !rt_scene->IsReady())
                        return;
                    shader->SetAccelerationStructure(kernel, "g_ray_tracing_scene", rt_scene);
                }
                shader->SetBuffer(kernel, "g_primitive_data", primitive_data);
                shader->SetBuffer(kernel, "g_material_buffer", material_data);
                if (unified_light_data != nullptr)
                    shader->SetBuffer(kernel, "_UnifiedLights", unified_light_data);

                for (const auto &dispatch : dispatches)
                {
                    const auto &volume = dispatch._volume;
                    shader->SetMatrix("_volume_local_to_world", volume._local_to_world);
                    shader->SetInt("_probe_count_x", static_cast<i32>(volume._probe_count.x));
                    shader->SetInt("_probe_count_y", static_cast<i32>(volume._probe_count.y));
                    shader->SetInt("_probe_count_z", static_cast<i32>(volume._probe_count.z));
                    shader->SetInt("_rays_per_probe", static_cast<i32>(DDGI::kRaysPerProbe));
                    const u64 frame = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
                    const bool freeze_ray_rotation = volume._freeze_ray_rotation || !feature->_use_random_rotation;
                    shader->SetInt("_frame_index", static_cast<i32>(frame));
                    shader->SetInt("_freeze_ray_rotation", freeze_ray_rotation ? 1 : 0);
                    shader->SetInt("_use_probe_relocation", volume._use_probe_relocation ? 1 : 0);
                    shader->SetInt("_unified_light_count", static_cast<i32>(unified_light_count));
                    shader->SetVector("_probe_spacing_and_distance",
                                      Vector4f(volume._probe_spacing.x, volume._probe_spacing.y,
                                               volume._probe_spacing.z, volume._max_ray_distance));
                    // Keep the existing environment radiance for miss rays; front-face hits use Unified Lights.
                    shader->SetVector("_environment_radiance_intensity", Vector4f::kOne * 0.1);
                    shader->SetBuffer(kernel, "_probe_ray_data", dispatch._probe_ray_data.get());
                    shader->SetBuffer(kernel, "_probe_data", dispatch._probe_data.get());
                    if (dispatch._probe_count > kMaxDispatchX)
                    {
                        LOG_ERROR("DDGI volume {} has {} probes; Dispatch X supports at most {} groups",
                                  volume._entity, dispatch._probe_count, kMaxDispatchX);
                        continue;
                    }
                    cmd->Dispatch(shader, kernel, static_cast<u16>(dispatch._probe_count), 1u, 1u);
                }
            });
    }

    DDGIProbeRadiancePass::DDGIProbeRadiancePass(DDGIFeature &feature) : RenderPass("DDGI Probe Radiance"),
                                                                         _feature(&feature)
    {
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }

    void DDGIProbeRadiancePass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr || _feature->_radiance_compute_shader == nullptr ||
            _feature->_probe_radiance_kernel == kInvalidComputeShaderKernelId || _feature->_volume_runtimes.empty())
        {
            return;
        }

        Vector<RadianceDispatchData> dispatches;
        dispatches.reserve(_feature->_volume_runtimes.size());
        for (const auto &[entity, runtime] : _feature->_volume_runtimes)
        {
            (void) entity;
            if (runtime._probe_ray_data == nullptr || runtime._probe_data == nullptr ||
                runtime._radiance_texture == nullptr || runtime._probe_count == 0u)
                continue;

            RadianceDispatchData dispatch;
            dispatch._volume = runtime._desc;
            dispatch._probe_ray_data = runtime._probe_ray_data;
            dispatch._probe_data = runtime._probe_data;
            dispatch._radiance_texture = runtime._radiance_texture;
            dispatch._radiance_history_texture = runtime._radiance_history_texture;
            dispatch._history_valid = runtime._temporal_history_valid;
            dispatch._probe_count = runtime._probe_count;
            dispatch._probe_ray_data_handle = graph.Import(runtime._probe_ray_data.get());
            dispatch._probe_data_handle = graph.Import(runtime._probe_data.get());
            dispatch._radiance_texture_handle = graph.Import(runtime._radiance_texture.get());
            if (dispatch._radiance_history_texture != nullptr)
                dispatch._radiance_history_texture_handle = graph.Import(dispatch._radiance_history_texture.get());
            dispatches.emplace_back(std::move(dispatch));
        }
        if (dispatches.empty())
            return;

        graph.AddPass("DDGI Probe Radiance", RDG::PassDesc(RDG::EPassType::kCompute),
            [dispatches, &rendering_data](RDG::RenderGraphBuilder &builder)
            {
                for (const auto &dispatch : dispatches)
                {
                    builder.Read(dispatch._probe_ray_data_handle, EResourceUsage::kReadSRV);
                    builder.Read(dispatch._probe_data_handle, EResourceUsage::kWriteUAV);
                    const auto probe_data_output = builder.Write(dispatch._probe_data_handle, EResourceUsage::kWriteUAV);
                    if (rendering_data._ddgi_lighting._probe_data == dispatch._probe_data)
                        rendering_data._ddgi_lighting._probe_data_handle = probe_data_output;
                    if (dispatch._history_valid && dispatch._radiance_history_texture_handle.IsValid())
                        builder.Read(dispatch._radiance_history_texture_handle, EResourceUsage::kReadSRV);
                    const auto output_handle = builder.Write(dispatch._radiance_texture_handle,
                                                             EResourceUsage::kWriteUAV);
                    if (rendering_data._ddgi_lighting._irradiance_atlas_handle == dispatch._radiance_texture_handle)
                        rendering_data._ddgi_lighting._irradiance_atlas_handle = output_handle;
                }
            },
            [feature = _feature, dispatches](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
            {
                auto *shader = feature->_radiance_compute_shader.get();
                const auto kernel = feature->_probe_radiance_kernel;
                if (shader == nullptr || cmd == nullptr || kernel == kInvalidComputeShaderKernelId)
                    return;

                const u64 frame = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
                for (const auto &dispatch : dispatches)
                {
                    const auto &volume = dispatch._volume;
                    if (dispatch._probe_count > kMaxDispatchX)
                    {
                        LOG_ERROR("DDGI volume {} has {} probes; radiance Dispatch X supports at most {} groups",
                                  volume._entity, dispatch._probe_count, kMaxDispatchX);
                        continue;
                    }

                    shader->SetInt("_probe_count_x", static_cast<i32>(volume._probe_count.x));
                    shader->SetInt("_probe_count_y", static_cast<i32>(volume._probe_count.y));
                    shader->SetInt("_probe_count_z", static_cast<i32>(volume._probe_count.z));
                    const bool freeze_ray_rotation = volume._freeze_ray_rotation || !feature->_use_random_rotation;
                    shader->SetInt("_frame_index", freeze_ray_rotation ? 0 : static_cast<i32>(frame));
                    shader->SetInt("_freeze_ray_rotation", freeze_ray_rotation ? 1 : 0);
                    shader->SetInt("_use_temporal", feature->_use_temporal ? 1 : 0);
                    shader->SetInt("_history_valid", dispatch._history_valid ? 1 : 0);
                    shader->SetFloat("_hysteresis", feature->_hysteresis);
                    shader->SetFloat("_history_step_ratio", feature->_history_step_ratio);
                    shader->SetInt("_use_probe_relocation", volume._use_probe_relocation ? 1 : 0);
                    shader->SetFloat("_max_ray_distance", volume._max_ray_distance);
                    shader->SetFloat("_random_ray_backface_threshold", volume._random_ray_backface_threshold);
                    shader->SetBuffer(kernel, "_probe_ray_data", dispatch._probe_ray_data.get());
                    shader->SetBuffer(kernel, "_probe_data", dispatch._probe_data.get());
                    shader->SetTexture(kernel, "_radiance_history_texture", dispatch._radiance_history_texture.get());
                    shader->SetTexture(kernel, "_radiance_texture", dispatch._radiance_texture.get());
                    cmd->Dispatch(shader, kernel, static_cast<u16>(dispatch._probe_count), 1u, 1u);
                }
            });
    }

    DDGIProbeBorderPass::DDGIProbeBorderPass(DDGIFeature &feature) : RenderPass("DDGI Probe Border"), _feature(&feature)
    {
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }

    void DDGIProbeBorderPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr || _feature->_border_compute_shader == nullptr ||
            _feature->_probe_border_kernel == kInvalidComputeShaderKernelId || _feature->_volume_runtimes.empty())
        {
            return;
        }

        Vector<BorderDispatchData> dispatches;
        dispatches.reserve(_feature->_volume_runtimes.size());
        for (const auto &[entity, runtime] : _feature->_volume_runtimes)
        {
            (void) entity;
            if (runtime._radiance_texture == nullptr || runtime._probe_count == 0u)
                continue;

            BorderDispatchData dispatch;
            dispatch._volume = runtime._desc;
            dispatch._radiance_texture = runtime._radiance_texture;
            dispatch._probe_count = runtime._probe_count;
            dispatch._radiance_texture_handle = graph.Import(runtime._radiance_texture.get());
            if (rendering_data._ddgi_lighting._irradiance_atlas_texture == runtime._radiance_texture)
                dispatch._radiance_texture_handle = rendering_data._ddgi_lighting._irradiance_atlas_handle;
            dispatches.emplace_back(std::move(dispatch));
        }
        if (dispatches.empty())
            return;

        graph.AddPass("DDGI Probe Border", RDG::PassDesc(RDG::EPassType::kCompute),
            [dispatches, &rendering_data](RDG::RenderGraphBuilder &builder)
            {
                for (const auto &dispatch : dispatches)
                {
                    // The border shader reads the interior through the UAV and writes disjoint texels.
                    builder.Read(dispatch._radiance_texture_handle, EResourceUsage::kWriteUAV);
                    const auto output_handle = builder.Write(dispatch._radiance_texture_handle,
                                                             EResourceUsage::kWriteUAV);
                    if (rendering_data._ddgi_lighting._irradiance_atlas_handle == dispatch._radiance_texture_handle)
                        rendering_data._ddgi_lighting._irradiance_atlas_handle = output_handle;
                }
            },
            [feature = _feature, dispatches](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
            {
                auto *shader = feature->_border_compute_shader.get();
                const auto kernel = feature->_probe_border_kernel;
                if (shader == nullptr || cmd == nullptr || kernel == kInvalidComputeShaderKernelId)
                    return;

                for (const auto &dispatch : dispatches)
                {
                    const auto &volume = dispatch._volume;
                    if (dispatch._probe_count > kMaxDispatchX)
                    {
                        LOG_ERROR("DDGI volume {} has {} probes; border Dispatch X supports at most {} groups",
                                  volume._entity, dispatch._probe_count, kMaxDispatchX);
                        continue;
                    }

                    shader->SetInt("_probe_count_x", static_cast<i32>(volume._probe_count.x));
                    shader->SetInt("_probe_count_y", static_cast<i32>(volume._probe_count.y));
                    shader->SetInt("_probe_count_z", static_cast<i32>(volume._probe_count.z));
                    shader->SetTexture(kernel, "_radiance_texture", dispatch._radiance_texture.get());
                    cmd->Dispatch(shader, kernel, static_cast<u16>(dispatch._probe_count), 1u, 1u);
                }
            });
    }

    DDGIProbeDistancePass::DDGIProbeDistancePass(DDGIFeature &feature) :
        RenderPass("DDGI Probe Distance"), _feature(&feature)
    {
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }

    void DDGIProbeDistancePass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr || _feature->_distance_compute_shader == nullptr ||
            _feature->_probe_distance_kernel == kInvalidComputeShaderKernelId || _feature->_volume_runtimes.empty())
        {
            return;
        }

        Vector<DistanceDispatchData> dispatches;
        dispatches.reserve(_feature->_volume_runtimes.size());
        for (const auto &[entity, runtime] : _feature->_volume_runtimes)
        {
            (void) entity;
            if (runtime._probe_ray_data == nullptr || runtime._probe_data == nullptr ||
                runtime._distance_texture == nullptr || runtime._probe_count == 0u)
                continue;

            DistanceDispatchData dispatch;
            dispatch._volume = runtime._desc;
            dispatch._probe_ray_data = runtime._probe_ray_data;
            dispatch._probe_data = runtime._probe_data;
            dispatch._distance_texture = runtime._distance_texture;
            dispatch._distance_history_texture = runtime._distance_history_texture;
            dispatch._history_valid = runtime._temporal_history_valid;
            dispatch._probe_count = runtime._probe_count;
            dispatch._probe_ray_data_handle = graph.Import(runtime._probe_ray_data.get());
            dispatch._probe_data_handle = graph.Import(runtime._probe_data.get());
            dispatch._distance_texture_handle = graph.Import(runtime._distance_texture.get());
            if (dispatch._distance_history_texture != nullptr)
                dispatch._distance_history_texture_handle = graph.Import(dispatch._distance_history_texture.get());
            if (dispatch._probe_data == rendering_data._ddgi_lighting._probe_data &&
                rendering_data._ddgi_lighting._probe_data_handle.IsValid())
            {
                dispatch._probe_data_handle = rendering_data._ddgi_lighting._probe_data_handle;
            }
            if (rendering_data._ddgi_lighting._distance_atlas_texture == runtime._distance_texture)
                dispatch._distance_texture_handle = rendering_data._ddgi_lighting._distance_atlas_handle;
            dispatches.emplace_back(std::move(dispatch));
        }
        if (dispatches.empty())
            return;

        graph.AddPass("DDGI Probe Distance", RDG::PassDesc(RDG::EPassType::kCompute),
            [dispatches, &rendering_data](RDG::RenderGraphBuilder &builder)
            {
                for (const auto &dispatch : dispatches)
                {
                    builder.Read(dispatch._probe_ray_data_handle, EResourceUsage::kReadSRV);
                    builder.Read(dispatch._probe_data_handle, EResourceUsage::kWriteUAV);
                    const auto probe_data_output = builder.Write(dispatch._probe_data_handle,
                                                                  EResourceUsage::kWriteUAV);
                    if (rendering_data._ddgi_lighting._probe_data == dispatch._probe_data)
                        rendering_data._ddgi_lighting._probe_data_handle = probe_data_output;
                    if (dispatch._history_valid && dispatch._distance_history_texture_handle.IsValid())
                        builder.Read(dispatch._distance_history_texture_handle, EResourceUsage::kReadSRV);
                    const auto output_handle = builder.Write(dispatch._distance_texture_handle,
                                                             EResourceUsage::kWriteUAV);
                    if (rendering_data._ddgi_lighting._distance_atlas_handle == dispatch._distance_texture_handle)
                        rendering_data._ddgi_lighting._distance_atlas_handle = output_handle;
                }
            },
            [feature = _feature, dispatches](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
            {
                auto *shader = feature->_distance_compute_shader.get();
                const auto kernel = feature->_probe_distance_kernel;
                if (shader == nullptr || cmd == nullptr || kernel == kInvalidComputeShaderKernelId)
                    return;

                for (const auto &dispatch : dispatches)
                {
                    const auto &volume = dispatch._volume;
                    if (dispatch._probe_count > kMaxDispatchX)
                    {
                        LOG_ERROR("DDGI volume {} has {} probes; distance Dispatch X supports at most {} groups",
                                  volume._entity, dispatch._probe_count, kMaxDispatchX);
                        continue;
                    }

                    shader->SetInt("_probe_count_x", static_cast<i32>(volume._probe_count.x));
                    shader->SetInt("_probe_count_y", static_cast<i32>(volume._probe_count.y));
                    shader->SetInt("_probe_count_z", static_cast<i32>(volume._probe_count.z));
                    const u64 frame = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
                    const bool freeze_ray_rotation = volume._freeze_ray_rotation || !feature->_use_random_rotation;
                    shader->SetInt("_frame_index", freeze_ray_rotation ? 0 : static_cast<i32>(frame));
                    shader->SetInt("_freeze_ray_rotation", freeze_ray_rotation ? 1 : 0);
                    shader->SetInt("_use_temporal", feature->_use_temporal ? 1 : 0);
                    shader->SetInt("_history_valid", dispatch._history_valid ? 1 : 0);
                    shader->SetFloat("_hysteresis", feature->_hysteresis);
                    shader->SetFloat("_history_step_ratio", feature->_history_step_ratio);
                    shader->SetInt("_use_probe_relocation", volume._use_probe_relocation ? 1 : 0);
                    shader->SetFloat("_max_ray_distance", volume._max_ray_distance);
                    shader->SetBuffer(kernel, "_probe_ray_data", dispatch._probe_ray_data.get());
                    shader->SetBuffer(kernel, "_probe_data", dispatch._probe_data.get());
                    shader->SetTexture(kernel, "_distance_history_texture", dispatch._distance_history_texture.get());
                    shader->SetTexture(kernel, "_distance_texture", dispatch._distance_texture.get());
                    cmd->Dispatch(shader, kernel, static_cast<u16>(dispatch._probe_count), 1u, 1u);
                }
            });
    }

    DDGIProbeDistanceBorderPass::DDGIProbeDistanceBorderPass(DDGIFeature &feature) :
        RenderPass("DDGI Probe Distance Border"), _feature(&feature)
    {
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }

    void DDGIProbeDistanceBorderPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr || _feature->_distance_border_compute_shader == nullptr ||
            _feature->_probe_distance_border_kernel == kInvalidComputeShaderKernelId ||
            _feature->_volume_runtimes.empty())
        {
            return;
        }

        Vector<DistanceBorderDispatchData> dispatches;
        dispatches.reserve(_feature->_volume_runtimes.size());
        for (const auto &[entity, runtime] : _feature->_volume_runtimes)
        {
            (void) entity;
            if (runtime._distance_texture == nullptr || runtime._probe_count == 0u)
                continue;

            DistanceBorderDispatchData dispatch;
            dispatch._volume = runtime._desc;
            dispatch._distance_texture = runtime._distance_texture;
            dispatch._probe_count = runtime._probe_count;
            dispatch._distance_texture_handle = graph.Import(runtime._distance_texture.get());
            if (rendering_data._ddgi_lighting._distance_atlas_texture == runtime._distance_texture)
                dispatch._distance_texture_handle = rendering_data._ddgi_lighting._distance_atlas_handle;
            dispatches.emplace_back(std::move(dispatch));
        }
        if (dispatches.empty())
            return;

        graph.AddPass("DDGI Probe Distance Border", RDG::PassDesc(RDG::EPassType::kCompute),
            [dispatches, &rendering_data](RDG::RenderGraphBuilder &builder)
            {
                for (const auto &dispatch : dispatches)
                {
                    // The border shader reads the interior through the UAV and writes disjoint texels.
                    builder.Read(dispatch._distance_texture_handle, EResourceUsage::kWriteUAV);
                    const auto output_handle = builder.Write(dispatch._distance_texture_handle,
                                                             EResourceUsage::kWriteUAV);
                    if (rendering_data._ddgi_lighting._distance_atlas_handle == dispatch._distance_texture_handle)
                        rendering_data._ddgi_lighting._distance_atlas_handle = output_handle;
                }
            },
            [feature = _feature, dispatches](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
            {
                auto *shader = feature->_distance_border_compute_shader.get();
                const auto kernel = feature->_probe_distance_border_kernel;
                if (shader == nullptr || cmd == nullptr || kernel == kInvalidComputeShaderKernelId)
                    return;

                for (const auto &dispatch : dispatches)
                {
                    const auto &volume = dispatch._volume;
                    if (dispatch._probe_count > kMaxDispatchX)
                    {
                        LOG_ERROR("DDGI volume {} has {} probes; distance border Dispatch X supports at most {} groups",
                                  volume._entity, dispatch._probe_count, kMaxDispatchX);
                        continue;
                    }

                    shader->SetInt("_probe_count_x", static_cast<i32>(volume._probe_count.x));
                    shader->SetInt("_probe_count_y", static_cast<i32>(volume._probe_count.y));
                    shader->SetInt("_probe_count_z", static_cast<i32>(volume._probe_count.z));
                    shader->SetTexture(kernel, "_distance_texture", dispatch._distance_texture.get());
                    cmd->Dispatch(shader, kernel, static_cast<u16>(dispatch._probe_count), 1u, 1u);
                }
            });
    }

    DDGIProbeRelocationPass::DDGIProbeRelocationPass(DDGIFeature &feature) :
        RenderPass("DDGI Probe Relocation"), _feature(&feature)
    {
        // Deferred lighting must consume the current offset and matching atlas data. Relocation publishes the
        // next offset after lighting, so Trace/Radiance/Distance consume it on the following frame.
        _event = ERenderPassEvent::kAfterDeferedLighting;
    }

    void DDGIProbeRelocationPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr || _feature->_relocation_compute_shader == nullptr ||
            _feature->_probe_relocation_kernel == kInvalidComputeShaderKernelId)
        {
            return;
        }

        Vector<RelocationDispatchData> dispatches;
        dispatches.reserve(_feature->_volume_runtimes.size());
        for (const auto &[entity, runtime] : _feature->_volume_runtimes)
        {
            (void) entity;
            if (!runtime._desc._use_probe_relocation || runtime._probe_ray_data == nullptr ||
                runtime._probe_data == nullptr || runtime._probe_count == 0u)
            {
                continue;
            }

            RelocationDispatchData dispatch;
            dispatch._volume = runtime._desc;
            dispatch._probe_ray_data = runtime._probe_ray_data;
            dispatch._probe_data = runtime._probe_data;
            dispatch._probe_count = runtime._probe_count;
            dispatch._probe_ray_data_handle = graph.Import(runtime._probe_ray_data.get());
            dispatch._probe_data_handle = graph.Import(runtime._probe_data.get());
            if (dispatch._probe_data == rendering_data._ddgi_lighting._probe_data &&
                rendering_data._ddgi_lighting._probe_data_handle.IsValid())
            {
                dispatch._probe_data_handle = rendering_data._ddgi_lighting._probe_data_handle;
            }
            dispatches.emplace_back(std::move(dispatch));
        }
        if (dispatches.empty())
            return;

        graph.AddPass("DDGI Probe Relocation", RDG::PassDesc(RDG::EPassType::kCompute),
            [dispatches, &rendering_data](RDG::RenderGraphBuilder &builder)
            {
                for (const auto &dispatch : dispatches)
                {
                    builder.Read(dispatch._probe_ray_data_handle, EResourceUsage::kReadSRV);
                    builder.Read(dispatch._probe_data_handle, EResourceUsage::kWriteUAV);
                    (void) builder.Write(dispatch._probe_data_handle, EResourceUsage::kWriteUAV);
                }
            },
            [feature = _feature, dispatches](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
            {
                auto *shader = feature->_relocation_compute_shader.get();
                const auto kernel = feature->_probe_relocation_kernel;
                if (shader == nullptr || cmd == nullptr || kernel == kInvalidComputeShaderKernelId)
                    return;

                for (const auto &dispatch : dispatches)
                {
                    const u64 group_count64 =
                        (static_cast<u64>(dispatch._probe_count) + DDGI::kRelocationRaysPerProbe - 1u) /
                        DDGI::kRelocationRaysPerProbe;
                    if (group_count64 > kMaxDispatchX)
                    {
                        LOG_ERROR("DDGI volume {} has {} relocation groups; Dispatch X supports at most {}",
                                  dispatch._volume._entity, group_count64, kMaxDispatchX);
                        continue;
                    }

                    const auto &volume = dispatch._volume;
                    shader->SetMatrix("_volume_world_to_local", Math::MatrixInverse(volume._local_to_world));
                    shader->SetMatrix("_volume_local_to_world", volume._local_to_world);
                    shader->SetVector("_probe_spacing", Vector4f(volume._probe_spacing.x, volume._probe_spacing.y,
                                                                    volume._probe_spacing.z, 0.0f));
                    shader->SetInt("_probe_count_x", static_cast<i32>(volume._probe_count.x));
                    shader->SetInt("_probe_count_y", static_cast<i32>(volume._probe_count.y));
                    shader->SetInt("_probe_count_z", static_cast<i32>(volume._probe_count.z));
                    shader->SetFloat("_max_ray_distance", volume._max_ray_distance);
                    shader->SetFloat("_backface_threshold", volume._relocation_backface_threshold);
                    shader->SetFloat("_min_frontface_distance_ratio",
                                     volume._relocation_min_frontface_distance_ratio);
                    shader->SetBuffer(kernel, "_probe_ray_data", dispatch._probe_ray_data.get());
                    shader->SetBuffer(kernel, "_probe_data", dispatch._probe_data.get());
                    cmd->Dispatch(shader, kernel, static_cast<u16>(group_count64), 1u, 1u);
                }
            });
    }

    DDGITemporalSwapPass::DDGITemporalSwapPass(DDGIFeature &feature) :
        RenderPass("DDGI Temporal Swap"), _feature(&feature)
    {
        _event = ERenderPassEvent::kAfterRender;
    }

    void DDGITemporalSwapPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &)
    {
        if (_feature == nullptr || _feature->_volume_runtimes.empty())
            return;

        Vector<RDG::RGHandle> output_handles;
        output_handles.reserve(_feature->_volume_runtimes.size() * 2u);
        for (const auto &[entity, runtime] : _feature->_volume_runtimes)
        {
            (void) entity;
            if (runtime._radiance_texture == nullptr || runtime._radiance_history_texture == nullptr ||
                runtime._distance_texture == nullptr || runtime._distance_history_texture == nullptr)
            {
                continue;
            }

            output_handles.emplace_back(graph.Import(runtime._radiance_texture.get()));
            output_handles.emplace_back(graph.Import(runtime._distance_texture.get()));
        }
        if (output_handles.empty())
            return;

        graph.AddPass("DDGI Temporal Swap", RDG::PassDesc(), [output_handles](RDG::RenderGraphBuilder &builder)
            {
                for (const auto handle : output_handles)
                    builder.Read(handle, EResourceUsage::kReadSRV);
            },
            [feature = _feature](RDG::RenderGraph &, CommandBuffer *, const RenderingData &)
            {
                for (auto &[entity, runtime] : feature->_volume_runtimes)
                {
                    (void) entity;
                    if (runtime._radiance_texture == nullptr || runtime._radiance_history_texture == nullptr ||
                        runtime._distance_texture == nullptr || runtime._distance_history_texture == nullptr)
                    {
                        continue;
                    }

                    auto radiance_texture = runtime._radiance_texture;
                    runtime._radiance_texture = runtime._radiance_history_texture;
                    runtime._radiance_history_texture = radiance_texture;
                    auto distance_texture = runtime._distance_texture;
                    runtime._distance_texture = runtime._distance_history_texture;
                    runtime._distance_history_texture = distance_texture;
                    runtime._temporal_history_valid = true;
                }
            });
    }

    DDGIDebugPass::DDGIDebugPass(DDGIFeature &feature) : RenderPass("DDGI Debug"), _feature(&feature)
    {
        // EditorDebugPass renders its own probe preview at kAfterPostprocess. Keep the DDGI data
        // visualization as the final scene overlay so the sampled debug colors remain visible.
        _event = ERenderPassEvent::kAfterRender;
        _debug_shader = ResourceMgr::Get().LoadAssetRef<Shader>(
            L"Shaders/hlsl/ray_trace/ddgi_probe_debug.alasset");
        RefreshDebugMaterial();
    }

    void DDGIDebugPass::RefreshDebugMaterial()
    {
        Shader *current_shader = _debug_shader.get();
        if (current_shader == nullptr)
            return;

        if (_debug_material == nullptr)
        {
            _debug_material = MakeRef<Material>(current_shader, "Runtime/DDGIProbeDebug");
        }
        else if (_debug_material->GetShader() != current_shader)
        {
            // Asset reload publishes a new Shader object.  Keep this runtime material bound to the
            // current AssetRef snapshot before IsReadyForDraw and command recording inspect it.
            _debug_material->ChangeShader(current_shader);
        }
    }

    bool DDGIDebugPass::IsAvailable() const
    {
        return _debug_material != nullptr && _debug_shader != nullptr;
    }

    void DDGIDebugPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        RefreshDebugMaterial();
        if (_feature == nullptr || !IsAvailable())
            return;

        Vector<DebugDispatchData> dispatches;
        dispatches.reserve(_feature->_volume_runtimes.size());
        for (const auto &[entity, runtime] : _feature->_volume_runtimes)
        {
            (void) entity;
            if ((!runtime._desc._draw_probes && !runtime._desc._draw_probe_rays &&
                 !runtime._desc._draw_relocation_debug) || runtime._probe_ray_data == nullptr ||
                runtime._probe_data == nullptr || runtime._probe_count == 0u)
                continue;

            const u64 ray_count = static_cast<u64>(runtime._probe_count) * DDGI::kRaysPerProbe;
            if (ray_count > std::numeric_limits<u32>::max())
                continue;

            DebugDispatchData dispatch;
            dispatch._volume = runtime._desc;
            dispatch._probe_ray_data = runtime._probe_ray_data;
            dispatch._probe_data = runtime._probe_data;
            dispatch._probe_count = runtime._probe_count;
            dispatch._ray_count = static_cast<u32>(ray_count);
            dispatch._probe_ray_data_handle = graph.Import(runtime._probe_ray_data.get());
            dispatch._probe_data_handle = graph.Import(runtime._probe_data.get());
            dispatch._radiance_texture = runtime._radiance_texture;
            if (dispatch._radiance_texture != nullptr)
                dispatch._radiance_texture_handle = graph.Import(dispatch._radiance_texture.get());
            dispatch._distance_texture = runtime._distance_texture;
            if (dispatch._distance_texture != nullptr)
                dispatch._distance_texture_handle = graph.Import(dispatch._distance_texture.get());
            if (dispatch._radiance_texture == rendering_data._ddgi_lighting._irradiance_atlas_texture &&
                rendering_data._ddgi_lighting._irradiance_atlas_handle.IsValid())
            {
                dispatch._radiance_texture_handle = rendering_data._ddgi_lighting._irradiance_atlas_handle;
            }
            if (dispatch._distance_texture == rendering_data._ddgi_lighting._distance_atlas_texture &&
                rendering_data._ddgi_lighting._distance_atlas_handle.IsValid())
            {
                dispatch._distance_texture_handle = rendering_data._ddgi_lighting._distance_atlas_handle;
            }
            dispatches.emplace_back(std::move(dispatch));
        }
        if (dispatches.empty())
            return;

        const auto color_target = rendering_data.FinalColorHandle();
        const auto depth_target = rendering_data._rg_handles._depth_target;
        graph.AddPass("DDGI Debug", RDG::PassDesc(), [&, dispatches, color_target, depth_target]
            (RDG::RenderGraphBuilder &builder)
            {
                for (const auto &dispatch : dispatches)
                {
                    builder.Read(dispatch._probe_ray_data_handle, EResourceUsage::kReadSRV);
                    builder.Read(dispatch._probe_data_handle, EResourceUsage::kReadSRV);
                    if (dispatch._radiance_texture_handle.IsValid())
                        builder.Read(dispatch._radiance_texture_handle, EResourceUsage::kReadSRV);
                    if (dispatch._distance_texture_handle.IsValid())
                        builder.Read(dispatch._distance_texture_handle, EResourceUsage::kReadSRV);
                }
                builder.Read(color_target, EResourceUsage::kWriteRTV);
                builder.Read(depth_target, EResourceUsage::kDSV);
                // 注册阶段确定叠加目标：_color_target 是场景颜色，录制阶段再读它会画进不会上屏的资源。
                rendering_data.FinalColorHandle() = builder.Write(color_target, EResourceUsage::kWriteRTV);
                rendering_data._rg_handles._depth_target = builder.Write(depth_target, EResourceUsage::kDSV);
            },
            [this, dispatches, color_target](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                if (cmd == nullptr || _debug_material == nullptr || !IsAvailable())
                    return;

                Mesh *sphere = Mesh::s_sphere.lock().get();
                if (sphere == nullptr)
                    return;

                cmd->SetRenderTarget(color_target, data._rg_handles._depth_target);
                const u64 frame = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
                for (const auto &dispatch : dispatches)
                {
                    auto *probe_ray_data = graph.Resolve<GPUBuffer>(dispatch._probe_ray_data_handle);
                    auto *probe_data = graph.Resolve<GPUBuffer>(dispatch._probe_data_handle);
                    if (probe_ray_data == nullptr || probe_data == nullptr)
                        continue;

                    const auto &volume = dispatch._volume;
                    auto *radiance_texture = dispatch._radiance_texture_handle.IsValid()
                        ? graph.Resolve<Texture>(dispatch._radiance_texture_handle) : nullptr;
                    auto *distance_texture = dispatch._distance_texture_handle.IsValid()
                        ? graph.Resolve<Texture>(dispatch._distance_texture_handle) : nullptr;
                    const bool needs_radiance = volume._debug_mode ==
                        static_cast<u32>(ECS::EDDGIProbeDebugMode::kIrradiance);
                    const bool needs_distance = volume._debug_mode ==
                        static_cast<u32>(ECS::EDDGIProbeDebugMode::kDistanceMean) ||
                        volume._debug_mode == static_cast<u32>(ECS::EDDGIProbeDebugMode::kDistanceVariance);
                    if ((needs_radiance && radiance_texture == nullptr) ||
                        (needs_distance && distance_texture == nullptr))
                    {
                        continue;
                    }
                    _debug_material->SetBuffer("_probe_ray_data", probe_ray_data);
                    _debug_material->SetBuffer("_probe_data", probe_data);
                    _debug_material->SetTexture("_radiance_texture", radiance_texture);
                    _debug_material->SetTexture("_distance_texture", distance_texture);
                    _debug_material->SetMatrix("_volume_local_to_world", volume._local_to_world);
                    _debug_material->SetInt("_probe_count_x", static_cast<i32>(volume._probe_count.x));
                    _debug_material->SetInt("_probe_count_y", static_cast<i32>(volume._probe_count.y));
                    _debug_material->SetInt("_probe_count_z", static_cast<i32>(volume._probe_count.z));
                    _debug_material->SetVector("_probe_spacing_and_distance",
                                                Vector4f(volume._probe_spacing.x, volume._probe_spacing.y,
                                                         volume._probe_spacing.z, volume._max_ray_distance));
                    _debug_material->SetFloat("_debug_probe_radius", volume._debug_probe_radius);
                    _debug_material->SetFloat("_debug_ray_length", volume._debug_ray_length);
                    _debug_material->SetInt("_debug_mode", static_cast<i32>(volume._debug_mode));
                    const bool freeze_ray_rotation = volume._freeze_ray_rotation || !_feature->_use_random_rotation;
                    _debug_material->SetInt("_frame_index", freeze_ray_rotation ? 0 : static_cast<i32>(frame));
                    _debug_material->SetInt("_freeze_ray_rotation", freeze_ray_rotation ? 1 : 0);
                    _debug_material->SetInt("_use_probe_relocation", volume._use_probe_relocation ? 1 : 0);
                    _debug_material->SetInt("_draw_relocation_debug", volume._draw_relocation_debug ? 1 : 0);
                    _debug_material->SetFloat("_random_ray_backface_threshold",
                                              volume._random_ray_backface_threshold);
                    const bool is_selected_volume = _feature->GetSelectedProbeEntity() == volume._entity;
                    const u32 selected_probe_index = is_selected_volume
                        ? _feature->GetSelectedProbeIndex() : DDGI::kInvalidProbeIndex;
                    _debug_material->SetInt("_debug_probe_index", static_cast<i32>(selected_probe_index));
                    _debug_material->SetInt("_radiance_atlas_tiles_x",
                                            static_cast<i32>(volume._radiance_atlas_tiles_x));
                    _debug_material->SetInt("_radiance_atlas_tiles_y",
                                            static_cast<i32>(volume._radiance_atlas_tiles_y));
                    _debug_material->SetInt("_radiance_atlas_width",
                                            static_cast<i32>(volume._radiance_atlas_width));
                    _debug_material->SetInt("_radiance_atlas_height",
                                            static_cast<i32>(volume._radiance_atlas_height));
                    _debug_material->SetInt("_distance_atlas_tiles_x",
                                            static_cast<i32>(volume._distance_atlas_tiles_x));
                    _debug_material->SetInt("_distance_atlas_tiles_y",
                                            static_cast<i32>(volume._distance_atlas_tiles_y));
                    _debug_material->SetInt("_distance_atlas_width",
                                            static_cast<i32>(volume._distance_atlas_width));
                    _debug_material->SetInt("_distance_atlas_height",
                                            static_cast<i32>(volume._distance_atlas_height));
                    _debug_material->SetInt("_debug_only_relocated", 0);

                    if ((volume._draw_probes || volume._draw_relocation_debug) &&
                        _debug_material->IsReadyForDraw(0u))
                    {
                        _debug_material->SetInt("_debug_only_relocated", volume._draw_probes ? 0 : 1);
                        cmd->DrawMesh(sphere, _debug_material.get(), Matrix4x4f::Identity(), 0u, 0u,
                                      dispatch._probe_count);
                    }

                    if (volume._draw_relocation_debug && _debug_material->IsReadyForDraw(2u))
                    {
                        cmd->DrawMesh(sphere, _debug_material.get(), Matrix4x4f::Identity(), 0u, 2u,
                                      dispatch._probe_count);
                    }

                    if (volume._draw_relocation_debug && _debug_material->IsReadyForDraw(3u))
                    {
                        cmd->DrawProcedural(_debug_material.get(), 3u, 2u, dispatch._probe_count);
                    }

                    if ((volume._draw_probe_rays || is_selected_volume) && _debug_material->IsReadyForDraw(1u))
                    {
                        const u32 ray_instance_count = is_selected_volume ? DDGI::kRaysPerProbe : dispatch._ray_count;
                        cmd->DrawProcedural(_debug_material.get(), 1u, 2u, ray_instance_count);
                    }
                }
            });
    }

    DDGIFeature::DDGIFeature() : RenderFeature("DDGI")
    {
        _is_active = true;
        _use_inline_ray_tracing = g_pGfxContext != nullptr && g_pGfxContext->IsInlineRayTracingSupported();
        _compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/ray_trace/ddgi.alasset");
        if (_compute_shader != nullptr)
        {
            _probe_trace_kernel = _compute_shader->FindKernel("ProbeTrace");
            if (_use_inline_ray_tracing)
                _compute_shader->EnableKeyword("AL_RAY_QUERY_DXR_INLINE");
            else
                _compute_shader->DisableKeyword("AL_RAY_QUERY_DXR_INLINE");
        }
        _radiance_compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(
            L"Shaders/hlsl/ray_trace/ddgi_radiance.alasset");
        _probe_radiance_kernel = FindRequiredKernel(_radiance_compute_shader, "ProbeRadiance", "ddgi_radiance");
        _border_compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(
            L"Shaders/hlsl/ray_trace/ddgi_border.alasset");
        _probe_border_kernel = FindRequiredKernel(_border_compute_shader, "ProbeBorder", "ddgi_border");
        _distance_compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(
            L"Shaders/hlsl/ray_trace/ddgi_distance.alasset");
        _probe_distance_kernel = FindRequiredKernel(_distance_compute_shader, "ProbeDistance", "ddgi_distance");
        _distance_border_compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(
            L"Shaders/hlsl/ray_trace/ddgi_distance_border.alasset");
        _probe_distance_border_kernel = FindRequiredKernel(_distance_border_compute_shader, "ProbeDistanceBorder",
                                                          "ddgi_distance_border");
        _relocation_compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(
            L"Shaders/hlsl/ray_trace/ddgi_relocation.alasset");
        _probe_relocation_kernel = FindRequiredKernel(_relocation_compute_shader, "ProbeRelocation",
                                                     "ddgi_relocation");
        _scene_rt_proxy = MakeScope<SceneRayTracingProxy>();
        _probe_trace_pass = MakeScope<DDGIProbeTracePass>(*this);
        _probe_radiance_pass = MakeScope<DDGIProbeRadiancePass>(*this);
        _probe_border_pass = MakeScope<DDGIProbeBorderPass>(*this);
        _probe_distance_pass = MakeScope<DDGIProbeDistancePass>(*this);
        _probe_distance_border_pass = MakeScope<DDGIProbeDistanceBorderPass>(*this);
        _probe_relocation_pass = MakeScope<DDGIProbeRelocationPass>(*this);
        _temporal_swap_pass = MakeScope<DDGITemporalSwapPass>(*this);
        _debug_pass = MakeScope<DDGIDebugPass>(*this);
    }

    void DDGIFeature::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
    {
        if (!_is_active || rendering_data._scene == nullptr)
            return;

        const u64 frame = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
        if (_last_scene != rendering_data._scene || _last_sync_frame != frame)
        {
            SyncSceneVolumes(*rendering_data._scene);
            _last_scene = rendering_data._scene;
            _last_sync_frame = frame;
        }

        if (_compute_shader == nullptr || _probe_trace_kernel == kInvalidComputeShaderKernelId ||
            _volume_runtimes.empty())
        {
            return;
        }

        // Periodic state report: "history_valid" stays false when the temporal swap never runs, and a
        // hysteresis that does not match the feature value means the compute constants never reached the
        // probe update.  Both silently degrade the probe grid to per-frame noise.
        if (frame != 0u && frame % 120u == 0u)
        {
            for (const auto &[entity, runtime] : _volume_runtimes)
            {
                LOG_INFO("DDGI temporal state: volume={} probes={} history_valid={} use_temporal={} hysteresis={:.3f} "
                         "random_rotation={}",
                         entity, runtime._probe_count, runtime._temporal_history_valid, _use_temporal, _hysteresis,
                         _use_random_rotation);
            }
        }

        // The probe grid does not depend on the camera, and the ray rotation only advances once per
        // frame.  Updating for every render of the same frame (asset preview views, cubemap probe
        // captures) would blend the same rotation sample N times over and weaken the temporal
        // history to hysteresis^N, so keep exactly one DDGI update per frame.
        if (renderer.IsRenderingProbe() || _last_update_frame == frame)
            return;
        _last_update_frame = frame;

        EnsureGpuResources();
        if (_scene_rt_proxy != nullptr)
        {
            if (_use_inline_ray_tracing)
            {
                _scene_rt_proxy->Sync(rendering_data._scene, rendering_data._primitive_data,
                                      rendering_data._scene_primitive_buffer, rendering_data._scene_primitives);
            }
            else
            {
                _scene_rt_proxy->SyncLightCache(rendering_data._scene);
            }
        }
        renderer.EnqueuePass(_probe_trace_pass.get());
        if (_probe_radiance_pass != nullptr && _radiance_compute_shader != nullptr &&
            _probe_radiance_kernel != kInvalidComputeShaderKernelId)
        {
            renderer.EnqueuePass(_probe_radiance_pass.get());
        }
        if (_probe_border_pass != nullptr && _border_compute_shader != nullptr &&
            _probe_border_kernel != kInvalidComputeShaderKernelId)
        {
            renderer.EnqueuePass(_probe_border_pass.get());
        }
        if (_probe_distance_pass != nullptr && _distance_compute_shader != nullptr &&
            _probe_distance_kernel != kInvalidComputeShaderKernelId)
        {
            renderer.EnqueuePass(_probe_distance_pass.get());
        }
        if (_probe_distance_border_pass != nullptr && _distance_border_compute_shader != nullptr &&
            _probe_distance_border_kernel != kInvalidComputeShaderKernelId)
        {
            renderer.EnqueuePass(_probe_distance_border_pass.get());
        }

        if (_probe_relocation_pass != nullptr && _relocation_compute_shader != nullptr &&
            _probe_relocation_kernel != kInvalidComputeShaderKernelId)
        {
            for (const auto &[entity, runtime] : _volume_runtimes)
            {
                (void) entity;
                if (runtime._desc._use_probe_relocation)
                {
                    renderer.EnqueuePass(_probe_relocation_pass.get());
                    break;
                }
            }
        }

        if (_debug_pass != nullptr && _debug_pass->IsAvailable())
        {
            for (const auto &[entity, runtime] : _volume_runtimes)
            {
                (void) entity;
                if (runtime._desc._draw_probes || runtime._desc._draw_probe_rays ||
                    runtime._desc._draw_relocation_debug)
                {
                    renderer.EnqueuePass(_debug_pass.get());
                    break;
                }
            }
        }

        // The swap has to follow every probe update that read the history.  It only depends on the trace
        // pass above, not on the optional kernels: gating it on all of them silently turns the temporal
        // accumulation off as soon as a single shader is unavailable, which shows up as per-frame noise
        // that no amount of denoising can remove.
        if (_temporal_swap_pass != nullptr)
        {
            renderer.EnqueuePass(_temporal_swap_pass.get());
        }
    }

    void DDGIFeature::PrepareDeferredLighting(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        rendering_data._ddgi_lighting = {};
        if (!_is_active || _volume_runtimes.empty())
            return;

        const DDGIVolumeRuntime *fallback_runtime = nullptr;
        const DDGIVolumeRuntime *camera_runtime = nullptr;
        f32 camera_volume = std::numeric_limits<f32>::max();
        for (const auto &[entity, runtime] : _volume_runtimes)
        {
            (void) entity;
            if (runtime._radiance_texture == nullptr || runtime._distance_texture == nullptr ||
                runtime._probe_data == nullptr ||
                runtime._probe_count == 0u)
                continue;

            if (fallback_runtime == nullptr)
                fallback_runtime = &runtime;

            if (rendering_data._camera == nullptr)
                continue;

            const Matrix4x4f world_to_local = Math::MatrixInverse(runtime._desc._local_to_world);
            const Vector3f camera_local = TransformCoord(world_to_local, rendering_data._camera->Position());
            const Vector3f half_extent =
                DDGI::GridSize(runtime._desc._probe_count, runtime._desc._probe_spacing) * 0.5f;
            const bool inside = std::abs(camera_local.x) <= half_extent.x + 1e-4f &&
                                std::abs(camera_local.y) <= half_extent.y + 1e-4f &&
                                std::abs(camera_local.z) <= half_extent.z + 1e-4f;
            const f32 volume = half_extent.x * half_extent.y * half_extent.z;
            if (inside && volume < camera_volume)
            {
                camera_runtime = &runtime;
                camera_volume = volume;
            }
        }

        const DDGIVolumeRuntime *runtime = camera_runtime != nullptr ? camera_runtime : fallback_runtime;
        if (runtime == nullptr)
            return;

        const auto &volume = runtime->_desc;
        auto &lighting = rendering_data._ddgi_lighting;
        lighting._irradiance_atlas_handle = graph.Import(runtime->_radiance_texture.get());
        lighting._irradiance_atlas_texture = runtime->_radiance_texture;
        lighting._distance_atlas_handle = graph.Import(runtime->_distance_texture.get());
        lighting._distance_atlas_texture = runtime->_distance_texture;
        lighting._probe_data_handle = graph.Import(runtime->_probe_data.get());
        lighting._probe_data = runtime->_probe_data;
        lighting._local_to_world = volume._local_to_world;
        lighting._world_to_local = Math::MatrixInverse(volume._local_to_world);
        lighting._probe_spacing = volume._probe_spacing;
        lighting._probe_count_x = volume._probe_count.x;
        lighting._probe_count_y = volume._probe_count.y;
        lighting._probe_count_z = volume._probe_count.z;
        lighting._atlas_tiles_x = volume._radiance_atlas_tiles_x;
        lighting._atlas_tiles_y = volume._radiance_atlas_tiles_y;
        lighting._atlas_width = volume._radiance_atlas_width;
        lighting._atlas_height = volume._radiance_atlas_height;
        lighting._distance_atlas_tiles_x = volume._distance_atlas_tiles_x;
        lighting._distance_atlas_tiles_y = volume._distance_atlas_tiles_y;
        lighting._distance_atlas_width = volume._distance_atlas_width;
        lighting._distance_atlas_height = volume._distance_atlas_height;
        lighting._use_visibility = _use_visibility;
        lighting._probe_normal_bias = std::isfinite(_probe_normal_bias) ? std::max(_probe_normal_bias, 0.0f) : 0.0f;
        lighting._probe_view_bias = std::isfinite(_probe_view_bias) ? std::max(_probe_view_bias, 0.0f) : 0.0f;
        lighting._use_probe_relocation = volume._use_probe_relocation;
        lighting._debug_mode = static_cast<u32>(volume._debug_mode);
        lighting._enabled = true;
    }

    void DDGIFeature::SyncSceneVolumes(const SceneManagement::Scene &scene)
    {
        for (auto &[entity, runtime] : _volume_runtimes)
        {
            (void) entity;
            runtime._seen_this_frame = false;
        }

        const auto &registry = scene.GetRegister();
        u32 volume_index = 0u;
        for (const auto &volume : registry.View<ECS::DDGIVolumeComponent>())
        {
            const ECS::Entity entity = registry.GetEntity<ECS::DDGIVolumeComponent>(volume_index++);
            if (!scene.IsEntityEnabled(entity) || !registry.IsComponentEnabled<ECS::DDGIVolumeComponent>(entity) ||
                !DDGI::IsValidProbeGrid(volume._probe_count, volume._probe_spacing))
            {
                continue;
            }

            const auto *transform = registry.GetComponent<ECS::TransformComponent>(entity);
            if (transform == nullptr)
                continue;

            DDGIVolumeRenderData data;
            data._entity = entity;
            data._local_to_world = BuildDDGIVolumeTransform(*transform);
            data._probe_count = volume._probe_count;
            data._probe_spacing = volume._probe_spacing;
            data._max_ray_distance = ReadMaxRayDistance(volume);
            if (!IsValidPositiveFinite(data._max_ray_distance))
            {
                LOG_ERROR("DDGI volume {} has invalid max ray distance {}; using {}", entity,
                          data._max_ray_distance, kDefaultMaxRayDistance);
                data._max_ray_distance = kDefaultMaxRayDistance;
            }
            data._draw_probes = volume._draw_probes;
            data._draw_probe_rays = ReadDrawProbeRays(volume);
            data._use_probe_relocation = volume._use_probe_relocation;
            data._relocation_backface_threshold = std::isfinite(volume._relocation_backface_threshold)
                ? std::clamp(volume._relocation_backface_threshold, 0.0f, 1.0f) : 0.25f;
            data._random_ray_backface_threshold = ReadRandomRayBackfaceThreshold(volume);
            if (!std::isfinite(data._random_ray_backface_threshold))
                data._random_ray_backface_threshold = kDefaultRandomRayBackfaceThreshold;
            data._random_ray_backface_threshold = std::clamp(data._random_ray_backface_threshold, 0.0f, 1.0f);
            data._relocation_min_frontface_distance_ratio =
                std::isfinite(volume._relocation_min_frontface_distance_ratio)
                ? std::clamp(volume._relocation_min_frontface_distance_ratio, 0.0f, 0.5f) : 0.1f;
            data._draw_relocation_debug = volume._draw_relocation_debug;
            data._freeze_ray_rotation = ReadFreezeRayRotation(volume);
            data._debug_probe_radius = ReadDebugProbeRadius(volume);
            data._debug_ray_length = ReadDebugRayLength(volume);
            data._debug_mode = ReadDebugMode(volume);
            CalculateRadianceAtlasLayout(data._probe_count, data._radiance_atlas_tiles_x, data._radiance_atlas_tiles_y,
                                         data._radiance_atlas_width, data._radiance_atlas_height);
            CalculateDistanceAtlasLayout(data._probe_count, data._distance_atlas_tiles_x, data._distance_atlas_tiles_y,
                                         data._distance_atlas_width, data._distance_atlas_height);
            if (!IsValidPositiveFinite(data._debug_probe_radius))
                data._debug_probe_radius = kDefaultDebugProbeRadius;
            if (!IsValidPositiveFinite(data._debug_ray_length))
                data._debug_ray_length = kDefaultDebugRayLength;

            auto [runtime_it, inserted] = _volume_runtimes.try_emplace(entity);
            auto &runtime = runtime_it->second;
            runtime._seen_this_frame = true;
            if (inserted && _last_sync_frame != ~u64(0))
            {
                // A runtime that keeps disappearing and coming back reallocates its probe ray data and
                // loses the temporal history on every cycle.
                LOG_WARNING("DDGI volume {} runtime re-created: probe ray data and temporal history were discarded",
                            entity);
            }
            // Probe history is only valid while the volume's probe layout and ray domain stay unchanged.
            // The transform is compared by value: an unrelated world version bump (hierarchy rebuild,
            // re-applied transform) must not throw the temporal history away, otherwise the probe update
            // degrades to raw per-frame noise.
            const bool probe_layout_changed = inserted || runtime._desc._probe_count != data._probe_count;
            const bool transform_changed = !(runtime._desc._local_to_world == data._local_to_world);
            const bool spacing_changed = !(runtime._desc._probe_spacing == data._probe_spacing);
            const bool ray_distance_changed = runtime._desc._max_ray_distance != data._max_ray_distance;
            const bool relocation_changed = runtime._desc._use_probe_relocation != data._use_probe_relocation;
            const bool probe_data_domain_changed = probe_layout_changed || transform_changed || spacing_changed ||
                relocation_changed;
            const bool history_domain_changed = probe_layout_changed || transform_changed || spacing_changed ||
                ray_distance_changed || relocation_changed;
            if (probe_layout_changed)
            {
                runtime._probe_count = 0u;
                runtime._probe_ray_data.reset();
            }
            if (probe_data_domain_changed)
                runtime._probe_data.reset();
            if (history_domain_changed)
            {
                if (!inserted)
                {
                    // A reset on every frame turns the temporal accumulation off; report it instead of
                    // silently falling back to per-frame noise.  Log the first one and then periodically
                    // so a permanent reset stays visible without flooding the log.
                    if (runtime._history_reset_count == 0u || runtime._history_reset_count % 60u == 0u)
                    {
                        LOG_WARNING("DDGI volume {} temporal history reset #{}: layout={}, transform={}, spacing={}, "
                                    "max_ray_distance={}, relocation={}; probe hysteresis cannot accumulate",
                                    entity, runtime._history_reset_count, probe_layout_changed, transform_changed,
                                    spacing_changed, ray_distance_changed, relocation_changed);
                    }
                    ++runtime._history_reset_count;
                }
                runtime._radiance_texture.reset();
                runtime._radiance_history_texture.reset();
                runtime._distance_texture.reset();
                runtime._distance_history_texture.reset();
                runtime._temporal_history_valid = false;
            }
            else
            {
                runtime._history_reset_count = 0u;
            }
            runtime._desc = data;
        }

        std::erase_if(_volume_runtimes, [](const auto &item)
        {
            return !item.second._seen_this_frame;
        });

        const auto selected_it = _volume_runtimes.find(_selected_probe_entity);
        if (selected_it == _volume_runtimes.end() ||
            _selected_probe_index >= DDGI::ProbeCount(selected_it->second._desc._probe_count))
            ClearProbeSelection();
    }

    bool DDGIFeature::PickProbe(const Camera &camera, const Math::Vector2f &screen_pos)
    {
        const Vector3f ray_origin = camera.Type() == ECameraType::kOrthographic
            ? camera.ScreenToWorld(screen_pos, -camera.Near()) : camera.Position();
        const Vector3f ray_direction = Normalize(camera.ScreenToWorld(screen_pos));
        f32 closest_distance = std::numeric_limits<f32>::max();
        ECS::Entity picked_entity = ECS::kInvalidEntity;
        u32 picked_probe_index = DDGI::kInvalidProbeIndex;

        for (const auto &[entity, runtime] : _volume_runtimes)
        {
            const auto &volume = runtime._desc;
            if (!volume._draw_probes || !DDGI::IsValidProbeGrid(volume._probe_count, volume._probe_spacing))
                continue;

            const f32 radius = std::max(volume._debug_probe_radius, 0.001f);
            const u32 probe_count = runtime._probe_count;
            if (probe_count == 0u)
                continue;
            const u32 xy_count = volume._probe_count.x * volume._probe_count.y;
            for (u32 probe_index = 0u; probe_index < probe_count; ++probe_index)
            {
                const Vector3UInt probe_coord(
                    probe_index % volume._probe_count.x,
                    (probe_index / volume._probe_count.x) % volume._probe_count.y,
                    probe_index / xy_count);
                const Vector3f local_position = DDGI::ProbeLocalPosition(
                    probe_coord, volume._probe_count, volume._probe_spacing);
                const Vector3f probe_position = TransformCoord(volume._local_to_world, local_position);
                const Vector3f offset = ray_origin - probe_position;
                const f32 half_b = DotProduct(offset, ray_direction);
                const f32 c = DotProduct(offset, offset) - radius * radius;
                const f32 discriminant = half_b * half_b - c;
                if (discriminant < 0.0f)
                    continue;

                f32 hit_distance = -half_b - std::sqrt(discriminant);
                if (hit_distance < 0.0f)
                    hit_distance = -half_b + std::sqrt(discriminant);
                if (hit_distance < 0.0f || hit_distance >= closest_distance)
                    continue;

                closest_distance = hit_distance;
                picked_entity = entity;
                picked_probe_index = probe_index;
            }
        }

        if (picked_entity == ECS::kInvalidEntity)
        {
            ClearProbeSelection();
            return false;
        }

        _selected_probe_entity = picked_entity;
        _selected_probe_index = picked_probe_index;
        return true;
    }

    void DDGIFeature::ClearProbeSelection()
    {
        _selected_probe_entity = ECS::kInvalidEntity;
        _selected_probe_index = DDGI::kInvalidProbeIndex;
    }

    void DDGIFeature::EnsureGpuResources()
    {
        for (auto &[entity, runtime] : _volume_runtimes)
        {
            const u64 probe_count64 = DDGI::ProbeCount(runtime._desc._probe_count);
            if (probe_count64 == 0u || probe_count64 > std::numeric_limits<u64>::max() / DDGI::kRaysPerProbe)
            {
                LOG_ERROR("DDGI volume {} has an invalid ProbeRayData probe count {}", entity, probe_count64);
                runtime._probe_ray_data.reset();
                runtime._probe_data.reset();
                runtime._radiance_texture.reset();
                runtime._radiance_history_texture.reset();
                runtime._distance_texture.reset();
                runtime._distance_history_texture.reset();
                runtime._temporal_history_valid = false;
                runtime._probe_count = 0u;
                continue;
            }

            const u64 ray_count64 = probe_count64 * DDGI::kRaysPerProbe;
            if (probe_count64 > std::numeric_limits<u32>::max() || ray_count64 > std::numeric_limits<u32>::max() ||
                ray_count64 > std::numeric_limits<u64>::max() / sizeof(DDGIProbeRayData) ||
                ray_count64 * sizeof(DDGIProbeRayData) > std::numeric_limits<u32>::max())
            {
                LOG_ERROR("DDGI volume {} has an invalid ProbeRayData allocation: probes={}, rays={}",
                          entity, probe_count64, ray_count64);
                runtime._probe_ray_data.reset();
                runtime._probe_data.reset();
                runtime._radiance_texture.reset();
                runtime._radiance_history_texture.reset();
                runtime._distance_texture.reset();
                runtime._distance_history_texture.reset();
                runtime._temporal_history_valid = false;
                runtime._probe_count = 0u;
                continue;
            }

            const u32 probe_count = static_cast<u32>(probe_count64);
            if (runtime._probe_count != probe_count || runtime._probe_ray_data == nullptr)
            {
                BufferDesc desc;
                desc._element_num = static_cast<u32>(ray_count64);
                desc._element_size = sizeof(DDGIProbeRayData);
                desc._size = static_cast<u32>(ray_count64 * sizeof(DDGIProbeRayData));
                desc._target = EGPUBufferTarget::kStructured;
                desc._is_random_write = true;
                runtime._probe_ray_data = GPUBuffer::Create(desc, std::format("DDGI_{}_ProbeRayData", entity));
                runtime._probe_count = runtime._probe_ray_data != nullptr ? probe_count : 0u;
            }

            if (runtime._probe_data == nullptr && runtime._probe_count == probe_count)
            {
                BufferDesc desc;
                desc._element_num = probe_count;
                desc._element_size = sizeof(DDGIProbeData);
                desc._size = probe_count * sizeof(DDGIProbeData);
                desc._target = EGPUBufferTarget::kStructured;
                desc._is_random_write = true;
                runtime._probe_data = GPUBuffer::Create(desc, std::format("DDGI_{}_ProbeData", entity));
                if (runtime._probe_data != nullptr)
                {
                    Vector<DDGIProbeData> initial_data;
                    initial_data.resize(probe_count);
                    runtime._probe_data->SetData(initial_data);
                }
            }

            if (runtime._probe_ray_data == nullptr || runtime._probe_data == nullptr)
            {
                runtime._radiance_texture.reset();
                runtime._radiance_history_texture.reset();
                runtime._distance_texture.reset();
                runtime._distance_history_texture.reset();
                runtime._probe_data.reset();
                runtime._temporal_history_valid = false;
                continue;
            }

            const auto &volume = runtime._desc;
            const bool radiance_size_matches = runtime._radiance_texture != nullptr &&
                runtime._radiance_history_texture != nullptr &&
                runtime._radiance_texture->Width() == volume._radiance_atlas_width &&
                runtime._radiance_texture->Height() == volume._radiance_atlas_height;
            const bool distance_size_matches = runtime._distance_texture != nullptr &&
                runtime._distance_history_texture != nullptr &&
                runtime._distance_texture->Width() == volume._distance_atlas_width &&
                runtime._distance_texture->Height() == volume._distance_atlas_height;
            if (!radiance_size_matches)
            {
                runtime._radiance_texture.reset();
                runtime._radiance_history_texture.reset();
                runtime._temporal_history_valid = false;
                if (volume._radiance_atlas_width != 0u && volume._radiance_atlas_height != 0u)
                {
                    TextureDesc desc;
                    desc._width = volume._radiance_atlas_width;
                    desc._height = volume._radiance_atlas_height;
                    desc._format = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
                    desc._is_random_access = true;
                    desc._mip_num = 1u;
                    runtime._radiance_texture = Texture2D::Create(desc);
                    if (runtime._radiance_texture != nullptr)
                    {
                        runtime._radiance_texture->Apply();
                        runtime._radiance_texture->Name(std::format("DDGI_{}_RadianceAtlasA", entity));
                    }
                    runtime._radiance_history_texture = Texture2D::Create(desc);
                    if (runtime._radiance_history_texture != nullptr)
                    {
                        runtime._radiance_history_texture->Apply();
                        runtime._radiance_history_texture->Name(std::format("DDGI_{}_RadianceAtlasB", entity));
                    }
                }
            }
            if (!distance_size_matches)
            {
                runtime._distance_texture.reset();
                runtime._distance_history_texture.reset();
                runtime._temporal_history_valid = false;
                if (volume._distance_atlas_width != 0u && volume._distance_atlas_height != 0u)
                {
                    TextureDesc desc;
                    desc._width = volume._distance_atlas_width;
                    desc._height = volume._distance_atlas_height;
                    desc._format = EALGFormat::kALGFormatR32G32_FLOAT;
                    desc._is_random_access = true;
                    desc._mip_num = 1u;
                    runtime._distance_texture = Texture2D::Create(desc);
                    if (runtime._distance_texture != nullptr)
                    {
                        runtime._distance_texture->Apply();
                        runtime._distance_texture->Name(std::format("DDGI_{}_DistanceAtlasA", entity));
                    }
                    runtime._distance_history_texture = Texture2D::Create(desc);
                    if (runtime._distance_history_texture != nullptr)
                    {
                        runtime._distance_history_texture->Apply();
                        runtime._distance_history_texture->Name(std::format("DDGI_{}_DistanceAtlasB", entity));
                    }
                }
            }
        }
    }
}
