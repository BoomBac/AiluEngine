#pragma once
#ifndef __RENDER_SYSTEM_H__
#define __RENDER_SYSTEM_H__
#include "Entity.hpp"
#include "Render/Material.h"
namespace Ailu
{
    namespace ECS
    {
        struct LightProbeHeadInfo
        {
            u16 _prefilter_size;
            u16 _radiance_size;
            EALGFormat::EALGFormat _format;
            u16 _padding;
        };
        class LightingSystem : public System
        {
            DECLARE_CLASS(LightingSystem)
        public:
            LightingSystem();
            void Update(Register &r, f32 delta_time) final;
            void OnPushEntity(Entity e) final;
            virtual Ref<System> Clone() final
            {
                auto copy = MakeRef<LightingSystem>();
                copy->_entities = _entities;
                return copy;
            };
        private:
            void ProcessVXGI(Register& r);
        private:
            Ref<Render::Material> _light_probe_debug_mat;
            Ref<Render::CubeMap> _test_prefilter_map;
            Ref<Render::CubeMap> _test_radiance_map;
        };
    }// namespace ECS
}// namespace Ailu


#endif// !RENDER_SYSTEM_H__
