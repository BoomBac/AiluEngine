#ifndef VOLUMETRIC_FOG_H
#define VOLUMETRIC_FOG_H
#pragma once
#include "RenderFeature.h"
#include "generated/VolumetricFog.gen.h"
namespace Ailu
{
    namespace Render
    {
        class VolumetricFogPass : public RenderPass
        {
            friend class VolumetricFog;
        public:
            VolumetricFogPass();
            ~VolumetricFogPass();
            void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;
            void BeginPass(GraphicsContext* context) final;
            void EndPass(GraphicsContext* context) final;

        private:
            ComputeShader* _volumetric_fog;
            Texture3D* _fog_texture;
        };

        ACLASS()
        class AILU_API VolumetricFog : public RenderFeature
        {
            GENERATED_BODY()
        public:
            VolumetricFog();
            ~VolumetricFog();
            void AddRenderPasses(Renderer& renderer, const RenderingData& rendering_data) override;
            Texture3D* GetFogTexture() const { return _fog_texture.get(); }
        public:
        private:
            Scope<VolumetricFogPass> _volumetric_fog_pass;
            Ref<Texture3D> _fog_texture;
            Ref<ComputeShader> _volumetric_fog_cs;
        };
    } // namespace Render
}
#endif // VOLUMETRIC_FOG_H