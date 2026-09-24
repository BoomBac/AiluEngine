#include "Render/Features/SurfelGI.h"

#include "Framework/Common/ResourceMgr.h"
#include "Framework/Math/MatrixMath.h"
#include "Render/Camera.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
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
        constexpr u32 kMaxCascadeCount = 5u;
        constexpr u32 kMaxNeighborCells = 27u;
        constexpr u32 kScanBlockSize = 1024u;
        constexpr u32 kScanFinalizeThreadCount = 256u;
        constexpr u32 kMaxScanBlockCount = 2048u;
        constexpr f32 kPi = 3.14159265358979323846f;

        const ShaderPropertyId kGridInfoPropertyId = Shader::PropertyID("_GridInfo");
        const ShaderPropertyId kSurfelParamsPropertyId = Shader::PropertyID("_SurfelParams");
        const ShaderPropertyId kCameraParamsPropertyId = Shader::PropertyID("_CameraParams");
        const ShaderPropertyId kGridResolutionPropertyId = Shader::PropertyID("_GridResolution");
        const ShaderPropertyId kCascadeCountPropertyId = Shader::PropertyID("_CascadeCount");
        const ShaderPropertyId kMaxSurfelCountPropertyId = Shader::PropertyID("_MaxSurfelCount");
        const ShaderPropertyId kSurfelPoolPropertyId = Shader::PropertyID("_SurfelPool");
        const ShaderPropertyId kSurfelPoolWritePropertyId = Shader::PropertyID("_SurfelPoolWrite");
        const ShaderPropertyId kSurfelStatePropertyId = Shader::PropertyID("_SurfelState");
        const ShaderPropertyId kGridCellsPropertyId = Shader::PropertyID("_GridCells");
        const ShaderPropertyId kGridWriteCountersPropertyId = Shader::PropertyID("_GridWriteCounters");
        const ShaderPropertyId kGridIndicesPropertyId = Shader::PropertyID("_GridIndices");
        const ShaderPropertyId kGridBlockPrefixPropertyId = Shader::PropertyID("_GridBlockPrefix");
        const ShaderPropertyId kSurfelDispatchArgsPropertyId = Shader::PropertyID("_SurfelDispatchArgs");
        const ShaderPropertyId kDrawArgsPropertyId = Shader::PropertyID("_DrawArgs");
        const ShaderPropertyId kCameraDepthTexturePropertyId = Shader::PropertyID("_CameraDepthTexture");
        const ShaderPropertyId kCameraNormalsTexturePropertyId = Shader::PropertyID("_CameraNormalsTexture");
        const ShaderPropertyId kCoverageTexturePropertyId = Shader::PropertyID("_CoverageTexture");
        const ShaderPropertyId kDebugCascadePropertyId = Shader::PropertyID("_DebugCascade");

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
    }

    SurfelGIFeature::SurfelGIFeature()
        : RenderFeature("SurfelGI"), _placement_pass(*this), _debug_pass(*this)
    {
        _is_active = true;
        _compute_shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(
            L"Shaders/hlsl/Compute/surfel_placement.alasset");
        if (_compute_shader != nullptr)
        {
            _grid_clear_kernel = _compute_shader->FindKernel("SurfelGridClearCS");
            _prepare_dispatch_kernel = _compute_shader->FindKernel("SurfelPrepareDispatchCS");
            _grid_count_kernel = _compute_shader->FindKernel("SurfelGridCountCS");
            _grid_prefix_local_kernel = _compute_shader->FindKernel("SurfelGridPrefixLocalCS");
            _grid_prefix_blocks_kernel = _compute_shader->FindKernel("SurfelGridPrefixBlocksCS");
            _grid_prefix_finalize_kernel = _compute_shader->FindKernel("SurfelGridPrefixFinalizeCS");
            _grid_fill_kernel = _compute_shader->FindKernel("SurfelGridFillCS");
            _coverage_kernel = _compute_shader->FindKernel("SurfelCoverageCS");
            _spawn_kernel = _compute_shader->FindKernel("SurfelSpawnCS");
            _prepare_draw_args_kernel = _compute_shader->FindKernel("SurfelPrepareDrawArgsCS");
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
            _settings._max_surfel_count = ClampMaxSurfelCount(component._max_surfel_count);
            _settings._debug_cascade =
                std::clamp(component._debug_cascade, -1, static_cast<i32>(kMaxCascadeCount - 1u));
            _spawn_once_pending = component._spawn_once_requested && _settings._manual_spawn;
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
            _resource_grid_resolution == grid_resolution && _resource_cascade_count == cascade_count)
            return;

        _surfel_pool.reset();
        _surfel_state.reset();
        _grid_cells.reset();
        _grid_write_counters.reset();
        _grid_indices.reset();
        _grid_block_prefix.reset();
        _surfel_dispatch_args.reset();
        _draw_args.reset();

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
        _surfel_state = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), 4u, "SurfelGI_State");
        _grid_cells = create_buffer(EGPUBufferTarget::kStructured, sizeof(SurfelGridCellData),
                                    static_cast<u32>(cell_count64), "SurfelGI_GridCells");
        _grid_write_counters = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), static_cast<u32>(cell_count64),
                                             "SurfelGI_GridWriteCounters");
        _grid_indices = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), static_cast<u32>(grid_index_count64),
                                      "SurfelGI_GridIndices");
        _grid_block_prefix = create_buffer(EGPUBufferTarget::kStructured, sizeof(u32), static_cast<u32>(block_count64),
                                           "SurfelGI_GridBlockPrefix");
        _surfel_dispatch_args = create_buffer(EGPUBufferTarget::kIndirectArguments, sizeof(SurfelDispatchArguments), 1u,
                                              "SurfelGI_DispatchArgs");
        _draw_args = create_buffer(EGPUBufferTarget::kIndirectArguments, sizeof(SurfelDrawArguments), 1u,
                                   "SurfelGI_DrawArgs");

        Vector<SurfelData> surfel_zeroes(max_surfel_count);
        Vector<u32> state_zeroes(4u);
        Vector<SurfelGridCellData> cell_zeroes(static_cast<u32>(cell_count64));
        _surfel_pool->SetData(surfel_zeroes);
        _surfel_state->SetData(state_zeroes);
        _grid_cells->SetData(cell_zeroes);
        _grid_write_counters->SetData(Vector<u32>(static_cast<u32>(cell_count64)));
        _grid_block_prefix->SetData(Vector<u32>(static_cast<u32>(block_count64)));
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

    void SurfelGIFeature::SetComputeParameters(const RenderingData &rendering_data)
    {
        if (_compute_shader == nullptr || rendering_data._camera == nullptr)
            return;

        const f32 tan_half_fov = std::tan(rendering_data._camera->FovH() * kPi / 360.0f);
        _compute_shader->SetVectorArray(kGridInfoPropertyId, _grid_info);
        _compute_shader->SetVector(kSurfelParamsPropertyId, Vector4f(_settings._base_cell_size,
                                                                     _settings._target_pixel_radius, 0.2f, 0.7f));
        _compute_shader->SetVector(kCameraParamsPropertyId, Vector4f(tan_half_fov,
                                                                     static_cast<f32>(rendering_data._height),
                                                                     _settings._spawn_coverage_threshold, 0.0f));
        _compute_shader->SetInt(kGridResolutionPropertyId, static_cast<i32>(_resource_grid_resolution));
        _compute_shader->SetInt(kCascadeCountPropertyId, static_cast<i32>(_resource_cascade_count));
        _compute_shader->SetInt(kMaxSurfelCountPropertyId, static_cast<i32>(_resource_max_surfel_count));
        _compute_shader->SetBuffer(kSurfelPoolPropertyId, _surfel_pool.get());
        _compute_shader->SetBuffer(kSurfelPoolWritePropertyId, _surfel_pool.get());
        _compute_shader->SetBuffer(kSurfelStatePropertyId, _surfel_state.get());
        _compute_shader->SetBuffer(kGridCellsPropertyId, _grid_cells.get());
        _compute_shader->SetBuffer(kGridWriteCountersPropertyId, _grid_write_counters.get());
        _compute_shader->SetBuffer(kGridIndicesPropertyId, _grid_indices.get());
        _compute_shader->SetBuffer(kGridBlockPrefixPropertyId, _grid_block_prefix.get());
        _compute_shader->SetBuffer(kSurfelDispatchArgsPropertyId, _surfel_dispatch_args.get());
        _compute_shader->SetBuffer(kDrawArgsPropertyId, _draw_args.get());
    }

    void SurfelGIFeature::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
    {
        if (!_is_active || rendering_data._scene == nullptr || rendering_data._camera == nullptr)
            return;

        SyncSceneSettings(*rendering_data._scene);
        if (!_has_active_component)
            return;

        const bool run_placement = !_settings._manual_spawn || _spawn_once_pending || _settings._debug_coverage;
        if (run_placement || _settings._debug_draw_surfels)
            EnsureGpuResources();

        _coverage_handle = {};
        if (run_placement && _surfel_pool != nullptr)
        {
            const Vector3f camera_position = rendering_data._camera->Position();
            for (u32 level = 0u; level < _resource_cascade_count; ++level)
            {
                const f32 cell_size = _settings._base_cell_size * static_cast<f32>(1u << level);
                const Vector3f center(std::floor(camera_position.x / cell_size) * cell_size,
                                      std::floor(camera_position.y / cell_size) * cell_size,
                                      std::floor(camera_position.z / cell_size) * cell_size);
                const f32 world_size = cell_size * static_cast<f32>(_resource_grid_resolution);
                _grid_info[level] = Vector4f(center - Vector3f(world_size * 0.5f), cell_size);
            }

            _placement_pass.SetSpawnEnabled(!_settings._manual_spawn || _spawn_once_pending);
            renderer.EnqueuePass(&_placement_pass);
            _spawn_once_pending = false;
        }
        if (_settings._debug_draw_surfels || _settings._debug_coverage || _settings._debug_draw_tile_grid)
            renderer.EnqueuePass(&_debug_pass);
    }

    SurfelPlacementPass::SurfelPlacementPass(SurfelGIFeature &feature)
        : RenderPass("Surfel Placement"), _feature(&feature)
    {
        _event = ERenderPassEvent::kBeforeDeferedLighting;
    }

    void SurfelPlacementPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (_feature == nullptr || _feature->_compute_shader == nullptr || _feature->_surfel_pool == nullptr)
            return;

        _feature->SetComputeParameters(rendering_data);
        RDG::RGHandle grid_cells = graph.Import(_feature->_grid_cells.get());
        RDG::RGHandle grid_write_counters = graph.Import(_feature->_grid_write_counters.get());
        RDG::RGHandle grid_indices = graph.Import(_feature->_grid_indices.get());
        RDG::RGHandle grid_block_prefix = graph.Import(_feature->_grid_block_prefix.get());
        RDG::RGHandle surfel_pool = graph.Import(_feature->_surfel_pool.get());
        RDG::RGHandle surfel_state = graph.Import(_feature->_surfel_state.get());
        RDG::RGHandle dispatch_args = graph.Import(_feature->_surfel_dispatch_args.get());
        RDG::RGHandle draw_args = graph.Import(_feature->_draw_args.get());
        const u32 cell_count = _feature->_resource_grid_resolution * _feature->_resource_grid_resolution *
                               _feature->_resource_grid_resolution * _feature->_resource_cascade_count;

        graph.AddPass("Surfel Grid Clear", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                grid_cells = builder.Write(grid_cells, EResourceUsage::kWriteUAV);
                grid_write_counters = builder.Write(grid_write_counters, EResourceUsage::kWriteUAV);
                surfel_state = builder.Write(surfel_state, EResourceUsage::kWriteUAV);
            },
            [this, cell_count](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                const u32 x = CalculateDispatchCount(cell_count, 64u);
                const u32 y = 1u;
                const u32 z = 1u;
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_clear_kernel, x, y, z);
            });

        graph.AddPass("Surfel Prepare Grid Dispatch", RDG::PassDesc(RDG::EPassType::kCompute),
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

        graph.AddPass("Surfel Grid Count", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                builder.Read(dispatch_args, EResourceUsage::kIndirectArgument);
                grid_cells = builder.Write(grid_cells, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_count_kernel,
                              _feature->_surfel_dispatch_args.get(), 0u);
            });

        graph.AddPass("Surfel Grid Prefix Local", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                grid_cells = builder.Write(grid_cells, EResourceUsage::kWriteUAV);
                grid_block_prefix = builder.Write(grid_block_prefix, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_prefix_local_kernel,
                              _feature->_resource_grid_block_count, 1u, 1u);
            });

        graph.AddPass("Surfel Grid Prefix Blocks", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                grid_block_prefix = builder.Write(grid_block_prefix, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_prefix_blocks_kernel, 1u, 1u, 1u);
            });

        graph.AddPass("Surfel Grid Prefix Finalize", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(grid_block_prefix, EResourceUsage::kReadSRV);
                grid_cells = builder.Write(grid_cells, EResourceUsage::kWriteUAV);
            },
            [this, cell_count](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_prefix_finalize_kernel,
                              CalculateDispatchCount(cell_count, kScanFinalizeThreadCount), 1u, 1u);
            });

        graph.AddPass("Surfel Grid Fill", RDG::PassDesc(RDG::EPassType::kCompute),
            [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                builder.Read(dispatch_args, EResourceUsage::kIndirectArgument);
                builder.Read(grid_cells, EResourceUsage::kReadSRV);
                grid_write_counters = builder.Write(grid_write_counters, EResourceUsage::kWriteUAV);
                grid_indices = builder.Write(grid_indices, EResourceUsage::kWriteUAV);
            },
            [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &data)
            {
                _feature->SetComputeParameters(data);
                cmd->Dispatch(_feature->_compute_shader.get(), _feature->_grid_fill_kernel,
                              _feature->_surfel_dispatch_args.get(), 0u);
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

        if (_spawn_enabled)
        {
            graph.AddPass("Surfel Spawn", RDG::PassDesc(RDG::EPassType::kCompute),
                [&, this](RDG::RenderGraphBuilder &builder)
                {
                    builder.Read(rendering_data._rg_handles._depth_tex, EResourceUsage::kReadSRV);
                    builder.Read(rendering_data._rg_handles._gbuffers[0], EResourceUsage::kReadSRV);
                    builder.Read(_feature->_coverage_handle, EResourceUsage::kReadSRV);
                    surfel_pool = builder.Write(surfel_pool, EResourceUsage::kWriteUAV);
                    surfel_state = builder.Write(surfel_state, EResourceUsage::kWriteUAV);
                },
                [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                {
                    _feature->SetComputeParameters(data);
                    _feature->_compute_shader->SetTexture(_feature->_spawn_kernel, kCameraDepthTexturePropertyId,
                                                          graph.Resolve<Texture>(data._rg_handles._depth_tex));
                    _feature->_compute_shader->SetTexture(_feature->_spawn_kernel, kCameraNormalsTexturePropertyId,
                                                          graph.Resolve<Texture>(data._rg_handles._gbuffers[0]));
                    _feature->_compute_shader->SetTexture(_feature->_spawn_kernel, kCoverageTexturePropertyId,
                                                          graph.Resolve<Texture>(data._rg_handles._surfel_coverage));
                    const auto [x, y, z] = _feature->_compute_shader->CalculateDispatchNum(
                        _feature->_spawn_kernel, data._width, data._height, 1u);
                    cmd->Dispatch(_feature->_compute_shader.get(), _feature->_spawn_kernel, x, y, z);
                });
        }

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
                                  _feature->_draw_args != nullptr && _feature->_disk_material != nullptr;
        if (!draw_coverage && !draw_tile_grid && !draw_surfels)
            return;

        RDG::RGHandle color_target = rendering_data.FinalColorHandle();
        RDG::RGHandle depth_target = rendering_data._rg_handles._depth_target;
        RDG::RGHandle coverage = _feature->_coverage_handle;
        RDG::RGHandle tile_grid_source = rendering_data._rg_handles._depth_tex;
        RDG::RGHandle surfel_pool;
        RDG::RGHandle draw_args;
        if (draw_surfels)
        {
            surfel_pool = graph.Import(_feature->_surfel_pool.get());
            draw_args = graph.Import(_feature->_draw_args.get());
        }
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
                    _feature->_disk_material->SetInt(kDebugCascadePropertyId, _feature->_settings._debug_cascade);
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
}
