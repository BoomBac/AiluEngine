#include "Input/InputControl.h"
#include "Input/InputDevice.h"

namespace Ailu
{
    InputValue ResolvedInputControl::Read() const
    {
        if (_device != nullptr)
            return _device->ReadControl(_control_index);
        return InputValue{};
    }

    u64 ResolvedInputControl::Hash() const
    {
        if (_device == nullptr)
            return 0u;
        return (static_cast<u64>(_device->GetDeviceId()) << 16) | static_cast<u64>(_control_index);
    }
} // namespace Ailu
