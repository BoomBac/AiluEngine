#include "Animation/AnimationControllerAsset.h"

namespace Ailu
{
    AnimationControllerAsset::AnimationControllerAsset() : Object("AnimationControllerAsset")
    {
    }

    AnimationControllerAsset::AnimationControllerAsset(const String &name) : Object(name)
    {
    }

    AnimationParameterId AnimationControllerAsset::AddParameter(String name, EAnimationParameterType type)
    {
        const AnimationParameterId id = static_cast<AnimationParameterId>(_parameters.size());
        _parameters.push_back(AnimationParameterDesc{std::move(name), 0u, type});
        _parameters.back()._name_hash = AnimationParameterNameHash(_parameters.back()._name);
        return id;
    }

    AnimationParameterId AnimationControllerAsset::GetParameterId(StringView name) const
    {
        const u32 name_hash = AnimationParameterNameHash(name);
        for (u16 index = 0; index < _parameters.size(); ++index)
        {
            const auto &parameter = _parameters[index];
            if (parameter._name_hash == name_hash && parameter._name == name)
                return index;
        }
        return kInvalidAnimationParameter;
    }

    u16 AnimationControllerAsset::AddState(AnimationState state)
    {
        const u16 index = static_cast<u16>(_states.size());
        _states.push_back(std::move(state));
        if (_entry_state == kInvalidAnimationState)
            _entry_state = index;
        return index;
    }

    u16 AnimationControllerAsset::AddTransition(AnimationTransition transition)
    {
        const u16 index = static_cast<u16>(_transitions.size());
        _transitions.push_back(std::move(transition));
        if (_transitions.back()._from_state == kInvalidAnimationState)
            _any_state_transitions.push_back(index);
        else if (_transitions.back()._from_state < _states.size())
            _states[_transitions.back()._from_state]._transitions.push_back(index);
        return index;
    }

}
