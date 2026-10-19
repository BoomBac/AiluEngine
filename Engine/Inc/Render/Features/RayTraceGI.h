#ifndef __RAYTRACE_GI_H__
#define __RAYTRACE_GI_H__
#include "Assets/AssetRef.h"
#include "RenderFeature.h"
#include "Render/Denoiser/PathTracingDenoiser.h"
#include "Render/RayTracing/SceneRayTracingProxy.h"
#include "Render/RayTracing/RayTracingShader.h"
#include <mutex>
#include "generated/RayTraceGI.gen.h"

namespace Ailu
{
    namespace Render
    {   
        AENUM()
        enum class EPathTracerAccumulationMode : u8
        {
            kDisabled,
            kReference,
            kRealtime
        };

        class GIPass;
        ACLASS()
        class AILU_API RayTraceGI : public RenderFeature
        {
            GENERATED_BODY()
        public:
            RayTraceGI();
            ~RayTraceGI() = default;
            void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data);
            void OnPropertyChanged(const PropertyInfo& prop) override;
            void SetActive(bool is_active) override;
            EDenoiserStatus DenoiserStatus() const;
            String DenoiserReason() const;
            PathTracingDenoiserDiagnostics DenoiserDiagnostics() const;
            //temp
            Vector2f _debug_pos = Vector2f{-1.0f,-1.0f};
            APROPERTY(Range(0,20))
            u32 _debug_hit_box = 0u;
            APROPERTY()
            EPathTracerAccumulationMode _accumulation_mode = EPathTracerAccumulationMode::kDisabled;
            APROPERTY()
            bool _is_show_debug = false;
            APROPERTY()
            EPathTracingDenoiser _denoiser = EPathTracingDenoiser::kNone;
            APROPERTY()
            EPathTracingDenoiserQuality _denoiser_quality = EPathTracingDenoiserQuality::kBalanced;
            APROPERTY()
            bool _denoiser_use_guides = false;
            APROPERTY(Range(1,256))
            u32 _denoiser_sample_interval = 32u;
            APROPERTY()
            bool _use_hardware_ray_tracing = false;
            APROPERTY()
            bool _enable_mnee = false;
            APROPERTY()
            bool _use_power_weighted_light_sampling = true;
            APROPERTY()
            bool _enable_ris = true;
            APROPERTY()
            bool _enable_resampling = true;
            bool _enable_environment_nee = true;
            // 0: final output, 1: raw, 2: previous, 3: current, 4: validation, 5: history length.
            APROPERTY(Range(0,5))
            u32 _debug_temporal_target = 0u;
        private:
            mutable std::mutex _denoiser_diagnostics_mutex;
            String _denoiser_reason = "Disabled";
            EDenoiserStatus _denoiser_status = EDenoiserStatus::kDisabled;
            PathTracingDenoiserDiagnostics _denoiser_diagnostics;
            Scope<GIPass> _gi_pass;
            AssetRef<ComputeShader> _gi_compute_shader;
            AssetRef<RayTracingShader> _gi_raytracing_shader;
        };

        class GIPass : public RenderPass
        {
            friend class RayTraceGI;
        public:
            struct Viewport
            {
                float left;
                float top;
                float right;
                float bottom;
            };

            struct RayGenConstantBuffer
            {
                Viewport viewport;
                Viewport stencil;
            };

            GIPass(const AssetRef<ComputeShader> &cs, const AssetRef<RayTracingShader> &rt_shader);
            ~GIPass() = default;
            void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;
        public:
            Vector2f _debug_pos;
            EPathTracingDenoiserQuality _denoiser_quality = EPathTracingDenoiserQuality::kBalanced;
            u32 _denoiser_sample_interval = 32u;
            EPathTracingDenoiser _denoiser = EPathTracingDenoiser::kNone;
            bool _denoiser_use_guides = false;
            EDenoiserStatus DenoiserStatus() const { return _denoiser_host.Status(); }
            const char *DenoiserReason() const { return _denoiser_host.Reason(); }
            EPathTracerAccumulationMode _accumulation_mode = EPathTracerAccumulationMode::kDisabled;
            bool _use_hardware_ray_tracing = false;
            bool _enable_mnee = false;
            bool _use_power_weighted_light_sampling = true;
            bool _enable_ris = true;
            bool _enable_resampling = true;
            bool _enable_environment_nee = true;
            u32 _debug_temporal_target = 0u;
        private:
            bool MakesureTarget(const RenderingData& rendering_data);
            bool CanUseHardwareRayTracing() const;
            void PrepareHardwareRayTracingScene(const RenderingData &rendering_data);
            void UpdateRayGenData(const RenderingData &rendering_data);
        private:
            AssetRef<ComputeShader> _gi_compute_shader;
            AssetRef<RayTracingShader> _gi_raytracing_shader;
            PathTracingDenoiserHost _denoiser_host;
            std::shared_ptr<std::atomic<bool>> _denoiser_active_flag = std::make_shared<std::atomic<bool>>(true);
            std::atomic<u64> _denoiser_activation_revision{0u};
            u64 _previous_denoiser_activation_revision = 0u;
            u64 _denoiser_frame_index = 0u;
            u64 _denoiser_shader_revision = 0u;
            u64 _denoiser_rt_shader_revision = 0u;
            u64 _last_denoiser_engine_frame = 0u;
            EPathTracingDenoiserQuality _previous_denoiser_quality = EPathTracingDenoiserQuality::kBalanced;
            u32 _reference_sample_count = 0u;
            bool _previous_denoiser_use_guides = false;
            Ref<RenderTexture> _guide_albedo, _guide_normal;
            RDG::RGHandle _guide_albedo_handle{}, _guide_normal_handle{};
            ComputeShaderKernelId _kernel_primary_ray = kInvalidComputeShaderKernelId;
            ComputeShaderKernelId _kernel_denoise = kInvalidComputeShaderKernelId;
            ComputeShaderKernelId _kernel_build_environment_weights = kInvalidComputeShaderKernelId;
            ComputeShaderKernelId _kernel_build_environment_cdf = kInvalidComputeShaderKernelId;
            Ref<GPUBuffer> _debug_buffer = nullptr;
            Ref<GPUBuffer> _debug_index_buffer = nullptr;
            Ref<GPUBuffer> _arg_buffer = nullptr;
            Ref<Material> _debug_line_mat;
            Ref<RenderTexture> _raw_path_trace_texture;
            Ref<RenderTexture> _history_texture_a;
            Ref<RenderTexture> _history_texture_b;
            Ref<RenderTexture> _temporal_debug_texture;
            Ref<RenderTexture> _environment_distribution_texture;
            RDG::RGHandle _raw_path_trace_handle;
            RDG::RGHandle _history_prev_handle;
            RDG::RGHandle _history_curr_handle;
            RDG::RGHandle _temporal_debug_handle;
            RDG::RGHandle _environment_distribution_handle;
            Ref<ConstantBuffer> _raygen_data;
            Scope<SceneRayTracingProxy> _scene_rt_proxy;
            RayGenConstantBuffer _raygen_cb{};
            bool _is_target_for_hardware_rt = false;
            bool _is_cur_a = true;
            bool _is_history_a = true;
            bool _temporal_history_valid = false;
            u64 _last_history_signature = 0u;
            bool _has_history_signature = false;
            Vector3f _previous_camera_position = Vector3f::kZero;
            Vector3f _previous_camera_forward = Vector3f::kForward;
            Vector3f _previous_camera_up = Vector3f::kUp;
            const Camera *_previous_camera = nullptr;
            u64 _previous_camera_reset_revision = 0u;
            Vector2f _previous_uv_jitter = Vector2f::kZero;
            bool _has_previous_camera = false;
            EPathTracerAccumulationMode _previous_accumulation_mode = EPathTracerAccumulationMode::kDisabled;
            bool _has_previous_accumulation_mode = false;
            u32 _tile_frame_counter = 0u;
            Ref<GPUBuffer> _reservoir_a = nullptr;
            Ref<GPUBuffer> _reservoir_b = nullptr;
            Ref<GPUBuffer> _surface_buffer_a = nullptr;
            Ref<GPUBuffer> _surface_buffer_b = nullptr;
            Texture *_environment_distribution_source = nullptr;
            u64 _environment_distribution_revision = 0u;
            bool _environment_distribution_dirty = true;
        };
    }
} // namespace Ailu

#endif//__RAYTRACE_GI_H__
