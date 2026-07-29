#ifndef VOLUMETRIC_FOG_H
#define VOLUMETRIC_FOG_H
#pragma once
#include "RenderFeature.h"
#include "generated/VolumetricFog.gen.h"
namespace Ailu
{
    namespace Render
    {
        class VolumetricFogPass;
        ACLASS()
        class AILU_API VolumetricFog : public RenderFeature
        {
            GENERATED_BODY()
        public:
            VolumetricFog();
            ~VolumetricFog();
            void AddRenderPasses(Renderer& renderer, const RenderingData& rendering_data) override;
            Texture3D* GetVolumetricLightTexture() const { return _is_cur_a? _volumetric_light_a.get() : _volumetric_light_b.get(); }
            Texture3D* GetAccTexture() const { return _accum_texture.get(); }
            void OnPropertyChanged(const PropertyInfo& prop) override;
        public:
            APROPERTY()
            bool _debug_voxel_pos = false;
            APROPERTY(Range(0.0f,10.0f))
            f32 _fog_density = 1.0f;
            APROPERTY()
            bool _temporal_reprojection = false;
            APROPERTY(Range(0.0f,1.0f))
            f32 _blend_factor = 0.5f;
            APROPERTY(Range(-1.0f,1.0f))
            f32 _g = 0.0f;
            APROPERTY(Range(0.0f,10.0f))
            f32 _intensity = 1.0f;
        private:
            Scope<VolumetricFogPass> _volumetric_fog_pass;
            Ref<Texture3D> _volumetric_light_a, _volumetric_light_b;
            Ref<Texture3D> _accum_texture;
            bool _is_cur_a = true;
            Ref<ComputeShader> _volumetric_fog_cs;
            Ref<ComputeShader> _max_z_cs;
            Vector3UInt _voxel_num = Vector3UInt(170, 90, 96);
        };

        class VolumetricFogPass : public RenderPass
        {
            friend class VolumetricFog;
        public:
            VolumetricFogPass();
            ~VolumetricFogPass();
            void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;
            void BeginPass(GraphicsContext* context) final;
            void EndPass(GraphicsContext* context) final;
        public:
            bool _debug_voxel_pos = false;
            Vector3UInt _voxel_num;
        private:
            ComputeShader* _volumetric_fog;
            ComputeShader* _max_z_cs;
            ComputeShaderKernelId _light_injection_kernel = kInvalidComputeShaderKernelId;
            ComputeShaderKernelId _light_integration_kernel = kInvalidComputeShaderKernelId;
            Texture3D* _cur_light_texture;
            Texture3D* _history_light_texture;
            Texture3D* _accum_texture;
            RDG::RGHandle _inject_handle;
            RDG::RGHandle _history_light_handle;
            RDG::RGHandle _accum_handle;
            Ref<Material> _debug_material;
            Matrix4x4f _matrix_prev_v, _matrix_prev_p;
        };


    } // namespace Render
}
#endif // VOLUMETRIC_FOG_H
