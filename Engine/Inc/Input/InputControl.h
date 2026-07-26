/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : InputControl descriptor and runtime-resolved reference
 *
 * Design:
 *   InputControlDesc  – pure-data descriptor used during loading / editing
 *   ResolvedInputControl – runtime pointer + index into a live device
 *
 * Paths follow the format:  <DeviceType>/control_name
 *   e.g.  "<Keyboard>/space", "<Mouse>/left_button", "<Gamepad>/left_stick"
 */

#pragma once
#ifndef __INPUT_CONTROL_H__
#define __INPUT_CONTROL_H__

#include "InputTypes.h"
#include "InputValue.h"
#include "Framework/Core/String.h"

namespace Ailu
{
    class InputDevice;

    // --- Descriptor: loaded from asset, used to resolve at runtime ---
    struct InputControlDesc
    {
        String _name;                           // e.g. "space", "left_stick"
        EInputValueType _value_type = EInputValueType::kButton;
        u16 _index = InputConstants::kInvalidControlIndex; // device-specific control index
    };

    // --- Resolved reference: holds device pointer + index, read every frame ---
    struct ResolvedInputControl
    {
        InputDevice *_device = nullptr;
        u16 _control_index = InputConstants::kInvalidControlIndex;
        EInputValueType _value_type = EInputValueType::kButton;

        [[nodiscard]] bool IsValid() const
        {
            return _device != nullptr && _control_index != InputConstants::kInvalidControlIndex;
        }

        /// @brief Read the current frame value from the resolved device
        [[nodiscard]] InputValue Read() const;

        /// @brief Unique hash for consumption-tracking (device id + control index)
        [[nodiscard]] u64 Hash() const;

        void Reset()
        {
            _device = nullptr;
            _control_index = InputConstants::kInvalidControlIndex;
            _value_type = EInputValueType::kButton;
        }
    };

} // namespace Ailu

#endif // !__INPUT_CONTROL_H__
