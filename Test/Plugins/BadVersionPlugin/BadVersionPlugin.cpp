#include "Framework/Plugin/PluginApi.h"

// Deliberately exports no descriptor/Create/Destroy: the version gate must reject it first.
AILU_PLUGIN_EXPORT Ailu::u32 AILU_PLUGIN_CALL AiluPluginGetApiVersion() noexcept
{
    return Ailu::kPluginApiVersion + 1u;
}
