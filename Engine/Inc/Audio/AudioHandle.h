#pragma once
#ifndef __AUDIO_HANDLE_H__
#define __AUDIO_HANDLE_H__

#include "Framework/Core/CoreMinimal.h"

namespace Ailu
{
    struct AILU_API AudioHandle
    {
        u32 _index = 0u;
        u32 _generation = 0u;

        bool IsValid() const
        {
            return _generation != 0u;
        }

        static AudioHandle Invalid()
        {
            return {};
        }

        bool operator==(const AudioHandle &other) const
        {
            return _index == other._index && _generation == other._generation;
        }

        bool operator!=(const AudioHandle &other) const
        {
            return !(*this == other);
        }
    };

    struct AILU_API BackendVoiceHandle
    {
        u32 _index = 0u;
        u32 _generation = 0u;

        bool IsValid() const
        {
            return _generation != 0u;
        }

        static BackendVoiceHandle Invalid()
        {
            return {};
        }
    };
}// namespace Ailu

#endif// __AUDIO_HANDLE_H__
