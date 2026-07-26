/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : InputBinding – maps a control path to processors and interactions
 *
 * Each binding represents one (or one part of a composite) physical control
 * bound to an InputAction. It resolves a string path to a device+control at
 * load time, and evaluates the value chain (Read → Processor → Interaction)
 * each frame.
 */

#pragma once
#ifndef __INPUT_BINDING_H__
#define __INPUT_BINDING_H__

#include "InputTypes.h"
#include "InputValue.h"
#include "InputControl.h"
#include "InputProcessor.h"
#include "InputInteraction.h"
#include "Framework/Core/String.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/Containers/Vector.h"

namespace Ailu
{
    class InputSystem;

    // ========================================================================
    //  InputBinding
    // ========================================================================
    struct AILU_API InputBinding
    {
        InputBinding() = default;
        InputBinding(const InputBinding &other);
        InputBinding(InputBinding &&other) noexcept = default;
        InputBinding &operator=(const InputBinding &other);
        InputBinding &operator=(InputBinding &&other) noexcept = default;

        String _name;                        // Display / debug name
        String _control_path;                // Original path string, e.g. "<Keyboard>/space"
        String _groups;                      // Control scheme group(s)

        Vector<Scope<InputProcessor>> _processors;     // Value transforms (in order)
        Vector<Scope<InputInteraction>> _interactions;  // Timing interpreters

        ResolvedInputControl _resolved_control;         // Resolved at load time

        // --- Composite support ---
        bool _is_composite = false;                    // This binding is a composite
        bool _is_part_of_composite = false;            // This binding is a child of a composite
        String _composite_part_name;                    // "up", "down", "left", "right", etc.

        // ====================================================================
        //  Evaluation
        // ====================================================================

        /// @brief Evaluate the value chain: Read control → run processors → return final value
        [[nodiscard]] InputValue Evaluate() const
        {
            if (!_is_composite && !_resolved_control.IsValid())
                return InputValue{};

            InputValue value = _resolved_control.Read();

            for (const auto &processor : _processors)
            {
                if (processor)
                    value = processor->Process(value);
            }

            return value;
        }

        /// @brief Run the interaction state machine; returns the highest-priority phase change
        InputInteractionResult EvaluateInteraction(const InputInteractionContext &context)
        {
            InputInteractionResult best_result = InputInteractionResult::None();

            for (auto &interaction : _interactions)
            {
                if (!interaction)
                    continue;
                InputInteractionResult result = interaction->Process(context);
                if (result._phase_changed)
                {
                    // Higher-priority phases take precedence
                    if (static_cast<u8>(result._phase) > static_cast<u8>(best_result._phase))
                        best_result = result;
                }
            }

            return best_result;
        }

        /// @brief Reset all interaction state machines
        void ResetInteractions()
        {
            for (auto &interaction : _interactions)
            {
                if (interaction)
                    interaction->Reset();
            }
        }

        /// @brief Whether this binding has any interactions attached
        [[nodiscard]] bool HasInteractions() const
        {
            return !_interactions.empty();
        }

        /// @brief Whether any control that feeds this binding has been consumed
        [[nodiscard]] bool IsConsumed() const { return _is_consumed; }
        void SetConsumed(bool consumed) { _is_consumed = consumed; }

    private:
        bool _is_consumed = false;
    };

} // namespace Ailu

#endif // !__INPUT_BINDING_H__
