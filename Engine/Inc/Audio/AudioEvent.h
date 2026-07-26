#pragma once
#ifndef __AUDIO_EVENT_H__
#define __AUDIO_EVENT_H__

#include "Audio/AudioTypes.h"
#include "Objects/Object.h"
#include "generated/AudioEvent.gen.h"

namespace Ailu
{
    ACLASS()
    class AILU_API AudioEvent : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        Vector<AudioEventClip> _clips;
        APROPERTY()
        EAudioBus _bus = EAudioBus::kSfx;
        APROPERTY()
        f32 _volume_min = 1.0f;
        APROPERTY()
        f32 _volume_max = 1.0f;
        APROPERTY()
        f32 _pitch_min = 1.0f;
        APROPERTY()
        f32 _pitch_max = 1.0f;
        APROPERTY()
        u32 _max_instances = 0u;
        APROPERTY()
        f32 _cooldown = 0.0f;
        APROPERTY()
        f32 _min_distance = 1.0f;
        APROPERTY()
        f32 _max_distance = 30.0f;
        APROPERTY()
        bool _is_3d = false;
        APROPERTY()
        bool _loop = false;
        APROPERTY()
        EAudioConcurrencyScope _concurrency_scope = EAudioConcurrencyScope::kGlobal;
    };
}// namespace Ailu

#endif// __AUDIO_EVENT_H__
