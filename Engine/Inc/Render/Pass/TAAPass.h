#pragma once
#ifndef __TAA_H__
#define __TAA_H__
#include "RenderPass.h"

#include "GlobalMarco.h"

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
            Ref<RenderTexture> _pre_camera_color;
        };

    public:
        TAAExecutePass();
        virtual ~TAAExecutePass();
        void Execute(GraphicsContext *context, RenderingData &rendering_data) override;
        void BeginPass(GraphicsContext *context) override;
        void EndPass(GraphicsContext *context) override;
        void Setup(Matrix4x4f pre_matrix, Matrix4x4f cur_matrix, Material *taa_mat, int camera_hash, Vector2f jitter);

    private:
        Map<u64, TAAInfo> _infos;
        u64 _cur_camera_hash;
        Vector2f _jitter = Vector2f::kZero;
        Material *_taa_material;
        Matrix4x4f _cur_vp_matrix;
        Scope<IConstantBuffer> _origin_camera_cbuf;
    };

    class TAAFeature : public RenderFeature
    {
    public:
        TAAFeature();
        ~TAAFeature();
        void AddRenderPasses(Renderer &renderer, RenderingData &rendering_data) override;

    private:
        TAAExecutePass _execute_pass;
        TAAPreparePass _prepare_pass;
        Map<u64, HaltonSequence> _halton_sequence;
        Ref<Material> _taa;
    };
}// namespace Ailu
#endif// __TAA_H__