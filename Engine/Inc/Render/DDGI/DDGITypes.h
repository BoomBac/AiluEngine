#pragma once
#ifndef __DDGI_TYPES_H__
#define __DDGI_TYPES_H__

#include "Framework/Math/Matrix.hpp"
#include "Framework/Math/Vector.hpp"
#include "Scene/Entity.h"

#include <limits>

namespace Ailu::Render
{
    class GPUBuffer;
    class Texture2D;

    using Math::Matrix4x4f;
    using Math::Vector3UInt;
    using Math::Vector3f;

    namespace DDGI
    {
        // Keep these values in lockstep with the DDGI shaders.
        inline constexpr u32 kRaysPerProbe = 128u;
        inline constexpr u32 kRelocationRaysPerProbe = 32u;
        inline constexpr f32 kMaxRelocationOffsetFraction = 0.45f;
        // 14（= 距离图集的分辨率）：6x6 时八面体折线（球面赤道）两侧的辐照度差很大，双线性跨折线
        // 混合后会在球面上留下一圈"圆角方形"的明暗软边界；折痕带宽 ∝ 1/interior。
        inline constexpr u32 kRadianceInteriorResolution = 14u;
        inline constexpr u32 kRadianceBorderSize = 1u;
        inline constexpr u32 kRadiancePhysicalResolution = kRadianceInteriorResolution + kRadianceBorderSize * 2u;
        inline constexpr u32 kDistanceInteriorResolution = 14u;
        inline constexpr u32 kDistanceBorderSize = 1u;
        inline constexpr u32 kDistancePhysicalResolution = kDistanceInteriorResolution + kDistanceBorderSize * 2u;
        inline constexpr u32 kInvalidProbeIndex = std::numeric_limits<u32>::max();
    }

    struct DDGIProbeRayData
    {
        Vector3f _radiance = Vector3f::kZero;
        f32 _distance = 0.0f;
    };

    static_assert(sizeof(DDGIProbeRayData) == 16u, "DDGIProbeRayData must be 16 bytes");

    struct DDGIProbeData
    {
        Vector3f _relocation_offset = Vector3f::kZero;
        f32 _state = 0.0f;
    };

    static_assert(sizeof(DDGIProbeData) == 16u, "DDGIProbeData must be 16 bytes");

    struct DDGIVolumeRenderData
    {
        ECS::Entity _entity = ECS::kInvalidEntity;
        Matrix4x4f _local_to_world = Matrix4x4f::Identity();
        Vector3UInt _probe_count = Vector3UInt(1u, 1u, 1u);
        Vector3f _probe_spacing = Vector3f::kOne;
        f32 _max_ray_distance = 50.0f;
        bool _draw_probes = false;
        bool _draw_probe_rays = true;
        bool _use_probe_relocation = false;
        f32 _relocation_backface_threshold = 0.25f;
        f32 _random_ray_backface_threshold = 0.10f;
        f32 _relocation_min_frontface_distance_ratio = 0.1f;
        bool _draw_relocation_debug = false;
        bool _freeze_ray_rotation = false;
        f32 _debug_probe_radius = 0.12f;
        f32 _debug_ray_length = 4.0f;
        u32 _debug_mode = 2u;
        u32 _radiance_atlas_tiles_x = 0u;
        u32 _radiance_atlas_tiles_y = 0u;
        u32 _radiance_atlas_width = 0u;
        u32 _radiance_atlas_height = 0u;
        u32 _distance_atlas_tiles_x = 0u;
        u32 _distance_atlas_tiles_y = 0u;
        u32 _distance_atlas_width = 0u;
        u32 _distance_atlas_height = 0u;
    };

    struct DDGIVolumeRuntime
    {
        DDGIVolumeRenderData _desc;
        Ref<GPUBuffer> _probe_ray_data;
        Ref<GPUBuffer> _probe_data;
        Ref<Texture2D> _radiance_texture;
        Ref<Texture2D> _radiance_history_texture;
        Ref<Texture2D> _distance_texture;
        Ref<Texture2D> _distance_history_texture;
        u32 _probe_count = 0u;
        u32 _history_reset_count = 0u;
        bool _temporal_history_valid = false;
        bool _seen_this_frame = false;
    };
}

#endif // __DDGI_TYPES_H__
