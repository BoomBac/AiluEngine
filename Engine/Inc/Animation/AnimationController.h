#pragma once
#ifndef __ANIMATION_CONTROLLER_H__
#define __ANIMATION_CONTROLLER_H__

#include "Animation/AnimationEvaluation.h"
#include "Animation/AnimationInstance.h"
#include "Framework/Core/Containers/Map.h"

namespace Ailu
{
    class BlendSpaceAsset;

    class AILU_API AnimationController
    {
    public:
        AnimationController() = default;
        explicit AnimationController(const AnimationControllerAsset *asset) : _asset(asset) {}

        void SetAsset(const AnimationControllerAsset *asset) { _asset = asset; }
        const AnimationControllerAsset *Asset() const { return _asset; }

        void BindBlendSpace(const Guid &asset_id, const BlendSpaceAsset *blend_space);
        void ClearBlendSpaces() { _blend_spaces.clear(); }

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
        Map<Guid, const BlendSpaceAsset *> _blend_spaces;
    };
}

#endif // __ANIMATION_CONTROLLER_H__
