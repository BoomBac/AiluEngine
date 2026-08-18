#pragma once
#ifndef __ANIMATION_CONTROLLER_ASSET_H__
#define __ANIMATION_CONTROLLER_ASSET_H__

#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Math/Guid.h"
#include "Objects/Object.h"
#include "generated/AnimationControllerAsset.gen.h"

namespace Ailu
{
    inline constexpr u16 kInvalidAnimationState = std::numeric_limits<u16>::max();
    inline constexpr u16 kInvalidAnimationParameter = std::numeric_limits<u16>::max();

    using AnimationParameterId = u16;

    inline u32 AnimationParameterNameHash(StringView name)
    {
        u32 hash = 2166136261u;
        for (const char ch : name)
        {
            hash ^= static_cast<u8>(ch);
            hash *= 16777619u;
        }
        return hash;
    }

    AENUM()
    enum class EAnimationParameterType : u8
    {
        kFloat,
        kInt,
        kBool,
        kTrigger
    };

    AENUM()
    enum class EAnimationMotionType : u8
    {
        kClip,
        kBlendSpace
    };

    AENUM()
    enum class EAnimationConditionOp : u8
    {
        kEqual,
        kNotEqual,
        kGreater,
        kGreaterEqual,
        kLess,
        kLessEqual,
        kTriggered
    };

    ASTRUCT()
    struct AILU_API AnimationParameterDesc
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        u32 _name_hash = 0u;
        APROPERTY()
        EAnimationParameterType _type = EAnimationParameterType::kFloat;
    };

    ASTRUCT()
    struct AILU_API AnimationMotion
    {
        GENERATED_BODY()

        APROPERTY()
        EAnimationMotionType _type = EAnimationMotionType::kClip;
        APROPERTY()
        Guid _asset = Guid::EmptyGuid();
    };

    ASTRUCT()
    struct AILU_API AnimationState
    {
        GENERATED_BODY()

        APROPERTY()
        String _name;
        APROPERTY()
        AnimationMotion _motion;
        APROPERTY()
        f32 _speed = 1.0f;
        APROPERTY()
        bool _loop = true;
        APROPERTY()
        Vector<u16> _transitions;
    };

    ASTRUCT()
    struct AILU_API AnimationCondition
    {
        GENERATED_BODY()

        APROPERTY()
        u16 _parameter_index = 0u;
        APROPERTY()
        EAnimationConditionOp _op = EAnimationConditionOp::kEqual;
        APROPERTY()
        f32 _float_value = 0.0f;
        APROPERTY()
        i32 _int_value = 0;
        APROPERTY()
        bool _bool_value = false;
    };

    ASTRUCT()
    struct AILU_API AnimationTransition
    {
        GENERATED_BODY()

        APROPERTY()
        u16 _from_state = kInvalidAnimationState;
        APROPERTY()
        u16 _to_state = kInvalidAnimationState;
        APROPERTY()
        Vector<AnimationCondition> _conditions;
        APROPERTY()
        f32 _duration = 0.15f;
        APROPERTY()
        bool _has_exit_time = false;
        APROPERTY()
        f32 _exit_time = 1.0f;
    };

    ACLASS()
    class AILU_API AnimationControllerAsset : public Object
    {
        GENERATED_BODY()

    public:
        AnimationControllerAsset();
        explicit AnimationControllerAsset(const String &name);

        const Vector<AnimationParameterDesc> &Parameters() const { return _parameters; }
        Vector<AnimationParameterDesc> &Parameters() { return _parameters; }
        const Vector<AnimationState> &States() const { return _states; }
        Vector<AnimationState> &States() { return _states; }
        const Vector<AnimationTransition> &Transitions() const { return _transitions; }
        Vector<AnimationTransition> &Transitions() { return _transitions; }
        const Vector<u16> &AnyStateTransitions() const { return _any_state_transitions; }
        Vector<u16> &AnyStateTransitions() { return _any_state_transitions; }

        u16 EntryState() const { return _entry_state; }
        void EntryState(u16 state) { _entry_state = state; }

        AnimationParameterId AddParameter(String name, EAnimationParameterType type);
        AnimationParameterId GetParameterId(StringView name) const;
        u16 AddState(AnimationState state);
        u16 AddTransition(AnimationTransition transition);

    private:
        APROPERTY()
        Vector<AnimationParameterDesc> _parameters;
        APROPERTY()
        Vector<AnimationState> _states;
        APROPERTY()
        Vector<AnimationTransition> _transitions;
        APROPERTY()
        Vector<u16> _any_state_transitions;
        APROPERTY()
        u16 _entry_state = kInvalidAnimationState;
    };
}

#endif // __ANIMATION_CONTROLLER_ASSET_H__
