/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : InputAction – a named, semantic input (Move, Jump, Fire, …)
 *
 * An InputAction holds one or more InputBindings, aggregates their values,
 * drives Interaction state machines, and exposes per-frame query helpers.
 *
 * Gameplay code interacts with InputAction, never with raw devices.
 */

#pragma once
#ifndef __INPUT_ACTION_H__
#define __INPUT_ACTION_H__

#include "InputTypes.h"
#include "InputValue.h"
#include "InputBinding.h"
#include "InputComposite.h"
#include "Framework/Core/String.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/Containers/Vector.h"
#include <functional>

namespace Ailu
{
    // ========================================================================
    //  InputActionEvent – dispatched when an action phase changes
    // ========================================================================
    struct AILU_API InputActionEvent
    {
        class InputAction *_action = nullptr;
        InputValue _value;
        EInputActionPhase _phase = EInputActionPhase::kWaiting;
        f64 _time = 0.0;
    };

    // ========================================================================
    //  InputActionUpdateContext – per-frame context passed into Update()
    // ========================================================================
    struct AILU_API InputActionUpdateContext
    {
        f64 _time = 0.0;
        f32 _delta_time = 0.0f;
        f32 _actuation_threshold = InputConstants::kDefaultActuationThreshold;
    };

    // ========================================================================
    //  InputAction
    // ========================================================================
    class AILU_API InputAction
    {
    public:
        using EventCallback = std::function<void(const InputActionEvent &)>;

        InputAction() = default;
        explicit InputAction(const String &name) : _name(name) {}
        InputAction(const InputAction &other);
        InputAction(InputAction &&other) noexcept = default;
        InputAction &operator=(const InputAction &other);
        InputAction &operator=(InputAction &&other) noexcept = default;

        // --- Lifecycle ---
        void Enable();
        void Disable();
        void Update(const InputActionUpdateContext &context);

        // --- Queries ---
        [[nodiscard]] const String &GetName() const { return _name; }
        void SetName(const String &name) { _name = name; }

        [[nodiscard]] u32 GetId() const { return _id; }
        void SetId(u32 id) { _id = id; }

        [[nodiscard]] EInputActionType GetActionType() const { return _action_type; }
        void SetActionType(EInputActionType type) { _action_type = type; }

        [[nodiscard]] EInputValueType GetValueType() const { return _value_type; }
        void SetValueType(EInputValueType type) { _value_type = type; }

        [[nodiscard]] EInputActionPhase GetPhase() const { return _phase; }
        [[nodiscard]] bool IsEnabled() const { return _enabled; }

        [[nodiscard]] const InputValue &GetValue() const { return _current_value; }
        [[nodiscard]] const InputValue &GetPreviousValue() const { return _previous_value; }

        // --- Per-frame edge queries (valid for one frame only) ---
        [[nodiscard]] bool WasStartedThisFrame() const { return _started_this_frame; }
        [[nodiscard]] bool WasPerformedThisFrame() const { return _performed_this_frame; }
        [[nodiscard]] bool WasCanceledThisFrame() const { return _canceled_this_frame; }
        [[nodiscard]] bool ValueChangedThisFrame() const
        {
            return _current_value._value.x != _previous_value._value.x ||
                   _current_value._value.y != _previous_value._value.y ||
                   _current_value._value.z != _previous_value._value.z;
        }

        // --- Bindings ---
        [[nodiscard]] Vector<InputBinding> &GetBindings() { return _bindings; }
        [[nodiscard]] const Vector<InputBinding> &GetBindings() const { return _bindings; }

        void AddBinding(const InputBinding &binding) { _bindings.push_back(binding); }
        void AddBinding(InputBinding &&binding) { _bindings.push_back(std::move(binding)); }

        // --- Composite ---
        [[nodiscard]] Scope<InputCompositeBinding> &GetComposite() { return _composite; }
        [[nodiscard]] const Scope<InputCompositeBinding> &GetComposite() const { return _composite; }

        void SetComposite(Scope<InputCompositeBinding> composite)
        {
            _composite = std::move(composite);
        }
        [[nodiscard]] bool IsComposite() const { return _composite != nullptr; }

        // --- Event callbacks ---
        void AddListener(const EventCallback &callback) { _listeners.push_back(callback); }
        void ClearListeners() { _listeners.clear(); }

        // --- Default interaction fallback ---
        void SetDefaultInteraction(Scope<InputInteraction> interaction)
        {
            _default_interaction = std::move(interaction);
        }

        // --- Binding merge strategy ---
        void SetMergeStrategy(EBindingMergeStrategy strategy) { _merge_strategy = strategy; }
        [[nodiscard]] EBindingMergeStrategy GetMergeStrategy() const { return _merge_strategy; }

    private:
        void ResetFrameState();
        void ApplyInteractionResult(const InputInteractionResult &result, f64 time);
        InputValue EvaluateBindings() const;
        void ProcessInteractions(const InputActionUpdateContext &context,
                                 bool was_actuated, bool is_actuated,
                                 const InputValue &current_value);

    private:
        String _name;
        u32 _id = 0;

        EInputActionType _action_type = EInputActionType::kButton;
        EInputValueType _value_type = EInputValueType::kButton;
        EInputActionPhase _phase = EInputActionPhase::kDisabled;

        Vector<InputBinding> _bindings;
        Scope<InputCompositeBinding> _composite;
        Scope<InputInteraction> _default_interaction;       // Used when a binding has no interactions

        EBindingMergeStrategy _merge_strategy = EBindingMergeStrategy::kMaxMagnitude;

        InputValue _current_value;
        InputValue _previous_value;

        bool _enabled = false;
        bool _started_this_frame = false;
        bool _performed_this_frame = false;
        bool _canceled_this_frame = false;

        Vector<EventCallback> _listeners;
    };

} // namespace Ailu

#endif // !__INPUT_ACTION_H__
