#pragma once
#ifndef __ANIMATION_INSTANCE_H__
#define __ANIMATION_INSTANCE_H__

#include "Animation/AnimationControllerAsset.h"
#include "Framework/Core/Containers/Vector.h"

namespace Ailu
{
    using AnimationInstanceHandle = u32;
    inline constexpr AnimationInstanceHandle kInvalidAnimationInstanceHandle = 0u;

    struct AILU_API AnimationInstance
    {
        u16 _current_state = kInvalidAnimationState;
        u16 _next_state = kInvalidAnimationState;

        f32 _state_time = 0.0f;
        f32 _next_state_time = 0.0f;
        f32 _transition_time = 0.0f;
        f32 _transition_duration = 0.0f;
        f32 _current_motion_duration = 0.0f;
        f32 _next_motion_duration = 0.0f;
        f32 _speed = 1.0f;
        u16 _previous_state = kInvalidAnimationState;
        u16 _previous_next_state = kInvalidAnimationState;
        f32 _previous_state_time = 0.0f;
        f32 _previous_next_state_time = 0.0f;

        Vector<f32> _float_parameters;
        Vector<i32> _int_parameters;
        Vector<u8> _bool_parameters;
        Vector<u8> _triggers;

        bool _in_transition = false;
        bool _initialized = false;

        void Initialize(const AnimationControllerAsset &controller);
        void SetFloat(AnimationParameterId id, f32 value);
        void SetInt(AnimationParameterId id, i32 value);
        void SetBool(AnimationParameterId id, bool value);
        void SetTrigger(AnimationParameterId id);
        void ResetTrigger(AnimationParameterId id);
    };

    class AILU_API AnimationInstancePool
    {
    public:
        AnimationInstanceHandle Create(const AnimationControllerAsset &controller);
        void Destroy(AnimationInstanceHandle handle);

        AnimationInstance *Get(AnimationInstanceHandle handle);
        const AnimationInstance *Get(AnimationInstanceHandle handle) const;

    private:
        Vector<AnimationInstance> _instances;
        Vector<u32> _free_indices;
    };
}

#endif // __ANIMATION_INSTANCE_H__
