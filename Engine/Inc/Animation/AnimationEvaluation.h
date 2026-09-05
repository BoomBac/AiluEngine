#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Math/Guid.h"
#include "Animation/Pose.h"
#include "Animation/RootMotion.h"

namespace Ailu
{
    struct AILU_API AnimationSample
    {
        Guid _clip = Guid::EmptyGuid();
        f32 _time = 0.0f;
        f32 _weight = 1.0f;
        bool _loop = true;
        bool _normalized_time = false;
        f32 _previous_time = 0.0f;
        bool _has_previous_time = false;
    };

    struct AILU_API AnimationEvaluation
    {
        Array<AnimationSample, 4> _samples;
        u8 _sample_count = 0;
        ERootMotionMode _root_motion_mode = ERootMotionMode::kDisabled;

        void Clear() { _sample_count = 0; }

        bool AddSample(const AnimationSample &sample)
        {
            if (_sample_count >= _samples.size())
                return false;
            _samples[_sample_count++] = sample;
            return true;
        }
    };

    struct AILU_API AnimationEvaluateResult
    {
        Pose _pose;
        RootMotionDelta _root_motion;
    };
}
