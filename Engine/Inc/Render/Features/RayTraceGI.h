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
            //temp
            Vector2f _debug_pos;
        private:
            Scope<GIPass> _gi_pass;
            Ref<ComputeShader> _gi_compute_shader;
        };

        class GIPass : public RenderPass
        {
        public:
            GIPass(ComputeShader* cs);
            ~GIPass() = default;
            void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;
        public:
            Vector2f _debug_pos;
        private:
            ComputeShader *_gi_compute_shader;
            u32 _kernel_ray_gen = 0u;
            Ref<GPUBuffer> _debug_buffer = nullptr;
            Ref<GPUBuffer> _debug_index_buffer = nullptr;
            Ref<GPUBuffer> _arg_buffer = nullptr;
            Ref<Material> _debug_line_mat;
        };
    }
} // namespace Ailu

#endif//__RAYTRACE_GI_H__