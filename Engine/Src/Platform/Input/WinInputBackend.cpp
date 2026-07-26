/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : WinInputBackend – Creates Windows keyboard/mouse devices
 */

#include "Platform/WinInputBackend.h"
#include "Input/Devices/KeyboardDevice.h"
#include "Input/Devices/MouseDevice.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Window.h"

namespace Ailu
{
    void WinInputBackend::Initialize(InputSystem &input_system, Window *main_window)
    {
        if (_initialized)
            return;

        _main_window = main_window;

        // Create keyboard device
        auto keyboard = MakeScope<KeyboardDevice>();
        _keyboard_device_id = keyboard->GetDeviceId();
        input_system.RegisterDevice(std::move(keyboard));
        LOG_INFO("WinInputBackend: Registered Keyboard device (id={})", _keyboard_device_id);

        // Create mouse device
        auto mouse = MakeScope<MouseDevice>();
        mouse->SetWindow(_main_window);
        _mouse_device_id = mouse->GetDeviceId();
        input_system.RegisterDevice(std::move(mouse));
        LOG_INFO("WinInputBackend: Registered Mouse device (id={})", _mouse_device_id);

        _initialized = true;
        LOG_INFO("WinInputBackend: Initialized successfully");
    }

    void WinInputBackend::Shutdown(InputSystem &input_system)
    {
        if (!_initialized)
            return;

        input_system.RemoveDevice(_keyboard_device_id);
        input_system.RemoveDevice(_mouse_device_id);

        _initialized = false;
        LOG_INFO("WinInputBackend: Shutdown complete");
    }

    void WinInputBackend::SetMainWindow(Window *window)
    {
        _main_window = window;
        // The mouse device needs to know about the window for client-area coordinates
        // If we need to update an existing device, we'd do it here
    }

} // namespace Ailu
