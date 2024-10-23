/*
 * @author    : BoomBac
 * @created   : 2024.10
*/

#pragma once
#ifndef __ANIM_SYS_H__
#define __ANIM_SYS_H__
#include "Scene/Entity.hpp"
#include "Clip.h"
namespace Ailu
{
    namespace ECS
    {
        class AnimationSystem : public System
        {
            DECLARE_CLASS(AnimationSystem)
        public:
            inline static u32 s_vertex_num_per_skin_task = 2000u;
            void Update(Register &r, f32 delta_time) final;
            void OnPushEntity(Entity entity) final;
            virtual Ref<System> Clone() final
            {
                auto copy = MakeRef<AnimationSystem>();
                copy->_entities = _entities;
                return copy;
            };
            void WaitFor() const final;
        private:
            Map<ECS::Entity, f32> _anim_playtime;
        };
    }// namespace ECS
}// namespace Ailu
#endif// !ANIM_SYS_H__
