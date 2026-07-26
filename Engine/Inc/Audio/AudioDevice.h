#pragma once
#ifndef __AUDIO_DEVICE_H__
#define __AUDIO_DEVICE_H__

#include "Audio/AudioVoicePool.h"
#include "Audio/Backend/IAudioBackend.h"
#include <map>

namespace Ailu
{
    class AILU_API AudioDevice
    {
    public:
        bool Initialize(const AudioDeviceConfig &config);
        void Shutdown();
        void Update(f32 delta_time);
        AudioHandle Play(const Guid &clip, const AudioPlayOptions &options);
        AudioHandle PostEvent(const Guid &event, const AudioPlayOptions &options);
        void Stop(AudioHandle handle);
        void StopOwner(ECS::Entity owner, bool include_persistent = false);
        void StopSceneVoices();
        void Pause(AudioHandle handle);
        void Resume(AudioHandle handle);
        void SetVolume(AudioHandle handle, f32 volume);
        void SetPitch(AudioHandle handle, f32 pitch);
        void SetPosition(AudioHandle handle, const Vector3f &position);
        void SetVelocity(AudioHandle handle, const Vector3f &velocity);
        void FadeIn(AudioHandle handle, f32 duration);
        void FadeOut(AudioHandle handle, f32 duration);
        void SetListener(const AudioListenerState &listener);
        void SetBusVolume(EAudioBus bus, f32 volume);
        void SetBusMuted(EAudioBus bus, bool muted);
        void StopBus(EAudioBus bus);
        void FadeBus(EAudioBus bus, f32 target_volume, f32 duration);
        bool IsPlaying(AudioHandle handle) const;
        bool IsValid(AudioHandle handle) const;
        u32 ActiveVoiceCount() const;
        u32 MaxVoiceCount() const;

    private:
        u32 CountEventInstances(const Guid &event, ECS::Entity owner, EAudioConcurrencyScope scope) const;
        f32 GetBusEffectiveVolume(EAudioBus bus) const;
        f32 GetVoiceEffectiveVolume(const AudioVoice &voice) const;
        void ReleaseVoice(AudioVoice &voice);

    private:
        struct BusState
        {
            f32 _volume = 1.0f;
            bool _muted = false;
            AudioFadeState _fade;
        };

    private:
        Scope<IAudioBackend> _backend;
        AudioVoicePool _voice_pool;
        AudioListenerState _listener;
        Array<BusState, static_cast<u32>(EAudioBus::kCount)> _buses;
        std::map<Guid, f32> _event_cooldowns;
        f32 _time = 0.0f;
        bool _initialized = false;
    };
}// namespace Ailu

#endif// __AUDIO_DEVICE_H__
