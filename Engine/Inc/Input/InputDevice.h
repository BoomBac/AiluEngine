/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : Abstract base for all input devices (Keyboard, Mouse, Gamepad, …)
 *
 * Concrete devices live under Devices/ and are created by the platform backend.
 */

#pragma once
#ifndef __INPUT_DEVICE_H__
#define __INPUT_DEVICE_H__

#include "InputTypes.h"
#include "InputValue.h"
#include "InputControl.h"
#include "Framework/Common/Assert.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"

namespace Ailu
{
    class InputDevice
    {
    public:
        virtual ~InputDevice() = default;

        /// @brief Poll hardware and update internal state for the current frame
        virtual void Poll() = 0;

        /// @brief Read the value of a control by its device-specific index
        virtual InputValue ReadControl(u16 control_index) const = 0;

        /// @brief Type identifier for path resolution and matching
        virtual EInputDeviceType GetDeviceType() const = 0;

        /// @brief Whether the device is currently connected / available
        virtual bool IsConnected() const = 0;

        // --- Identification ---
        u32 GetDeviceId() const { return _device_id; }
        const String &GetDeviceName() const { return _device_name; }

        // --- Control descriptor lookup ---
        /// @brief Retrieve the control descriptor at the given index
        const InputControlDesc &GetControlDesc(u16 index) const
        {
            AL_ASSERT(index < _control_descs.size());
            return _control_descs[index];
        }

        /// @brief Number of controls this device exposes
        u16 GetControlCount() const { return static_cast<u16>(_control_descs.size()); }

        /// @brief Find a control index by name; returns kInvalidControlIndex if not found
        u16 FindControlIndex(StringView name) const
        {
            for (u16 i = 0; i < static_cast<u16>(_control_descs.size()); ++i)
            {
                if (_control_descs[i]._name == name)
                    return i;
            }
            return InputConstants::kInvalidControlIndex;
        }

    protected:
        u32 _device_id = 0;
        String _device_name;
        Vector<InputControlDesc> _control_descs;
    };

} // namespace Ailu

#endif // !__INPUT_DEVICE_H__
