#include "Audio/AudioVoicePool.h"
#include "Framework/Math/VectorMath.hpp"
#include <limits>

namespace Ailu
{
    void AudioVoicePool::Initialize(u32 max_voices)
    {
        _voices.clear();
        _generations.clear();
        _voices.resize(max_voices);
        _generations.resize(max_voices, 1u);
    }

    void AudioVoicePool::Shutdown()
    {
        _voices.clear();
        _generations.clear();
    }

    AudioVoice *AudioVoicePool::Allocate(const AudioVoice &voice, const AudioListenerState &listener)
    {
        u32 slot = FindFreeSlot();
        if (slot == std::numeric_limits<u32>::max())
            slot = FindStealableSlot(voice, listener);
        if (slot == std::numeric_limits<u32>::max())
            return nullptr;

        AudioVoice &dst = _voices[slot];
        dst = voice;
        dst._handle = AudioHandle{slot, _generations[slot]};
        dst._active = true;
        return &dst;
    }

    void AudioVoicePool::Release(AudioHandle handle)
    {
        if (!IsValid(handle))
            return;
        AudioVoice &voice = _voices[handle._index];
        voice = {};
        ++_generations[handle._index];
        if (_generations[handle._index] == 0u)
            _generations[handle._index] = 1u;
    }

    AudioVoice *AudioVoicePool::Get(AudioHandle handle)
    {
        return IsValid(handle) ? &_voices[handle._index] : nullptr;
    }

    const AudioVoice *AudioVoicePool::Get(AudioHandle handle) const
    {
        return IsValid(handle) ? &_voices[handle._index] : nullptr;
    }

    bool AudioVoicePool::IsValid(AudioHandle handle) const
    {
        return handle.IsValid() && handle._index < _voices.size() && _generations[handle._index] == handle._generation &&
               _voices[handle._index]._active;
    }

    u32 AudioVoicePool::ActiveVoiceCount() const
    {
        u32 count = 0u;
        for (const auto &voice: _voices)
        {
            if (voice._active)
                ++count;
        }
        return count;
    }

    f32 AudioVoicePool::CalculateVoiceScore(const AudioVoice &voice, const Vector3f &listener_position) const
    {
        if (!voice._is_3d)
            return voice._priority + 1.0f;

        const f32 distance = Math::Distance(voice._position, listener_position);
        const f32 max_distance = std::max(voice._max_distance, 0.001f);
        const f32 normalized_distance = std::clamp(distance / max_distance, 0.0f, 1.0f);
        return voice._priority * 2.0f + (1.0f - normalized_distance);
    }

    u32 AudioVoicePool::FindFreeSlot() const
    {
        for (u32 i = 0u; i < _voices.size(); ++i)
        {
            if (!_voices[i]._active)
                return i;
        }
        return std::numeric_limits<u32>::max();
    }

    u32 AudioVoicePool::FindStealableSlot(const AudioVoice &new_voice, const AudioListenerState &listener) const
    {
        f32 lowest_score = std::numeric_limits<f32>::max();
        u32 lowest_index = std::numeric_limits<u32>::max();
        for (u32 i = 0u; i < _voices.size(); ++i)
        {
            const AudioVoice &voice = _voices[i];
            if (!voice._active || voice._persistent)
                continue;

            const f32 score = CalculateVoiceScore(voice, listener._position);
            if (score < lowest_score)
            {
                lowest_score = score;
                lowest_index = i;
            }
        }

        if (lowest_index == std::numeric_limits<u32>::max())
            return lowest_index;

        const f32 new_score = CalculateVoiceScore(new_voice, listener._position);
        return new_score > lowest_score ? lowest_index : std::numeric_limits<u32>::max();
    }
}// namespace Ailu
