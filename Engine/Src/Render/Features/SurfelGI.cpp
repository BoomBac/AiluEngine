#include "Render/Features/SurfelGI.h"
#include "Animation/SkinningSystem.h"

#include "Framework/Common/ResourceMgr.h"
#include "Framework/Math/MatrixMath.h"
#include "Render/Camera.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/Renderer.h"
#include "Render/Texture.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Ailu::Render
{
    namespace
    {
        constexpr u32 kMaxCascadeCount = 5u;
        constexpr u32 kMaxNeighborCells = 27u;
        constexpr u32 kScanBlockSize = 1024u;
        constexpr u32 kScanFinalizeThreadCount = 256u;
        constexpr u32 kMaxScanBlockCount = 2048u;
        constexpr f32 kPi = 3.14159265358979323846f;

        const ShaderPropertyId kGridInfoPropertyId = Shader::PropertyID("_GridInfo");
        const ShaderPropertyId kSurfelParamsPropertyId = Shader::PropertyID("_SurfelParams");
        const ShaderPropertyId kSpawnParamsPropertyId = Shader::PropertyID("_SpawnParams");
        const ShaderPropertyId kCameraParamsPropertyId = Shader::PropertyID("_CameraParams");
        const ShaderPropertyId kGridResolutionPropertyId = Shader::PropertyID("_GridResolution");
        const ShaderPropertyId kCascadeCountPropertyId = Shader::PropertyID("_CascadeCount");
        const ShaderPropertyId kMaxSurfelCountPropertyId = Shader::PropertyID("_MaxSurfelCount");
        const ShaderPropertyId kMaxSpawnPerFramePropertyId = Shader::PropertyID("_MaxSpawnPerFrame");
        const ShaderPropertyId kCandidateCapacityPropertyId = Shader::PropertyID("_CandidateCapacity");
        const ShaderPropertyId kSurfelPoolPropertyId = Shader::PropertyID("_SurfelPool");
        const ShaderPropertyId kSurfelLightingPropertyId = Shader::PropertyID("_SurfelLighting");
        const ShaderPropertyId kSurfelLightingWritePropertyId = Shader::PropertyID("_SurfelLightingWrite");
        const ShaderPropertyId kSurfelPoolWritePropertyId = Shader::PropertyID("_SurfelPoolWrite");
        const ShaderPropertyId kSurfelStateReadPropertyId = Shader::PropertyID("_SurfelStateRead");
        const ShaderPropertyId kSurfelStatePropertyId = Shader::PropertyID("_SurfelState");
        const ShaderPropertyId kSurfelFreeListPropertyId = Shader::PropertyID("_SurfelFreeList");
        const ShaderPropertyId kSurfelDeferredFreeListPropertyId = Shader::PropertyID("_SurfelDeferredFreeList");
        const ShaderPropertyId kSurfelDeferredFreeListReadPropertyId =
            Shader::PropertyID("_SurfelDeferredFreeListRead");
        const ShaderPropertyId kSurfelFrameStatePropertyId = Shader::PropertyID("_SurfelFrameState");
        const ShaderPropertyId kSurfelCandidatesPropertyId = Shader::PropertyID("_SurfelCandidates");
        const ShaderPropertyId kCandidateStatePropertyId = Shader::PropertyID("_CandidateState");
        const ShaderPropertyId kSpawnOccupancyPropertyId = Shader::PropertyID("_SpawnOccupancy");
        const ShaderPropertyId kGridCellsPropertyId = Shader::PropertyID("_GridCells");
        const ShaderPropertyId kGridCellsReadPropertyId = Shader::PropertyID("_GridCellsRead");
        const ShaderPropertyId kGridWriteCountersPropertyId = Shader::PropertyID("_GridWriteCounters");
        const ShaderPropertyId kGridIndicesPropertyId = Shader::PropertyID("_GridIndices");
        const ShaderPropertyId kGridIndicesReadPropertyId = Shader::PropertyID("_GridIndicesRead");
        const ShaderPropertyId kGridBlockPrefixPropertyId = Shader::PropertyID("_GridBlockPrefix");
        const ShaderPropertyId kSurfelDispatchArgsPropertyId = Shader::PropertyID("_SurfelDispatchArgs");
        const ShaderPropertyId kDrawArgsPropertyId = Shader::PropertyID("_DrawArgs");
        const ShaderPropertyId kCameraDepthTexturePropertyId = Shader::PropertyID("_CameraDepthTexture");
        const ShaderPropertyId kCameraNormalsTexturePropertyId = Shader::PropertyID("_CameraNormalsTexture");
        const ShaderPropertyId kCoverageTexturePropertyId = Shader::PropertyID("_CoverageTexture");
        const ShaderPropertyId kBlueNoiseTexturePropertyId = Shader::PropertyID("_BlueNoiseTexture");
        const ShaderPropertyId kDebugCascadePropertyId = Shader::PropertyID("_DebugCascade");
        const ShaderPropertyId kDebugShowIrradiancePropertyId = Shader::PropertyID("_DebugShowIrradiance");
        const ShaderPropertyId kDebugOpaqueDisksPropertyId = Shader::PropertyID("_DebugOpaqueDisks");
        const ShaderPropertyId kDebugIrradianceExposurePropertyId = Shader::PropertyID("_DebugIrradianceExposure");
        const ShaderPropertyId kDebugLightingModePropertyId = Shader::PropertyID("_DebugLightingMode");

        struct SurfelGridCellData
        {
            u32 _offset = 0u;
            u32 _count = 0u;
        };

        struct SurfelDispatchArguments
        {
            u32 _thread_group_num_x = 1u;
            u32 _thread_group_num_y = 1u;
            u32 _thread_group_num_z = 1u;
        };

        struct SurfelDrawArguments
        {
            u32 _vertex_count_per_instance = 48u;
            u32 _instance_count = 0u;
            u32 _start_vertex_location = 0u;
            u32 _start_instance_location = 0u;
        };

        u32 CalculateDispatchCount(u32 value, u32 group_size)
        {
            return (value + group_size - 1u) / group_size;
        }

        u32 ClampGridResolution(u32 value)
        {
            return std::clamp(value, 8u, 128u);
        }

        u32 ClampCascadeCount(u32 value)
        {
            return std::clamp(value, 1u, kMaxCascadeCount);
        }

        u32 ClampMaxSurfelCount(u32 value)
        {
            return std::clamp(value, 1u, 1u << 20u);
        }

        template<typename T>
        void HashLightingValue(u64 &hash, const T &value)
        {
            const auto *bytes = reinterpret_cast<const u8 *>(&value);
            for (size_t i = 0u; i < sizeof(T); ++i)
                hash = (hash ^ bytes[i]) * 1099511628211ull;
        }

        u64 CalculateLightingSignature(const RenderingData &data, const SceneRayTracingProxy &proxy, f32 ray_distance,
                                       Vector<Matrix4x4f> &history_transforms, Vector<UnifiedLightData> &history_lights)
        {
            u64 hash = 14695981039346656037ull;
            HashLightingValue(hash, data._scene);
            HashLightingValue(hash, ray_distance);
            if (data._environment != nullptr)
            {
                const auto &environment = *data._environment;
                HashLightingValue(hash, environment._sky_cubemap.get());
                HashLightingValue(hash, environment._source_revision);
                HashLightingValue(hash, environment._intensity);
                HashLightingValue(hash, environment._rotation);
                HashLightingValue(hash, environment._enable_ibl);
                HashLightingValue(hash, environment._sky_color);
                HashLightingValue(hash, environment._ground_color);
            }
            // An asynchronous TLAS replacement must not keep samples traced against the old scene.
            HashLightingValue(hash, proxy.GetScene());
            HashLightingValue(hash, proxy.HasRenderableScene());
            if (data._scene_primitives != nullptr && data._primitive_data != nullptr)
            {
                history_transforms.resize(data._primitive_data->size());
                for (const auto &primitive : *data._scene_primitives)
                {
                    HashLightingValue(hash, primitive._entity_id);
                    HashLightingValue(hash, primitive._mesh);
                    HashLightingValue(hash, primitive._submesh_index);
                    HashLightingValue(hash, primitive._flags);
                    HashLightingValue(hash, primitive._vertex_buffer);
                    HashLightingValue(hash, primitive._index_buffer);
                    if (primitive._vertex_buffer != nullptr)
                        HashLightingValue(hash, primitive._vertex_buffer->GetViewVersion());
                    if (primitive._index_buffer != nullptr)
                        HashLightingValue(hash, primitive._index_buffer->GetViewVersion());
                    HashLightingValue(hash, primitive._material);
                    if (primitive._primitive_index < data._primitive_data->size())
                    {
                        const auto &matrix = (*data._primitive_data)[primitive._primitive_index]._local_to_world;
                        auto &history_matrix = history_transforms[primitive._primitive_index];
                        bool changed = false;
                        // Mixing identical transforms can differ by a few ULPs. Compare against the last
                        // accepted transform so tiny per-frame motion still accumulates into a real change.
                        constexpr f32 kTransformTolerance = 0.0001f;
                        for (u32 row = 0u; row < 4u; ++row)
                            for (u32 column = 0u; column < 4u; ++column)
                                changed |= std::abs(matrix[row][column] - history_matrix[row][column]) >
                                           kTransformTolerance;
                        if (changed)
                            history_matrix = matrix;
                        HashLightingValue(hash, history_matrix);
                    }
                    if (primitive._material != nullptr)
                        HashLightingValue(hash, primitive._material->PropertyVersion());
                    HashLightingValue(hash, ECS::SkinningSystem::ResolveSkinVersion(
                        primitive._mesh, primitive._entity_id));
                }
            }
            const auto &lights = proxy.GetUnifiedLights();
            history_lights.resize(lights.size());
            const auto stable_pose = [](const Vector3f &value, Vector3f &history)
            {
                constexpr f32 kPoseTolerance = 0.0001f;
                if (std::abs(value.x - history.x) > kPoseTolerance ||
                    std::abs(value.y - history.y) > kPoseTolerance ||
                    std::abs(value.z - history.z) > kPoseTolerance)
                    history = value;
                return history;
            };
            for (size_t i = 0u; i < lights.size(); ++i)
            {
                auto light = lights[i];
                auto &history = history_lights[i];
                light._position = stable_pose(light._position, history._position);
                light._direction = stable_pose(light._direction, history._direction);
                light._shape_u = stable_pose(light._shape_u, history._shape_u);
                light._shape_v = stable_pose(light._shape_v, history._shape_v);
                HashLightingValue(hash, light);
            }
            return hash;
        }
    }

    SurfelGIFeature::SurfelGIFeature()
        : RenderFeature("SurfelGI"), _placement_pass(*this), _lighting_pass(*this), _debug_pass(*this)
    {
        _is_active = true;
        _use_inline_ray_tracing = g_pGfxContext != nullptr && g_pGfxContext->IsInlineRayTracingSupported();
        _scene_rt_proxy = MakeScope<SceneRayTracingProxy>();
        _compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(
            L"Shaders/hlsl/Compute/surfel_placement.alasset");
        if (_compute_shader != nullptr)
        {
            _grid_clear_kernel = _compute_shader->FindKernel("SurfelGridClearCS");
            _frame_state_clear_kernel = _compute_shader->FindKernel("SurfelFrameStateClearCS");
            _prepare_dispatch_kernel = _compute_shader->FindKernel("SurfelPrepareDispatchCS");
            _flush_deferred_free_list_kernel = _compute_shader->FindKernel("SurfelFlushDeferredFreeListCS");
            _reset_deferred_free_count_kernel = _compute_shader->FindKernel("SurfelResetDeferredFreeCountCS");
            _active_list_remove_kernel = _compute_shader->FindKernel("SurfelActiveListRemoveCS");
            _maintenance_kernel = _compute_shader->FindKernel("SurfelMaintenanceCS");
            _grid_count_kernel = _compute_shader->FindKernel("SurfelGridCountCS");
            _grid_prefix_local_kernel = _compute_shader->FindKernel("SurfelGridPrefixLocalCS");
            _grid_prefix_blocks_kernel = _compute_shader->FindKernel("SurfelGridPrefixBlocksCS");
            _grid_prefix_finalize_kernel = _compute_shader->FindKernel("SurfelGridPrefixFinalizeCS");
            _grid_fill_kernel = _compute_shader->FindKernel("SurfelGridFillCS");
            _migration_resolve_kernel = _compute_shader->FindKernel("SurfelMigrationResolveCS");
            _migration_recycle_kernel = _compute_shader->FindKernel("SurfelMigrationRecycleCS");
            _coverage_kernel = _compute_shader->FindKernel("SurfelCoverageCS");
            _candidate_clear_kernel = _compute_shader->FindKernel("SurfelCandidateClearCS");
            _candidate_kernel = _compute_shader->FindKernel("SurfelCandidateCS");
            _candidate_claim_kernel = _compute_shader->FindKernel("SurfelCandidateClaimCS");
            _candidate_commit_kernel = _compute_shader->FindKernel("SurfelCandidateCommitCS");
            _migration_finalize_kernel = _compute_shader->FindKernel("SurfelMigrationFinalizeCS");
            _prepare_draw_args_kernel = _compute_shader->FindKernel("SurfelPrepareDrawArgsCS");
        }
        _blue_noise_texture = ResourceMgr::Get().LoadAssetRef<Texture2D>(EnginePath::kEngineTexturePathW +
                                                                         L"blue_noise.alasset");

        _lighting_compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(
            L"Shaders/hlsl/ray_trace/surfel_lighting.alasset");
        if (_lighting_compute_shader != nullptr)
        {
            _lighting_update_kernel = _lighting_compute_shader->FindKernel("SurfelLightingUpdateCS");
            _lighting_reset_kernel = _lighting_compute_shader->FindKernel("SurfelLightingResetCS");
            if (_use_inline_ray_tracing)
                _lighting_compute_shader->EnableKeyword("AL_RAY_QUERY_DXR_INLINE");
            else
                _lighting_compute_shader->DisableKeyword("AL_RAY_QUERY_DXR_INLINE");
        }

        _disk_shader = ResourceMgr::Get().LoadAssetRef<Shader>(L"Shaders/hlsl/surfel_disk.alasset");
        _coverage_debug_shader = ResourceMgr::Get().LoadAssetRef<Shader>(L"Shaders/hlsl/surfel_coverage_debug.alasset");
        _tile_grid_shader = ResourceMgr::Get().LoadAssetRef<Shader>(L"Shaders/hlsl/surfel_tile_grid.alasset");
        if (_disk_shader != nullptr)
            _disk_material = MakeRef<Material>(_disk_shader.get(), "Runtime/SurfelDisk");
        if (_coverage_debug_shader != nullptr)
            _coverage_debug_material = MakeRef<Material>(_coverage_debug_shader.get(), "Runtime/SurfelCoverageDebug");
        if (_tile_grid_shader != nullptr)
            _tile_grid_material = MakeRef<Material>(_tile_grid_shader.get(), "Runtime/SurfelTileGrid");
    }

    void SurfelGIFeature::SyncSceneSettings(const SceneManagement::Scene &scene)
    {
        const bool spawn_once_pending = _lighting_scene == &scene && _spawn_once_pending;
        _has_active_component = false;
        _spawn_once_pending = false;
        const auto &registry = scene.GetRegister();
        u32 component_index = 0u;
        for (const auto &component : registry.View<ECS::SurfelGIComponent>())
        {
            const ECS::Entity entity = registry.GetEntity<ECS::SurfelGIComponent>(component_index++);
            if (!scene.IsEntityEnabled(entity) || !registry.IsComponentEnabled<ECS::SurfelGIComponent>(entity) ||
                !component._enabled)
            {
                component._spawn_once_requested = false;
                continue;
            }

            _settings._enabled = component._enabled;
            _settings._debug_draw_surfels = component._debug_draw_surfels;
            _settings._debug_coverage = component._debug_coverage;
            _settings._debug_draw_tile_grid = component._debug_draw_tile_grid;
            _settings._manual_spawn = component._manual_spawn;
            _settings._base_cell_size = std::clamp(component._base_cell_size, 0.001f, 10.0f);
            _settings._cascade_count = ClampCascadeCount(component._cascade_count);
            _settings._grid_resolution = ClampGridResolution(component._grid_resolution);
            _settings._target_pixel_radius = std::clamp(component._target_pixel_radius, 1.0f, 64.0f);
            _settings._spawn_coverage_threshold = std::clamp(component._spawn_coverage_threshold, 0.0f, 1.0f);
            _settings._spawn_spacing_scale = std::clamp(component._spawn_spacing_scale, 0.5f, 3.0f);
            _settings._max_spawn_per_frame = std::clamp(component._max_spawn_per_frame, 1u, 4096u);
            _settings._lighting_enabled = component._lighting_enabled;
            _settings._rays_per_surfel = std::clamp(component._rays_per_surfel, 1u, 8u);
            _settings._max_lighting_updates_per_frame =
                std::clamp(component._max_lighting_updates_per_frame, 1u, 4096u);
            _settings._lighting_max_ray_distance = std::isfinite(component._lighting_max_ray_distance)
                ? std::max(component._lighting_max_ray_distance, 0.01f) : 50.0f;
            _settings._debug_show_irradiance = component._debug_show_irradiance;
            _settings._debug_lighting_mode = std::min(component._debug_lighting_mode, 3u);
            _settings._debug_opaque_disks = component._debug_opaque_disks;
            _settings._debug_irradiance_exposure = std::isfinite(component._debug_irradiance_exposure)
                ? std::max(component._debug_irradiance_exposure, 0.0f) : 1.0f;
            _settings._spawn_normal_threshold = std::clamp(component._spawn_normal_threshold, 0.0f, 1.0f);
            _settings._blue_noise_strength = std::clamp(component._blue_noise_strength, 0.0f, 1.0f);
            _settings._max_surfel_count = ClampMaxSurfelCount(component._max_surfel_count);
            _settings._debug_cascade =
                std::clamp(component._debug_cascade, -1, static_cast<i32>(kMaxCascadeCount - 1u));
            _spawn_once_pending = (spawn_once_pending || component._spawn_once_requested) && _settings._manual_spawn;
            component._spawn_once_requested = false;
            _has_active_component = true;
            return;
        }
    }

    void SurfelGIFeature::EnsureGpuResources()
    {
        if (_compute_shader == nullptr)
            return;

        const u32 max_surfel_count = ClampMaxSurfelCount(_settings._max_surfel_count);
        const u32 grid_resolution = ClampGridResolution(_settings._grid_resolution);
        const u32 cascade_count = ClampCascadeCount(_settings._cascade_count);
        if (_surfel_pool != nullptr && _resource_max_surfel_count == max_surfel_count &&
            _surfel_lighting != nullptr && _resource_grid_resolution == grid_resolution &&
            _resource_cascade_count == cascade_count)
            return;

        _surfel_pool.reset();
        _surfel_lighting.reset();
        _surfel_state.reset();
        _surfel_free_list.reset();
        _surfel_deferred_free_list.reset();
        _active_surfel_indices.reset();
        _surfel_active_list_indices.reset();
        _surfel_frame_state.reset();
        _grid_cells.reset();
        _grid_write_counters.reset();
        _grid_indices.reset();
        _grid_block_prefix.reset();
        _surfel_candidates.reset();
        _candidate_state.reset();
        _spawn_occupancy.reset();
        _resource_candidate_capacity = 0u;
        _surfel_dispatch_args.reset();
        _draw_args.reset();
        _lighting_update_cursor = 0u;
        _lighting_reset_pending = true;
        _grid_dirty = true;

        const u64 cell_count64 = static_cast<u64>(grid_resolution) * grid_resolution * grid_resolution * cascade_count;
        const u64 block_count64 = (cell_count64 + kScanBlockSize - 1u) / kScanBlockSize;
        const u64 grid_index_count64 = static_cast<u64>(max_surfel_count) * kMaxNeighborCells;
        if (cell_count64 > std::numeric_limits<u32>::max() || block_count64 > kMaxScanBlockCount ||
            grid_index_count64 > std::numeric_limits<u32>::max())
        {
            LOG_ERROR("SurfelGI: unsupported resource dimensions, resolution={}, cascades={}, surfels={}, blocks={}",
                      grid_resolution, cascade_count, max_surfel_count, block_count64);
            return;
        }

        auto create_buffer = [](EGPUBufferTarget target, u32 element_size, u32 element_num, const String &name)
        {
            BufferDesc desc;
            desc._target = target;
            desc._element_size = element_size;
            desc._element_num = element_num;
            desc._size = element_size * element_num;
            desc._is_random_write = true;
            desc._is_readable = false;
            desc._is_create_srv = true;
            desc._is_create_uav = true;
            return GPUBuffer::Create(desc, name);
        };

        _surfel_pool = create_buffer(EGPUBufferTarget::kStructured, sizeof(SurfelData), max_surfel_count,
                                     "SurfelGI_Pool");
        _surfel_lighting = create_buffer(EGPUBufferTarget::kStructured, sizeof(SurfelLightingData), max_surfel_count,
                                         "SurfelGI_Lighting");
        _surfel_state = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), 6u, "SurfelGI_State");
        _surfel_free_list = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), max_surfel_count,
                                          "SurfelGI_FreeList");
        _surfel_deferred_free_list = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), max_surfel_count,
                                                   "SurfelGI_DeferredFreeList");
        _active_surfel_indices = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), max_surfel_count,
                                               "SurfelGI_ActiveSurfelIndices");
        _surfel_active_list_indices = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), max_surfel_count,
                                                    "SurfelGI_SurfelActiveListIndices");
        _surfel_frame_state = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), 14u,
                                            "SurfelGI_FrameState");
        _grid_cells = create_buffer(EGPUBufferTarget::kStructured, sizeof(SurfelGridCellData),
                                    static_cast<u32>(cell_count64), "SurfelGI_GridCells");
        _grid_write_counters = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), static_cast<u32>(cell_count64),
                                             "SurfelGI_GridWriteCounters");
        _grid_indices = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), static_cast<u32>(grid_index_count64),
                                      "SurfelGI_GridIndices");
        _grid_block_prefix = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), static_cast<u32>(block_count64),
                                            "SurfelGI_GridBlockPrefix");
        _candidate_state = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), 6u, "SurfelGI_CandidateState");
        _spawn_occupancy = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), static_cast<u32>(cell_count64),
                                         "SurfelGI_SpawnOccupancy");
        _surfel_dispatch_args = create_buffer(EGPUBufferTarget::kIndirectArguments, sizeof(SurfelDispatchArguments), 1u,
                                              "SurfelGI_DispatchArgs");
        _draw_args = create_buffer(EGPUBufferTarget::kIndirectArguments, sizeof(SurfelDrawArguments), 1u,
                                   "SurfelGI_DrawArgs");

        Vector<SurfelData> surfel_zeroes(max_surfel_count);
        Vector<SurfelLightingData> lighting_zeroes(max_surfel_count);
        Vector<u32> state_zeroes(6u);
        Vector<u32> free_list_zeroes(max_surfel_count);
        Vector<u32> deferred_free_list_zeroes(max_surfel_count);
        Vector<u32> active_surfel_indices_zeroes(max_surfel_count);
        Vector<u32> surfel_active_list_indices(max_surfel_count, 0xffffffffu);
        Vector<u32> frame_state_zeroes(14u);
        Vector<SurfelGridCellData> cell_zeroes(static_cast<u32>(cell_count64));
        _surfel_pool->SetData(surfel_zeroes);
        _surfel_lighting->SetData(lighting_zeroes);
        _surfel_state->SetData(state_zeroes);
        _surfel_free_list->SetData(free_list_zeroes);
        _surfel_deferred_free_list->SetData(deferred_free_list_zeroes);
        _active_surfel_indices->SetData(active_surfel_indices_zeroes);
        _surfel_active_list_indices->SetData(surfel_active_list_indices);
        _surfel_frame_state->SetData(frame_state_zeroes);
        _grid_cells->SetData(cell_zeroes);
        _grid_write_counters->SetData(Vector<u32>(static_cast<u32>(cell_count64)));
        _grid_block_prefix->SetData(Vector<u32>(static_cast<u32>(block_count64)));
        _candidate_state->SetData(Vector<u32>(6u));
        _spawn_occupancy->SetData(Vector<u32>(static_cast<u32>(cell_count64)));
        SurfelDispatchArguments dispatch_args;
        _surfel_dispatch_args->SetData(reinterpret_cast<const u8 *>(&dispatch_args), sizeof(dispatch_args));
        SurfelDrawArguments draw_args;
        _draw_args->SetData(reinterpret_cast<const u8 *>(&draw_args), sizeof(draw_args));

        _resource_max_surfel_count = max_surfel_count;
        _resource_grid_resolution = grid_resolution;
        _resource_cascade_count = cascade_count;
        _resource_grid_block_count = static_cast<u32>(block_count64);
        _grid_info.resize(cascade_count);
    }

    void SurfelGIFeature::EnsureCandidateResources(u32 width, u32 height)
    {
        const u64 tile_count = ((static_cast<u64>(width) + 15u) / 16u) *
                               ((static_cast<u64>(height) + 15u) / 16u);
        if (tile_count == 0u || tile_count > std::numeric_limits<u32>::max() / sizeof(SurfelCandidate))
        {
            LOG_ERROR("SurfelGI: unsupported candidate dimensions {}x{}", width, height);
            return;
        }

        const u32 candidate_capacity = static_cast<u32>(tile_count);
        if (_surfel_candidates != nullptr && _resource_candidate_capacity == candidate_capacity)
            return;

        _surfel_candidates.reset();
        BufferDesc desc;
        desc._target = EGPUBufferTarget::kStructured;
        desc._element_size = sizeof(SurfelCandidate);
        desc._element_num = candidate_capacity;
        desc._size = desc._element_size * desc._element_num;
        desc._is_random_write = true;
        desc._is_readable = false;
        desc._is_create_srv = true;
        desc._is_create_uav = true;
        _surfel_candidates = GPUBuffer::Create(desc, "SurfelGI_Candidates");
        _surfel_candidates->SetData(Vector<SurfelCandidate>(candidate_capacity));
        _resource_candidate_capacity = candidate_capacity;
    }

    void SurfelGIFeature::SetComputeParameters(const RenderingData &rendering_data)
    {
        if (_compute_shader == nullptr || rendering_data._camera == nullptr)
            return;

        const f32 tan_half_fov = std::tan(rendering_data._camera->FovH() * kPi / 360.0f);
        _compute_shader->SetVectorArray(kGridInfoPropertyId, _grid_info);
        _compute_shader->SetVector(kSurfelParamsPropertyId, Vector4f(_settings._base_cell_size,
                                                                     _settings._target_pixel_radius, 0.2f, 0.7f));
        _compute_shader->SetVector(kSpawnParamsPropertyId,
                                   Vector4f(_settings._spawn_spacing_scale, _settings._spawn_normal_threshold,
                                            _settings._blue_noise_strength, 0.0f));
        _compute_shader->SetVector(kCameraParamsPropertyId, Vector4f(tan_half_fov,
                                                                     static_cast<f32>(rendering_data._height),
                                                                     _settings._spawn_coverage_threshold, 0.0f));
        _compute_shader->SetInt(kGridResolutionPropertyId, static_cast<i32>(_resource_grid_resolution));
        _compute_shader->SetInt(kCascadeCountPropertyId, static_cast<i32>(_resource_cascade_count));
        _compute_shader->SetInt(kMaxSurfelCountPropertyId, static_cast<i32>(_resource_max_surfel_count));
        _compute_shader->SetInt(kMaxSpawnPerFramePropertyId, static_cast<i32>(_settings._max_spawn_per_frame));
        _compute_shader->SetInt(kCandidateCapacityPropertyId, static_cast<i32>(_resource_candidate_capacity));
        _compute_shader->SetBuffer(kSurfelPoolPropertyId, _surfel_pool.get());
        _compute_shader->SetBuffer(kSurfelPoolWritePropertyId, _surfel_pool.get());
        if (_surfel_lighting != nullptr)
        {
            _compute_shader->SetBuffer(kSurfelLightingWritePropertyId, _surfel_lighting.get());
            _compute_shader->SetBuffer(kSurfelLightingPropertyId, _surfel_lighting.get());
        }
        _compute_shader->SetBuffer(kSurfelStateReadPropertyId, _surfel_state.get());
        _compute_shader->SetBuffer(kSurfelStatePropertyId, _surfel_state.get());
        _compute_shader->SetBuffer(kSurfelFreeListPropertyId, _surfel_free_list.get());
        _compute_shader->SetBuffer(kSurfelDeferredFreeListPropertyId, _surfel_deferred_free_list.get());
        _compute_shader->SetBuffer(kSurfelDeferredFreeListReadPropertyId, _surfel_deferred_free_list.get());
        _compute_shader->SetBuffer(Shader::PropertyID("_ActiveSurfelIndices"), _active_surfel_indices.get());
        _compute_shader->SetBuffer(Shader::PropertyID("_SurfelActiveListIndices"),
                                   _surfel_active_list_indices.get());
        _compute_shader->SetBuffer(kSurfelFrameStatePropertyId, _surfel_frame_state.get());
        if (_surfel_candidates != nullptr)
            _compute_shader->SetBuffer(kSurfelCandidatesPropertyId, _surfel_candidates.get());
        if (_candidate_state != nullptr)
            _compute_shader->SetBuffer(kCandidateStatePropertyId, _candidate_state.get());
        if (_spawn_occupancy != nullptr)
            _compute_shader->SetBuffer(kSpawnOccupancyPropertyId, _spawn_occupancy.get());
        _compute_shader->SetBuffer(kGridCellsPropertyId, _grid_cells.get());
        _compute_shader->SetBuffer(kGridCellsReadPropertyId, _grid_cells.get());
        _compute_shader->SetBuffer(kGridWriteCountersPropertyId, _grid_write_counters.get());
        _compute_shader->SetBuffer(kGridIndicesPropertyId, _grid_indices.get());
        _compute_shader->SetBuffer(kGridIndicesReadPropertyId, _grid_indices.get());
        _compute_shader->SetBuffer(kGridBlockPrefixPropertyId, _grid_block_prefix.get());
        _compute_shader->SetBuffer(kSurfelDispatchArgsPropertyId, _surfel_dispatch_args.get());
        _compute_shader->SetBuffer(kDrawArgsPropertyId, _draw_args.get());
    }

    void SurfelGIFeature::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
    {
        _migration_finalize_pending = false;
        _ready_for_current_view = false;
        _coverage_handle = {};
        // Probe captures have no copied camera depth and must not update the camera-dependent pool.
        if (renderer.IsRenderingProbe())
            return;
        if (!_is_active || rendering_data._scene == nullptr || rendering_data._camera == nullptr)
            return;

        SyncSceneSettings(*rendering_data._scene);
        if (!_has_active_component)
            return;

        if (_lighting_scene != rendering_data._scene)
        {
            _surfel_pool.reset();
            _lighting_scene = rendering_data._scene;
        }

        const bool run_spawn = !_settings._manual_spawn || _spawn_once_pending;
        const bool run_migration = !_settings._manual_spawn || _spawn_once_pending;
        EnsureGpuResources();
        if (run_spawn && _surfel_pool != nullptr)
            EnsureCandidateResources(rendering_data._width, rendering_data._height);

        // Resource creation is asynchronous, including candidate-buffer recreation after a resize.
        // Keep pending spawn/grid work intact until every buffer used by these passes is ready.
        for (auto *buffer : {_surfel_pool.get(), _surfel_lighting.get(), _surfel_state.get(),
                             _surfel_free_list.get(), _surfel_deferred_free_list.get(),
                             _active_surfel_indices.get(), _surfel_active_list_indices.get(),
                             _surfel_frame_state.get(), _grid_cells.get(), _grid_write_counters.get(),
                             _grid_indices.get(), _grid_block_prefix.get(), _candidate_state.get(),
                             _spawn_occupancy.get(), _surfel_dispatch_args.get(), _draw_args.get()})
        {
            if (buffer == nullptr || !buffer->IsReady())
                return;
        }
        if (run_spawn && (_surfel_candidates == nullptr || !_surfel_candidates->IsReady() ||
                          _blue_noise_texture == nullptr || !_blue_noise_texture->IsReady()))
            return;
        _ready_for_current_view = true;
        if (_surfel_pool != nullptr)
        {
            const Vector3f camera_position = rendering_data._camera->Position();
            bool grid_origin_changed = false;
            for (u32 level = 0u; level < _resource_cascade_count; ++level)
            {
                const f32 cell_size = _settings._base_cell_size * static_cast<f32>(1u << level);
                const Vector3f center(std::floor(camera_position.x / cell_size) * cell_size,
                                      std::floor(camera_position.y / cell_size) * cell_size,
                                      std::floor(camera_position.z / cell_size) * cell_size);
                const f32 world_size = cell_size * static_cast<f32>(_resource_grid_resolution);
                const Vector4f next_grid_info(center - Vector3f(world_size * 0.5f), cell_size);
                const Vector4f previous_grid_info = _grid_info[level];
                grid_origin_changed |= previous_grid_info.x != next_grid_info.x ||
                                       previous_grid_info.y != next_grid_info.y ||
                                       previous_grid_info.z != next_grid_info.z ||
                                       previous_grid_info.w != next_grid_info.w;
                _grid_info[level] = next_grid_info;
            }

            const bool query_enabled = _settings._enabled && _settings._lighting_enabled;
            const bool run_grid = !_settings._manual_spawn || _spawn_once_pending || _settings._debug_coverage ||
                                  (query_enabled && (_grid_dirty || grid_origin_changed));
            _placement_pass.SetGridEnabled(run_grid);
            _placement_pass.SetMigrationEnabled(run_migration);
            _placement_pass.SetSpawnEnabled(run_spawn);
            _migration_finalize_pending = run_grid && run_migration;
            if (run_grid)
                _grid_dirty = run_spawn;
            else if (grid_origin_changed)
                _grid_dirty = true;
            if (_settings._debug_draw_surfels || _settings._debug_coverage)
            {
                ++_debug_stats_frame_counter;
                if (_debug_stats_frame_counter >= 300u)
                {
                    _debug_stats_frame_counter = 0u;
                    _debug_readback_pending = true;
                }
            }
            renderer.EnqueuePass(&_placement_pass);
            _spawn_once_pending = false;
        }

        const u64 frame = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
        if (_settings._lighting_enabled && _surfel_pool != nullptr && _surfel_lighting != nullptr &&
            _lighting_compute_shader != nullptr && _lighting_update_kernel != kInvalidComputeShaderKernelId &&
            _last_lighting_update_frame != frame)
        {
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
                const u64 signature = CalculateLightingSignature(
                    rendering_data, *_scene_rt_proxy, _settings._lighting_max_ray_distance,
                    _lighting_transforms, _lighting_lights);
                if (signature != _lighting_signature)
                {
                    _lighting_signature = signature;
                    _lighting_reset_pending = true;
                    _lighting_update_cursor = 0u;
                }
            }
            renderer.EnqueuePass(&_lighting_pass);
            _last_lighting_update_frame = frame;
        }
        if (_settings._debug_draw_surfels || _settings._debug_coverage || _settings._debug_draw_tile_grid ||
            _migration_finalize_pending)
            renderer.EnqueuePass(&_debug_pass);
    }

    SurfelPlacementPass::SurfelPlacementPass(SurfelGIFeature &feature)
        : RenderPass("Surfel Placement"), _feature(&feature)
    {
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }

    void SurfelPlacementPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr || _feature->_compute_shader == nullptr || _feature->_surfel_pool == nullptr ||
            _feature->_surfel_lighting == nullptr ||
            _feature->_surfel_free_list == nullptr || _feature->_surfel_deferred_free_list == nullptr ||
            _feature->_surfel_frame_state == nullptr)
            return;

        _feature->SetComputeParameters(rendering_data);
        RDG::RGHandle grid_cells = graph.Import(_feature->_grid_cells.get());
        RDG::RGHandle grid_write_counters = graph.Import(_feature->_grid_write_counters.get());
        RDG::RGHandle grid_indices = graph.Import(_feature->_grid_indices.get());
        RDG::RGHandle grid_block_prefix = graph.Import(_feature->_grid_block_prefix.get());
        RDG::RGHandle surfel_pool = graph.Import(_feature->_surfel_pool.get());
        RDG::RGHandle surfel_state = graph.Import(_feature->_surfel_state.get());
        RDG::RGHandle surfel_free_list = graph.Import(_feature->_surfel_free_list.get());
        RDG::RGHandle surfel_deferred_free_list = graph.Import(_feature->_surfel_deferred_free_list.get());
        RDG::RGHandle active_surfel_indices = graph.Import(_feature->_active_surfel_indices.get());
        RDG::RGHandle surfel_active_list_indices = graph.Import(_feature->_surfel_active_list_indices.get());
        RDG::RGHandle surfel_frame_state = graph.Import(_feature->_surfel_frame_state.get());
        RDG::RGHandle surfel_candidates;
        RDG::RGHandle candidate_state;
        RDG::RGHandle spawn_occupancy;
        RDG::RGHandle surfel_lighting;
        const bool run_spawn = _spawn_enabled && _feature->_surfel_candidates != nullptr &&
                               _feature->_candidate_state != nullptr && _feature->_spawn_occupancy != nullptr &&
                               _feature->_blue_noise_texture != nullptr;
        if (run_spawn)
        {
            surfel_candidates = graph.Import(_feature->_surfel_candidates.get());
            candidate_state = graph.Import(_feature->_candidate_state.get());
            spawn_occupancy = graph.Import(_feature->_spawn_occupancy.get());
            surfel_lighting = graph.Import(_feature->_surfel_lighting.get());
        }
        RDG::RGHandle dispatch_args = graph.Import(_feature->_surfel_dispatch_args.get());
        RDG::RGHandle draw_args = graph.Import(_feature->_draw_args.get());
        const u32 cell_count = _feature->_resource_grid_resolution * _feature->_resource_grid_resolution *
                               _feature->_resource_grid_resolution * _feature->_resource_cascade_count;

        graph.AddPass("Surfel Frame State Clear", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                surfel_frame_state = builder.Write(surfel_frame_state, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_frame_state_clear_kernel, 1u, 1u, 1u);
                cmd->UavBarrier(_feature->_surfel_frame_state.get());
            });

        // Deferred entries cannot exceed high_watermark, so one indirect range covers both list flush and maintenance.
        graph.AddPass("Surfel Prepare Maintenance Dispatch", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(surfel_state, EResourceUsage::kReadSRV);
                dispatch_args = builder.Write(dispatch_args, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_prepare_dispatch_kernel, 1u, 1u, 1u);
            });

        graph.AddPass("Surfel Flush Deferred Free List", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(dispatch_args, EResourceUsage::kIndirectArgument);
                builder.Read(surfel_deferred_free_list, EResourceUsage::kReadSRV);
                builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                surfel_state = builder.WriteAfter(surfel_state, EResourceUsage::kWriteUAV);
                surfel_free_list = builder.WriteAfter(surfel_free_list, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_flush_deferred_free_list_kernel,
                              _feature->_surfel_dispatch_args.get(), 0u);
                cmd->UavBarrier();
            });

        graph.AddPass("Surfel Reset Deferred Free Count", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                surfel_state = builder.WriteAfter(surfel_state, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_reset_deferred_free_count_kernel,
                              1u, 1u, 1u);
            });

        if (_migration_enabled)
        {
            graph.AddPass("Surfel Maintenance", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(dispatch_args, EResourceUsage::kIndirectArgument);
                    surfel_pool = builder.WriteAfter(surfel_pool, EResourceUsage::kWriteUAV);
                    surfel_state = builder.WriteAfter(surfel_state, EResourceUsage::kWriteUAV);
                    surfel_lighting = builder.WriteAfter(surfel_lighting, EResourceUsage::kWriteUAV);
                    surfel_free_list = builder.WriteAfter(surfel_free_list, EResourceUsage::kWriteUAV);
                    surfel_deferred_free_list = builder.WriteAfter(surfel_deferred_free_list,
                                                                     EResourceUsage::kWriteUAV);
                    surfel_frame_state = builder.WriteAfter(surfel_frame_state, EResourceUsage::kWriteUAV);
                },
                [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_maintenance_kernel,
                                  _feature->_surfel_dispatch_args.get(), 0u);
                    cmd->UavBarrier();
                });
        }

        auto add_prepare_draw_args = [&]()
        {
            graph.AddPass("Surfel Prepare Draw Args", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(surfel_state, EResourceUsage::kReadSRV);
                    draw_args = builder.Write(draw_args, EResourceUsage::kWriteUAV);
                },
                [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_prepare_draw_args_kernel, 1u, 1u, 1u);
                });
        };

        if (!_grid_enabled)
        {
            graph.AddPass("Surfel Active List Remove", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(surfel_deferred_free_list, EResourceUsage::kReadSRV);
                    builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                    surfel_state = builder.WriteAfter(surfel_state, EResourceUsage::kWriteUAV);
                    active_surfel_indices = builder.WriteAfter(active_surfel_indices, EResourceUsage::kWriteUAV);
                    surfel_active_list_indices = builder.WriteAfter(surfel_active_list_indices,
                                                                     EResourceUsage::kWriteUAV);
                },
                [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_active_list_remove_kernel, 1u, 1u, 1u);
                    cmd->UavBarrier();
                });
            add_prepare_draw_args();
            return;
        }

        graph.AddPass("Surfel Grid Clear", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                grid_cells = builder.WriteAfter(grid_cells, EResourceUsage::kWriteUAV);
                grid_write_counters = builder.Write(grid_write_counters, EResourceUsage::kWriteUAV);
            },
            [this, cell_count](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                const u32 x = CalculateDispatchCount(cell_count, 64u);
                const u32 y = 1u;
                const u32 z = 1u;
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_clear_kernel, x, y, z);
                cmd->UavBarrier();
            });

        graph.AddPass("Surfel Grid Count", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                builder.Read(surfel_state, EResourceUsage::kReadSRV);
                builder.Read(dispatch_args, EResourceUsage::kIndirectArgument);
                grid_cells = builder.WriteAfter(grid_cells, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_count_kernel,
                              _feature->_surfel_dispatch_args.get(), 0u);
                cmd->UavBarrier(_feature->_grid_cells.get());
            });

        graph.AddPass("Surfel Grid Prefix Local", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                grid_cells = builder.WriteAfter(grid_cells, EResourceUsage::kWriteUAV);
                grid_block_prefix = builder.WriteAfter(grid_block_prefix, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_prefix_local_kernel,
                              _feature->_resource_grid_block_count, 1u, 1u);
                cmd->UavBarrier();
            });

        graph.AddPass("Surfel Grid Prefix Blocks", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                grid_block_prefix = builder.WriteAfter(grid_block_prefix, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_prefix_blocks_kernel, 1u, 1u, 1u);
                cmd->UavBarrier(_feature->_grid_block_prefix.get());
            });

        graph.AddPass("Surfel Grid Prefix Finalize", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(grid_block_prefix, EResourceUsage::kWriteUAV);
                grid_cells = builder.WriteAfter(grid_cells, EResourceUsage::kWriteUAV);
            },
            [this, cell_count](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_prefix_finalize_kernel,
                              CalculateDispatchCount(cell_count, kScanFinalizeThreadCount), 1u, 1u);
                cmd->UavBarrier(_feature->_grid_cells.get());
            });

        graph.AddPass("Surfel Grid Fill", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                builder.Read(surfel_state, EResourceUsage::kReadSRV);
                builder.Read(dispatch_args, EResourceUsage::kIndirectArgument);
                builder.Read(grid_cells, EResourceUsage::kWriteUAV);
                grid_write_counters = builder.WriteAfter(grid_write_counters, EResourceUsage::kWriteUAV);
                grid_indices = builder.Write(grid_indices, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_fill_kernel,
                              _feature->_surfel_dispatch_args.get(), 0u);
                cmd->UavBarrier();
            });

        if (_migration_enabled)
        {
            // Keep the source pool immutable while every surfel tests its overlap rank.
            graph.AddPass("Surfel Migration Resolve", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(dispatch_args, EResourceUsage::kIndirectArgument);
                    builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                    builder.Read(surfel_lighting, EResourceUsage::kReadSRV);
                    builder.Read(grid_cells, EResourceUsage::kReadSRV);
                    builder.Read(grid_indices, EResourceUsage::kReadSRV);
                    surfel_state = builder.WriteAfter(surfel_state, EResourceUsage::kWriteUAV);
                    surfel_deferred_free_list = builder.WriteAfter(surfel_deferred_free_list,
                                                                    EResourceUsage::kWriteUAV);
                },
                [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_migration_resolve_kernel,
                                  _feature->_surfel_dispatch_args.get(), 0u);
                    cmd->UavBarrier();
                });

            graph.AddPass("Surfel Migration Recycle", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(dispatch_args, EResourceUsage::kIndirectArgument);
                    builder.Read(surfel_deferred_free_list, EResourceUsage::kReadSRV);
                    surfel_pool = builder.WriteAfter(surfel_pool, EResourceUsage::kWriteUAV);
                    surfel_state = builder.WriteAfter(surfel_state, EResourceUsage::kWriteUAV);
                    surfel_frame_state = builder.WriteAfter(surfel_frame_state, EResourceUsage::kWriteUAV);
                },
                [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_migration_recycle_kernel,
                                  _feature->_surfel_dispatch_args.get(), 0u);
                    cmd->UavBarrier();
                });
        }

        graph.AddPass("Surfel Active List Remove", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(surfel_deferred_free_list, EResourceUsage::kReadSRV);
                builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                surfel_state = builder.WriteAfter(surfel_state, EResourceUsage::kWriteUAV);
                active_surfel_indices = builder.WriteAfter(active_surfel_indices, EResourceUsage::kWriteUAV);
                surfel_active_list_indices = builder.WriteAfter(surfel_active_list_indices,
                                                                 EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_active_list_remove_kernel, 1u, 1u, 1u);
                cmd->UavBarrier();
            });

        TextureDesc coverage_desc(rendering_data._width, rendering_data._height, ERenderTargetFormat::kRFloat);
        coverage_desc._is_random_access = true;
        coverage_desc._mip_num = 1u;
        graph.AddPass("Surfel Coverage", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(rendering_data._rg_handles._depth_tex, EResourceUsage::kReadSRV);
                builder.Read(rendering_data._rg_handles._gbuffers[0], EResourceUsage::kReadSRV);
                builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                builder.Read(grid_cells, EResourceUsage::kReadSRV);
                builder.Read(grid_indices, EResourceUsage::kReadSRV);
                _feature->_coverage_handle = builder.AllocTexture(coverage_desc, "SurfelGI_Coverage");
                _feature->_coverage_handle = builder.Write(_feature->_coverage_handle, EResourceUsage::kWriteUAV);
                rendering_data._rg_handles._surfel_coverage = _feature->_coverage_handle;
            },
            [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                _feature->_compute_shader->SetTexture(_feature->_coverage_kernel, kCameraDepthTexturePropertyId,
                                                      graph.Resolve<Texture>(data._rg_handles._depth_tex));
                _feature->_compute_shader->SetTexture(_feature->_coverage_kernel, kCameraNormalsTexturePropertyId,
                                                      graph.Resolve<Texture>(data._rg_handles._gbuffers[0]));
                _feature->_compute_shader->SetTexture(_feature->_coverage_kernel, kCoverageTexturePropertyId,
                                                      graph.Resolve<Texture>(data._rg_handles._surfel_coverage));
                const auto [x, y, z] = _feature->_compute_shader->CalculateDispatchNum(
                    _feature->_coverage_kernel, data._width, data._height, 1u);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_coverage_kernel, x, y, z);
            });

        if (run_spawn)
        {
            graph.AddPass("Surfel Candidate Clear", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    candidate_state = builder.WriteAfter(candidate_state, EResourceUsage::kWriteUAV);
                    spawn_occupancy = builder.WriteAfter(spawn_occupancy, EResourceUsage::kWriteUAV);
                },
                [this, cell_count](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_candidate_clear_kernel,
                                  CalculateDispatchCount(cell_count, 64u), 1u, 1u);
                });

            graph.AddPass("Surfel Candidate Generate", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(_feature->_coverage_handle, EResourceUsage::kReadSRV);
                    builder.Read(rendering_data._rg_handles._depth_tex, EResourceUsage::kReadSRV);
                    builder.Read(rendering_data._rg_handles._gbuffers[0], EResourceUsage::kReadSRV);
                    surfel_candidates = builder.WriteAfter(surfel_candidates, EResourceUsage::kWriteUAV);
                    candidate_state = builder.WriteAfter(candidate_state, EResourceUsage::kWriteUAV);
                },
                [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    _feature->_compute_shader->SetTexture(_feature->_candidate_kernel, kCameraDepthTexturePropertyId,
                                                          graph.Resolve<Texture>(data._rg_handles._depth_tex));
                    _feature->_compute_shader->SetTexture(_feature->_candidate_kernel,
                                                          kCameraNormalsTexturePropertyId,
                                                          graph.Resolve<Texture>(data._rg_handles._gbuffers[0]));
                    _feature->_compute_shader->SetTexture(_feature->_candidate_kernel, kCoverageTexturePropertyId,
                                                          graph.Resolve<Texture>(data._rg_handles._surfel_coverage));
                    const auto [x, y, z] = _feature->_compute_shader->CalculateDispatchNum(
                        _feature->_candidate_kernel, data._width, data._height, 1u);
                    _feature->_compute_shader->SetTexture(_feature->_candidate_kernel, kBlueNoiseTexturePropertyId,
                                                          _feature->_blue_noise_texture.get());
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_candidate_kernel, x, y, z);
                });

            const u32 candidate_group_count = CalculateDispatchCount(_feature->_resource_candidate_capacity, 64u);
            graph.AddPass("Surfel Candidate Claim", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(surfel_lighting, EResourceUsage::kReadSRV);
                    surfel_candidates = builder.WriteAfter(surfel_candidates, EResourceUsage::kWriteUAV);
                    builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                    builder.Read(grid_cells, EResourceUsage::kReadSRV);
                    builder.Read(grid_indices, EResourceUsage::kReadSRV);
                    spawn_occupancy = builder.WriteAfter(spawn_occupancy, EResourceUsage::kWriteUAV);
                    candidate_state = builder.WriteAfter(candidate_state, EResourceUsage::kWriteUAV);
                },
                [this, candidate_group_count](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_candidate_claim_kernel,
                                  candidate_group_count, 1u, 1u);
                });

            graph.AddPass("Surfel Candidate Commit", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    surfel_candidates = builder.WriteAfter(surfel_candidates, EResourceUsage::kWriteUAV);
                    spawn_occupancy = builder.WriteAfter(spawn_occupancy, EResourceUsage::kWriteUAV);
                    surfel_pool = builder.WriteAfter(surfel_pool, EResourceUsage::kWriteUAV);
                    surfel_state = builder.WriteAfter(surfel_state, EResourceUsage::kWriteUAV);
                    surfel_free_list = builder.WriteAfter(surfel_free_list, EResourceUsage::kWriteUAV);
                    surfel_lighting = builder.WriteAfter(surfel_lighting, EResourceUsage::kWriteUAV);
                    active_surfel_indices = builder.WriteAfter(active_surfel_indices, EResourceUsage::kWriteUAV);
                    surfel_active_list_indices = builder.WriteAfter(surfel_active_list_indices,
                                                                     EResourceUsage::kWriteUAV);
                    surfel_frame_state = builder.WriteAfter(surfel_frame_state, EResourceUsage::kWriteUAV);
                    candidate_state = builder.WriteAfter(candidate_state, EResourceUsage::kWriteUAV);
                },
                [this, candidate_group_count](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_candidate_commit_kernel,
                                  candidate_group_count, 1u, 1u);
                    cmd->UavBarrier();
                });
        }

        add_prepare_draw_args();
    }

    SurfelLightingPass::SurfelLightingPass(SurfelGIFeature &feature)
        : RenderPass("Surfel Lighting"), _feature(&feature)
    {
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }

    void SurfelLightingPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr || _feature->_lighting_compute_shader == nullptr ||
            _feature->_lighting_update_kernel == kInvalidComputeShaderKernelId ||
            _feature->_surfel_pool == nullptr || _feature->_surfel_state == nullptr ||
            _feature->_surfel_lighting == nullptr || _feature->_scene_rt_proxy == nullptr)
        {
            return;
        }

        GPUBuffer *primitive_data = rendering_data._scene_primitive_buffer;
        GPUBuffer *material_data = rendering_data._material_buffer;
        if (primitive_data == nullptr || material_data == nullptr)
            return;

        SceneRayTracingProxy *proxy = _feature->_scene_rt_proxy.get();
        const bool use_inline_ray_tracing = _feature->_use_inline_ray_tracing;
        RayTracingScene *rt_scene = use_inline_ray_tracing ? proxy->GetScene() : nullptr;
        if (use_inline_ray_tracing && rt_scene == nullptr)
            return;

        const SceneManagement::Scene *scene = rendering_data._scene;
        GPUBuffer *scene_triangle_data = nullptr;
        GPUBuffer *scene_tlas_data = nullptr;
        GPUBuffer *scene_blas_data = nullptr;
        if (!use_inline_ray_tracing)
        {
            if (scene == nullptr || scene->TriangleCount() == 0u)
                return;
            scene_triangle_data = scene->GetSceneMeshDataBuffer();
            scene_tlas_data = scene->GetTLASBuffer();
            scene_blas_data = scene->GetBLASBuffer();
            if (scene_triangle_data == nullptr || scene_tlas_data == nullptr || scene_blas_data == nullptr)
                return;
        }

        GPUBuffer *unified_light_data = proxy->GetUnifiedLightData();
        const u32 unified_light_count = proxy->GetUnifiedLightCount();
        if (use_inline_ray_tracing)
        {
            proxy->AddBLASUpdatePass(graph);
            proxy->AddTLASUpdatePass(graph);
        }

        RDG::RGHandle surfel_pool = graph.Import(_feature->_surfel_pool.get());
        RDG::RGHandle surfel_state = graph.Import(_feature->_surfel_state.get());
        RDG::RGHandle surfel_lighting = graph.Import(_feature->_surfel_lighting.get());
        RDG::RGHandle active_surfel_indices = graph.Import(_feature->_active_surfel_indices.get());
        RDG::RGHandle surfel_frame_state = graph.Import(_feature->_surfel_frame_state.get());
        const u32 update_budget = _feature->_settings._max_lighting_updates_per_frame;
        const u32 update_cursor = _feature->_lighting_update_cursor;
        const u32 rays_per_surfel = _feature->_settings._rays_per_surfel;
        const f32 max_ray_distance = _feature->_settings._lighting_max_ray_distance;

        if (_feature->_lighting_reset_pending)
        {
            const auto reset_kernel = _feature->_lighting_reset_kernel;
            if (reset_kernel == kInvalidComputeShaderKernelId)
                return;
            const u32 max_surfel_count = _feature->_resource_max_surfel_count;
            graph.AddPass("Surfel Lighting Reset", RDG::PassDesc(RDG::EPassType::kCompute),
                [&](RDG::RenderGraphBuilder &builder)
                {
                    surfel_lighting = builder.WriteAfter(surfel_lighting, EResourceUsage::kWriteUAV);
                },
                [this, reset_kernel, max_surfel_count](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
                {
                    auto *shader = _feature->_lighting_compute_shader.get();
                    shader->SetBuffer(reset_kernel, "_SurfelLighting", _feature->_surfel_lighting.get());
                    shader->SetInt("_LightingMaxSurfelCount", static_cast<i32>(max_surfel_count));
                    cmd->Dispatch(shader, reset_kernel, CalculateDispatchCount(max_surfel_count, 256u), 1u, 1u);
                });
            ++_feature->_lighting_reset_count;
            _feature->_last_lighting_reset_frame = g_pGfxContext != nullptr
                ? static_cast<u32>(g_pGfxContext->GetFrameCount()) : 0u;
            _feature->_lighting_reset_pending = false;
        }

        graph.AddPass("Surfel Lighting Update", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, use_inline_ray_tracing, rt_scene, proxy, primitive_data, material_data,
             scene_triangle_data, scene_tlas_data, scene_blas_data, unified_light_data]
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
                builder.Read(rendering_data._rg_handles._environment_sky);
                builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                builder.Read(surfel_state, EResourceUsage::kReadSRV);
                builder.Read(active_surfel_indices, EResourceUsage::kReadSRV);
                surfel_frame_state = builder.WriteAfter(surfel_frame_state, EResourceUsage::kWriteUAV);
                surfel_lighting = builder.WriteAfter(surfel_lighting, EResourceUsage::kWriteUAV);
            },
            [this, use_inline_ray_tracing, rt_scene, primitive_data, material_data, unified_light_data,
             unified_light_count, update_budget, update_cursor, rays_per_surfel, max_ray_distance]
            (RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                auto *shader = _feature->_lighting_compute_shader.get();
                const auto kernel = _feature->_lighting_update_kernel;
                if (shader == nullptr || cmd == nullptr)
                    return;
                if (use_inline_ray_tracing)
                {
                    if (rt_scene == nullptr || !rt_scene->IsReady())
                        return;
                    shader->SetAccelerationStructure(kernel, "g_ray_tracing_scene", rt_scene);
                }
                shader->SetTexture(kernel, "_EnvironmentSky",
                                   graph.Resolve<Texture>(data._rg_handles._environment_sky));
                shader->SetBuffer(kernel, "g_primitive_data", primitive_data);
                shader->SetBuffer(kernel, "g_material_buffer", material_data);
                if (unified_light_data != nullptr)
                    shader->SetBuffer(kernel, "_UnifiedLights", unified_light_data);
                shader->SetBuffer(kernel, "_SurfelPool", _feature->_surfel_pool.get());
                shader->SetBuffer(kernel, "_SurfelStateRead", _feature->_surfel_state.get());
                shader->SetBuffer(kernel, "_ActiveSurfelIndices", _feature->_active_surfel_indices.get());
                shader->SetBuffer(kernel, "_SurfelFrameState", _feature->_surfel_frame_state.get());
                shader->SetBuffer(kernel, "_SurfelLighting", _feature->_surfel_lighting.get());
                const u32 frame_index = g_pGfxContext != nullptr
                    ? static_cast<u32>(g_pGfxContext->GetFrameCount()) : 0u;
                shader->SetInt("_SurfelFrameIndex", static_cast<i32>(frame_index));
                shader->SetInt("_LightingUpdateCursor", static_cast<i32>(update_cursor));
                shader->SetInt("_LightingUpdateBudget", static_cast<i32>(update_budget));
                shader->SetInt("_RaysPerSurfel", static_cast<i32>(rays_per_surfel));
                shader->SetFloat("_LightingMaxRayDistance", max_ray_distance);
                shader->SetInt("_UnifiedLightCount", static_cast<i32>(unified_light_count));
                cmd->Dispatch(shader, kernel, update_budget, 1u, 1u);
                cmd->UavBarrier(_feature->_surfel_frame_state.get());
                if (_feature->_debug_readback_pending)
                {
                    _feature->_debug_readback_pending = false;
                    const u32 rays = rays_per_surfel;
                    const u32 cursor = update_cursor;
                    const u32 reset_count = _feature->_lighting_reset_count;
                    const u32 reset_frame = _feature->_last_lighting_reset_frame;
                    cmd->ReadbackBuffer(_feature->_surfel_frame_state.get(), false, sizeof(u32) * 14u,
                        [rays, cursor, reset_count, reset_frame](const u8 *bytes, u32 size)
                        {
                            if (bytes == nullptr || size < sizeof(u32) * 14u)
                                return;
                            const auto *frame_state = reinterpret_cast<const u32 *>(bytes);
                            LOG_INFO("SurfelGI pool: active={} active_list={} high_watermark={} free={} deferred={}",
                                     frame_state[11], frame_state[9], frame_state[10], frame_state[12], frame_state[13]);
                            const u32 scheduled_updates = frame_state[8];
                            const f32 effective_ratio = scheduled_updates > 0u
                                ? static_cast<f32>(frame_state[6]) / static_cast<f32>(scheduled_updates) : 1.0f;
                            const f32 average_interval = frame_state[6] > 0u
                                ? static_cast<f32>(frame_state[9]) / static_cast<f32>(frame_state[6]) : 0.0f;
                            LOG_INFO("SurfelGI lighting: valid_updates={} invalid_skipped={} requested_updates={} "
                                     "rays_per_update={} gi_rays={} avg_interval={:.2f} effective_ratio={:.3f} "
                                     "cursor={} reset_count={} last_reset_frame={} spawned={} recycled={}",
                                     frame_state[6], frame_state[7], scheduled_updates, rays, frame_state[6] * rays,
                                     average_interval, effective_ratio, cursor, reset_count, reset_frame,
                                     frame_state[0], frame_state[1]);
                        });
                }
            });

        _feature->_lighting_update_cursor += update_budget;
    }

    SurfelDebugPass::SurfelDebugPass(SurfelGIFeature &feature)
        : RenderPass("Surfel Debug"), _feature(&feature)
    {
        _event = ERenderPassEvent::kAfterRender;
    }

    void SurfelDebugPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr)
            return;

        const bool draw_coverage = _feature->_settings._debug_coverage && _feature->_coverage_handle.IsValid();
        const bool draw_tile_grid = _feature->_settings._debug_draw_tile_grid &&
                                    _feature->_tile_grid_material != nullptr;
        const bool draw_surfels = _feature->_settings._debug_draw_surfels && _feature->_surfel_pool != nullptr &&
                                  _feature->_surfel_lighting != nullptr && _feature->_draw_args != nullptr &&
                                  _feature->_disk_material != nullptr;
        const bool finalize_migration = _feature->_migration_finalize_pending &&
                                        _feature->_compute_shader != nullptr && _feature->_surfel_pool != nullptr;
        if (!draw_coverage && !draw_tile_grid && !draw_surfels && !finalize_migration)
            return;

        RDG::RGHandle color_target = rendering_data.FinalColorHandle();
        RDG::RGHandle depth_target = rendering_data._rg_handles._depth_target;
        RDG::RGHandle coverage = _feature->_coverage_handle;
        RDG::RGHandle tile_grid_source = rendering_data._rg_handles._depth_tex;
        RDG::RGHandle surfel_pool;
        RDG::RGHandle draw_args;
        RDG::RGHandle surfel_lighting;
        RDG::RGHandle surfel_state;
        RDG::RGHandle dispatch_args;
        if (draw_surfels || finalize_migration)
        {
            surfel_pool = graph.Import(_feature->_surfel_pool.get());
        }
        if (draw_surfels)
        {
            draw_args = graph.Import(_feature->_draw_args.get());
            surfel_lighting = graph.Import(_feature->_surfel_lighting.get());
        }
        if (finalize_migration)
        {
            surfel_state = graph.Import(_feature->_surfel_state.get());
            dispatch_args = graph.Import(_feature->_surfel_dispatch_args.get());
        }
        if (draw_coverage || draw_tile_grid || draw_surfels)
        {
            graph.AddPass("Surfel Debug Overlay", RDG::PassDesc(),
                [&, draw_coverage, draw_tile_grid, draw_surfels](RDG::RenderGraphBuilder &builder)
                {
                    if (draw_coverage)
                        builder.Read(coverage, EResourceUsage::kReadSRV);
                    if (draw_tile_grid)
                        builder.Read(tile_grid_source, EResourceUsage::kReadSRV);
                    if (draw_surfels)
                    {
                        builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                        builder.Read(surfel_lighting, EResourceUsage::kReadSRV);
                        builder.Read(draw_args, EResourceUsage::kIndirectArgument);
                        builder.Read(depth_target, EResourceUsage::kDSV);
                    }
                    builder.Read(color_target, EResourceUsage::kWriteRTV);
                    color_target = builder.Write(color_target, EResourceUsage::kWriteRTV);
                    rendering_data.FinalColorHandle() = color_target;
                    if (draw_surfels)
                    {
                        depth_target = builder.Write(depth_target, EResourceUsage::kDSV);
                        rendering_data._rg_handles._depth_target = depth_target;
                    }
                },
                [this, coverage, tile_grid_source, draw_coverage, draw_tile_grid, draw_surfels]
                (RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    if (draw_coverage && _feature->_coverage_debug_material != nullptr)
                        cmd->Blit(coverage, data.FinalColorHandle(), _feature->_coverage_debug_material.get());

                    if (draw_surfels)
                    {
                        _feature->_disk_material->SetBuffer(kSurfelPoolPropertyId, _feature->_surfel_pool.get());
                        _feature->_disk_material->SetBuffer(kSurfelLightingPropertyId,
                                                           _feature->_surfel_lighting.get());
                        _feature->_disk_material->SetInt(kDebugCascadePropertyId, _feature->_settings._debug_cascade);
                        _feature->_disk_material->SetInt(kDebugShowIrradiancePropertyId,
                                                        _feature->_settings._debug_show_irradiance ? 1 : 0);
                        _feature->_disk_material->SetInt(kDebugOpaqueDisksPropertyId,
                                                        _feature->_settings._debug_opaque_disks ? 1 : 0);
                        _feature->_disk_material->SetFloat(kDebugIrradianceExposurePropertyId,
                                                          _feature->_settings._debug_irradiance_exposure);
                        _feature->_disk_material->SetInt(kDebugLightingModePropertyId,
                            static_cast<i32>(_feature->_settings._debug_lighting_mode));
                        _feature->_disk_material->SetInt(Shader::PropertyID("_DebugFrameIndex"),
                            g_pGfxContext != nullptr ? static_cast<i32>(g_pGfxContext->GetFrameCount()) : 0);
                        cmd->SetRenderTarget(data.FinalColorHandle(), data._rg_handles._depth_target);
                        cmd->SetRenderTargetLoadAction(data.FinalColorHandle(), ELoadStoreAction::kLoad);
                        cmd->SetRenderTargetLoadAction(data._rg_handles._depth_target, ELoadStoreAction::kLoad);
                        cmd->DrawProceduralIndirect(_feature->_disk_material.get(), 0u, _feature->_draw_args.get());
                    }

                    if (draw_tile_grid)
                    {
                        cmd->SetRenderTargetLoadAction(data.FinalColorHandle(), ELoadStoreAction::kLoad);
                        cmd->Blit(tile_grid_source, data.FinalColorHandle(), _feature->_tile_grid_material.get());
                    }
                });
        }

        if (finalize_migration)
        {
            // Debug drawing consumes the per-frame migration flags before this cleanup pass clears them.
            graph.AddPass("Surfel Migration Finalize", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(surfel_state, EResourceUsage::kReadSRV);
                    builder.Read(dispatch_args, EResourceUsage::kIndirectArgument);
                    surfel_pool = builder.WriteAfter(surfel_pool, EResourceUsage::kWriteUAV);
                },
                [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_migration_finalize_kernel,
                                  _feature->_surfel_dispatch_args.get(), 0u);
                    cmd->UavBarrier(_feature->_surfel_pool.get());
                });
            _feature->_migration_finalize_pending = false;
        }
    }
}
