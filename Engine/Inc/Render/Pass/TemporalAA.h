#pragma once
#ifndef __TAA_H__
#define __TAA_H__
#include "RenderPass.h"
#include "GlobalMarco.h"
#include "generated/TemporalAA.gen.h"

namespace Ailu
{
    struct HaltonSequence
    {
        int count;
        int index;
        std::vector<float> arrX;
        std::vector<float> arrY;

        Matrix4x4f prevViewProj;
        u64 frameCount;
        HaltonSequence() = default;
        HaltonSequence(int count) : count(count), index(0), arrX(count), arrY(count), frameCount(0)
        {
            for (int i = 0; i < count; ++i)
            {
                arrX[i] = get(i, 2);
            }

            for (int i = 0; i < count; ++i)
            {
                arrY[i] = get(i, 3);
            }
        }

        float get(int index, int base_)
        {
            float fraction = 1.0f;
            float result = 0.0f;

            while (index > 0)
            {
                fraction /= base_;
                result += fraction * (index % base_);
                index /= base_;
            }

            return result;
        }

        void Get(float &x, float &y)
        {
            if (++index == count) index = 1;
            x = arrX[index];
            y = arrY[index];
        }
    };

    class TAAPreparePass : public RenderPass
    {
    public:
        TAAPreparePass();
        void Setup(Matrix4x4f jitter);
        virtual ~TAAPreparePass();
        void Execute(GraphicsContext *context, RenderingData &rendering_data) override;
        void BeginPass(GraphicsContext *context) override;
        void EndPass(GraphicsContext *context) override;

    private:
        Matrix4x4f _jitter_matrix;
    };

    class TAAExecutePass : public RenderPass
    {
        struct TAAInfo
        {
            Matrix4x4f _pre_matrix;
            bool _first_tick;
            Ref<RenderTexture> _target_a;
            Ref<RenderTexture> _target_b;
            bool _is_cur_a;
        };

    public:
        TAAExecutePass();
        virtual ~TAAExecutePass();
        void Execute(GraphicsContext *context, RenderingData &rendering_data) override;
        void BeginPass(GraphicsContext *context) override;
        void EndPass(GraphicsContext *context) override;
        void Setup(Matrix4x4f pre_matrix, Matrix4x4f cur_matrix, Material *taa_mat, int camera_hash, Vector2f jitter,Vector4f params,Vector4f quality);

    private:
        Map<u64, TAAInfo> _infos;
        u64 _cur_camera_hash;
        Vector4f _jitter;
        Vector4f _params;
        Vector4f _quality;
        Material *_taa_material;
        Matrix4x4f _cur_vp_matrix;
        Scope<IConstantBuffer> _origin_camera_cbuf;
        Ref<ComputeShader> _taa_gen;
    };

    ACLASS()
    class TemporalAA : public RenderFeature
    {
        GENERATED_BODY()
    public:
        TemporalAA();
        ~TemporalAA();
        void AddRenderPasses(Renderer &renderer, RenderingData &rendering_data) override;
        APROPERTY(Range(0.0f,1.0f))
        f32 _history_factor = 0.95f;
        APROPERTY(Range(0.0f,1.0f))
        f32 _jitter_scale = 0.9f;
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
        TAAExecutePass _execute_pass;
        TAAPreparePass _prepare_pass;
        Map<u64, HaltonSequence> _halton_sequence;
        Ref<Material> _taa;
    };
}// namespace Ailu
#endif// __TAA_H__