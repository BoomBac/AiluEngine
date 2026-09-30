#pragma once
#include "Framework/Core/String.h"
#include "Framework/Core/Types.h"

namespace Ailu
{
    // Engine-issued, process-unique per load attempt. Zero is invalid; never reused on reload.
    struct PluginToken
    {
        u64 _value = 0;
        bool operator==(const PluginToken&) const = default;
    };

    struct PluginRegistrationHandle
    {
        PluginToken _owner;
        u64 _value = 0;
        bool operator==(const PluginRegistrationHandle&) const = default;
    };

    enum class EPluginRegistrationType : u8
    {
        kHostRecord,
    };

    // The initial scope owns host records only. No callback or plugin-defined cleanup target is stored.
    // Future services add typed Engine-owned detach logic before exposing menus/panels/callbacks.
    struct PluginRegistrationRecord
    {
        PluginToken _owner;
        PluginRegistrationHandle _handle;
        EPluginRegistrationType _type = EPluginRegistrationType::kHostRecord;
        String _name;
    };
}
