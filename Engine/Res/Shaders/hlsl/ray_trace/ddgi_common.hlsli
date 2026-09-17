#ifndef AIL_DDGI_COMMON_HLSLI
#define AIL_DDGI_COMMON_HLSLI

#include "../common.hlsli"

// Must stay in sync with the CPU DDGIProbeRayData allocation.
static const uint kRaysPerProbe = 128u;
static const uint kRelocationRaysPerProbe = 32u;
static const float kMaxRelocationOffsetFraction = 0.45f;
static const float kBackfaceDistanceScale = 0.2f;
static const float kDDGIProbeStateNormal = 0.0f;
static const float kDDGIProbeStateRelocated = 1.0f;
static const float kDDGIProbeStateBackfaceNear = 2.0f;
static const float kDDGIProbeStateBackfaceRejected = 3.0f;
static const float kDDGIProbeStateHistoryReset = 4.0f;

bool DDGIProbeNeedsHistoryReset(float state)
{
    return state >= kDDGIProbeStateHistoryReset;
}

float DDGIProbeStateWithoutHistoryReset(float state)
{
    return DDGIProbeNeedsHistoryReset(state) ? state - kDDGIProbeStateHistoryReset : state;
}
// 14（= 距离图集的分辨率）：6x6 时八面体折线（球面赤道）两侧的辐照度差很大，双线性跨折线混合
// 会在球面上留下一圈"圆角方形"的明暗软边界；折痕带宽 ∝ 1/interior。改这里也要改 DDGITypes.h。
static const uint kRadianceInteriorResolution = 14u;
static const uint kRadianceBorderSize = 1u;
static const uint kRadiancePhysicalResolution = kRadianceInteriorResolution + kRadianceBorderSize * 2u;
static const uint kDistanceInteriorResolution = 14u;
static const uint kDistanceBorderSize = 1u;
static const uint kDistancePhysicalResolution = kDistanceInteriorResolution + kDistanceBorderSize * 2u;

static const uint kDDGIProbeDebugModeRadiance = 0u;
static const uint kDDGIProbeDebugModeDistance = 1u;
static const uint kDDGIProbeDebugModeHitType = 2u;
static const uint kDDGIProbeDebugModeRayDirection = 3u;
static const uint kDDGIProbeDebugModeIrradiance = 4u;
static const uint kDDGIProbeDebugModeDistanceMean = 5u;
static const uint kDDGIProbeDebugModeDistanceVariance = 6u;
static const uint kDDGIProbeDebugModeFinalWeight = 7u;
static const uint kDDGIInvalidProbeIndex = 0xffffffffu;

struct DDGIProbeRayData
{
    float3 _radiance;
    // Positive = front-face hit, negative = back-face hit, abs(max) = miss.
    float _distance;
};

struct DDGIProbeData
{
    float3 _relocation_offset;
    float _state;
};

uint DDGIProbeIndex(uint3 probe_coord, uint3 probe_count)
{
    return probe_coord.x + probe_coord.y * probe_count.x + probe_coord.z * probe_count.x * probe_count.y;
}

uint ProbeIndex(uint3 probe_coord, uint3 probe_count)
{
    return DDGIProbeIndex(probe_coord, probe_count);
}

uint3 DDGIProbeCoord(uint probe_index, uint3 probe_count)
{
    const uint xy_count = probe_count.x * probe_count.y;
    const uint z = probe_index / xy_count;
    const uint remainder = probe_index - z * xy_count;
    const uint y = remainder / probe_count.x;
    const uint x = remainder - y * probe_count.x;
    return uint3(x, y, z);
}

uint3 ProbeCoord(uint probe_index, uint3 probe_count)
{
    return DDGIProbeCoord(probe_index, probe_count);
}

float3 DDGIProbeLocalPosition(uint3 probe_coord, uint3 probe_count, float3 probe_spacing)
{
    const float3 grid_size = float3(
        (float)(probe_count.x > 0u ? probe_count.x - 1u : 0u),
        (float)(probe_count.y > 0u ? probe_count.y - 1u : 0u),
        (float)(probe_count.z > 0u ? probe_count.z - 1u : 0u)) * probe_spacing;
    return grid_size * -0.5f + (float3)probe_coord * probe_spacing;
}

float3 ProbeLocalPosition(uint3 probe_coord, uint3 probe_count, float3 probe_spacing)
{
    return DDGIProbeLocalPosition(probe_coord, probe_count, probe_spacing);
}

float3 DDGIProbeLocalPosition(uint probe_index, uint3 probe_count, float3 probe_spacing)
{
    return DDGIProbeLocalPosition(DDGIProbeCoord(probe_index, probe_count), probe_count, probe_spacing);
}

uint DDGIRayDataIndex(uint probe_index, uint ray_index)
{
    return probe_index * kRaysPerProbe + ray_index;
}

uint RayDataIndex(uint probe_index, uint ray_index)
{
    return DDGIRayDataIndex(probe_index, ray_index);
}

float3 DDGIGetSphericalFibonacciDirection(uint ray_index, uint ray_count)
{
    // Spherical Fibonacci point set: one deterministic, evenly distributed direction per raw ray.
    const float index = (float)ray_index + 0.5f;
    const float z = 1.0f - 2.0f * index / (float)ray_count;
    const float radius = sqrt(saturate(1.0f - z * z));
    const float golden_angle = 2.39996322972865332f;
    const float phi = golden_angle * index;
    return float3(cos(phi) * radius, sin(phi) * radius, z);
}

float3 DDGIGetBaseRayDirection(uint ray_index)
{
    return DDGIGetSphericalFibonacciDirection(ray_index, kRaysPerProbe);
}

bool DDGIIsFixedRelocationRay(uint ray_index, uint use_probe_relocation)
{
    return use_probe_relocation != 0u && ray_index < kRelocationRaysPerProbe;
}

float DDGIHashToUnit(uint value)
{
    value ^= value * 0x3d20adeau;
    value ^= value >> 16u;
    value *= 0x05526c56u;
    value ^= value >> 16u;
    return (float)value / 4294967295.0f;
}

float3x3 DDGIGetRayRotation(uint frame_index, uint freeze_ray_rotation, uint probe_index)
{
    if (freeze_ray_rotation != 0u)
    {
        return float3x3(
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f);
    }

    // Deterministic per-frame, per-probe rotation keeps Trace, Sphere and Line debug in lockstep.
    // Seeding with the probe index decorrelates the estimator error between neighbouring probes:
    // a single volume-wide rotation makes every probe's error identical, so the trilinear probe
    // interpolation and the temporal accumulation cannot average it out.
    const uint probe_offset = probe_index * 0x9e3779b9u;
    const float yaw = DoublePI * DDGIHashToUnit(frame_index * 747796405u + probe_offset + 2891336453u);
    const float pitch = (DDGIHashToUnit(frame_index * 277803737u + probe_offset + 1013904223u) - 0.5f) * 0.5f;
    const float roll = (DDGIHashToUnit(frame_index * 362437u + probe_offset + 2246822519u) - 0.5f) * 0.5f;
    const float sin_yaw = sin(yaw);
    const float cos_yaw = cos(yaw);
    const float sin_pitch = sin(pitch);
    const float cos_pitch = cos(pitch);
    const float sin_roll = sin(roll);
    const float cos_roll = cos(roll);

    const float3x3 yaw_matrix = float3x3(
        cos_yaw, 0.0f, sin_yaw,
        0.0f, 1.0f, 0.0f,
        -sin_yaw, 0.0f, cos_yaw);
    const float3x3 pitch_matrix = float3x3(
        1.0f, 0.0f, 0.0f,
        0.0f, cos_pitch, -sin_pitch,
        0.0f, sin_pitch, cos_pitch);
    const float3x3 roll_matrix = float3x3(
        cos_roll, -sin_roll, 0.0f,
        sin_roll, cos_roll, 0.0f,
        0.0f, 0.0f, 1.0f);
    return mul(roll_matrix, mul(pitch_matrix, yaw_matrix));
}

float3 DDGIGetProbeRayDirection(uint ray_index, float3x3 ray_rotation)
{
    return normalize(mul(ray_rotation, DDGIGetBaseRayDirection(ray_index)));
}

float3 DDGIGetProbeRayDirection(uint ray_index, float3x3 ray_rotation, uint use_probe_relocation)
{
    if (DDGIIsFixedRelocationRay(ray_index, use_probe_relocation))
        return normalize(DDGIGetSphericalFibonacciDirection(ray_index, kRelocationRaysPerProbe));

    const uint lighting_ray_index = use_probe_relocation != 0u
        ? ray_index - kRelocationRaysPerProbe : ray_index;
    const uint lighting_ray_count = use_probe_relocation != 0u
        ? kRaysPerProbe - kRelocationRaysPerProbe : kRaysPerProbe;
    return normalize(mul(ray_rotation, DDGIGetSphericalFibonacciDirection(lighting_ray_index, lighting_ray_count)));
}

float3 DDGIOctDecode(float2 encoded)
{
    const float2 oct = encoded * 2.0f - 1.0f;
    float3 direction = float3(oct, 1.0f - abs(oct.x) - abs(oct.y));
    if (direction.z < 0.0f)
    {
        direction.xy = (1.0f - abs(direction.yx)) * float2(
            direction.x >= 0.0f ? 1.0f : -1.0f,
            direction.y >= 0.0f ? 1.0f : -1.0f);
    }
    return normalize(direction);
}

float2 DDGIOctEncode(float3 direction)
{
    direction = normalize(direction);
    direction /= max(abs(direction.x) + abs(direction.y) + abs(direction.z), 0.00001f);
    float2 encoded = direction.xy;
    if (direction.z < 0.0f)
    {
        encoded = (1.0f - abs(encoded.yx)) * float2(
            encoded.x >= 0.0f ? 1.0f : -1.0f,
            encoded.y >= 0.0f ? 1.0f : -1.0f);
    }
    return encoded * 0.5f + 0.5f;
}

bool DDGIIsMiss(float signed_distance, float max_ray_distance)
{
    return abs(signed_distance) >= max(max_ray_distance, 0.0f);
}

bool DDGIIsBackFaceHit(float signed_distance, float max_ray_distance)
{
    return !DDGIIsMiss(signed_distance, max_ray_distance) && signed_distance < 0.0f;
}

bool DDGIIsFrontFaceHit(float signed_distance, float max_ray_distance)
{
    return !DDGIIsMiss(signed_distance, max_ray_distance) && signed_distance > 0.0f;
}

#endif // AIL_DDGI_COMMON_HLSLI
