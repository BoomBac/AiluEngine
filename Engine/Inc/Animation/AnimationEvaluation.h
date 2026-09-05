#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Math/Guid.h"

namespace Ailu
{
    struct AILU_API AnimationSample
    {
        Guid _clip = Guid::EmptyGuid();
        f32 _time = 0.0f;
        f32 _weight = 1.0f;
        bool _loop = true;
        bool _normalized_time = false;
    };

    struct AILU_API AnimationEvaluation
    {
        Array<AnimationSample, 4> _samples;
        u8 _sample_count = 0;

        void Clear() { _sample_count = 0; }

        bool AddSample(const AnimationSample &sample)
        {
            if (_sample_count >= _samples.size())
                return false;
            _samples[_sample_count++] = sample;
            return true;
        }
    };
}
