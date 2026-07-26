#pragma once
#ifndef __AUDIO_H__
#define __AUDIO_H__

#include "Audio/AudioDevice.h"

namespace Ailu::Audio
{
    AILU_API bool Initialize(const AudioDeviceConfig &config = {});
    AILU_API void Shutdown();
    AILU_API void Update(f32 delta_time);
    AILU_API AudioDevice *GetDevice();
    AILU_API AudioHandle Play(const Guid &clip, const AudioPlayOptions &options = {});
    AILU_API AudioHandle PostEvent(const Guid &event, const AudioPlayOptions &options = {});
    AILU_API AudioHandle PostEventAt(const Guid &event, const Vector3f &position, const AudioPlayOptions &options = {});
    AILU_API AudioHandle PlayMusic(const Guid &clip, f32 fade_in_duration = 0.5f);
    AILU_API void StopMusic(f32 fade_out_duration = 0.5f);
    AILU_API void Stop(AudioHandle handle);
    AILU_API void Pause(AudioHandle handle);
    AILU_API void Resume(AudioHandle handle);
    AILU_API void SetVolume(AudioHandle handle, f32 volume);
    AILU_API void SetPitch(AudioHandle handle, f32 pitch);
    AILU_API void SetPosition(AudioHandle handle, const Vector3f &position);
    AILU_API void FadeIn(AudioHandle handle, f32 duration);
    AILU_API void FadeOut(AudioHandle handle, f32 duration);
    AILU_API void SetBusVolume(EAudioBus bus, f32 volume);
    AILU_API void SetBusMuted(EAudioBus bus, bool muted);
    AILU_API void StopBus(EAudioBus bus);
}// namespace Ailu::Audio

#endif// __AUDIO_H__
