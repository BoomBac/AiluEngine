#pragma once
#ifndef __SURFEL_GI_FEATURE_H__
#define __SURFEL_GI_FEATURE_H__

#include "Assets/AssetRef.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Buffer.h"
#include "Render/Features/RenderFeature.h"
#include "Render/RenderGraph/RenderGraphFwd.h"
#include "Render/Shader.h"

#include <cstdint>

namespace Ailu::SceneManagement
{
    class Scene;
}

namespace Ailu::Render
{
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
        u32 _padding = 0u;
        u32 _spawn_pixel_x = 0u;
        u32 _spawn_pixel_y = 0u;
    };

    static_assert(sizeof(SurfelData) == 56u, "SurfelData must match the HLSL structured buffer layout");

    struct SurfelGISettings
    {
        bool _enabled = true;
        bool _debug_draw_surfels = true;
        bool _debug_coverage = false;
        bool _debug_draw_tile_grid = false;
        bool _manual_spawn = true;
        f32 _base_cell_size = 0.125f;
        u32 _cascade_count = 5u;
        u32 _grid_resolution = 64u;
        f32 _target_pixel_radius = 8.0f;
        f32 _spawn_coverage_threshold = 0.5f;
        u32 _max_surfel_count = 262144u;
        i32 _debug_cascade = -1;
    };

    class AILU_API SurfelPlacementPass final : public RenderPass
    {
    public:
        explicit SurfelPlacementPass(SurfelGIFeature &feature);
        void SetSpawnEnabled(bool enabled) { _spawn_enabled = enabled; }
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        SurfelGIFeature *_feature = nullptr;
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

    class AILU_API SurfelGIFeature final : public RenderFeature
    {
        friend class SurfelPlacementPass;
        friend class SurfelDebugPass;

    public:
        SurfelGIFeature();
        void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) final;

    private:
        void SyncSceneSettings(const SceneManagement::Scene &scene);
        void EnsureGpuResources();
        void SetComputeParameters(const RenderingData &rendering_data);

        SurfelGISettings _settings;
        bool _has_active_component = false;
        bool _spawn_once_pending = false;
        u32 _resource_max_surfel_count = 0u;
        u32 _resource_grid_resolution = 0u;
        u32 _resource_cascade_count = 0u;
        u32 _resource_grid_block_count = 0u;

        AssetRef<ComputeShader> _compute_shader;
        ComputeShaderKernelId _grid_clear_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _prepare_dispatch_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_count_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_prefix_local_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_prefix_blocks_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_prefix_finalize_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _grid_fill_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _coverage_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _spawn_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _prepare_draw_args_kernel = kInvalidComputeShaderKernelId;

        AssetRef<Shader> _disk_shader;
        AssetRef<Shader> _coverage_debug_shader;
        AssetRef<Shader> _tile_grid_shader;
        Ref<Material> _disk_material;
        Ref<Material> _coverage_debug_material;
        Ref<Material> _tile_grid_material;

        Ref<GPUBuffer> _surfel_pool;
        Ref<GPUBuffer> _surfel_state;
        Ref<GPUBuffer> _grid_cells;
        Ref<GPUBuffer> _grid_write_counters;
        Ref<GPUBuffer> _grid_indices;
        Ref<GPUBuffer> _grid_block_prefix;
        Ref<GPUBuffer> _surfel_dispatch_args;
        Ref<GPUBuffer> _draw_args;
        RDG::RGHandle _coverage_handle;

        Vector<Vector4f> _grid_info;

        SurfelPlacementPass _placement_pass;
        SurfelDebugPass _debug_pass;
    };
}

#endif // __SURFEL_GI_FEATURE_H__
