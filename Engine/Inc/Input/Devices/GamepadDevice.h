/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : GamepadDevice – XInput-based gamepad state polling (Phase 2)
 *
 * NOTE: This is a stub for Phase 2 implementation.
 * The control descriptor layout matches standard Xbox controller mapping.
 */

#pragma once
#ifndef __GAMEPAD_DEVICE_H__
#define __GAMEPAD_DEVICE_H__

#include "Input/InputDevice.h"
#include "Input/InputControl.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Math/Vector.hpp"

namespace Ailu
{
    // --- Gamepad control indices ---
    enum class EGamepadControl : u16
    {
        // Buttons
        kButtonSouth = 0,   // A
        kButtonEast,        // B
        kButtonWest,        // X
        kButtonNorth,       // Y
        kLeftShoulder,      // LB
        kRightShoulder,     // RB
        kLeftThumb,         // LS
        kRightThumb,        // RS
        kStart,
        kBack,
        kDpadUp,
        kDpadDown,
        kDpadLeft,
        kDpadRight,

        // Axes
        kLeftStickX,
        kLeftStickY,
        kLeftStick2D,       // Axis2D
        kRightStickX,
        kRightStickY,
        kRightStick2D,      // Axis2D
        kLeftTrigger,
        kRightTrigger,

        kControlCount
    };

    class GamepadDevice final : public InputDevice
    {
    public:
        explicit GamepadDevice(u32 player_index = 0);

        void Poll() override;
        InputValue ReadControl(u16 control_index) const override;

        EInputDeviceType GetDeviceType() const override { return EInputDeviceType::kGamepad; }
        bool IsConnected() const override { return _is_connected; }

        u32 GetPlayerIndex() const { return _player_index; }

    private:
        void BuildControlDescriptors();
        bool PollXInput();

    private:
        u32 _player_index = 0;
        bool _is_connected = false;

        // Per-frame state
        Array<bool, InputConstants::kMaxGamepadButtons> _current_buttons{};
        Array<bool, InputConstants::kMaxGamepadButtons> _previous_buttons{};
        Array<f32, InputConstants::kMaxGamepadAxes> _current_axes{};
        Array<f32, InputConstants::kMaxGamepadAxes> _previous_axes{};
    };

} // namespace Ailu

#endif // !__GAMEPAD_DEVICE_H__
