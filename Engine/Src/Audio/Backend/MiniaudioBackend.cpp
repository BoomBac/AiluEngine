#include "Audio/Backend/MiniaudioBackend.h"
#include "Audio/AudioClip.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"

#include "miniaudio/miniaudio.h"
#include <algorithm>
#include <limits>

namespace Ailu
{
    namespace
    {
        constexpr u32 kInvalidBackendSlot = std::numeric_limits<u32>::max();

        u32 BusIndex(EAudioBus bus)
        {
            return static_cast<u32>(bus);
        }
    }

    struct MiniaudioBackend::Implementation
    {
        struct VoiceSlot
        {
            ma_sound _sound{};
            u32 _generation = 1u;
            bool _active = false;
            bool _initialized = false;
        };

        ma_engine _engine{};
        Array<ma_sound_group, static_cast<u32>(EAudioBus::kCount)> _groups{};
        Array<bool, static_cast<u32>(EAudioBus::kCount)> _group_initialized{};
        Vector<VoiceSlot> _voices;
        bool _initialized = false;
    };

    MiniaudioBackend::MiniaudioBackend() = default;

    MiniaudioBackend::~MiniaudioBackend()
    {
        Shutdown();
    }

    bool MiniaudioBackend::Initialize(const AudioDeviceConfig &config)
    {
        if (_impl && _impl->_initialized)
            return true;

        _impl = AL_NEW_TAG(EMemoryTag::kAudio, Implementation);

        ma_engine_config engine_config = ma_engine_config_init();
        engine_config.sampleRate = config._sample_rate;
        if (ma_engine_init(&engine_config, &_impl->_engine) != MA_SUCCESS)
        {
            AL_DELETE(_impl);
            return false;
        }

        const ma_result master_result = ma_sound_group_init(&_impl->_engine, 0, nullptr, &_impl->_groups[BusIndex(EAudioBus::kMaster)]);
        if (master_result != MA_SUCCESS)
        {
            ma_engine_uninit(&_impl->_engine);
            AL_DELETE(_impl);
            return false;
        }
        _impl->_group_initialized[BusIndex(EAudioBus::kMaster)] = true;

        for (u32 i = 0u; i < static_cast<u32>(EAudioBus::kCount); ++i)
        {
            if (i == BusIndex(EAudioBus::kMaster))
                continue;
            ma_sound_group *master_group = &_impl->_groups[BusIndex(EAudioBus::kMaster)];
            if (ma_sound_group_init(&_impl->_engine, 0, master_group, &_impl->_groups[i]) == MA_SUCCESS)
                _impl->_group_initialized[i] = true;
        }

        _impl->_voices.resize(config._max_voices);
        _impl->_initialized = true;
        LOG_INFO("Audio: miniaudio backend initialized");
        return true;
    }

    void MiniaudioBackend::Shutdown()
    {
        if (!_impl || !_impl->_initialized)
            return;

        for (auto &voice: _impl->_voices)
        {
            if (voice._initialized)
                ma_sound_uninit(&voice._sound);
            voice = {};
        }

        for (u32 i = static_cast<u32>(EAudioBus::kCount); i > 0u; --i)
        {
            const u32 index = i - 1u;
            if (_impl->_group_initialized[index])
                ma_sound_group_uninit(&_impl->_groups[index]);
        }

        ma_engine_uninit(&_impl->_engine);
        AL_DELETE(_impl);
    }

    void MiniaudioBackend::Update()
    {
    }

    BackendVoiceHandle MiniaudioBackend::CreateVoice(const BackendVoiceCreateInfo &create_info)
    {
        if (!_impl || !_impl->_initialized)
            return {};

        u32 slot_index = kInvalidBackendSlot;
        for (u32 i = 0u; i < _impl->_voices.size(); ++i)
        {
            if (!_impl->_voices[i]._active)
            {
                slot_index = i;
                break;
            }
        }
        if (slot_index == kInvalidBackendSlot)
            return {};

        Ref<AudioClip> clip = ResourceMgr::Get().GetRef<AudioClip>(create_info._clip);
        if (!clip && !ResourceMgr::Get().GuidToAssetPath(create_info._clip).empty())
            clip = ResourceMgr::Get().Load<AudioClip>(create_info._clip);

        WString sound_path;
        if (clip && !clip->_runtime_path.empty())
            sound_path = ToWChar(clip->_runtime_path);
        else if (!ResourceMgr::Get().GuidToAssetPath(create_info._clip).empty())
            sound_path = ResourceMgr::GetResSysPath(ResourceMgr::Get().GuidToAssetPath(create_info._clip));

        if (sound_path.empty())
        {
            LOG_WARNING("Audio: failed to resolve audio clip path");
            return {};
        }

        ma_uint32 flags = create_info._streaming ? MA_SOUND_FLAG_STREAM : 0u;
        if (!create_info._is_3d)
            flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;

        const u32 bus_index = std::min(BusIndex(create_info._bus), static_cast<u32>(EAudioBus::kCount) - 1u);
        ma_sound_group *group = _impl->_group_initialized[bus_index] ? &_impl->_groups[bus_index] : nullptr;
        Implementation::VoiceSlot &slot = _impl->_voices[slot_index];
        const String sound_path_utf8 = ToChar(sound_path);
        if (ma_sound_init_from_file(&_impl->_engine, sound_path_utf8.c_str(), flags, group, nullptr, &slot._sound) != MA_SUCCESS)
        {
            LOG_WARNING("Audio: failed to load audio file {}", sound_path_utf8);
            return {};
        }

        slot._initialized = true;
        slot._active = true;
        ma_sound_set_volume(&slot._sound, create_info._volume);
        ma_sound_set_pitch(&slot._sound, create_info._pitch);
        ma_sound_set_looping(&slot._sound, create_info._loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_position(&slot._sound, create_info._position.x, create_info._position.y, create_info._position.z);
        ma_sound_set_velocity(&slot._sound, create_info._velocity.x, create_info._velocity.y, create_info._velocity.z);
        ma_sound_set_min_distance(&slot._sound, std::max(create_info._min_distance, 0.0f));
        ma_sound_set_max_distance(&slot._sound, std::max(create_info._max_distance, create_info._min_distance + 0.001f));

        return BackendVoiceHandle{slot_index, slot._generation};
    }

    void MiniaudioBackend::DestroyVoice(BackendVoiceHandle handle)
    {
        if (!_impl || handle._index >= _impl->_voices.size())
            return;

        Implementation::VoiceSlot &slot = _impl->_voices[handle._index];
        if (!slot._active || slot._generation != handle._generation)
            return;

        if (slot._initialized)
            ma_sound_uninit(&slot._sound);
        ++slot._generation;
        if (slot._generation == 0u)
            slot._generation = 1u;
        slot._active = false;
        slot._initialized = false;
    }

    void MiniaudioBackend::Play(BackendVoiceHandle handle)
    {
        if (!_impl || handle._index >= _impl->_voices.size())
            return;
        Implementation::VoiceSlot &slot = _impl->_voices[handle._index];
        if (slot._active && slot._generation == handle._generation)
            ma_sound_start(&slot._sound);
    }

    void MiniaudioBackend::Stop(BackendVoiceHandle handle)
    {
        if (!_impl || handle._index >= _impl->_voices.size())
            return;
        Implementation::VoiceSlot &slot = _impl->_voices[handle._index];
        if (slot._active && slot._generation == handle._generation)
        {
            ma_sound_stop(&slot._sound);
            ma_sound_seek_to_pcm_frame(&slot._sound, 0u);
        }
    }

    void MiniaudioBackend::Pause(BackendVoiceHandle handle)
    {
        if (!_impl || handle._index >= _impl->_voices.size())
            return;
        Implementation::VoiceSlot &slot = _impl->_voices[handle._index];
        if (slot._active && slot._generation == handle._generation)
            ma_sound_stop(&slot._sound);
    }

    void MiniaudioBackend::Resume(BackendVoiceHandle handle)
    {
        Play(handle);
    }

    void MiniaudioBackend::SetVolume(BackendVoiceHandle handle, f32 volume)
    {
        if (!_impl || handle._index >= _impl->_voices.size())
            return;
        Implementation::VoiceSlot &slot = _impl->_voices[handle._index];
        if (slot._active && slot._generation == handle._generation)
            ma_sound_set_volume(&slot._sound, std::max(volume, 0.0f));
    }

    void MiniaudioBackend::SetPitch(BackendVoiceHandle handle, f32 pitch)
    {
        if (!_impl || handle._index >= _impl->_voices.size())
            return;
        Implementation::VoiceSlot &slot = _impl->_voices[handle._index];
        if (slot._active && slot._generation == handle._generation)
            ma_sound_set_pitch(&slot._sound, std::max(pitch, 0.01f));
    }

    void MiniaudioBackend::SetPosition(BackendVoiceHandle handle, const Vector3f &position)
    {
        if (!_impl || handle._index >= _impl->_voices.size())
            return;
        Implementation::VoiceSlot &slot = _impl->_voices[handle._index];
        if (slot._active && slot._generation == handle._generation)
            ma_sound_set_position(&slot._sound, position.x, position.y, position.z);
    }

    void MiniaudioBackend::SetVelocity(BackendVoiceHandle handle, const Vector3f &velocity)
    {
        if (!_impl || handle._index >= _impl->_voices.size())
            return;
        Implementation::VoiceSlot &slot = _impl->_voices[handle._index];
        if (slot._active && slot._generation == handle._generation)
            ma_sound_set_velocity(&slot._sound, velocity.x, velocity.y, velocity.z);
    }

    void MiniaudioBackend::SetListener(const AudioListenerState &listener)
    {
        if (!_impl || !_impl->_initialized)
            return;
        ma_engine_listener_set_position(&_impl->_engine, 0u, listener._position.x, listener._position.y, listener._position.z);
        ma_engine_listener_set_direction(&_impl->_engine, 0u, listener._forward.x, listener._forward.y, listener._forward.z);
        ma_engine_listener_set_world_up(&_impl->_engine, 0u, listener._up.x, listener._up.y, listener._up.z);
        ma_engine_listener_set_velocity(&_impl->_engine, 0u, listener._velocity.x, listener._velocity.y, listener._velocity.z);
    }

    void MiniaudioBackend::SetBusVolume(EAudioBus bus, f32 volume)
    {
        if (!_impl)
            return;
        const u32 index = BusIndex(bus);
        if (index < _impl->_groups.size() && _impl->_group_initialized[index])
            ma_sound_group_set_volume(&_impl->_groups[index], std::max(volume, 0.0f));
    }

    void MiniaudioBackend::SetBusMuted(EAudioBus bus, bool muted)
    {
        SetBusVolume(bus, muted ? 0.0f : 1.0f);
    }

    bool MiniaudioBackend::IsPlaying(BackendVoiceHandle handle) const
    {
        if (!_impl || handle._index >= _impl->_voices.size())
            return false;
        const Implementation::VoiceSlot &slot = _impl->_voices[handle._index];
        return slot._active && slot._generation == handle._generation && ma_sound_is_playing(&slot._sound);
    }
}// namespace Ailu
