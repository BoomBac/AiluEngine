#pragma once
#ifndef AILU_ANIMATION_KEY_REDUCER_H
#define AILU_ANIMATION_KEY_REDUCER_H

#include "Framework/Core/CoreMinimal.h"

namespace Ailu
{
    class AnimationClip;
    class Pose;

    struct AnimationReductionSettings
    {
        f32 _position_tolerance = 0.0005f;
        f32 _rotation_tolerance = 0.002f;
        f32 _scale_tolerance = 0.0005f;
    };

    class AILU_API AnimationKeyReducer
    {
    public:
        static void Reduce(AnimationClip &clip, const AnimationReductionSettings &settings);
        static void Reduce(AnimationClip &clip, const Pose &reference_pose,
                           const AnimationReductionSettings &settings);
    };
}

#endif // AILU_ANIMATION_KEY_REDUCER_H
