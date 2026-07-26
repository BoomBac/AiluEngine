/*
 * @author    : BoomBac
 * @created   : 2026.7
 */

#include "Input/InputAction.h"
#include "Framework/Common/Log.h"
#include <algorithm>

namespace Ailu
{
    InputAction::InputAction(const InputAction &other)
        : _name(other._name),
          _id(other._id),
          _action_type(other._action_type),
          _value_type(other._value_type),
          _phase(other._phase),
          _bindings(other._bindings),
          _merge_strategy(other._merge_strategy),
          _current_value(other._current_value),
          _previous_value(other._previous_value),
          _enabled(other._enabled),
          _started_this_frame(other._started_this_frame),
          _performed_this_frame(other._performed_this_frame),
          _canceled_this_frame(other._canceled_this_frame),
          _listeners(other._listeners)
    {
        if (other._composite)
            _composite = other._composite->Clone();
        if (other._default_interaction)
            _default_interaction = other._default_interaction->Clone();
    }

    InputAction &InputAction::operator=(const InputAction &other)
    {
        if (this == &other)
            return *this;

        _name = other._name;
        _id = other._id;
        _action_type = other._action_type;
        _value_type = other._value_type;
        _phase = other._phase;
        _bindings = other._bindings;
        _composite = other._composite ? other._composite->Clone() : nullptr;
        _default_interaction = other._default_interaction ? other._default_interaction->Clone() : nullptr;
        _merge_strategy = other._merge_strategy;
        _current_value = other._current_value;
        _previous_value = other._previous_value;
        _enabled = other._enabled;
        _started_this_frame = other._started_this_frame;
        _performed_this_frame = other._performed_this_frame;
        _canceled_this_frame = other._canceled_this_frame;
        _listeners = other._listeners;
        return *this;
    }

    // ========================================================================
    //  Lifecycle
    // ========================================================================
    void InputAction::Enable()
    {
        if (_enabled)
            return;

        _enabled = true;
        _phase = EInputActionPhase::kWaiting;
    }

    void InputAction::Disable()
    {
        if (!_enabled)
            return;

        // If currently in a started state, emit Canceled before disabling
        if (_phase == EInputActionPhase::kStarted)
        {
            InputActionEvent event;
            event._action = this;
            event._value = _current_value;
            event._phase = EInputActionPhase::kCanceled;
            event._time = 0.0; // will be filled by InputSystem
            for (auto &listener : _listeners)
                listener(event);
        }

        _enabled = false;
        _phase = EInputActionPhase::kDisabled;
        _current_value = InputValue{};
        _previous_value = InputValue{};
        ResetFrameState();

        // Reset all binding interactions
        for (auto &binding : _bindings)
            binding.ResetInteractions();

        if (_default_interaction)
            _default_interaction->Reset();

        if (_composite)
        {
            for (auto *child : _composite->GetChildBindings())
                child->ResetInteractions();
        }
    }

    // ========================================================================
    //  Per-frame update
    // ========================================================================
    void InputAction::Update(const InputActionUpdateContext &context)
    {
        ResetFrameState();

        if (!_enabled)
            return;

        _previous_value = _current_value;
        _current_value = EvaluateBindings();

        const bool was_actuated = _previous_value.IsActuated(context._actuation_threshold);
        const bool is_actuated = _current_value.IsActuated(context._actuation_threshold);

        // --- Pass-through actions just update value and fire events per-binding ---
        if (_action_type == EInputActionType::kPassThrough)
        {
            _phase = EInputActionPhase::kWaiting;
            if (is_actuated)
                _performed_this_frame = true;
            return;
        }

        // --- Button action default: no interaction → implicit Press ---
        if (_action_type == EInputActionType::kButton)
        {
            ProcessInteractions(context, was_actuated, is_actuated, _current_value);

            // If no interaction handled it and we have no interactions configured,
            // use the default Press-like behavior directly
            bool has_any_interactions = _default_interaction != nullptr;
            for (const auto &binding : _bindings)
            {
                if (binding.HasInteractions())
                {
                    has_any_interactions = true;
                    break;
                }
            }

            if (!has_any_interactions)
            {
                // Default: rising edge → Performed
                if (!was_actuated && is_actuated)
                {
                    _phase = EInputActionPhase::kPerformed;
                    _performed_this_frame = true;
                    InputActionEvent event;
                    event._action = this;
                    event._value = _current_value;
                    event._phase = EInputActionPhase::kPerformed;
                    event._time = context._time;
                    for (auto &listener : _listeners)
                        listener(event);
                }
                else
                {
                    _phase = EInputActionPhase::kWaiting;
                }
            }
        }
        else
        {
            // Value action: continuous update
            _phase = EInputActionPhase::kWaiting;
            if (is_actuated)
                _performed_this_frame = _current_value.Magnitude() >= context._actuation_threshold;
        }
    }

    // ========================================================================
    //  Binding evaluation
    // ========================================================================
    InputValue InputAction::EvaluateBindings() const
    {
        // --- Composite takes precedence ---
        if (_composite)
        {
            return _composite->Evaluate();
        }

        if (_bindings.empty())
            return InputValue{};

        // --- Single binding: straight pass-through ---
        if (_bindings.size() == 1)
        {
            InputValue val = _bindings[0].Evaluate();
            // Apply default interaction if present
            return val;
        }

        // --- Multiple bindings: merge according to strategy ---
        switch (_merge_strategy)
        {
        case EBindingMergeStrategy::kMaxMagnitude:
        {
            InputValue best;
            f32 best_mag = 0.0f;
            for (const auto &binding : _bindings)
            {
                InputValue val = binding.Evaluate();
                if (binding.IsConsumed())
                    continue;
                f32 mag = val.Magnitude();
                if (mag > best_mag)
                {
                    best_mag = mag;
                    best = val;
                }
            }
            return best;
        }

        case EBindingMergeStrategy::kAccumulate:
        {
            InputValue accumulated;
            accumulated._type = _value_type;
            for (const auto &binding : _bindings)
            {
                if (binding.IsConsumed())
                    continue;
                InputValue val = binding.Evaluate();
                accumulated._value.x += val._value.x;
                accumulated._value.y += val._value.y;
                accumulated._value.z += val._value.z;
            }
            // Clamp to [-1, 1]
            accumulated._value.x = std::clamp(accumulated._value.x, -1.0f, 1.0f);
            accumulated._value.y = std::clamp(accumulated._value.y, -1.0f, 1.0f);
            accumulated._value.z = std::clamp(accumulated._value.z, -1.0f, 1.0f);
            return accumulated;
        }

        case EBindingMergeStrategy::kLastActiveDevice:
        case EBindingMergeStrategy::kPassThrough:
        default:
        {
            // Return first valid binding
            for (const auto &binding : _bindings)
            {
                if (!binding.IsConsumed())
                    return binding.Evaluate();
            }
            return InputValue{};
        }
        }
    }

    void InputAction::ProcessInteractions(const InputActionUpdateContext &context,
                                          bool was_actuated, bool is_actuated,
                                          const InputValue &current_value)
    {
        InputInteractionContext ictx;
        ictx._value = current_value;
        ictx._time = context._time;
        ictx._delta_time = context._delta_time;
        ictx._was_actuated = was_actuated;
        ictx._is_actuated = is_actuated;

        InputInteractionResult best_result = InputInteractionResult::None();

        // Check bindings for interactions
        for (auto &binding : _bindings)
        {
            if (binding.IsConsumed())
                continue;
            auto result = binding.EvaluateInteraction(ictx);
            if (result._phase_changed &&
                static_cast<u8>(result._phase) > static_cast<u8>(best_result._phase))
            {
                best_result = result;
            }
        }

        // Fallback to default interaction on the action itself
        if (!best_result._phase_changed && _default_interaction)
        {
            best_result = _default_interaction->Process(ictx);
        }

        if (best_result._phase_changed)
        {
            ApplyInteractionResult(best_result, context._time);
        }
    }

    void InputAction::ApplyInteractionResult(const InputInteractionResult &result, f64 time)
    {
        _phase = result._phase;

        switch (result._phase)
        {
        case EInputActionPhase::kStarted:
            _started_this_frame = true;
            break;
        case EInputActionPhase::kPerformed:
            _performed_this_frame = true;
            break;
        case EInputActionPhase::kCanceled:
            _canceled_this_frame = true;
            break;
        default:
            break;
        }

        // Dispatch to listeners
        if (result._phase_changed)
        {
            InputActionEvent event;
            event._action = this;
            event._value = _current_value;
            event._phase = result._phase;
            event._time = time;
            for (auto &listener : _listeners)
                listener(event);
        }
    }

    void InputAction::ResetFrameState()
    {
        _started_this_frame = false;
        _performed_this_frame = false;
        _canceled_this_frame = false;
    }

} // namespace Ailu
