/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : WinInputBackend – Windows platform backend for the Input Action System
 *
 * Creates and registers native devices (Keyboard, Mouse) with the InputSystem,
 * bridging the existing Ailu::Input polling to the new InputDevice interface.
 */

#pragma once
#ifndef __WIN_INPUT_BACKEND_H__
#define __WIN_INPUT_BACKEND_H__

#include "Input/InputSystem.h"

namespace Ailu
{
    class Window;

    class AILU_API WinInputBackend
    {
    public:
        WinInputBackend() = default;
        ~WinInputBackend() = default;

        /// @brief Create native devices and register them with the InputSystem.
        /// Call once during engine initialisation.
        void Initialize(InputSystem &input_system, Window *main_window = nullptr);

        /// @brief Unregister devices. Call during engine shutdown.
        void Shutdown(InputSystem &input_system);

        /// @brief Update the window handle (e.g. after window resize / move)
        void SetMainWindow(Window *window);

    private:
        Window *_main_window = nullptr;
        u32 _keyboard_device_id = 0;
        u32 _mouse_device_id = 0;
        bool _initialized = false;
    };

} // namespace Ailu

#endif // !__WIN_INPUT_BACKEND_H__
