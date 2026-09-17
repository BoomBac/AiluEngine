#pragma once
#ifndef __DDGI_FEATURE_H__
#define __DDGI_FEATURE_H__

#include "Assets/AssetRef.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/Vector.h"
#include "Render/Buffer.h"
#include "Render/DDGI/DDGITypes.h"
#include "Render/Features/RenderFeature.h"
#include "Render/RayTracing/SceneRayTracingProxy.h"
#include "Render/Shader.h"
#include "generated/DDGI.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    namespace Render
    {
    class Camera;
    class DDGIFeature;

    class AILU_API DDGIProbeTracePass final : public RenderPass
    {
    public:
        explicit DDGIProbeTracePass(DDGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        DDGIFeature *_feature = nullptr;
    };

    class AILU_API DDGIProbeRadiancePass final : public RenderPass
    {
    public:
        explicit DDGIProbeRadiancePass(DDGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        DDGIFeature *_feature = nullptr;
    };

    class AILU_API DDGIProbeBorderPass final : public RenderPass
    {
    public:
        explicit DDGIProbeBorderPass(DDGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        DDGIFeature *_feature = nullptr;
    };

    class AILU_API DDGIProbeDistancePass final : public RenderPass
    {
    public:
        explicit DDGIProbeDistancePass(DDGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        DDGIFeature *_feature = nullptr;
    };

    class AILU_API DDGIProbeDistanceBorderPass final : public RenderPass
    {
    public:
        explicit DDGIProbeDistanceBorderPass(DDGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        DDGIFeature *_feature = nullptr;
    };

    class AILU_API DDGIProbeRelocationPass final : public RenderPass
    {
    public:
        explicit DDGIProbeRelocationPass(DDGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        DDGIFeature *_feature = nullptr;
    };

    class AILU_API DDGITemporalSwapPass final : public RenderPass
    {
    public:
        explicit DDGITemporalSwapPass(DDGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        DDGIFeature *_feature = nullptr;
    };

    class AILU_API DDGIDebugPass final : public RenderPass
    {
    public:
        explicit DDGIDebugPass(DDGIFeature &feature);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
        bool IsAvailable() const;

    private:
        void RefreshDebugMaterial();

        DDGIFeature *_feature = nullptr;
        AssetRef<Shader> _debug_shader;
        Ref<Material> _debug_material;
    };

    ACLASS()
    class AILU_API DDGIFeature final : public RenderFeature
    {
        GENERATED_BODY()

        friend class DDGIProbeTracePass;
        friend class DDGIProbeRadiancePass;
        friend class DDGIProbeBorderPass;
        friend class DDGIProbeDistancePass;
        friend class DDGIProbeDistanceBorderPass;
        friend class DDGIProbeRelocationPass;
        friend class DDGITemporalSwapPass;
        friend class DDGIDebugPass;

    public:
        DDGIFeature();

        APROPERTY()
        bool _use_visibility = true;

        APROPERTY()
        bool _use_temporal = true;

        APROPERTY(Range(0.0f, 1.0f))
        f32 _hysteresis = 0.97f;

        // Maximum step, as a fraction of the value a probe texel already holds, that one probe update may
        // apply.  A rare hit on a small, very bright emitter would otherwise move the texel by an
        // unbounded amount and the field would pulse for 1/(1-hysteresis) frames afterwards.  0 disables
        // the limit.  See DDGI_HistoryStepMinLuminance in the probe update shaders for the dark end.
        APROPERTY(Range(0.0f, 1.0f))
        f32 _history_step_ratio = 0.25f;

        // World-space offsets used when querying probe visibility at a shaded surface.
        APROPERTY()
        f32 _probe_normal_bias = 0.1f;

        APROPERTY()
        f32 _probe_view_bias = 0.1f;

        APROPERTY()
        bool _use_random_rotation = true;

        void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) final;
        void PrepareDeferredLighting(RDG::RenderGraph &graph, RenderingData &rendering_data);
        bool PickProbe(const Camera &camera, const Math::Vector2f &screen_pos);
        void ClearProbeSelection();
        ECS::Entity GetSelectedProbeEntity() const { return _selected_probe_entity; }
        u32 GetSelectedProbeIndex() const { return _selected_probe_index; }

    private:
        void SyncSceneVolumes(const SceneManagement::Scene &scene);
        void EnsureGpuResources();

        HashMap<ECS::Entity, DDGIVolumeRuntime> _volume_runtimes;
        AssetRef<ComputeShader> _compute_shader;
        ComputeShaderKernelId _probe_trace_kernel = kInvalidComputeShaderKernelId;
        AssetRef<ComputeShader> _radiance_compute_shader;
        ComputeShaderKernelId _probe_radiance_kernel = kInvalidComputeShaderKernelId;
        AssetRef<ComputeShader> _border_compute_shader;
        ComputeShaderKernelId _probe_border_kernel = kInvalidComputeShaderKernelId;
        AssetRef<ComputeShader> _distance_compute_shader;
        ComputeShaderKernelId _probe_distance_kernel = kInvalidComputeShaderKernelId;
        AssetRef<ComputeShader> _distance_border_compute_shader;
        ComputeShaderKernelId _probe_distance_border_kernel = kInvalidComputeShaderKernelId;
        AssetRef<ComputeShader> _relocation_compute_shader;
        ComputeShaderKernelId _probe_relocation_kernel = kInvalidComputeShaderKernelId;
        Scope<SceneRayTracingProxy> _scene_rt_proxy;
        Scope<DDGIProbeTracePass> _probe_trace_pass;
        Scope<DDGIProbeRadiancePass> _probe_radiance_pass;
        Scope<DDGIProbeBorderPass> _probe_border_pass;
        Scope<DDGIProbeDistancePass> _probe_distance_pass;
        Scope<DDGIProbeDistanceBorderPass> _probe_distance_border_pass;
        Scope<DDGIProbeRelocationPass> _probe_relocation_pass;
        Scope<DDGITemporalSwapPass> _temporal_swap_pass;
        Scope<DDGIDebugPass> _debug_pass;
        bool _use_inline_ray_tracing = false;
        ECS::Entity _selected_probe_entity = ECS::kInvalidEntity;
        u32 _selected_probe_index = DDGI::kInvalidProbeIndex;
        const SceneManagement::Scene *_last_scene = nullptr;
        u64 _last_sync_frame = ~u64(0);
        u64 _last_update_frame = ~u64(0);
    };
    }
}

#endif // __DDGI_FEATURE_H__
