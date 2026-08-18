#include "Animation/AnimationInstance.h"

namespace Ailu
{
    void AnimationInstance::Initialize(const AnimationControllerAsset &controller)
    {
        _current_state = controller.EntryState();
        _next_state = kInvalidAnimationState;
        _state_time = 0.0f;
        _next_state_time = 0.0f;
        _transition_time = 0.0f;
        _transition_duration = 0.0f;
        _current_motion_duration = 0.0f;
        _next_motion_duration = 0.0f;
        _float_parameters.assign(controller.Parameters().size(), 0.0f);
        _int_parameters.assign(controller.Parameters().size(), 0);
        _bool_parameters.assign(controller.Parameters().size(), 0u);
        _triggers.assign(controller.Parameters().size(), 0u);
        _previous_state = _current_state;
        _previous_next_state = kInvalidAnimationState;
        _previous_state_time = 0.0f;
        _previous_next_state_time = 0.0f;
        _in_transition = false;
        _initialized = true;
    }

    void AnimationInstance::SetFloat(AnimationParameterId id, f32 value)
    {
        if (id < _float_parameters.size())
            _float_parameters[id] = value;
    }

    void AnimationInstance::SetInt(AnimationParameterId id, i32 value)
    {
        if (id < _int_parameters.size())
            _int_parameters[id] = value;
    }

    void AnimationInstance::SetBool(AnimationParameterId id, bool value)
    {
        if (id < _bool_parameters.size())
            _bool_parameters[id] = value ? 1u : 0u;
    }

    void AnimationInstance::SetTrigger(AnimationParameterId id)
    {
        if (id < _triggers.size())
            _triggers[id] = 1u;
    }

    void AnimationInstance::ResetTrigger(AnimationParameterId id)
    {
        if (id < _triggers.size())
            _triggers[id] = 0u;
    }

    AnimationInstanceHandle AnimationInstancePool::Create(const AnimationControllerAsset &controller)
    {
        u32 index = 0u;
        if (_free_indices.empty())
        {
            index = static_cast<u32>(_instances.size());
            _instances.emplace_back();
        }
        else
        {
            index = _free_indices.back();
            _free_indices.pop_back();
        }

        _instances[index].Initialize(controller);
        return index + 1u;
    }

    void AnimationInstancePool::Destroy(AnimationInstanceHandle handle)
    {
        if (handle == kInvalidAnimationInstanceHandle)
            return;
        const u32 index = handle - 1u;
        if (index >= _instances.size() || !_instances[index]._initialized)
            return;
        _instances[index] = AnimationInstance{};
        _free_indices.push_back(index);
    }

    AnimationInstance *AnimationInstancePool::Get(AnimationInstanceHandle handle)
    {
        if (handle == kInvalidAnimationInstanceHandle)
            return nullptr;
        const u32 index = handle - 1u;
        return index < _instances.size() && _instances[index]._initialized ? &_instances[index] : nullptr;
    }

    const AnimationInstance *AnimationInstancePool::Get(AnimationInstanceHandle handle) const
    {
        if (handle == kInvalidAnimationInstanceHandle)
            return nullptr;
        const u32 index = handle - 1u;
        return index < _instances.size() && _instances[index]._initialized ? &_instances[index] : nullptr;
    }
}
