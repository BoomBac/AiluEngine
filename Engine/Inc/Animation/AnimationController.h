#pragma once
#ifndef __ANIMATION_CONTROLLER_H__
#define __ANIMATION_CONTROLLER_H__

#include "Animation/AnimationEvaluation.h"
#include "Animation/AnimationInstance.h"

namespace Ailu
{
    class AILU_API AnimationController
    {
    public:
        AnimationController() = default;
        explicit AnimationController(const AnimationControllerAsset *asset) : _asset(asset) {}

        void SetAsset(const AnimationControllerAsset *asset) { _asset = asset; }
        const AnimationControllerAsset *Asset() const { return _asset; }

        void Update(AnimationInstance &instance, f32 delta_time) const;
        AnimationEvaluation Evaluate(const AnimationInstance &instance) const;

    private:
        bool CheckTransition(const AnimationInstance &instance, const AnimationTransition &transition) const;
        bool CheckCondition(const AnimationInstance &instance, const AnimationCondition &condition) const;
        bool TryStartTransition(AnimationInstance &instance, u16 transition_index) const;
        void ConsumeTriggers(AnimationInstance &instance, const AnimationTransition &transition) const;
        void AddStateSample(const AnimationInstance &instance, u16 state_index, f32 weight, f32 state_time,
                            AnimationEvaluation &evaluation) const;

        const AnimationControllerAsset *_asset = nullptr;
    };
}

#endif // __ANIMATION_CONTROLLER_H__
