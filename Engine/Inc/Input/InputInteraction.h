/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : InputInteraction base and built-in interactions
 *
 * Interactions interpret the *timing* of input: was it a tap, a hold, a press?
 * They consume InputInteractionContext (input + time) and emit
 * InputInteractionResult (phase transition).
 *
 * Built-in: Press, Release, Hold, Tap
 */

#pragma once
#ifndef __INPUT_INTERACTION_H__
#define __INPUT_INTERACTION_H__

#include "InputTypes.h"
#include "InputValue.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/Types.h"

namespace Ailu
{
    // ========================================================================
    //  Context passed into Interaction::Process each frame
    // ========================================================================
    struct InputInteractionContext
    {
        InputValue _value;              // current (processed) value
        f64 _time = 0.0;                // absolute seconds since start
        f32 _delta_time = 0.0f;         // seconds since last frame
        bool _was_actuated = false;     // previous-frame actuation
        bool _is_actuated = false;      // current-frame actuation
    };

    // ========================================================================
    //  Result returned by Interaction::Process
    // ========================================================================
    struct InputInteractionResult
    {
        EInputActionPhase _phase = EInputActionPhase::kWaiting;
        bool _phase_changed = false;

        static InputInteractionResult None()
        {
            return {EInputActionPhase::kWaiting, false};
        }
    };

    // ========================================================================
    //  Base
    // ========================================================================
    class InputInteraction
    {
    public:
        virtual ~InputInteraction() = default;

        /// @brief Process one frame of input; return a phase transition if any
        virtual InputInteractionResult Process(const InputInteractionContext &context) = 0;

        /// @brief Reset internal state (on disable, context pop, device disconnect)
        virtual void Reset() = 0;

        virtual Scope<InputInteraction> Clone() const = 0;
    };

    // ========================================================================
    //  Built-in Interactions
    // ========================================================================

    /// @brief Fires Performed on the rising edge (not-pressed → pressed)
    class PressInteraction final : public InputInteraction
    {
    public:
        InputInteractionResult Process(const InputInteractionContext &context) override
        {
            if (!context._was_actuated && context._is_actuated)
                return {EInputActionPhase::kPerformed, true};
            return InputInteractionResult::None();
        }

        void Reset() override {}

        Scope<InputInteraction> Clone() const override { return MakeScope<PressInteraction>(); }
    };

    /// @brief Fires Performed on the falling edge (pressed → not-pressed)
    class ReleaseInteraction final : public InputInteraction
    {
    public:
        InputInteractionResult Process(const InputInteractionContext &context) override
        {
            if (context._was_actuated && !context._is_actuated)
                return {EInputActionPhase::kPerformed, true};
            return InputInteractionResult::None();
        }

        void Reset() override {}

        Scope<InputInteraction> Clone() const override { return MakeScope<ReleaseInteraction>(); }
    };

    /// @brief Fires Started on press, Performed after holding for duration, Canceled on early release
    class HoldInteraction final : public InputInteraction
    {
    public:
        explicit HoldInteraction(f32 duration = InputConstants::kDefaultHoldDuration)
            : _duration(duration) {}

        InputInteractionResult Process(const InputInteractionContext &context) override
        {
            // --- Start holding ---
            if (!_is_holding && !context._was_actuated && context._is_actuated)
            {
                _is_holding = true;
                _has_performed = false;
                _start_time = context._time;
                return {EInputActionPhase::kStarted, true};
            }

            // --- Held long enough → perform ---
            if (_is_holding && !_has_performed && context._is_actuated &&
                (context._time - _start_time) >= static_cast<f64>(_duration))
            {
                _has_performed = true;
                return {EInputActionPhase::kPerformed, true};
            }

            // --- Released ---
            if (_is_holding && !context._is_actuated)
            {
                const bool was_performed = _has_performed;
                Reset();
                if (!was_performed)
                    return {EInputActionPhase::kCanceled, true};
            }

            return InputInteractionResult::None();
        }

        void Reset() override
        {
            _start_time = 0.0;
            _is_holding = false;
            _has_performed = false;
        }

        Scope<InputInteraction> Clone() const override { return MakeScope<HoldInteraction>(_duration); }

        void SetDuration(f32 duration) { _duration = duration; }
        f32 GetDuration() const { return _duration; }

    private:
        f64 _start_time = 0.0;
        f32 _duration = InputConstants::kDefaultHoldDuration;
        bool _is_holding = false;
        bool _has_performed = false;
    };

    /// @brief Fires Started on press, Performed on release within max duration, Canceled if held too long
    class TapInteraction final : public InputInteraction
    {
    public:
        explicit TapInteraction(f32 max_duration = InputConstants::kDefaultTapMaxDuration)
            : _max_duration(max_duration) {}

        InputInteractionResult Process(const InputInteractionContext &context) override
        {
            // --- Started ---
            if (!_is_tapping && !context._was_actuated && context._is_actuated)
            {
                _is_tapping = true;
                _start_time = context._time;
                return {EInputActionPhase::kStarted, true};
            }

            // --- Still holding ---
            if (_is_tapping && context._is_actuated)
            {
                if ((context._time - _start_time) > static_cast<f64>(_max_duration))
                {
                    Reset();
                    return {EInputActionPhase::kCanceled, true};
                }
            }

            // --- Released within window → tap performed ---
            if (_is_tapping && !context._is_actuated)
            {
                const bool within_window = (context._time - _start_time) <= static_cast<f64>(_max_duration);
                Reset();
                if (within_window)
                    return {EInputActionPhase::kPerformed, true};
                return {EInputActionPhase::kCanceled, true};
            }

            return InputInteractionResult::None();
        }

        void Reset() override
        {
            _start_time = 0.0;
            _is_tapping = false;
        }

        Scope<InputInteraction> Clone() const override { return MakeScope<TapInteraction>(_max_duration); }

        void SetMaxDuration(f32 max_duration) { _max_duration = max_duration; }
        f32 GetMaxDuration() const { return _max_duration; }

    private:
        f64 _start_time = 0.0;
        f32 _max_duration = InputConstants::kDefaultTapMaxDuration;
        bool _is_tapping = false;
    };

} // namespace Ailu

#endif // !__INPUT_INTERACTION_H__
