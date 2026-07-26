#pragma once
#ifndef __AUDIO_VOICE_H__
#define __AUDIO_VOICE_H__

#include "Audio/AudioTypes.h"

namespace Ailu
{
    struct AILU_API AudioVoice
    {
        AudioHandle _handle;
        BackendVoiceHandle _backend_handle;
        Guid _clip;
        Guid _event;
        ECS::Entity _owner = ECS::kInvalidEntity;
        EAudioBus _bus = EAudioBus::kSfx;
        f32 _base_volume = 1.0f;
        f32 _current_volume = 1.0f;
        f32 _pitch = 1.0f;
        f32 _priority = 0.5f;
        f32 _min_distance = 1.0f;
        f32 _max_distance = 30.0f;
        f32 _age = 0.0f;
        Vector3f _position = Vector3f::kZero;
        Vector3f _velocity = Vector3f::kZero;
        AudioFadeState _fade;
        bool _is_3d = false;
        bool _loop = false;
        bool _paused = false;
        bool _persistent = false;
        bool _active = false;
        bool _streaming = false;
    };
}// namespace Ailu

#endif// __AUDIO_VOICE_H__
