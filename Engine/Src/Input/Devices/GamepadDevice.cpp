/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : GamepadDevice – Stub implementation for Phase 2
 *
 * When XInput/GDI are available, PollXInput() will be implemented to read
 * the XInput state. For now this stub returns neutral values.
 */

#include "Input/Devices/GamepadDevice.h"
#include "Framework/Common/Log.h"
#include <algorithm>

namespace Ailu
{
    GamepadDevice::GamepadDevice(u32 player_index)
        : _player_index(player_index)
    {
        _device_name = "Gamepad_" + std::to_string(player_index);
        _device_id = 0x4750 + player_index; // 'GP' prefix + index

        BuildControlDescriptors();
    }

    void GamepadDevice::BuildControlDescriptors()
    {
        _control_descs.clear();
        _control_descs.reserve(static_cast<size_t>(EGamepadControl::kControlCount));

        // --- Buttons ---
        const char *button_names[] = {
            "button_south",   // A
            "button_east",    // B
            "button_west",    // X
            "button_north",   // Y
            "left_shoulder",  // LB
            "right_shoulder", // RB
            "left_thumb",     // LS
            "right_thumb",    // RS
            "start",
            "back",
            "dpad_up",
            "dpad_down",
            "dpad_left",
            "dpad_right",
        };
        for (u16 i = 0; i < 14; ++i)
        {
            InputControlDesc desc;
            desc._name = button_names[i];
            desc._value_type = EInputValueType::kButton;
            desc._index = i;
            _control_descs.push_back(std::move(desc));
        }

        // --- Axes ---
        {
            InputControlDesc desc;
            desc._name = "left_stick_x";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EGamepadControl::kLeftStickX);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "left_stick_y";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EGamepadControl::kLeftStickY);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "left_stick";
            desc._value_type = EInputValueType::kAxis2D;
            desc._index = static_cast<u16>(EGamepadControl::kLeftStick2D);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "right_stick_x";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EGamepadControl::kRightStickX);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "right_stick_y";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EGamepadControl::kRightStickY);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "right_stick";
            desc._value_type = EInputValueType::kAxis2D;
            desc._index = static_cast<u16>(EGamepadControl::kRightStick2D);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "left_trigger";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EGamepadControl::kLeftTrigger);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "right_trigger";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EGamepadControl::kRightTrigger);
            _control_descs.push_back(std::move(desc));
        }
    }

    void GamepadDevice::Poll()
    {
        _previous_buttons = _current_buttons;
        _previous_axes = _current_axes;

        _is_connected = PollXInput();
    }

    bool GamepadDevice::PollXInput()
    {
        // Phase 2: Implement XInput polling via XInputGetState()
        // For now return neutral/default values and mark as disconnected
        for (auto &b : _current_buttons) b = false;
        for (auto &a : _current_axes) a = 0.0f;
        return false;
    }

    InputValue GamepadDevice::ReadControl(u16 control_index) const
    {
        InputValue value;

        switch (static_cast<EGamepadControl>(control_index))
        {
        // --- Buttons ---
        case EGamepadControl::kButtonSouth:
        case EGamepadControl::kButtonEast:
        case EGamepadControl::kButtonWest:
        case EGamepadControl::kButtonNorth:
        case EGamepadControl::kLeftShoulder:
        case EGamepadControl::kRightShoulder:
        case EGamepadControl::kLeftThumb:
        case EGamepadControl::kRightThumb:
        case EGamepadControl::kStart:
        case EGamepadControl::kBack:
        case EGamepadControl::kDpadUp:
        case EGamepadControl::kDpadDown:
        case EGamepadControl::kDpadLeft:
        case EGamepadControl::kDpadRight:
            value._type = EInputValueType::kButton;
            value._value.x = _current_buttons[control_index] ? 1.0f : 0.0f;
            break;

        // --- 1D Axes ---
        case EGamepadControl::kLeftStickX:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _current_axes[static_cast<u16>(EGamepadControl::kLeftStickX) - 14];
            break;
        case EGamepadControl::kLeftStickY:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _current_axes[static_cast<u16>(EGamepadControl::kLeftStickY) - 14];
            break;
        case EGamepadControl::kRightStickX:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _current_axes[static_cast<u16>(EGamepadControl::kRightStickX) - 14];
            break;
        case EGamepadControl::kRightStickY:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _current_axes[static_cast<u16>(EGamepadControl::kRightStickY) - 14];
            break;
        case EGamepadControl::kLeftTrigger:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _current_axes[static_cast<u16>(EGamepadControl::kLeftTrigger) - 14];
            break;
        case EGamepadControl::kRightTrigger:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _current_axes[static_cast<u16>(EGamepadControl::kRightTrigger) - 14];
            break;

        // --- 2D Axes ---
        case EGamepadControl::kLeftStick2D:
            value._type = EInputValueType::kAxis2D;
            value._value = {
                _current_axes[static_cast<u16>(EGamepadControl::kLeftStickX) - 14],
                _current_axes[static_cast<u16>(EGamepadControl::kLeftStickY) - 14],
                0.0f};
            break;
        case EGamepadControl::kRightStick2D:
            value._type = EInputValueType::kAxis2D;
            value._value = {
                _current_axes[static_cast<u16>(EGamepadControl::kRightStickX) - 14],
                _current_axes[static_cast<u16>(EGamepadControl::kRightStickY) - 14],
                0.0f};
            break;

        default:
            break;
        }

        return value;
    }

} // namespace Ailu
