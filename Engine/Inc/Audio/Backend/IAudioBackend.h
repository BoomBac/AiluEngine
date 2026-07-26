#pragma once
#ifndef __I_AUDIO_BACKEND_H__
#define __I_AUDIO_BACKEND_H__

#include "Audio/AudioTypes.h"

namespace Ailu
{
    class AILU_API IAudioBackend
    {
    public:
        virtual ~IAudioBackend() = default;
        virtual bool Initialize(const AudioDeviceConfig &config) = 0;
        virtual void Shutdown() = 0;
        virtual void Update() = 0;
        virtual BackendVoiceHandle CreateVoice(const BackendVoiceCreateInfo &create_info) = 0;
        virtual void DestroyVoice(BackendVoiceHandle handle) = 0;
        virtual void Play(BackendVoiceHandle handle) = 0;
        virtual void Stop(BackendVoiceHandle handle) = 0;
        virtual void Pause(BackendVoiceHandle handle) = 0;
        virtual void Resume(BackendVoiceHandle handle) = 0;
        virtual void SetVolume(BackendVoiceHandle handle, f32 volume) = 0;
        virtual void SetPitch(BackendVoiceHandle handle, f32 pitch) = 0;
        virtual void SetPosition(BackendVoiceHandle handle, const Vector3f &position) = 0;
        virtual void SetVelocity(BackendVoiceHandle handle, const Vector3f &velocity) = 0;
        virtual void SetListener(const AudioListenerState &listener) = 0;
        virtual void SetBusVolume(EAudioBus bus, f32 volume) = 0;
        virtual void SetBusMuted(EAudioBus bus, bool muted) = 0;
        virtual bool IsPlaying(BackendVoiceHandle handle) const = 0;
    };
}// namespace Ailu

#endif// __I_AUDIO_BACKEND_H__
