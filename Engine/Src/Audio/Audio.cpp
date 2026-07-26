#include "Audio/Audio.h"

namespace Ailu::Audio
{
    namespace
    {
        Scope<AudioDevice> s_device;
        AudioHandle s_music_handle;
    }

    bool Initialize(const AudioDeviceConfig &config)
    {
        if (s_device)
            return true;
        s_device = MakeScope<AudioDevice>();
        return s_device->Initialize(config);
    }

    void Shutdown()
    {
        if (!s_device)
            return;
        s_device->Shutdown();
        s_device.reset();
        s_music_handle = AudioHandle::Invalid();
    }

    void Update(f32 delta_time)
    {
        if (s_device)
            s_device->Update(delta_time);
    }

    AudioDevice *GetDevice()
    {
        return s_device.get();
    }

    AudioHandle Play(const Guid &clip, const AudioPlayOptions &options)
    {
        return s_device ? s_device->Play(clip, options) : AudioHandle::Invalid();
    }

    AudioHandle PostEvent(const Guid &event, const AudioPlayOptions &options)
    {
        return s_device ? s_device->PostEvent(event, options) : AudioHandle::Invalid();
    }

    AudioHandle PostEventAt(const Guid &event, const Vector3f &position, const AudioPlayOptions &options)
    {
        AudioPlayOptions spatial_options = options;
        spatial_options._position = position;
        spatial_options._is_3d = true;
        return PostEvent(event, spatial_options);
    }

    AudioHandle PlayMusic(const Guid &clip, f32 fade_in_duration)
    {
        if (s_device && s_music_handle.IsValid())
            s_device->FadeOut(s_music_handle, fade_in_duration);

        AudioPlayOptions options;
        options._bus = EAudioBus::kMusic;
        options._loop = true;
        options._persistent = true;
        options._streaming = true;
        options._fade_in_duration = fade_in_duration;
        s_music_handle = Play(clip, options);
        return s_music_handle;
    }

    void StopMusic(f32 fade_out_duration)
    {
        if (!s_device || !s_music_handle.IsValid())
            return;
        if (fade_out_duration > 0.0f)
            s_device->FadeOut(s_music_handle, fade_out_duration);
        else
            s_device->Stop(s_music_handle);
        s_music_handle = AudioHandle::Invalid();
    }

    void Stop(AudioHandle handle)
    {
        if (s_device)
            s_device->Stop(handle);
    }

    void Pause(AudioHandle handle)
    {
        if (s_device)
            s_device->Pause(handle);
    }

    void Resume(AudioHandle handle)
    {
        if (s_device)
            s_device->Resume(handle);
    }

    void SetVolume(AudioHandle handle, f32 volume)
    {
        if (s_device)
            s_device->SetVolume(handle, volume);
    }

    void SetPitch(AudioHandle handle, f32 pitch)
    {
        if (s_device)
            s_device->SetPitch(handle, pitch);
    }

    void SetPosition(AudioHandle handle, const Vector3f &position)
    {
        if (s_device)
            s_device->SetPosition(handle, position);
    }

    void FadeIn(AudioHandle handle, f32 duration)
    {
        if (s_device)
            s_device->FadeIn(handle, duration);
    }

    void FadeOut(AudioHandle handle, f32 duration)
    {
        if (s_device)
            s_device->FadeOut(handle, duration);
    }

    void SetBusVolume(EAudioBus bus, f32 volume)
    {
        if (s_device)
            s_device->SetBusVolume(bus, volume);
    }

    void SetBusMuted(EAudioBus bus, bool muted)
    {
        if (s_device)
            s_device->SetBusMuted(bus, muted);
    }

    void StopBus(EAudioBus bus)
    {
        if (s_device)
            s_device->StopBus(bus);
    }
}// namespace Ailu::Audio
