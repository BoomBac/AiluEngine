/*
 * @author    : BoomBac
 * @created   : 2024.10
*/

#pragma once
#ifndef __ANIM_SYS_H__
#define __ANIM_SYS_H__
#include "Scene/Entity.h"
#include "Animation/AnimationEvent.h"
#include "Clip.h"
#include "Animation/SkinningSystem.h"
#include "Animation/SpriteAnimationBinding.h"
namespace Ailu
{
    namespace ECS
    {
        class AnimationSystem : public System
        {
            DECLARE_SYSTEM(AnimationSystem)
        public:
            inline static u32 s_vertex_num_per_skin_task = 2000u;
            void Update(Register &r, f32 delta_time) final;
            std::span<const AnimationEventMessage> Events() const { return _event_queue.Events(); }
            ESystemPhase GetPhase() const final { return ESystemPhase::kAnimation; }
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
            Scope<SkinningSystem> _skinning_system = MakeScope<SkinningSystem>();
            AnimationEventQueue _event_queue;
            Vector<AnimationEvent> _event_scratch;
        };
    }// namespace ECS
}// namespace Ailu
#endif// !ANIM_SYS_H__
