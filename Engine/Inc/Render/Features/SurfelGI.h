#pragma once
#ifndef __SURFEL_GI_FEATURE_H__
#define __SURFEL_GI_FEATURE_H__

#include "Assets/AssetRef.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Buffer.h"
#include "Render/Features/RenderFeature.h"
#include "Render/RenderGraph/RenderGraphFwd.h"
#include "Render/RayTracing/SceneRayTracingProxy.h"
#include "Render/Shader.h"

#include <cstdint>

namespace Ailu::SceneManagement
{
    class Scene;
}

namespace Ailu::Render
{
    class Texture2D;
    class SurfelGIFeature;

    struct SurfelData
    {
        Vector3f _position = Vector3f::kZero;
        f32 _radius = 0.0f;
        Vector3f _normal = Vector3f::kUp;
        u32 _flags = 0u;
        u32 _cascade_level = 0u;
        u32 _last_touched_frame = 0u;
        u32 _object_id = 0u;
        f32 _retire_start_weight = 0.0f;
        u32 _spawn_pixel_x = 0u;
        u32 _spawn_pixel_y = 0u;
    };

    static_assert(sizeof(SurfelData) == 56u, "SurfelData must match the HLSL structured buffer layout");

    struct SurfelLightingData
    {
        Vector3f _irradiance = Vector3f::kZero;
        f32 _history_weight = 0.0f;
        u32 _last_update_frame = 0u;
        u32 _sample_count = 0u;
        u32 _flags = 0u;
        u32 _padding = 0u;
        Vector3f _sampled_irradiance = Vector3f::kZero;
        f32 _sampled_padding = 0.0f;
        Vector3f _inherited_irradiance = Vector3f::kZero;
        u32 _inheritance_depth = 0u;
    };

    static_assert(sizeof(SurfelLightingData) == 64u,
                  "SurfelLightingData must match the HLSL structured buffer layout");

    struct SurfelCandidate
    {
        Vector3f _position = Vector3f::kZero;
        f32 _radius = 0.0f;
        Vector3f _normal = Vector3f::kUp;
        f32 _coverage = 0.0f;
        u32 _cascade_level = 0u;
        u32 _pixel_x = 0u;
        u32 _pixel_y = 0u;
        u32 _priority = 0u;
        Vector3f _inherited_irradiance = Vector3f::kZero;
        u32 _inheritance_depth = 0u;
    };

    static_assert(sizeof(SurfelCandidate) == 64u, "SurfelCandidate must match the HLSL structured buffer layout");

    struct SurfelGISettings
    {
        bool _enabled = true;
        bool _debug_draw_surfels = true;
        bool _debug_show_irradiance = false;
        bool _debug_opaque_disks = false;
        f32 _debug_irradiance_exposure = 1.0f;
        u32 _debug_lighting_mode = 0u;
        bool _debug_coverage = false;
        bool _debug_draw_tile_grid = false;
        bool _manual_spawn = false;
        f32 _base_cell_size = 0.125f;
        u32 _cascade_count = 5u;
        u32 _grid_resolution = 64u;
        f32 _target_pixel_radius = 8.0f;
        f32 _spawn_coverage_threshold = 0.2f;
        f32 _spawn_spacing_scale = 1.25f;
        u32 _max_spawn_per_frame = 512u;
        bool _lighting_enabled = true;
        u32 _rays_per_surfel = 4u;
        u32 _max_lighting_updates_per_frame = 256u;
        f32 _lighting_max_ray_distance = 50.0f;
        f32 _spawn_normal_threshold = 0.75f;
        f32 _blue_noise_strength = 0.15f;
        u32 _max_surfel_count = 262144u;
        i32 _debug_cascade = -1;
    };

    class AILU_API SurfelPlacementPass final : public RenderPass
    {
    public:
        explicit SurfelPlacementPass(SurfelGIFeature &feature);
        void SetGridEnabled(bool enabled) { _grid_enabled = enabled; }
        void SetMigrationEnabled(bool enabled) { _migration_enabled = enabled; }
        void SetSpawnEnabled(bool enabled) { _spawn_enabled = enabled; }
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        SurfelGIFeature *_feature = nullptr;
        bool _grid_enabled = true;
        bool _migration_enabled = true;
        bool _spawn_enabled = true;
    };

    class AILU_API SurfelDebugPass final : public RenderPass
    {
    public:
        explicit SurfelDebugPass(SurfelGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        SurfelGIFeature *_feature = nullptr;
    };

    class AILU_API SurfelLightingPass final : public RenderPass
    {
    public:
        explicit SurfelLightingPass(SurfelGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        SurfelGIFeature *_feature = nullptr;
    };

    class AILU_API SurfelGIFeature final : public RenderFeature
    {
        friend class SurfelPlacementPass;
        friend class SurfelLightingPass;
        friend class SurfelDebugPass;
        friend class DeferredLightingPass;

    public:
        SurfelGIFeature();
        void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) final;

    private:
        void SyncSceneSettings(const SceneManagement::Scene &scene);
        void EnsureGpuResources();
        void EnsureCandidateResources(u32 width, u32 height);
        void SetComputeParameters(const RenderingData &rendering_data);

        SurfelGISettings _settings;
        bool _has_active_component = false;
        bool _ready_for_current_view = false;
        bool _spawn_once_pending = false;
        bool _grid_dirty = true;
        bool _migration_finalize_pending = false;
        bool _debug_readback_pending = false;
        bool _query_debug_readback_pending = false;
        u32 _debug_stats_frame_counter = 0u;
        u32 _resource_max_surfel_count = 0u;
        u32 _resource_grid_resolution = 0u;
        u32 _resource_cascade_count = 0u;
        u32 _resource_grid_block_count = 0u;
        u32 _resource_candidate_capacity = 0u;

        AssetRef<ComputeShader> _compute_shader;
        AssetRef<Texture2D> _blue_noise_texture;
        ComputeShaderKernelId _grid_clear_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _frame_state_clear_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _prepare_dispatch_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _flush_deferred_free_list_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _reset_deferred_free_count_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _active_list_remove_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _maintenance_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_count_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_prefix_local_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_prefix_blocks_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_prefix_finalize_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_fill_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _query_grid_clear_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _query_grid_count_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _query_grid_prefix_local_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _query_grid_prefix_blocks_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _query_grid_prefix_finalize_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _query_grid_fill_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _migration_resolve_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _migration_recycle_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _coverage_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _candidate_clear_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _candidate_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _candidate_claim_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _candidate_commit_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _migration_finalize_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _prepare_draw_args_kernel = kInvalidComputeShaderKernelId;
        AssetRef<ComputeShader> _lighting_compute_shader;
        ComputeShaderKernelId _lighting_update_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _lighting_reset_kernel = kInvalidComputeShaderKernelId;
        u64 _lighting_signature = 0u;
        Vector<Matrix4x4f> _lighting_transforms;
        Vector<UnifiedLightData> _lighting_lights;
        bool _lighting_reset_pending = true;
        const SceneManagement::Scene *_lighting_scene = nullptr;
        Scope<SceneRayTracingProxy> _scene_rt_proxy;
        bool _use_inline_ray_tracing = false;
        u32 _lighting_update_cursor = 0u;
        u64 _last_lighting_update_frame = ~u64(0);
        u32 _lighting_reset_count = 0u;
        u32 _last_lighting_reset_frame = 0u;

        AssetRef<Shader> _disk_shader;
        AssetRef<Shader> _coverage_debug_shader;
        AssetRef<Shader> _tile_grid_shader;
        Ref<Material> _disk_material;
        Ref<Material> _coverage_debug_material;
        Ref<Material> _tile_grid_material;

        Ref<GPUBuffer> _surfel_pool;
        Ref<GPUBuffer> _surfel_lighting;
        Ref<GPUBuffer> _surfel_state;
        Ref<GPUBuffer> _surfel_free_list;
        Ref<GPUBuffer> _surfel_deferred_free_list;
        Ref<GPUBuffer> _active_surfel_indices;
        Ref<GPUBuffer> _surfel_active_list_indices;
        Ref<GPUBuffer> _surfel_frame_state;
        Ref<GPUBuffer> _grid_cells;
        Ref<GPUBuffer> _grid_write_counters;
        Ref<GPUBuffer> _grid_indices;
        Ref<GPUBuffer> _grid_block_prefix;
        Ref<GPUBuffer> _query_grid_cells;
        Ref<GPUBuffer> _query_grid_write_counters;
        Ref<GPUBuffer> _query_grid_indices;
        Ref<GPUBuffer> _query_grid_block_prefix;
        Ref<GPUBuffer> _query_debug_counters;
        Ref<GPUBuffer> _surfel_candidates;
        Ref<GPUBuffer> _candidate_state;
        Ref<GPUBuffer> _spawn_occupancy;
        Ref<GPUBuffer> _surfel_dispatch_args;
        Ref<GPUBuffer> _draw_args;
        RDG::RGHandle _coverage_handle;

        Vector<Vector4f> _grid_info;

        SurfelPlacementPass _placement_pass;
        SurfelLightingPass _lighting_pass;
        SurfelDebugPass _debug_pass;
    };
}

#endif // __SURFEL_GI_FEATURE_H__
