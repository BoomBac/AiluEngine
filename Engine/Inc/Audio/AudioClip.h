#pragma once
#ifndef __AUDIO_CLIP_H__
#define __AUDIO_CLIP_H__

#include "Audio/AudioTypes.h"
#include "Objects/Object.h"
#include "generated/AudioClip.gen.h"

namespace Ailu
{
    ACLASS()
    class AILU_API AudioClip : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        EAudioLoadMode _load_mode = EAudioLoadMode::kMemory;
        APROPERTY()
        EAudioChannelMode _channel_mode = EAudioChannelMode::kAuto;
        APROPERTY()
        bool _force_mono = false;
        APROPERTY()
        f32 _duration = 0.0f;
        APROPERTY()
        u32 _sample_rate = 0u;
        APROPERTY()
        u32 _channel_count = 0u;
        APROPERTY()
        String _runtime_path;
    };
}// namespace Ailu

#endif// __AUDIO_CLIP_H__
