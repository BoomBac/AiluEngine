#pragma once
#ifndef __DDGI_PROBE_LAYOUT_H__
#define __DDGI_PROBE_LAYOUT_H__

#include "Framework/Math/Vector.hpp"
#include <cmath>
#include <cstdint>
#include <limits>

namespace Ailu::Render::DDGI
{
    using Math::Vector3UInt;
    using Math::Vector3f;

    inline u64 ProbeCount(const Vector3UInt &count)
    {
        u64 result = static_cast<u64>(count.x);
        for (const u32 dimension : {count.y, count.z})
        {
            if (result == 0u || dimension == 0u)
                return 0u;
            if (result > std::numeric_limits<u64>::max() / static_cast<u64>(dimension))
                return std::numeric_limits<u64>::max();
            result *= static_cast<u64>(dimension);
        }
        return result;
    }

    inline u32 ProbeIndex(const Vector3UInt &coord, const Vector3UInt &count)
    {
        return coord.x + coord.y * count.x + coord.z * count.x * count.y;
    }

    inline Vector3f GridSize(const Vector3UInt &count, const Vector3f &spacing)
    {
        const Vector3f cell_count(
            static_cast<f32>(count.x > 0u ? count.x - 1u : 0u),
            static_cast<f32>(count.y > 0u ? count.y - 1u : 0u),
            static_cast<f32>(count.z > 0u ? count.z - 1u : 0u));
        return cell_count * spacing;
    }

    inline Vector3f ProbeLocalPosition(const Vector3UInt &coord, const Vector3UInt &count,
                                       const Vector3f &spacing)
    {
        const Vector3f start = GridSize(count, spacing) * -0.5f;
        return start + Vector3f(static_cast<f32>(coord.x), static_cast<f32>(coord.y), static_cast<f32>(coord.z)) * spacing;
    }

    inline bool IsValidProbeGrid(const Vector3UInt &count, const Vector3f &spacing)
    {
        return count.x > 0u && count.y > 0u && count.z > 0u &&
               std::isfinite(spacing.x) && std::isfinite(spacing.y) && std::isfinite(spacing.z) &&
               spacing.x > 0.0f && spacing.y > 0.0f && spacing.z > 0.0f;
    }
}

#endif // __DDGI_PROBE_LAYOUT_H__
