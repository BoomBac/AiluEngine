/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : KeyboardDevice – polls the engine's Input system for key state
 *
 * This is a thin wrapper around Ailu::Input that presents keyboard state
 * through the InputDevice interface, making it compatible with the
 * Action/Binding pipeline.
 */

#pragma once
#ifndef __KEYBOARD_DEVICE_H__
#define __KEYBOARD_DEVICE_H__

#include "Input/InputDevice.h"
#include "Input/InputControl.h"
#include "Framework/Common/KeyCode.h"
#include "Framework/Core/Containers/Array.h"

namespace Ailu
{
    class KeyboardDevice final : public InputDevice
    {
    public:
        KeyboardDevice();

        void Poll() override;
        InputValue ReadControl(u16 control_index) const override;

        EInputDeviceType GetDeviceType() const override { return EInputDeviceType::kKeyboard; }
        bool IsConnected() const override { return true; } // Keyboard always connected on desktop

        /// @brief Map an EKey enum value to the device's control index
        [[nodiscard]] u16 KeyToControlIndex(EKey key) const;

        /// @brief The key name for a given control index
        [[nodiscard]] StringView ControlIndexToName(u16 control_index) const;

    private:
        void BuildControlDescriptors();

    private:
        // Per-frame state
        Array<bool, InputConstants::kMaxKeyboardKeys> _current_keys{};
        Array<bool, InputConstants::kMaxKeyboardKeys> _previous_keys{};

        // EKey → control index mapping (built once at construction)
        Array<u16, 512> _key_to_control_index{};
    };

} // namespace Ailu

#endif // !__KEYBOARD_DEVICE_H__
