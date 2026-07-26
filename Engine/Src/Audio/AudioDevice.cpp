#include "Audio/AudioDevice.h"
#include "Audio/AudioEvent.h"
#include "Audio/Backend/MiniaudioBackend.h"
#include "Audio/Backend/NullAudioBackend.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Math/VectorMath.hpp"
#include <algorithm>
#include <random>

namespace Ailu
{
    namespace
    {
        f32 Clamp01(f32 value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }
    }

    bool AudioDevice::Initialize(const AudioDeviceConfig &config)
    {
        if (_initialized)
            return true;

        _voice_pool.Initialize(config._max_voices);
        for (auto &bus: _buses)
            bus = {};

        if (config._enabled)
        {
            _backend = MakeScope<MiniaudioBackend>();
            if (!_backend->Initialize(config))
            {
                LOG_WARNING("Audio: miniaudio backend unavailable, using NullAudioBackend");
                _backend = MakeScope<NullAudioBackend>();
                _backend->Initialize(config);
            }
        }
        else
        {
            _backend = MakeScope<NullAudioBackend>();
            _backend->Initialize(config);
        }

        _initialized = true;
        return true;
    }

    void AudioDevice::Shutdown()
    {
        if (!_initialized)
            return;

        for (auto &voice: _voice_pool.Voices())
        {
            if (voice._active && voice._backend_handle.IsValid())
                _backend->DestroyVoice(voice._backend_handle);
        }
        _voice_pool.Shutdown();
        _backend->Shutdown();
        _backend.reset();
        _initialized = false;
    }

    void AudioDevice::Update(f32 delta_time)
    {
        if (!_initialized)
            return;

        _time += delta_time;
        for (auto &bus: _buses)
        {
            if (!bus._fade._active)
                continue;
            bus._fade._elapsed += delta_time;
            const f32 t = bus._fade._duration > 0.0f ? Clamp01(bus._fade._elapsed / bus._fade._duration) : 1.0f;
            bus._volume = Math::Lerp(bus._fade._start_volume, bus._fade._target_volume, t);
            if (t >= 1.0f)
                bus._fade._active = false;
        }

        Vector<AudioHandle> pending_release;
        for (auto &voice: _voice_pool.Voices())
        {
            if (!voice._active)
                continue;

            voice._age += delta_time;
            if (voice._fade._active)
            {
                voice._fade._elapsed += delta_time;
                const f32 t = voice._fade._duration > 0.0f ? Clamp01(voice._fade._elapsed / voice._fade._duration) : 1.0f;
                voice._current_volume = Math::Lerp(voice._fade._start_volume, voice._fade._target_volume, t);
                _backend->SetVolume(voice._backend_handle, GetVoiceEffectiveVolume(voice));
                if (t >= 1.0f)
                {
                    const bool stop_when_finished = voice._fade._stop_when_finished;
                    voice._fade._active = false;
                    if (stop_when_finished)
                        pending_release.push_back(voice._handle);
                }
            }

            if (!voice._loop && !voice._paused && !_backend->IsPlaying(voice._backend_handle))
                pending_release.push_back(voice._handle);
        }

        for (AudioHandle handle: pending_release)
            Stop(handle);
        _backend->Update();
    }

    AudioHandle AudioDevice::Play(const Guid &clip, const AudioPlayOptions &options)
    {
        if (!_initialized || clip == Guid::EmptyGuid())
            return AudioHandle::Invalid();

        AudioVoice voice;
        voice._clip = clip;
        voice._owner = options._owner;
        voice._bus = options._bus;
        voice._base_volume = std::max(options._volume, 0.0f);
        voice._current_volume = options._fade_in_duration > 0.0f ? 0.0f : voice._base_volume;
        voice._pitch = std::max(options._pitch, 0.01f);
        voice._priority = options._priority;
        voice._min_distance = std::max(options._min_distance, 0.0f);
        voice._max_distance = std::max(options._max_distance, voice._min_distance + 0.001f);
        voice._position = options._position;
        voice._velocity = options._velocity;
        voice._is_3d = options._is_3d;
        voice._loop = options._loop;
        voice._persistent = options._persistent;
        voice._streaming = options._streaming;

        AudioVoice *allocated = _voice_pool.Allocate(voice, _listener);
        if (allocated == nullptr)
        {
            LOG_WARNING("Audio: voice pool exhausted");
            return AudioHandle::Invalid();
        }

        BackendVoiceCreateInfo create_info;
        create_info._clip = clip;
        create_info._bus = allocated->_bus;
        create_info._position = allocated->_position;
        create_info._velocity = allocated->_velocity;
        create_info._volume = GetVoiceEffectiveVolume(*allocated);
        create_info._pitch = allocated->_pitch;
        create_info._min_distance = allocated->_min_distance;
        create_info._max_distance = allocated->_max_distance;
        create_info._is_3d = allocated->_is_3d;
        create_info._loop = allocated->_loop;
        create_info._streaming = allocated->_streaming;

        allocated->_backend_handle = _backend->CreateVoice(create_info);
        if (!allocated->_backend_handle.IsValid())
        {
            _voice_pool.Release(allocated->_handle);
            return AudioHandle::Invalid();
        }

        if (options._fade_in_duration > 0.0f)
            FadeIn(allocated->_handle, options._fade_in_duration);
        _backend->Play(allocated->_backend_handle);
        return allocated->_handle;
    }

    AudioHandle AudioDevice::PostEvent(const Guid &event, const AudioPlayOptions &options)
    {
        if (!_initialized || event == Guid::EmptyGuid())
            return AudioHandle::Invalid();

        Ref<AudioEvent> audio_event = ResourceMgr::Get().GetRef<AudioEvent>(event);
        if (!audio_event && !ResourceMgr::Get().GuidToAssetPath(event).empty())
            audio_event = ResourceMgr::Get().Load<AudioEvent>(event);
        if (!audio_event || audio_event->_clips.empty())
            return AudioHandle::Invalid();

        const auto cooldown_iter = _event_cooldowns.find(event);
        if (cooldown_iter != _event_cooldowns.end() && cooldown_iter->second > _time)
            return AudioHandle::Invalid();

        if (audio_event->_max_instances > 0u &&
            CountEventInstances(event, options._owner, audio_event->_concurrency_scope) >= audio_event->_max_instances)
        {
            return AudioHandle::Invalid();
        }

        f32 total_weight = 0.0f;
        for (const auto &clip: audio_event->_clips)
            total_weight += std::max(clip._weight, 0.0f);
        if (total_weight <= 0.0f)
            return AudioHandle::Invalid();

        static std::mt19937 s_rng{std::random_device{}()};
        std::uniform_real_distribution<f32> weight_distribution(0.0f, total_weight);
        f32 selected_weight = weight_distribution(s_rng);
        Guid selected_clip = audio_event->_clips.back()._clip;
        for (const auto &clip: audio_event->_clips)
        {
            selected_weight -= std::max(clip._weight, 0.0f);
            if (selected_weight <= 0.0f)
            {
                selected_clip = clip._clip;
                break;
            }
        }

        AudioPlayOptions merged_options = options;
        merged_options._bus = audio_event->_bus;
        merged_options._is_3d = audio_event->_is_3d || options._is_3d;
        merged_options._loop = audio_event->_loop || options._loop;
        merged_options._min_distance = options._min_distance != 1.0f ? options._min_distance : audio_event->_min_distance;
        merged_options._max_distance = options._max_distance != 30.0f ? options._max_distance : audio_event->_max_distance;

        const f32 volume_min = std::min(audio_event->_volume_min, audio_event->_volume_max);
        const f32 volume_max = std::max(audio_event->_volume_min, audio_event->_volume_max);
        const f32 pitch_min = std::min(audio_event->_pitch_min, audio_event->_pitch_max);
        const f32 pitch_max = std::max(audio_event->_pitch_min, audio_event->_pitch_max);
        merged_options._volume *= std::uniform_real_distribution<f32>(volume_min, volume_max)(s_rng);
        merged_options._pitch *= std::uniform_real_distribution<f32>(pitch_min, pitch_max)(s_rng);

        AudioHandle handle = Play(selected_clip, merged_options);
        if (AudioVoice *voice = _voice_pool.Get(handle))
            voice->_event = event;
        if (handle.IsValid() && audio_event->_cooldown > 0.0f)
            _event_cooldowns[event] = _time + audio_event->_cooldown;
        return handle;
    }

    void AudioDevice::Stop(AudioHandle handle)
    {
        AudioVoice *voice = _voice_pool.Get(handle);
        if (voice == nullptr)
            return;
        ReleaseVoice(*voice);
    }

    void AudioDevice::StopOwner(ECS::Entity owner, bool include_persistent)
    {
        Vector<AudioHandle> handles;
        for (const auto &voice: _voice_pool.Voices())
        {
            if (voice._active && voice._owner == owner && (include_persistent || !voice._persistent))
                handles.push_back(voice._handle);
        }
        for (AudioHandle handle: handles)
            Stop(handle);
    }

    void AudioDevice::StopSceneVoices()
    {
        Vector<AudioHandle> handles;
        for (const auto &voice: _voice_pool.Voices())
        {
            if (voice._active && !voice._persistent)
                handles.push_back(voice._handle);
        }
        for (AudioHandle handle: handles)
            Stop(handle);
    }

    void AudioDevice::Pause(AudioHandle handle)
    {
        AudioVoice *voice = _voice_pool.Get(handle);
        if (voice == nullptr)
            return;
        voice->_paused = true;
        _backend->Pause(voice->_backend_handle);
    }

    void AudioDevice::Resume(AudioHandle handle)
    {
        AudioVoice *voice = _voice_pool.Get(handle);
        if (voice == nullptr)
            return;
        voice->_paused = false;
        _backend->Resume(voice->_backend_handle);
    }

    void AudioDevice::SetVolume(AudioHandle handle, f32 volume)
    {
        AudioVoice *voice = _voice_pool.Get(handle);
        if (voice == nullptr)
            return;
        voice->_base_volume = std::max(volume, 0.0f);
        voice->_current_volume = voice->_base_volume;
        _backend->SetVolume(voice->_backend_handle, GetVoiceEffectiveVolume(*voice));
    }

    void AudioDevice::SetPitch(AudioHandle handle, f32 pitch)
    {
        AudioVoice *voice = _voice_pool.Get(handle);
        if (voice == nullptr)
            return;
        voice->_pitch = std::max(pitch, 0.01f);
        _backend->SetPitch(voice->_backend_handle, voice->_pitch);
    }

    void AudioDevice::SetPosition(AudioHandle handle, const Vector3f &position)
    {
        AudioVoice *voice = _voice_pool.Get(handle);
        if (voice == nullptr)
            return;
        voice->_position = position;
        _backend->SetPosition(voice->_backend_handle, position);
    }

    void AudioDevice::SetVelocity(AudioHandle handle, const Vector3f &velocity)
    {
        AudioVoice *voice = _voice_pool.Get(handle);
        if (voice == nullptr)
            return;
        voice->_velocity = velocity;
        _backend->SetVelocity(voice->_backend_handle, velocity);
    }

    void AudioDevice::FadeIn(AudioHandle handle, f32 duration)
    {
        AudioVoice *voice = _voice_pool.Get(handle);
        if (voice == nullptr)
            return;
        voice->_fade = {0.0f, voice->_base_volume, std::max(duration, 0.0f), 0.0f, false, true};
        voice->_current_volume = 0.0f;
        _backend->SetVolume(voice->_backend_handle, GetVoiceEffectiveVolume(*voice));
    }

    void AudioDevice::FadeOut(AudioHandle handle, f32 duration)
    {
        AudioVoice *voice = _voice_pool.Get(handle);
        if (voice == nullptr)
            return;
        voice->_fade = {voice->_current_volume, 0.0f, std::max(duration, 0.0f), 0.0f, true, true};
    }

    void AudioDevice::SetListener(const AudioListenerState &listener)
    {
        _listener = listener;
        if (_initialized)
            _backend->SetListener(listener);
    }

    void AudioDevice::SetBusVolume(EAudioBus bus, f32 volume)
    {
        const u32 index = static_cast<u32>(bus);
        if (index >= _buses.size())
            return;
        _buses[index]._volume = std::max(volume, 0.0f);
        _buses[index]._fade._active = false;
        if (_initialized)
            _backend->SetBusVolume(bus, GetBusEffectiveVolume(bus));
    }

    void AudioDevice::SetBusMuted(EAudioBus bus, bool muted)
    {
        const u32 index = static_cast<u32>(bus);
        if (index >= _buses.size())
            return;
        _buses[index]._muted = muted;
        if (_initialized)
            _backend->SetBusMuted(bus, muted);
    }

    void AudioDevice::StopBus(EAudioBus bus)
    {
        Vector<AudioHandle> handles;
        for (const auto &voice: _voice_pool.Voices())
        {
            if (voice._active && voice._bus == bus)
                handles.push_back(voice._handle);
        }
        for (AudioHandle handle: handles)
            Stop(handle);
    }

    void AudioDevice::FadeBus(EAudioBus bus, f32 target_volume, f32 duration)
    {
        const u32 index = static_cast<u32>(bus);
        if (index >= _buses.size())
            return;
        BusState &state = _buses[index];
        state._fade = {state._volume, std::max(target_volume, 0.0f), std::max(duration, 0.0f), 0.0f, false, true};
    }

    bool AudioDevice::IsPlaying(AudioHandle handle) const
    {
        const AudioVoice *voice = _voice_pool.Get(handle);
        return voice != nullptr && !voice->_paused && _backend->IsPlaying(voice->_backend_handle);
    }

    bool AudioDevice::IsValid(AudioHandle handle) const
    {
        return _voice_pool.IsValid(handle);
    }

    u32 AudioDevice::ActiveVoiceCount() const
    {
        return _voice_pool.ActiveVoiceCount();
    }

    u32 AudioDevice::MaxVoiceCount() const
    {
        return _voice_pool.MaxVoiceCount();
    }

    u32 AudioDevice::CountEventInstances(const Guid &event, ECS::Entity owner, EAudioConcurrencyScope scope) const
    {
        u32 count = 0u;
        for (const auto &voice: _voice_pool.Voices())
        {
            if (!voice._active || !(voice._event == event))
                continue;
            if (scope == EAudioConcurrencyScope::kGlobal || voice._owner == owner)
                ++count;
        }
        return count;
    }

    f32 AudioDevice::GetBusEffectiveVolume(EAudioBus bus) const
    {
        const u32 index = static_cast<u32>(bus);
        if (index >= _buses.size())
            return 1.0f;
        const BusState &state = _buses[index];
        if (state._muted)
            return 0.0f;
        if (bus == EAudioBus::kMaster)
            return state._volume;
        return state._volume * GetBusEffectiveVolume(EAudioBus::kMaster);
    }

    f32 AudioDevice::GetVoiceEffectiveVolume(const AudioVoice &voice) const
    {
        return voice._current_volume * GetBusEffectiveVolume(voice._bus);
    }

    void AudioDevice::ReleaseVoice(AudioVoice &voice)
    {
        const AudioHandle handle = voice._handle;
        if (voice._backend_handle.IsValid())
        {
            _backend->Stop(voice._backend_handle);
            _backend->DestroyVoice(voice._backend_handle);
        }
        _voice_pool.Release(handle);
    }
}// namespace Ailu
