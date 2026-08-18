#pragma once
#ifndef __ANIMATION_EVENT_H__
#define __ANIMATION_EVENT_H__

#include "Framework/Core/Containers/Vector.h"
#include "Scene/Entity.h"

#include <span>

namespace Ailu
{
    enum class EAnimationEventKind : u8
    {
        kGameplay,
        kCosmetic
    };

    struct AnimationEvent
    {
        f32 _time = 0.0f;
        u32 _event_id = 0u;
        EAnimationEventKind _kind = EAnimationEventKind::kCosmetic;
    };

    struct AnimationEventMessage
    {
        ECS::Entity _entity = ECS::kInvalidEntity;
        u32 _event_id = 0u;
        EAnimationEventKind _kind = EAnimationEventKind::kCosmetic;
    };

    class AILU_API AnimationEventQueue
    {
    public:
        void Push(const AnimationEventMessage &event) { _events.push_back(event); }
        std::span<const AnimationEventMessage> Events() const { return _events; }
        void Clear() { _events.clear(); }

    private:
        Vector<AnimationEventMessage> _events;
    };

    class AnimationClip;
    AILU_API void CollectAnimationEvents(const AnimationClip &clip, f32 previous_time, f32 current_time,
                                         Vector<AnimationEvent> &out_events);
}

#endif // __ANIMATION_EVENT_H__
