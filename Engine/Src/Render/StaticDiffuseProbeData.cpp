#include "pch.h"
#include "Render/StaticDiffuseProbeData.h"

namespace Ailu::Render
{
    u32 ProbeGridDesc::ProbeCount() const
    {
        return _probe_count.x > 0 && _probe_count.y > 0 && _probe_count.z > 0
            ? static_cast<u32>(_probe_count.x * _probe_count.y * _probe_count.z) : 0u;
    }

    u32 ProbeGridDesc::FlattenIndex(i32 x, i32 y, i32 z) const
    {
        return static_cast<u32>(x + y * _probe_count.x + z * _probe_count.x * _probe_count.y);
    }
}
