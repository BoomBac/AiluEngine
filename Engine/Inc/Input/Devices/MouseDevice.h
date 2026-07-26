/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : MouseDevice – polls the engine's Input system for mouse state
 */

#pragma once
#ifndef __MOUSE_DEVICE_H__
#define __MOUSE_DEVICE_H__

#include "Input/InputDevice.h"
#include "Input/InputControl.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Math/Vector.hpp"

namespace Ailu
{
    class Window;

    // --- Mouse control indices ---
    enum class EMouseControl : u16
    {
        kLeftButton = 0,
        kRightButton,
        kMiddleButton,
        kXButton1,
        kXButton2,
        kPositionX,      // Axis1D : X position
        kPositionY,      // Axis1D : Y position
        kPosition2D,     // Axis2D : combined (position_x, position_y)
        kDeltaX,         // Axis1D : X delta
        kDeltaY,         // Axis1D : Y delta
        kDelta2D,        // Axis2D : combined (delta_x, delta_y)
        kWheel,          // Axis1D : scroll wheel delta
        kControlCount
    };

    class MouseDevice final : public InputDevice
    {
    public:
        MouseDevice();

        /// @brief Set the window used for client-area mouse coordinates
        void SetWindow(Window *window) { _window = window; }

        void Poll() override;
        InputValue ReadControl(u16 control_index) const override;

        EInputDeviceType GetDeviceType() const override { return EInputDeviceType::kMouse; }
        bool IsConnected() const override { return true; }

    private:
        void BuildControlDescriptors();

    private:
        Window *_window = nullptr;

        // Per-frame state
        Array<bool, InputConstants::kMaxMouseButtons> _current_buttons{};
        Array<bool, InputConstants::kMaxMouseButtons> _previous_buttons{};

        Vector2f _current_position = Vector2f::kZero;
        Vector2f _previous_position = Vector2f::kZero;
        Vector2f _delta = Vector2f::kZero;
        f32 _wheel = 0.0f;
    };

} // namespace Ailu

#endif // !__MOUSE_DEVICE_H__
