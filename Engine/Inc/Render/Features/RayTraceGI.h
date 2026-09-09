#ifndef __RAYTRACE_GI_H__
#define __RAYTRACE_GI_H__
#include "RenderFeature.h"
#include "Render/RayTracing/SceneRayTracingProxy.h"
#include "Render/RayTracing/RayTracingShader.h"
#include "generated/RayTraceGI.gen.h"

namespace Ailu
{
    namespace Render
    {   
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
            //temp
            Vector2f _debug_pos = Vector2f{-1.0f,-1.0f};
            APROPERTY(Range(0,20))
            u32 _debug_hit_box = 0u;
            APROPERTY()
            bool _is_temporal_denoise = false;
            APROPERTY()
            bool _is_show_debug = false;
            APROPERTY()
            bool _use_hardware_ray_tracing = false;
            APROPERTY()
            bool _enable_ris = true;
            APROPERTY()
            bool _enable_resampling = true;
        private:
            Scope<GIPass> _gi_pass;
            Ref<ComputeShader> _gi_compute_shader;
            Ref<RayTracingShader> _gi_raytracing_shader;
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

            GIPass(ComputeShader* cs, RayTracingShader *rt_shader);
            ~GIPass() = default;
            void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;
        public:
            Vector2f _debug_pos;
            bool _is_temporal_denoise = false;
            bool _use_hardware_ray_tracing = false;
        private:
            bool MakesureTarget(const RenderingData& rendering_data);
            bool CanUseHardwareRayTracing() const;
            void PrepareHardwareRayTracingScene(const RenderingData &rendering_data);
            void UpdateRayGenData(const RenderingData &rendering_data);
        private:
            ComputeShader *_gi_compute_shader;
            RayTracingShader *_gi_raytracing_shader;
            ComputeShaderKernelId _kernel_ray_gen = kInvalidComputeShaderKernelId;
            ComputeShaderKernelId _kernel_denoise = kInvalidComputeShaderKernelId;
            Ref<GPUBuffer> _debug_buffer = nullptr;
            Ref<GPUBuffer> _debug_index_buffer = nullptr;
            Ref<GPUBuffer> _arg_buffer = nullptr;
            Ref<Material> _debug_line_mat;
            Ref<Texture2D> _gi_texture_a;
            Ref<Texture2D> _gi_texture_b;
            RDG::RGHandle _cur_target_handle;
            RDG::RGHandle _history_target_handle;
            Ref<ConstantBuffer> _raygen_data;
            Scope<SceneRayTracingProxy> _scene_rt_proxy;
            RayGenConstantBuffer _raygen_cb{};
            bool _is_target_for_hardware_rt = false;
            bool _is_cur_a = true;
            u32 _tile_frame_counter = 0u;
            Ref<GPUBuffer> _reservoir_a = nullptr;
            Ref<GPUBuffer> _reservoir_b = nullptr;
            Ref<GPUBuffer> _surface_buffer_a = nullptr;
            Ref<GPUBuffer> _surface_buffer_b = nullptr;
        };
    }
} // namespace Ailu

#endif//__RAYTRACE_GI_H__
