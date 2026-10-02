#pragma once
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/TransformMath.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Ailu::Render
{
    struct FroxelBounds
    {
        Vector3UInt _min = Vector3UInt(0u, 0u, 0u);
        Vector3UInt _max = Vector3UInt(0u, 0u, 0u);
        bool IsEmpty() const { return _min.x >= _max.x || _min.y >= _max.y || _min.z >= _max.z; }
        u64 Count() const
        {
            return IsEmpty() ? 0u : u64(_max.x - _min.x) * (_max.y - _min.y) * (_max.z - _min.z);
        }
    };

    // Both light types fit inside Sphere(position, range). Min is inclusive, max is exclusive.
    inline FroxelBounds CalculateFroxelBounds(const Vector3f &position, f32 range, const Matrix4x4f &view,
        const Matrix4x4f &projection, f32 near_depth, f32 far_depth, const Vector3UInt &dimensions)
    {
        FroxelBounds bounds;
        if (!std::isfinite(range) || range <= 0.0f || near_depth <= 0.0f || far_depth <= near_depth ||
            !std::isfinite(near_depth) || !std::isfinite(far_depth) ||
            dimensions.x == 0u || dimensions.y == 0u || dimensions.z == 0u ||
            !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return bounds;
        const auto center = Math::TransformCoord(view, position);
        const f32 depth_min = center.z - range;
        const f32 depth_max = center.z + range;
        if (depth_max <= 0.0f)
            return bounds;
        const f32 log_range = std::log(far_depth / near_depth);
        // Injection jitters up to .4 slice thickness along the view ray: include neighbouring slices.
        const f32 min_slice = depth_min > 0.0f ? std::log(depth_min / near_depth) / log_range * dimensions.z : 0.0f;
        const f32 max_slice = std::log(depth_max / near_depth) / log_range * dimensions.z;
        const auto index = [](f32 value, u32 count) { return u32(std::clamp(value, 0.0f, f32(count))); };
        bounds._min.z = index(std::floor(min_slice) - 2.0f, dimensions.z);
        bounds._max.z = index(std::ceil(max_slice) + 2.0f, dimensions.z);
        bounds._max.x = dimensions.x;
        bounds._max.y = dimensions.y;
        // Near-plane intersections (including a camera inside the sphere) conservatively cover all XY.
        if (depth_min > near_depth)
        {
            Vector2f uv_min(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max());
            Vector2f uv_max(-uv_min.x, -uv_min.y);
            for (u32 corner = 0u; corner < 8u; ++corner)
            {
                const Vector3f point = center + Vector3f((corner & 1u) ? range : -range,
                    (corner & 2u) ? range : -range, (corner & 4u) ? range : -range);
                const auto clip = Math::TransformVector(projection, Vector4f(point, 1.0f));
                if (clip.w <= 1e-6f || !std::isfinite(clip.w))
                    return FroxelBounds{Vector3UInt(0u, 0u, bounds._min.z), dimensions};
                const Vector2f uv(clip.x / clip.w * .5f + .5f, clip.y / clip.w * .5f + .5f);
                uv_min.x = std::min(uv_min.x, uv.x);
                uv_min.y = std::min(uv_min.y, uv.y);
                uv_max.x = std::max(uv_max.x, uv.x);
                uv_max.y = std::max(uv_max.y, uv.y);
            }
            bounds._min.x = index(std::floor(uv_min.x * dimensions.x) - 1.0f, dimensions.x);
            bounds._min.y = index(std::floor(uv_min.y * dimensions.y) - 1.0f, dimensions.y);
            bounds._max.x = index(std::ceil(uv_max.x * dimensions.x) + 1.0f, dimensions.x);
            bounds._max.y = index(std::ceil(uv_max.y * dimensions.y) + 1.0f, dimensions.y);
        }
        return bounds;
    }
}
