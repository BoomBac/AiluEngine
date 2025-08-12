//
// Created by 22292 on 2024/12/09.
//
#pragma once
#ifndef __CLOUD_RENDERER_H__
#define __CLOUD_RENDERER_H__
#include "../Texture.h"
#include "RenderFeature.h"
#include "generated/VolumetricClouds.gen.h"
namespace Ailu::Render
{
    struct CloudsShaderParams
    {
        Vector3f _base_pos;
        Vector4f _params;
        Vector3f _absorption;
        Vector3f _scattering;
        float _exposure;
        float _density_multply;
        float _threshold;
        bool _is_tile_render;
        bool _is_high_quailty;
        f32 _thickness;
        f32 _height;
    };
    class VolumetricCloudsPass : public RenderPass
    {
    public:
        VolumetricCloudsPass();
        ~VolumetricCloudsPass();
        void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
        void BeginPass(GraphicsContext *context) final;
        void EndPass(GraphicsContext *context) final;
        void Setup(f32 speed);

    public:
        CloudsShaderParams _params;

    private:
        void UpdateShaderParams();

    private:
        Ref<Material> _global_cloud;
        Ref<Texture3D> _shape_noise;
        Ref<Texture3D> _detail_noise;
        Ref<Texture2D> _blue_noise;
        Ref<Texture2D> _curl_noise;
        Ref<Texture2D> _weather_map;
        Ref<ComputeShader> _noise_gen;
        Ref<ComputeShader> _cloud_gen;
        Ref<RenderTexture> _cloud_rt_a;
        Ref<RenderTexture> _cloud_rt_b;
        bool _is_cur_a;
    };
    ACLASS()
    class AILU_API VolumetricClouds : public RenderFeature
    {
        GENERATED_BODY()
    public:
        VolumetricClouds();
        ~VolumetricClouds();
        void AddRenderPasses(Renderer &renderer, const RenderingData & rendering_data) override;
        APROPERTY(Category = "Base")
        bool _is_tile_render = true;
        APROPERTY(Category = "Base")
        bool _is_high_quality = true;
        APROPERTY(Category = "Base"; Range(0.0f, 10000.0f))
        f32 _height = 5000.0f;
        APROPERTY(Category = "Base"; Range(100.0f, 10000.0f))
        f32 _thickness = 6000.0f;
        APROPERTY(Category = "Base")
        Color _absorbtion = Color(0.0f,0.0f,0.0f,1.0f);
        APROPERTY(Category = "Base")
        Color _scattering = Color(200.0f,200.0f,200.0f,1.0f);
        APROPERTY(Category = "Base"; Range(0.0f, 50.0f))
        f32 _speed = 6.0f;
        APROPERTY(Category = "Base"; Range(0.0f, 10.0f))
        f32 _exposure = 1.0f;
        APROPERTY(Category = "Base"; Range(-1.0f, 1.0f))
        f32 _type_offset = 0.0f; 
        APROPERTY(Category = "Base"; Range(0.0f, 4.0f))
        f32 _density_multiplier = 1.0f;
        APROPERTY(Category = "Base"; Range(0.0f, 1.0f))
        f32 _threshold = 0.0f;
    private:
        VolumetricCloudsPass _pass;
    };
}// namespace Ailu

#endif// !__CLOUD_RENDERER_H__