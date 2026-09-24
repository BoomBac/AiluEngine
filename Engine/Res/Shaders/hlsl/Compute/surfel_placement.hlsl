#pragma kernel SurfelGridClearCS
#pragma kernel SurfelPrepareDispatchCS
#pragma kernel SurfelGridCountCS
#pragma kernel SurfelGridPrefixLocalCS
#pragma kernel SurfelGridPrefixBlocksCS
#pragma kernel SurfelGridPrefixFinalizeCS
#pragma kernel SurfelGridFillCS
#pragma kernel SurfelCoverageCS
#pragma kernel SurfelSpawnCS
#pragma kernel SurfelPrepareDrawArgsCS

#include "../common.hlsli"

struct SurfelData
{
    float3 _position;
    float _radius;
    float3 _normal;
    uint _flags;
    uint _cascade_level;
    uint _last_touched_frame;
    uint _object_id;
    uint _padding;
    uint2 _spawn_pixel;
};

struct SurfelGridCell
{
    uint _offset;
    uint _count;
};

struct DispatchArguments
{
    uint _thread_group_num_x;
    uint _thread_group_num_y;
    uint _thread_group_num_z;
};

struct DrawArguments
{
    uint _vertex_count_per_instance;
    uint _instance_count;
    uint _start_vertex_location;
    uint _start_instance_location;
};

CBUFFER_START(SurfelPlacementCB)
    uint _GridResolution;
    uint _CascadeCount;
    uint _MaxSurfelCount;
    float4 _GridInfo[5];
    float4 _SurfelParams;
    float4 _CameraParams;
CBUFFER_END

StructuredBuffer<SurfelData> _SurfelPool;
RWStructuredBuffer<SurfelData> _SurfelPoolWrite;
RWStructuredBuffer<uint> _SurfelState;
RWStructuredBuffer<SurfelGridCell> _GridCells;
RWStructuredBuffer<uint> _GridWriteCounters;
RWStructuredBuffer<uint> _GridIndices;
RWStructuredBuffer<uint> _GridBlockPrefix;
RWStructuredBuffer<DispatchArguments> _SurfelDispatchArgs;
RWStructuredBuffer<DrawArguments> _DrawArgs;

static const uint kSurfelValid = 1u;
static const uint kMaxCascadeCount = 5u;
static const uint kMaxNeighborCount = 27u;
static const uint kScanBlockSize = 1024u;
static const uint kScanThreadCount = 512u;
static const float kInvalidCoverage = 1.0e20f;

groupshared uint s_scan_local[kScanBlockSize];
groupshared uint s_scan_blocks[kScanBlockSize * 2u];

TEXTURE2D(_CameraDepthTexture)
TEXTURE2D(_CameraNormalsTexture)
RWTEXTURE2D(_CoverageTexture, float)

uint GetCellCount()
{
    return _GridResolution * _GridResolution * _GridResolution * _CascadeCount;
}

uint GetCellIndex(uint level, int3 cell)
{
    return ((level * _GridResolution + (uint)cell.z) * _GridResolution + (uint)cell.y) * _GridResolution +
           (uint)cell.x;
}

bool IsInsideCell(int3 cell)
{
    return all(cell >= 0) && all(cell < (int)_GridResolution);
}

bool IsValidSurfel(SurfelData surfel)
{
    return (surfel._flags & kSurfelValid) != 0u && surfel._radius > 0.0f;
}

float ComputeDesiredRadius(float3 world_position, out uint cascade_level, out bool is_in_grid)
{
    float view_distance = max(distance(_CameraPos.xyz, world_position), 0.001f);
    float world_size_per_pixel = 2.0f * view_distance * _CameraParams.x / max(_CameraParams.y, 1.0f);
    float desired_radius = world_size_per_pixel * _SurfelParams.y;
    float cell_size = max(_SurfelParams.x, 0.0001f);
    cascade_level = 0u;
    [loop]
    while (cascade_level + 1u < _CascadeCount)
    {
        int3 cell = (int3)floor((world_position - _GridInfo[cascade_level].xyz) / cell_size);
        if (desired_radius <= cell_size * _SurfelParams.w && IsInsideCell(cell))
            break;

        cell_size *= 2.0f;
        ++cascade_level;
    }

    int3 cell = (int3)floor((world_position - _GridInfo[cascade_level].xyz) / cell_size);
    is_in_grid = IsInsideCell(cell);
    return clamp(desired_radius, cell_size * _SurfelParams.z, cell_size * _SurfelParams.w);
}

bool IsBackground(float depth)
{
#if defined(_REVERSED_Z)
    return depth <= 0.00001f;
#else
    return depth >= 0.99999f;
#endif
}

float3 ReadWorldPosition(uint2 pixel_index, float depth)
{
    float2 uv = (float2(pixel_index) + 0.5f) / _ScreenParams.zw;
    return Unproject(uv, depth);
}

float3 ReadWorldNormal(uint2 pixel_index)
{
    float2 packed_normal = _CameraNormalsTexture.Load(int3(pixel_index, 0)).xy;
    return UnpackNormal(packed_normal);
}

bool SurfelOverlapsCell(SurfelData surfel, uint level, int3 cell)
{
    if (!IsInsideCell(cell))
        return false;

    float cell_size = _GridInfo[level].w;
    float3 cell_min = _GridInfo[level].xyz + float3(cell) * cell_size;
    float3 closest_point = clamp(surfel._position, cell_min, cell_min + cell_size);
    float3 delta = surfel._position - closest_point;
    return dot(delta, delta) <= surfel._radius * surfel._radius;
}

void ForEachOverlappingCell(SurfelData surfel, uint level, uint surfel_index, bool write_indices)
{
    float cell_size = _GridInfo[level].w;
    int3 base_cell = (int3)floor((surfel._position - _GridInfo[level].xyz) / cell_size);
    [unroll]
    for (int z = -1; z <= 1; ++z)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                int3 cell = base_cell + int3(x, y, z);
                if (!SurfelOverlapsCell(surfel, level, cell))
                    continue;

                uint cell_index = GetCellIndex(level, cell);
                if (write_indices)
                {
                    uint write_index;
                    InterlockedAdd(_GridWriteCounters[cell_index], 1u, write_index);
                    if (write_index < _GridCells[cell_index]._count)
                        _GridIndices[_GridCells[cell_index]._offset + write_index] = surfel_index;
                }
                else
                {
                    InterlockedAdd(_GridCells[cell_index]._count, 1u);
                }
            }
        }
    }
}

[numthreads(64, 1, 1)]
void SurfelGridClearCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint cell_index = dispatch_thread_id.x;
    if (cell_index >= GetCellCount())
        return;

    _GridCells[cell_index]._count = 0u;
    _GridWriteCounters[cell_index] = 0u;
    if (cell_index == 0u)
    {
        _SurfelState[1] = 0u;
        _SurfelState[2] = 0u;
        _SurfelState[3] = 0u;
    }
}

[numthreads(1, 1, 1)]
void SurfelPrepareDispatchCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x != 0u)
        return;

    uint surfel_count = min(_SurfelState[0], _MaxSurfelCount);
    _SurfelDispatchArgs[0]._thread_group_num_x = (surfel_count + 63u) / 64u;
    _SurfelDispatchArgs[0]._thread_group_num_y = 1u;
    _SurfelDispatchArgs[0]._thread_group_num_z = 1u;
}

[numthreads(64, 1, 1)]
void SurfelGridCountCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint surfel_index = dispatch_thread_id.x;
    if (surfel_index >= _MaxSurfelCount)
        return;

    SurfelData surfel = _SurfelPool[surfel_index];
    if (!IsValidSurfel(surfel))
        return;

    ForEachOverlappingCell(surfel, min(surfel._cascade_level, _CascadeCount - 1u), surfel_index, false);
}

[numthreads(512, 1, 1)]
void SurfelGridPrefixLocalCS(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    uint cell_count = GetCellCount();
    uint thread_index = group_thread_id.x;
    uint block_start = group_id.x * kScanBlockSize;
    uint index0 = block_start + thread_index;
    uint index1 = index0 + kScanThreadCount;

    s_scan_local[thread_index] = index0 < cell_count ? _GridCells[index0]._count : 0u;
    s_scan_local[thread_index + kScanThreadCount] = index1 < cell_count ? _GridCells[index1]._count : 0u;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 1u; stride < kScanBlockSize; stride <<= 1u)
    {
        uint scan_index = (thread_index + 1u) * stride * 2u - 1u;
        if (scan_index < kScanBlockSize)
            s_scan_local[scan_index] += s_scan_local[scan_index - stride];
        GroupMemoryBarrierWithGroupSync();
    }

    if (thread_index == 0u)
    {
        _GridBlockPrefix[group_id.x] = s_scan_local[kScanBlockSize - 1u];
        s_scan_local[kScanBlockSize - 1u] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = kScanBlockSize >> 1u; stride > 0u; stride >>= 1u)
    {
        uint scan_index = (thread_index + 1u) * stride * 2u - 1u;
        if (scan_index < kScanBlockSize)
        {
            uint left_sum = s_scan_local[scan_index - stride];
            s_scan_local[scan_index - stride] = s_scan_local[scan_index];
            s_scan_local[scan_index] += left_sum;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (index0 < cell_count)
        _GridCells[index0]._offset = s_scan_local[thread_index];
    if (index1 < cell_count)
        _GridCells[index1]._offset = s_scan_local[thread_index + kScanThreadCount];
}

[numthreads(1024, 1, 1)]
void SurfelGridPrefixBlocksCS(uint3 group_thread_id : SV_GroupThreadID)
{
    uint block_count = (GetCellCount() + kScanBlockSize - 1u) / kScanBlockSize;
    uint thread_index = group_thread_id.x;
    uint index0 = thread_index;
    uint index1 = index0 + kScanBlockSize;

    s_scan_blocks[index0] = index0 < block_count ? _GridBlockPrefix[index0] : 0u;
    s_scan_blocks[index1] = index1 < block_count ? _GridBlockPrefix[index1] : 0u;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 1u; stride < kScanBlockSize * 2u; stride <<= 1u)
    {
        uint scan_index = (thread_index + 1u) * stride * 2u - 1u;
        if (scan_index < kScanBlockSize * 2u)
            s_scan_blocks[scan_index] += s_scan_blocks[scan_index - stride];
        GroupMemoryBarrierWithGroupSync();
    }

    if (thread_index == 0u)
        s_scan_blocks[kScanBlockSize * 2u - 1u] = 0u;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = kScanBlockSize; stride > 0u; stride >>= 1u)
    {
        uint scan_index = (thread_index + 1u) * stride * 2u - 1u;
        if (scan_index < kScanBlockSize * 2u)
        {
            uint left_sum = s_scan_blocks[scan_index - stride];
            s_scan_blocks[scan_index - stride] = s_scan_blocks[scan_index];
            s_scan_blocks[scan_index] += left_sum;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (index0 < block_count)
        _GridBlockPrefix[index0] = s_scan_blocks[index0];
    if (index1 < block_count)
        _GridBlockPrefix[index1] = s_scan_blocks[index1];
}

[numthreads(256, 1, 1)]
void SurfelGridPrefixFinalizeCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint cell_index = dispatch_thread_id.x;
    uint cell_count = GetCellCount();
    if (cell_index >= cell_count)
        return;

    uint block_index = cell_index / kScanBlockSize;
    _GridCells[cell_index]._offset += _GridBlockPrefix[block_index];
}

[numthreads(64, 1, 1)]
void SurfelGridFillCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint surfel_index = dispatch_thread_id.x;
    if (surfel_index >= _MaxSurfelCount)
        return;

    SurfelData surfel = _SurfelPool[surfel_index];
    if (!IsValidSurfel(surfel))
        return;

    ForEachOverlappingCell(surfel, min(surfel._cascade_level, _CascadeCount - 1u), surfel_index, true);
}

float ComputeSurfelCoverageWeight(float3 position, float3 normal, SurfelData surfel)
{
    float3 delta = position - surfel._position;
    float plane_distance = abs(dot(delta, surfel._normal));
    float distance_sq = dot(delta, delta);
    float tangent_distance = sqrt(max(distance_sq - plane_distance * plane_distance, 0.0f));
    float radial_weight = saturate(1.0f - tangent_distance / surfel._radius);
    float normal_weight = saturate(dot(normal, surfel._normal));
    float plane_weight = saturate(1.0f - plane_distance / surfel._radius);
    return radial_weight * normal_weight * plane_weight;
}

float ComputeCoverage(uint2 pixel_index, out float3 world_position, out float3 world_normal, out uint cascade_level)
{
    float depth = _CameraDepthTexture.Load(int3(pixel_index, 0)).r;
    world_position = 0.0f;
    world_normal = 0.0f;
    cascade_level = 0u;
    if (IsBackground(depth))
        return kInvalidCoverage;

    world_position = ReadWorldPosition(pixel_index, depth);
    world_normal = ReadWorldNormal(pixel_index);
    if (dot(world_normal, world_normal) < 0.5f)
        return kInvalidCoverage;

    bool is_in_grid;
    float desired_radius = ComputeDesiredRadius(world_position, cascade_level, is_in_grid);
    if (!is_in_grid)
        return kInvalidCoverage;

    float cell_size = _GridInfo[cascade_level].w;
    int3 cell = (int3)floor((world_position - _GridInfo[cascade_level].xyz) / cell_size);

    uint cell_index = GetCellIndex(cascade_level, cell);
    SurfelGridCell grid_cell = _GridCells[cell_index];
    float coverage = 0.0f;
    for (uint index = 0u; index < grid_cell._count; ++index)
    {
        uint surfel_index = _GridIndices[grid_cell._offset + index];
        if (surfel_index >= _MaxSurfelCount)
            continue;
        SurfelData surfel = _SurfelPool[surfel_index];
        if (IsValidSurfel(surfel))
            coverage += ComputeSurfelCoverageWeight(world_position, world_normal, surfel);
    }
    return coverage;
}

[numthreads(16, 16, 1)]
void SurfelCoverageCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 pixel_index = dispatch_thread_id.xy;
    if (pixel_index.x >= (uint)_ScreenParams.z || pixel_index.y >= (uint)_ScreenParams.w)
        return;

    float3 world_position;
    float3 world_normal;
    uint cascade_level;
    _CoverageTexture[pixel_index] = ComputeCoverage(pixel_index, world_position, world_normal, cascade_level);
}

groupshared float s_coverage[256];
groupshared uint s_pixel_index[256];

[numthreads(16, 16, 1)]
void SurfelSpawnCS(uint3 dispatch_thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID)
{
    uint local_index = group_thread_id.y * 16u + group_thread_id.x;
    uint2 pixel_index = dispatch_thread_id.xy;
    bool in_bounds = pixel_index.x < (uint)_ScreenParams.z && pixel_index.y < (uint)_ScreenParams.w;
    float coverage = in_bounds ? _CoverageTexture[pixel_index] : kInvalidCoverage;
    s_coverage[local_index] = coverage;
    s_pixel_index[local_index] = in_bounds ? pixel_index.y * (uint)_ScreenParams.z + pixel_index.x : 0u;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 128u; stride > 0u; stride >>= 1u)
    {
        if (local_index < stride && s_coverage[local_index + stride] < s_coverage[local_index])
        {
            s_coverage[local_index] = s_coverage[local_index + stride];
            s_pixel_index[local_index] = s_pixel_index[local_index + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (local_index != 0u || s_coverage[0] >= _CameraParams.z)
        return;

    uint width = (uint)_ScreenParams.z;
    uint2 candidate_pixel = uint2(s_pixel_index[0] % width, s_pixel_index[0] / width);
    float depth = _CameraDepthTexture.Load(int3(candidate_pixel, 0)).r;
    if (IsBackground(depth))
        return;

    float3 world_position = ReadWorldPosition(candidate_pixel, depth);
    float3 world_normal = ReadWorldNormal(candidate_pixel);
    if (dot(world_normal, world_normal) < 0.5f)
        return;

    uint cascade_level;
    bool is_in_grid;
    float desired_radius = ComputeDesiredRadius(world_position, cascade_level, is_in_grid);
    if (!is_in_grid)
        return;

    uint surfel_index;
    InterlockedAdd(_SurfelState[0], 1u, surfel_index);
    if (surfel_index >= _MaxSurfelCount)
    {
        InterlockedAdd(_SurfelState[0], 0xffffffffu);
        InterlockedAdd(_SurfelState[2], 1u);
        return;
    }

    SurfelData surfel;
    surfel._position = world_position;
    surfel._radius = desired_radius;//_GridInfo[cascade_level].w;
    surfel._normal = normalize(world_normal);
    surfel._flags = kSurfelValid;
    surfel._cascade_level = cascade_level;
    surfel._last_touched_frame = _FrameIndex;
    surfel._object_id = 0u;
    surfel._padding = 0u;
    surfel._spawn_pixel = candidate_pixel;
    _SurfelPoolWrite[surfel_index] = surfel;
    InterlockedAdd(_SurfelState[1], 1u);
}
[shader("compute")]
[numthreads(1, 1, 1)]
void SurfelPrepareDrawArgsCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x != 0u)
        return;

    _DrawArgs[0]._vertex_count_per_instance = 48u;
    _DrawArgs[0]._instance_count = min(_SurfelState[0], _MaxSurfelCount);
    _DrawArgs[0]._start_vertex_location = 0u;
    _DrawArgs[0]._start_instance_location = 0u;
}
