#pragma once
#ifndef __AUDIO_TYPES_H__
#define __AUDIO_TYPES_H__

#include "Audio/AudioHandle.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Core/String.h"
#include "Framework/Math/Guid.h"
#include "Framework/Math/Vector.hpp"
#include "Scene/Entity.h"

namespace Ailu
{
    enum class EAudioLoadMode
    {
        kMemory,
        kStreaming
    };

    enum class EAudioChannelMode
    {
        kAuto,
        kMono,
        kStereo
    };

    enum class EAudioBus
    {
        kMaster,
        kMusic,
        kSfx,
        kUi,
        kAmbient,
        kVoice,
        kCount
    };

    enum class EAudioConcurrencyScope
    {
        kGlobal,
        kPerEntity
    };

    enum class EAudioAttenuationModel
    {
        kLinear,
        kInverse
    };

    struct AILU_API AudioDeviceConfig
    {
        bool _enabled = true;
        u32 _sample_rate = 48000u;
        u32 _max_voices = 128u;
        u32 _max_streaming_voices = 8u;
        String _device_name;
    };

    struct AILU_API AudioListenerState
    {
        Vector3f _position = Vector3f::kZero;
        Vector3f _forward = Vector3f::kForward;
        Vector3f _up = Vector3f::kUp;
        Vector3f _velocity = Vector3f::kZero;
    };

    struct AILU_API AudioPlayOptions
    {
        ECS::Entity _owner = ECS::kInvalidEntity;
        Vector3f _position = Vector3f::kZero;
        Vector3f _velocity = Vector3f::kZero;
        EAudioBus _bus = EAudioBus::kSfx;
        f32 _volume = 1.0f;
        f32 _pitch = 1.0f;
        f32 _priority = 0.5f;
        f32 _min_distance = 1.0f;
        f32 _max_distance = 30.0f;
        f32 _fade_in_duration = 0.0f;
        bool _is_3d = false;
        bool _loop = false;
        bool _persistent = false;
        bool _streaming = false;
    };

    struct AILU_API BackendVoiceCreateInfo
    {
        Guid _clip;
        EAudioBus _bus = EAudioBus::kSfx;
        Vector3f _position = Vector3f::kZero;
        Vector3f _velocity = Vector3f::kZero;
        f32 _volume = 1.0f;
        f32 _pitch = 1.0f;
        f32 _min_distance = 1.0f;
        f32 _max_distance = 30.0f;
        bool _is_3d = false;
        bool _loop = false;
        bool _streaming = false;
    };

    struct AILU_API AudioFadeState
    {
        f32 _start_volume = 1.0f;
        f32 _target_volume = 1.0f;
        f32 _duration = 0.0f;
        f32 _elapsed = 0.0f;
        bool _stop_when_finished = false;
        bool _active = false;
    };

    struct AILU_API AudioEventClip
    {
        Guid _clip;
        f32 _weight = 1.0f;
    };
}// namespace Ailu

#endif// __AUDIO_TYPES_H__
