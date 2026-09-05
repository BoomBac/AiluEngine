#include "Animation/AnimationController.h"
#include "Animation/BlendSpace.h"

#include <algorithm>

namespace Ailu
{
    void AnimationController::BindBlendSpace(const Guid &asset_id, const BlendSpaceAsset *blend_space)
    {
        if (asset_id.IsEmpty() || blend_space == nullptr)
            return;
        _blend_spaces[asset_id] = blend_space;
    }

    void AnimationController::Update(AnimationInstance &instance, f32 delta_time) const
    {
        if (_asset == nullptr || !_asset->States().size() || !instance._initialized)
            return;

        if (instance._current_state == kInvalidAnimationState)
            instance._current_state = _asset->EntryState();
        if (instance._current_state >= _asset->States().size())
            return;

        const f32 dt = std::max(delta_time, 0.0f);
        instance._state_time += dt;
        if (instance._in_transition)
        {
            instance._next_state_time += dt;
            instance._transition_time += dt;
            if (instance._transition_time >= instance._transition_duration)
            {
                instance._current_state = instance._next_state;
                instance._state_time = instance._next_state_time;
                instance._next_state = kInvalidAnimationState;
                instance._next_state_time = 0.0f;
                instance._transition_time = 0.0f;
                instance._transition_duration = 0.0f;
                instance._current_motion_duration = instance._next_motion_duration;
                instance._next_motion_duration = 0.0f;
                instance._in_transition = false;
            }
            return;
        }

        const auto &state = _asset->States()[instance._current_state];
        for (u16 transition_index : _asset->AnyStateTransitions())
        {
            if (TryStartTransition(instance, transition_index))
                return;
        }
        for (u16 transition_index : state._transitions)
        {
            if (TryStartTransition(instance, transition_index))
                return;
        }
    }

    AnimationEvaluation AnimationController::Evaluate(const AnimationInstance &instance) const
    {
        AnimationEvaluation evaluation;
        if (_asset == nullptr || !instance._initialized || instance._current_state >= _asset->States().size())
            return evaluation;

        AddStateSample(instance, instance._current_state, instance._in_transition ?
                       1.0f - std::clamp(instance._transition_time / instance._transition_duration, 0.0f, 1.0f) : 1.0f,
                       instance._state_time, instance._current_motion_duration, evaluation);
        if (instance._in_transition && instance._next_state != kInvalidAnimationState)
        {
            const f32 weight = std::clamp(instance._transition_time / instance._transition_duration, 0.0f, 1.0f);
            AddStateSample(instance, instance._next_state, weight, instance._next_state_time,
                           instance._next_motion_duration, evaluation);
        }
        return evaluation;
    }

    bool AnimationController::CheckTransition(const AnimationInstance &instance,
                                               const AnimationTransition &transition) const
    {
        if (transition._from_state != kInvalidAnimationState && transition._from_state != instance._current_state)
            return false;
        if (transition._to_state == kInvalidAnimationState || transition._to_state >= _asset->States().size())
            return false;
        if (transition._has_exit_time)
        {
            if (instance._current_motion_duration <= 0.0f)
                return false;
            const f32 normalized_time = instance._state_time / instance._current_motion_duration;
            if (normalized_time < transition._exit_time)
                return false;
        }
        for (const auto &condition : transition._conditions)
        {
            if (!CheckCondition(instance, condition))
                return false;
        }
        return true;
    }

    bool AnimationController::CheckCondition(const AnimationInstance &instance,
                                             const AnimationCondition &condition) const
    {
        if (condition._parameter_index >= _asset->Parameters().size())
            return false;
        const auto &parameter = _asset->Parameters()[condition._parameter_index];
        switch (parameter._type)
        {
        case EAnimationParameterType::kFloat:
        {
            const f32 value = instance._float_parameters[condition._parameter_index];
            switch (condition._op)
            {
            case EAnimationConditionOp::kEqual: return value == condition._float_value;
            case EAnimationConditionOp::kNotEqual: return value != condition._float_value;
            case EAnimationConditionOp::kGreater: return value > condition._float_value;
            case EAnimationConditionOp::kGreaterEqual: return value >= condition._float_value;
            case EAnimationConditionOp::kLess: return value < condition._float_value;
            case EAnimationConditionOp::kLessEqual: return value <= condition._float_value;
            default: return false;
            }
        }
        case EAnimationParameterType::kInt:
        {
            const i32 value = instance._int_parameters[condition._parameter_index];
            switch (condition._op)
            {
            case EAnimationConditionOp::kEqual: return value == condition._int_value;
            case EAnimationConditionOp::kNotEqual: return value != condition._int_value;
            case EAnimationConditionOp::kGreater: return value > condition._int_value;
            case EAnimationConditionOp::kGreaterEqual: return value >= condition._int_value;
            case EAnimationConditionOp::kLess: return value < condition._int_value;
            case EAnimationConditionOp::kLessEqual: return value <= condition._int_value;
            default: return false;
            }
        }
        case EAnimationParameterType::kBool:
        {
            const bool value = instance._bool_parameters[condition._parameter_index] != 0u;
            if (condition._op == EAnimationConditionOp::kEqual)
                return value == condition._bool_value;
            if (condition._op == EAnimationConditionOp::kNotEqual)
                return value != condition._bool_value;
            return false;
        }
        case EAnimationParameterType::kTrigger:
            return condition._op == EAnimationConditionOp::kTriggered &&
                   instance._triggers[condition._parameter_index] != 0u;
        default:
            return false;
        }
    }

    bool AnimationController::TryStartTransition(AnimationInstance &instance, u16 transition_index) const
    {
        if (transition_index >= _asset->Transitions().size())
            return false;
        const auto &transition = _asset->Transitions()[transition_index];
        if (!CheckTransition(instance, transition))
            return false;

        ConsumeTriggers(instance, transition);
        instance._next_state = transition._to_state;
        instance._next_state_time = 0.0f;
        instance._transition_time = 0.0f;
        instance._transition_duration = std::max(transition._duration, 0.0f);
        instance._in_transition = instance._transition_duration > 0.0f;
        if (!instance._in_transition)
        {
            instance._current_state = instance._next_state;
            instance._state_time = 0.0f;
            instance._next_state = kInvalidAnimationState;
        }
        return true;
    }

    void AnimationController::ConsumeTriggers(AnimationInstance &instance, const AnimationTransition &transition) const
    {
        for (const auto &condition : transition._conditions)
        {
            if (condition._parameter_index < _asset->Parameters().size() &&
                _asset->Parameters()[condition._parameter_index]._type == EAnimationParameterType::kTrigger &&
                condition._op == EAnimationConditionOp::kTriggered)
                instance.ResetTrigger(condition._parameter_index);
        }
    }

    void AnimationController::AddStateSample(const AnimationInstance &instance, u16 state_index, f32 weight,
                                             f32 state_time, f32 motion_duration,
                                             AnimationEvaluation &evaluation) const
    {
        const auto &state = _asset->States()[state_index];
        if (weight <= 0.0f)
            return;

        const f32 sample_time = state_time * state._speed * instance._speed;
        if (state._motion._type == EAnimationMotionType::kClip)
        {
            evaluation.AddSample(AnimationSample{state._motion._asset, sample_time, weight, state._loop});
            return;
        }

        const auto blend_space_iter = _blend_spaces.find(state._motion._asset);
        if (blend_space_iter == _blend_spaces.end() || blend_space_iter->second == nullptr)
            return;

        Vector2f position = Vector2f::kZero;
        u16 parameter_index = state._motion._parameter_index;
        u16 parameter_y_index = state._motion._parameter_y_index;
        for (u16 axis = 0u; axis < 2u; ++axis)
        {
            u16 &axis_index = axis == 0u ? parameter_index : parameter_y_index;
            if (axis_index != kInvalidAnimationParameter)
                continue;
            for (u16 index = 0u; index < _asset->Parameters().size(); ++index)
            {
                if (_asset->Parameters()[index]._type == EAnimationParameterType::kFloat &&
                    (axis == 0u || index != parameter_index))
                {
                    axis_index = index;
                    break;
                }
            }
        }
        if (parameter_index < instance._float_parameters.size())
            position.x = instance._float_parameters[parameter_index];
        if (parameter_y_index < instance._float_parameters.size())
            position.y = instance._float_parameters[parameter_y_index];
        const f32 phase = motion_duration > 0.0f ? sample_time / motion_duration : 0.0f;
        blend_space_iter->second->AddSamples(position, phase, weight, state._loop, evaluation, true);
    }
}
