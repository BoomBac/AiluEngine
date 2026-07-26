#pragma once
#ifndef __MINIAUDIO_BACKEND_H__
#define __MINIAUDIO_BACKEND_H__

#include "Audio/Backend/IAudioBackend.h"

namespace Ailu
{
    class AILU_API MiniaudioBackend final : public IAudioBackend
    {
    public:
        MiniaudioBackend();
        ~MiniaudioBackend() override;
        bool Initialize(const AudioDeviceConfig &config) override;
        void Shutdown() override;
        void Update() override;
        BackendVoiceHandle CreateVoice(const BackendVoiceCreateInfo &create_info) override;
        void DestroyVoice(BackendVoiceHandle handle) override;
        void Play(BackendVoiceHandle handle) override;
        void Stop(BackendVoiceHandle handle) override;
        void Pause(BackendVoiceHandle handle) override;
        void Resume(BackendVoiceHandle handle) override;
        void SetVolume(BackendVoiceHandle handle, f32 volume) override;
        void SetPitch(BackendVoiceHandle handle, f32 pitch) override;
        void SetPosition(BackendVoiceHandle handle, const Vector3f &position) override;
        void SetVelocity(BackendVoiceHandle handle, const Vector3f &velocity) override;
        void SetListener(const AudioListenerState &listener) override;
        void SetBusVolume(EAudioBus bus, f32 volume) override;
        void SetBusMuted(EAudioBus bus, bool muted) override;
        bool IsPlaying(BackendVoiceHandle handle) const override;

    private:
        struct Implementation;
        Implementation *_impl = nullptr;
    };
}// namespace Ailu

#endif// __MINIAUDIO_BACKEND_H__
