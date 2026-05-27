#pragma once
#ifndef __RTXDI_FEATURE_H__
#define __RTXDI_FEATURE_H__

#include "RenderFeature.h"
#include "Render/RayTracing/SceneRayTracingProxy.h"
#include "generated/RTXDI.gen.h"

namespace Ailu::Render
{
    class RTXDIPass;
    ACLASS()
    class AILU_API RTXDI : public RenderFeature
    {
        GENERATED_BODY()
    public:
        RTXDI();
        ~RTXDI() override = default;
        void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) override;
        APROPERTY(Range(0,16))
        u32 _light_sample_count = 4;
        APROPERTY(Range(0,8))
        u32 _brdf_sample_count = 2;
        APROPERTY()
        bool _enable_resampling = false;
    private:
        void OnPropertyChanged(const PropertyInfo& prop) final;
    private:
        Scope<RTXDIPass> _rtxdi_pass;
        Ref<ComputeShader> _rtxdi_compute_shader;
    };

    class RTXDIPass : public RenderPass
    {
    public:
        explicit RTXDIPass(ComputeShader *shader);
        ~RTXDIPass() override = default;
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        bool EnsureTarget(const RenderingData &rendering_data);

    private:
        ComputeShader *_compute_shader = nullptr;
        Scope<SceneRayTracingProxy> _scene_rt_proxy;
        Ref<Texture2D> _output_texture = nullptr;
        RDG::RGHandle _output_handle;
        u16 _kernel_ray_gen = static_cast<u16>(-1);
        Ref<GPUBuffer> _reservoir_a;
        Ref<GPUBuffer> _reservoir_b;
        bool _use_reservoir_a = true;
    };


}

#endif