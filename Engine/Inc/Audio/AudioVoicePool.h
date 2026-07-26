#pragma once
#ifndef __AUDIO_VOICE_POOL_H__
#define __AUDIO_VOICE_POOL_H__

#include "Audio/AudioVoice.h"

namespace Ailu
{
    class AILU_API AudioVoicePool
    {
    public:
        void Initialize(u32 max_voices);
        void Shutdown();
        AudioVoice *Allocate(const AudioVoice &voice, const AudioListenerState &listener);
        void Release(AudioHandle handle);
        AudioVoice *Get(AudioHandle handle);
        const AudioVoice *Get(AudioHandle handle) const;
        bool IsValid(AudioHandle handle) const;
        u32 ActiveVoiceCount() const;
        u32 MaxVoiceCount() const { return static_cast<u32>(_voices.size()); }
        Vector<AudioVoice> &Voices() { return _voices; }
        const Vector<AudioVoice> &Voices() const { return _voices; }
        f32 CalculateVoiceScore(const AudioVoice &voice, const Vector3f &listener_position) const;

    private:
        u32 FindFreeSlot() const;
        u32 FindStealableSlot(const AudioVoice &new_voice, const AudioListenerState &listener) const;

    private:
        Vector<AudioVoice> _voices;
        Vector<u32> _generations;
    };
}// namespace Ailu

#endif// __AUDIO_VOICE_POOL_H__
