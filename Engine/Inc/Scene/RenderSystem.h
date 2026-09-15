#pragma once
#ifndef __RENDER_SYSTEM_H__
#define __RENDER_SYSTEM_H__
#include <atomic>
#include "Entity.h"
#include "Render/AlgFormat.h"
#include "Render/SphericalHarmonicsProjector.h"

namespace Ailu
{
    namespace Render
    {
        class Material;
        class Shader;
        class CubeMap;
        class RenderTexture;
        class StaticDiffuseProbeData;
    }
}

namespace Ailu
{
    namespace ECS
    {
        struct LightProbeHeadInfo
        {
            u16 _prefilter_size;
            u16 _radiance_size;
            EALGFormat _format;
            u16 _padding;
        };
        class LightingSystem : public System
        {
            DECLARE_SYSTEM(LightingSystem)
        public:
            LightingSystem();
            void Update(Register &r, f32 delta_time) final;
            ESystemPhase GetPhase() const final { return ESystemPhase::kRenderData; }
            void OnPushEntity(Entity e) final;
            AILU_API Render::RenderTexture *GetStaticProbePreviewCubemap(Entity entity, u32 probe_index) const;
            virtual Ref<System> Clone() final
            {
                auto copy = MakeRef<LightingSystem>();
                copy->_entities = _entities;
                return copy;
            };
        private:
            void UpdateLightWorldState(Register &r);
            void ProcessVXGI(Register& r);
            void UpdateStaticDiffuseProbeBake(Register &r);
        private:
            Ref<Render::Material> _light_probe_debug_mat;
            Ref<Render::CubeMap> _test_prefilter_map;
            Ref<Render::CubeMap> _test_radiance_map;
            Render::SphericalHarmonicsProjector _static_probe_projector;
            Ref<Render::StaticDiffuseProbeData> _static_probe_bake_data;
            Ref<Render::RenderTexture> _static_probe_bake_cubemap;
            Vector<Ref<Render::RenderTexture>> _static_probe_preview_cubemaps;
            Entity _static_probe_bake_group = kInvalidEntity;
            Entity _static_probe_preview_group = kInvalidEntity;
            u32 _static_probe_bake_index = 0u;
            u32 _static_probe_bake_pass = 0u;
            u32 _static_probe_bake_pass_count = 1u;
            std::atomic<u32> _static_probe_bake_readback_count = 0u;
            bool _static_probe_bake_active = false;
            bool _static_probe_bake_next_pass = false;
            bool _static_probe_capture_requested = false;
        };
    }// namespace ECS
}// namespace Ailu


#endif// !RENDER_SYSTEM_H__
