#pragma once
#ifndef __TAA_H__
#define __TAA_H__
#include "Assets/AssetRef.h"
#include "RenderFeature.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/CoreType.h"
#include "Render/Texture.h"
#include "generated/TemporalAA.gen.h"

namespace Ailu::Render
{
    class Camera;

    class TAAReactivePreparePass : public RenderPass
    {
    public:
        TAAReactivePreparePass();
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) override;
    };

    class TAAExecutePass : public RenderPass
    {
        struct TAAHistoryState
        {
            Ref<RenderTexture> _target_a;
            Ref<RenderTexture> _target_b;
            Ref<RenderTexture> _reactive_a;
            Ref<RenderTexture> _reactive_b;
            Ref<RenderTexture> _stable_target;
            Vector2f _previous_uv_jitter = Vector2f::kZero;
            Vector3f _previous_position = Vector3f::kZero;
            Vector3f _previous_forward = Vector3f::kZero;
            Vector3f _previous_up = Vector3f::kZero;
            f32 _previous_fov = 0.0f;
            f32 _previous_near = 0.0f;
            f32 _previous_far = 0.0f;
            f32 _previous_aspect = 0.0f;
            u8 _projection_type = 0u;
            u32 _width = 0u;
            u32 _height = 0u;
            EALGFormat _format = EALGFormat::kALGFormatUNKOWN;
            u64 _last_render_frame = 0u;
            u64 _camera_reset_revision = 0u;
            f32 _previous_pre_exposure = 1.0f;
            u8 _previous_exposure_mode = 0u;
            const Camera *_camera_identity = nullptr;
            bool _write_a = true;
            bool _history_valid = false;
            bool _has_rendered = false;
        };

    public:
        TAAExecutePass();
        ~TAAExecutePass() override = default;
        void Execute(GraphicsContext *context, RenderingData &rendering_data) override;
        void EndPass(GraphicsContext *context) override;
        void Setup(const Camera &camera, u64 frame_index, u32 width, u32 height, Vector2f current_uv_jitter,
                   Vector4f params, Vector4f quality);
        void Setup(const Camera &camera, u64 frame_index, u32 width, u32 height, Vector2f current_uv_jitter,
                   const TextureDesc &color_desc, Vector4f params, Vector4f quality);
        void Setup(const Camera &camera, u64 frame_index, u32 width, u32 height, Vector2f current_uv_jitter,
                   const TextureDesc &color_desc, Vector4f params, Vector4f quality, f32 pre_exposure);
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) override;
        void InvalidateHistory(u64 camera_hash);
        void InvalidateAllHistory();
        u64 GetHistoryCameraCount() const { return static_cast<u64>(_infos.size()); }

    private:
        Map<u64, TAAHistoryState> _infos;
        u64 _cur_camera_hash = 0u;
        u64 _current_frame_index = 0u;
        const Camera *_current_camera = nullptr;
        Vector2f _current_uv_jitter = Vector2f::kZero;
        Vector4f _params = Vector4f::kZero;
        Vector4f _quality = Vector4f::kZero;
        f32 _current_pre_exposure = 1.0f;
        u8 _current_exposure_mode = 0u;
        bool _resolve_executed = false;
        AssetRef<ComputeShader> _taa_gen;
        ComputeShaderKernelId _taa_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _stabilize_kernel = kInvalidComputeShaderKernelId;
    };

    ACLASS()
    class TemporalAA : public RenderFeature
    {
        GENERATED_BODY()
    public:
        TemporalAA();
        ~TemporalAA() override = default;
        void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) override;
        u64 GetHistoryCameraCount() const { return _execute_pass.GetHistoryCameraCount(); }
        APROPERTY(Range(0.0f,1.0f))
        f32 _history_factor = 0.95f;
        APROPERTY(Range(0.0f, 2.0f))
        f32 _clamp_quality = 2.0f;
        APROPERTY(Range(0.0f, 2.0f))
        f32 _history_quality = 2.0f;
        APROPERTY(Range(0.0f, 2.0f))
        f32 _motion_quality = 0.0f;
        APROPERTY(Range(0.0f, 2.0f))
        f32 _variance_clip_scale = 1.0f;
        APROPERTY(Range(0.0f, 4.0f))
        f32 _sharpness = 1.0f;
    private:
        TAAReactivePreparePass _prepare_pass;
        TAAExecutePass _execute_pass;
    };
}
#endif// __TAA_H__
