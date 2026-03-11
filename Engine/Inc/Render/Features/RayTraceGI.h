#ifndef __RAYTRACE_GI_H__
#define __RAYTRACE_GI_H__
#include "RenderFeature.h"
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
        private:
            Scope<GIPass> _gi_pass;
            Ref<ComputeShader> _gi_compute_shader;
        };

        class GIPass : public RenderPass
        {
            friend class RayTraceGI;
        public:
            GIPass(ComputeShader* cs);
            ~GIPass() = default;
            void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;
        public:
            Vector2f _debug_pos;
            bool _is_temporal_denoise = false;
        private:
            bool MakesureTarget(const RenderingData& rendering_data);
        private:
            ComputeShader *_gi_compute_shader;
            u32 _kernel_ray_gen = 0u,_kernel_denoise = 0u;
            Ref<GPUBuffer> _debug_buffer = nullptr;
            Ref<GPUBuffer> _debug_index_buffer = nullptr;
            Ref<GPUBuffer> _arg_buffer = nullptr;
            Ref<Material> _debug_line_mat;
            Ref<Texture2D> _gi_texture_a;
            Ref<Texture2D> _gi_texture_b;
            bool _is_cur_a = true;
            u32 _tile_frame_counter = 0u;
        };
    }
} // namespace Ailu

#endif//__RAYTRACE_GI_H__