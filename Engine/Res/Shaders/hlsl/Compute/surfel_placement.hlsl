#pragma kernel SurfelGridClearCS
#pragma kernel SurfelFrameStateClearCS
#pragma kernel SurfelPrepareDispatchCS
#pragma kernel SurfelFlushDeferredFreeListCS
#pragma kernel SurfelResetDeferredFreeCountCS
#pragma kernel SurfelActiveListRemoveCS
#pragma kernel SurfelMaintenanceCS
#pragma kernel SurfelGridCountCS
#pragma kernel SurfelGridPrefixLocalCS
#pragma kernel SurfelGridPrefixBlocksCS
#pragma kernel SurfelGridPrefixFinalizeCS
#pragma kernel SurfelGridFillCS
#pragma kernel SurfelMigrationResolveCS
#pragma kernel SurfelMigrationRecycleCS
#pragma kernel SurfelCoverageCS
#pragma kernel SurfelCandidateClearCS
#pragma kernel SurfelCandidateCS
#pragma kernel SurfelCandidateClaimCS
#pragma kernel SurfelCandidateCommitCS
#pragma kernel SurfelMigrationFinalizeCS
#pragma kernel SurfelPrepareDrawArgsCS

#include "../common.hlsli"
#include "../surfel_common.hlsli"

struct SurfelGridCell
{
    uint _offset;
    uint _count;
};

struct SurfelCandidate
{
    float3 _position;
    float _radius;
    float3 _normal;
    float _coverage;
    uint _cascade_level;
    uint _pixel_x;
    uint _pixel_y;
    uint _priority;
    float3 _inherited_irradiance;
    uint _inheritance_depth;
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
    float4 _SpawnParams;
    float4 _CameraParams;
    uint _MaxSpawnPerFrame;
    uint _CandidateCapacity;
CBUFFER_END

StructuredBuffer<SurfelData> _SurfelPool;
RWStructuredBuffer<SurfelData> _SurfelPoolWrite;
RWStructuredBuffer<SurfelLightingData> _SurfelLightingWrite;
StructuredBuffer<SurfelLightingData> _SurfelLighting;
StructuredBuffer<uint> _SurfelStateRead;
RWStructuredBuffer<uint> _SurfelState;
RWStructuredBuffer<uint> _SurfelFreeList;
RWStructuredBuffer<uint> _SurfelDeferredFreeList;
StructuredBuffer<uint> _SurfelDeferredFreeListRead;
RWStructuredBuffer<uint> _ActiveSurfelIndices;
RWStructuredBuffer<uint> _SurfelActiveListIndices;
RWStructuredBuffer<uint> _SurfelFrameState;
RWStructuredBuffer<SurfelGridCell> _GridCells;
StructuredBuffer<SurfelGridCell> _GridCellsRead;
RWStructuredBuffer<uint> _GridWriteCounters;
RWStructuredBuffer<uint> _GridIndices;
StructuredBuffer<uint> _GridIndicesRead;
RWStructuredBuffer<uint> _GridBlockPrefix;
RWStructuredBuffer<SurfelCandidate> _SurfelCandidates;
RWStructuredBuffer<uint> _CandidateState;
RWStructuredBuffer<uint> _SpawnOccupancy;
RWStructuredBuffer<DispatchArguments> _SurfelDispatchArgs;
RWStructuredBuffer<DrawArguments> _DrawArgs;

static const uint kSurfelStateActiveCount = 1u;
static const uint kSurfelStateFreeCount = 2u;
static const uint kSurfelStateDeferredFreeCount = 3u;
static const uint kSurfelStateOverflowCount = 4u;
static const uint kSurfelFrameSpawnCount = 0u;
static const uint kSurfelFrameRecycleCount = 1u;
static const uint kSurfelFrameOverflowCount = 2u;
static const uint kSurfelFrameMigratedFiner = 3u;
static const uint kSurfelFrameMigratedCoarser = 4u;
static const uint kSurfelFrameOverlapRecycleCount = 5u;
static const uint kMigrationStay = 0u;
static const uint kMigrationFiner = 1u;
static const uint kMigrationCoarser = 2u;
static const uint kMigrationRecycle = 3u;
static const uint kMaxCascadeCount = 5u;
static const uint kMaxNeighborCount = 27u;
static const uint kScanBlockSize = 1024u;
static const uint kScanThreadCount = 512u;
static const float kInvalidCoverage = 1.0e20f;
// Preserve settled samples across camera dolly. Refine only after a full octave of detail is missing.
static const float kRadiusRefineThreshold = 0.5f;
static const float kMigrationEnterMarginCells = 1.0f;
static const float kMigrationFinerEnterScale = 0.90f;
static const float kMigrationCoarserEnterScale = 1.10f;

struct SurfelMigrationDecision
{
    uint _mode;
    uint _target_cascade;
    float _target_radius;
};

groupshared uint s_scan_local[kScanBlockSize];
groupshared uint s_scan_blocks[kScanBlockSize * 2u];

TEXTURE2D(_CameraDepthTexture)
TEXTURE2D(_CameraNormalsTexture)
TEXTURE2D(_BlueNoiseTexture)
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

bool IsInsideCascade(float3 position, uint cascade_level)
{
    float3 grid_min = _GridInfo[cascade_level].xyz;
    float cell_size = _GridInfo[cascade_level].w;
    float3 grid_max = grid_min + cell_size * (float)_GridResolution;
    return all(position >= grid_min) && all(position < grid_max);
}

bool IsInsideCascadeWithMargin(float3 world_position, uint cascade_level, float margin_cell_count)
{
    float cell_size = _GridInfo[cascade_level].w;
    float margin = cell_size * margin_cell_count;
    float3 grid_min = _GridInfo[cascade_level].xyz + margin;
    float3 grid_max = _GridInfo[cascade_level].xyz + cell_size * (float)_GridResolution - margin;
    return all(world_position >= grid_min) && all(world_position < grid_max);
}

void RecycleSurfelImmediate(uint surfel_index)
{
    SurfelData surfel = _SurfelPoolWrite[surfel_index];
    if (!IsValidSurfel(surfel))
        return;

    surfel._flags &= ~kSurfelValid;
    _SurfelPoolWrite[surfel_index] = surfel;

    uint deferred_index;
    InterlockedAdd(_SurfelState[kSurfelStateDeferredFreeCount], 1u, deferred_index);
    if (deferred_index < _MaxSurfelCount)
        _SurfelDeferredFreeList[deferred_index] = surfel_index;

    InterlockedAdd(_SurfelState[kSurfelStateActiveCount], 0xffffffffu);
    InterlockedAdd(_SurfelFrameState[kSurfelFrameRecycleCount], 1u);
}

bool AllocateSurfelSlot(out uint surfel_index)
{
    uint free_count;
    InterlockedAdd(_SurfelState[kSurfelStateFreeCount], 0u, free_count);
    [allow_uav_condition]
    while (free_count > 0u)
    {
        uint previous_count;
        InterlockedCompareExchange(_SurfelState[kSurfelStateFreeCount], free_count, free_count - 1u,
                                   previous_count);
        if (previous_count == free_count)
        {
            surfel_index = _SurfelFreeList[free_count - 1u];
            return surfel_index < _MaxSurfelCount;
        }
        free_count = previous_count;
    }

    uint high_watermark;
    InterlockedAdd(_SurfelState[kSurfelStateHighWatermark], 0u, high_watermark);
    [allow_uav_condition]
    while (high_watermark < _MaxSurfelCount)
    {
        uint previous_high_watermark;
        InterlockedCompareExchange(_SurfelState[kSurfelStateHighWatermark], high_watermark,
                                   high_watermark + 1u, previous_high_watermark);
        if (previous_high_watermark == high_watermark)
        {
            surfel_index = high_watermark;
            return true;
        }
        high_watermark = previous_high_watermark;
    }

    surfel_index = 0u;
    return false;
}

float ComputeRawDesiredRadius(float3 world_position)
{
    float view_distance = max(distance(_CameraPos.xyz, world_position), 0.001f);
    float world_size_per_pixel = 2.0f * view_distance * _CameraParams.x / max(_CameraParams.y, 1.0f);
    return world_size_per_pixel * _SurfelParams.y;
}

float ClampRadiusForCascade(float raw_radius, uint cascade_level)
{
    float cell_size = _GridInfo[cascade_level].w;
    return clamp(raw_radius, cell_size * _SurfelParams.z, cell_size * _SurfelParams.w);
}

float ComputeDesiredRadius(float3 world_position, out uint cascade_level, out bool is_in_grid)
{
    float raw_radius = ComputeRawDesiredRadius(world_position);
    cascade_level = 0u;
    [loop]
    while (cascade_level + 1u < _CascadeCount)
    {
        float cell_size = _GridInfo[cascade_level].w;
        int3 cell = (int3)floor((world_position - _GridInfo[cascade_level].xyz) / cell_size);
        if (raw_radius <= cell_size * _SurfelParams.w && IsInsideCell(cell))
            break;

        ++cascade_level;
    }

    float cell_size = _GridInfo[cascade_level].w;
    int3 cell = (int3)floor((world_position - _GridInfo[cascade_level].xyz) / cell_size);
    is_in_grid = IsInsideCell(cell);
    return ClampRadiusForCascade(raw_radius, cascade_level);
}

SurfelMigrationDecision ComputeMigrationDecision(SurfelData surfel)
{
    SurfelMigrationDecision decision;
    uint current_cascade = min(surfel._cascade_level, _CascadeCount - 1u);
    decision._mode = kMigrationStay;
    decision._target_cascade = current_cascade;
    decision._target_radius = surfel._radius;

    uint desired_cascade;
    bool is_in_grid;
    float desired_radius = ComputeDesiredRadius(surfel._position, desired_cascade, is_in_grid);
    if (!is_in_grid)
    {
        decision._mode = kMigrationRecycle;
        return decision;
    }

    // The grid is an index, not a reason to replace a settled surface sample. Keep the current
    // representation while it remains addressable and meets the requested detail within one octave.
    // Limit bucket coarseness too: a long excursion must not strand many tiny samples in one coarse cell.
    const bool needs_refinement = desired_radius < surfel._radius * kRadiusRefineThreshold;
    const bool retains_index_resolution = current_cascade <= desired_cascade + 1u;
    if (IsInsideCascade(surfel._position, current_cascade) && !needs_refinement && retains_index_resolution)
        return decision;

    if (desired_cascade == current_cascade)
    {
        decision._target_radius = desired_radius;
        return decision;
    }

    int level_delta = (int)desired_cascade - (int)current_cascade;
    float raw_radius = ComputeRawDesiredRadius(surfel._position);
    if (abs(level_delta) >= 2)
    {
        decision._mode = level_delta < 0 ? kMigrationFiner : kMigrationCoarser;
    }
    else if (level_delta < 0)
    {
        float finer_radius_threshold = _GridInfo[desired_cascade].w * _SurfelParams.w;
        if (!IsInsideCascadeWithMargin(surfel._position, desired_cascade, kMigrationEnterMarginCells) ||
            raw_radius > finer_radius_threshold * kMigrationFinerEnterScale)
        {
            return decision;
        }

        decision._mode = kMigrationFiner;
    }
    else
    {
        float coarser_radius_threshold = _GridInfo[current_cascade].w * _SurfelParams.w;
        if (IsInsideCascade(surfel._position, current_cascade) &&
            raw_radius < coarser_radius_threshold * kMigrationCoarserEnterScale)
        {
            return decision;
        }

        decision._mode = kMigrationCoarser;
    }

    decision._target_cascade = desired_cascade;
    // Moving a point into a larger indexing window must not grow its surface footprint.
    decision._target_radius = min(surfel._radius, desired_radius);
    return decision;
}

void UpdateSameCascadeRadius(uint surfel_index, SurfelData surfel, float target_radius)
{
    // Zooming out does not invalidate existing detail. Growing these radii repeatedly retires
    // mature neighbors, then zooming in must recreate them with different lighting histories.
    // Keep a world-space footprint until a substantial zoom-in actually requires finer coverage.
    if (target_radius < surfel._radius * kRadiusRefineThreshold)
        surfel._radius = target_radius;
    _SurfelPoolWrite[surfel_index] = surfel;
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

uint HashPixel(uint2 pixel)
{
    uint hash = pixel.x * 0x8da6b343u ^ pixel.y * 0xd8163841u;
    hash ^= hash >> 13u;
    hash *= 0x85ebca6bu;
    hash ^= hash >> 16u;
    return hash;
}

float ReadBlueNoise(uint2 pixel)
{
    uint width;
    uint height;
    _BlueNoiseTexture.GetDimensions(width, height);
    uint2 noise_pixel = pixel % max(uint2(width, height), 1u);
    return _BlueNoiseTexture.Load(int3(noise_pixel, 0)).r;
}

bool ConflictsWithSurface(float3 lhs_position, float3 lhs_normal, float lhs_radius,
                          float3 rhs_position, float3 rhs_normal, float rhs_radius)
{
    if (dot(lhs_normal, rhs_normal) < _SpawnParams.y)
        return false;

    float3 delta = lhs_position - rhs_position;
    float plane_distance = abs(dot(delta, rhs_normal));
    float max_radius = max(lhs_radius, rhs_radius);
    if (plane_distance > max_radius * 0.5f)
        return false;

    float tangent_distance_sq = max(dot(delta, delta) - plane_distance * plane_distance, 0.0f);
    float spacing_radius = max_radius * _SpawnParams.x;
    return tangent_distance_sq < spacing_radius * spacing_radius;
}

bool ConflictsWithSurfel(SurfelCandidate candidate, SurfelData surfel)
{
    return ConflictsWithSurface(candidate._position, candidate._normal, candidate._radius,
                                surfel._position, surfel._normal, surfel._radius);
}

bool ConflictsWithSurfels(SurfelData lhs, SurfelData rhs)
{
    return ConflictsWithSurface(lhs._position, lhs._normal, lhs._radius,
                                rhs._position, rhs._normal, rhs._radius);
}

bool HasHigherCandidateRank(uint priority, uint index, uint other_priority, uint other_index)
{
    return priority > other_priority || (priority == other_priority && index < other_index);
}

void GetConflictCellBounds(SurfelCandidate candidate, uint level, out int3 min_cell, out int3 max_cell)
{
    // Bound center distance within this cascade, including its largest configured surfel radius.
    float distance_scale = sqrt(_SpawnParams.x * _SpawnParams.x + 0.25f);
    float cell_size = _GridInfo[level].w;
    float search_radius = max(candidate._radius, cell_size * _SurfelParams.w) * distance_scale;
    float3 relative_position = candidate._position - _GridInfo[level].xyz;
    min_cell = max((int3)floor((relative_position - search_radius) / cell_size), int3(0, 0, 0));
    max_cell = min((int3)floor((relative_position + search_radius) / cell_size),
                   int3(_GridResolution - 1u, _GridResolution - 1u, _GridResolution - 1u));
}

bool ConflictsWithExistingSurfel(SurfelCandidate candidate)
{
    // A retained sample may be indexed in a different cascade than today's candidate. Reject
    // redundant samples across all levels, but permit refinement of a genuinely oversized donor.
    [loop]
    for (uint level = 0u; level < _CascadeCount; ++level)
    {
        int3 min_cell;
        int3 max_cell;
        GetConflictCellBounds(candidate, level, min_cell, max_cell);
        if (any(min_cell > max_cell))
            continue;

        [loop]
        for (int z = min_cell.z; z <= max_cell.z; ++z)
        {
            [loop]
            for (int y = min_cell.y; y <= max_cell.y; ++y)
            {
                [loop]
                for (int x = min_cell.x; x <= max_cell.x; ++x)
                {
                    SurfelGridCell grid_cell = _GridCellsRead[GetCellIndex(level, int3(x, y, z))];
                    for (uint index = 0u; index < grid_cell._count; ++index)
                    {
                        uint surfel_index = _GridIndicesRead[grid_cell._offset + index];
                        if (surfel_index >= _MaxSurfelCount)
                            continue;

                        SurfelData surfel = _SurfelPool[surfel_index];
                        if (!IsValidSurfel(surfel) || (surfel._flags & kSurfelRetiring) != 0u ||
                            surfel._cascade_level != level)
                            continue;

                        if (candidate._radius < surfel._radius * kRadiusRefineThreshold)
                            continue;
                        if (ConflictsWithSurfel(candidate, surfel))
                            return true;
                    }
                }
            }
        }
    }
    return false;
}

bool ConflictsWithHigherRankedWinner(SurfelCandidate candidate, uint candidate_index)
{
    // Claim has finished. Keep this winner set immutable throughout Commit, including budget rejection.
    uint level = min(candidate._cascade_level, _CascadeCount - 1u);
    int3 min_cell;
    int3 max_cell;
    GetConflictCellBounds(candidate, level, min_cell, max_cell);
    if (any(min_cell > max_cell))
        return false;

    [loop]
    for (int z = min_cell.z; z <= max_cell.z; ++z)
    {
        [loop]
        for (int y = min_cell.y; y <= max_cell.y; ++y)
        {
            [loop]
            for (int x = min_cell.x; x <= max_cell.x; ++x)
            {
                uint winner = _SpawnOccupancy[GetCellIndex(level, int3(x, y, z))];
                if (winner == 0u || winner == candidate_index + 1u)
                    continue;
                uint winner_index = winner - 1u;
                SurfelCandidate neighbor = _SurfelCandidates[winner_index];
                if (neighbor._cascade_level != level ||
                    !HasHigherCandidateRank(neighbor._priority, winner_index, candidate._priority, candidate_index))
                    continue;

                // Differing normals make the plane test directional; reject if either direction conflicts.
                if (ConflictsWithSurface(candidate._position, candidate._normal, candidate._radius,
                                         neighbor._position, neighbor._normal, neighbor._radius) ||
                    ConflictsWithSurface(neighbor._position, neighbor._normal, neighbor._radius,
                                         candidate._position, candidate._normal, candidate._radius))
                    return true;
            }
        }
    }
    return false;
}

bool HasHigherMigrationRank(uint other_index, SurfelData other, uint self_index, SurfelData self)
{
    const SurfelLightingData other_lighting = _SurfelLighting[other_index];
    const SurfelLightingData self_lighting = _SurfelLighting[self_index];
    const uint other_maturity = (other_lighting._flags & kSurfelLightingValid) != 0u
        ? other_lighting._sample_count : 0u;
    const uint self_maturity = (self_lighting._flags & kSurfelLightingValid) != 0u
        ? self_lighting._sample_count : 0u;
    if (other_maturity != self_maturity)
        return other_maturity > self_maturity;

    bool other_needs_resolve = (other._flags & kSurfelNeedsOverlapResolve) != 0u;
    bool self_needs_resolve = (self._flags & kSurfelNeedsOverlapResolve) != 0u;
    if (other_needs_resolve != self_needs_resolve)
        return !other_needs_resolve;

    return other_index < self_index;
}

void GetSurfelConflictCellBounds(SurfelData surfel, uint level, out int3 min_cell, out int3 max_cell)
{
    float cell_size = _GridInfo[level].w;
    float distance_scale = sqrt(_SpawnParams.x * _SpawnParams.x + 0.25f);
    float search_radius = max(surfel._radius, cell_size * _SurfelParams.w) * distance_scale;
    float3 relative_position = surfel._position - _GridInfo[level].xyz;
    min_cell = max((int3)floor((relative_position - search_radius) / cell_size), int3(0, 0, 0));
    max_cell = min((int3)floor((relative_position + search_radius) / cell_size),
                   int3(_GridResolution - 1u, _GridResolution - 1u, _GridResolution - 1u));
}

bool HasBetterOverlappingSurfel(uint surfel_index, SurfelData surfel)
{
    uint level = min(surfel._cascade_level, _CascadeCount - 1u);
    int3 min_cell;
    int3 max_cell;
    GetSurfelConflictCellBounds(surfel, level, min_cell, max_cell);
    if (any(min_cell > max_cell))
        return false;

    [loop]
    for (int z = min_cell.z; z <= max_cell.z; ++z)
    {
        [loop]
        for (int y = min_cell.y; y <= max_cell.y; ++y)
        {
            [loop]
            for (int x = min_cell.x; x <= max_cell.x; ++x)
            {
                SurfelGridCell grid_cell = _GridCellsRead[GetCellIndex(level, int3(x, y, z))];
                for (uint index = 0u; index < grid_cell._count; ++index)
                {
                    uint other_index = _GridIndicesRead[grid_cell._offset + index];
                    if (other_index == surfel_index || other_index >= _MaxSurfelCount)
                        continue;

                    SurfelData other = _SurfelPool[other_index];
                    if (!IsValidSurfel(other) || (other._flags & kSurfelRetiring) != 0u ||
                        other._cascade_level != level ||
                        !ConflictsWithSurfels(surfel, other))
                    {
                        continue;
                    }

                    if (HasHigherMigrationRank(other_index, other, surfel_index, surfel))
                        return true;
                }
            }
        }
    }

    return false;
}

[numthreads(64, 1, 1)]
void SurfelGridClearCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint cell_index = dispatch_thread_id.x;
    if (cell_index >= GetCellCount())
        return;

    _GridCells[cell_index]._count = 0u;
    _GridWriteCounters[cell_index] = 0u;
}

[numthreads(64, 1, 1)]
void SurfelFrameStateClearCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint index = dispatch_thread_id.x;
    if (index < 14u)
        _SurfelFrameState[index] = 0u;
}

[numthreads(1, 1, 1)]
void SurfelPrepareDispatchCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x != 0u)
        return;

    uint surfel_count = min(_SurfelStateRead[kSurfelStateHighWatermark], _MaxSurfelCount);
    _SurfelDispatchArgs[0]._thread_group_num_x = (surfel_count + 63u) / 64u;
    _SurfelDispatchArgs[0]._thread_group_num_y = 1u;
    _SurfelDispatchArgs[0]._thread_group_num_z = 1u;
}

[numthreads(64, 1, 1)]
void SurfelFlushDeferredFreeListCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint deferred_index = dispatch_thread_id.x;
    uint deferred_count = min(_SurfelState[kSurfelStateDeferredFreeCount], _MaxSurfelCount);
    if (deferred_index >= deferred_count)
        return;

    const uint surfel_index = _SurfelDeferredFreeListRead[deferred_index];
    if (surfel_index >= _MaxSurfelCount || IsValidSurfel(_SurfelPool[surfel_index]))
        return;

    uint free_index;
    InterlockedAdd(_SurfelState[kSurfelStateFreeCount], 1u, free_index);
    if (free_index < _MaxSurfelCount)
        _SurfelFreeList[free_index] = _SurfelDeferredFreeListRead[deferred_index];
}

[numthreads(1, 1, 1)]
void SurfelResetDeferredFreeCountCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x == 0u)
        _SurfelState[kSurfelStateDeferredFreeCount] = 0u;
}

[numthreads(1, 1, 1)]
void SurfelActiveListRemoveCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint active_count = min(_SurfelState[kSurfelStateActiveListCount], _MaxSurfelCount);
    const uint deferred_count = min(_SurfelState[kSurfelStateDeferredFreeCount], _MaxSurfelCount);
    [loop]
    for (uint i = 0u; i < deferred_count; ++i)
    {
        const uint surfel_index = _SurfelDeferredFreeListRead[i];
        if (surfel_index >= _MaxSurfelCount || IsValidSurfel(_SurfelPool[surfel_index]))
            continue;

        const uint list_index = _SurfelActiveListIndices[surfel_index];
        if (list_index >= active_count)
            continue;

        const uint last_index = active_count - 1u;
        const uint last_surfel_index = _ActiveSurfelIndices[last_index];
        _ActiveSurfelIndices[list_index] = last_surfel_index;
        _SurfelActiveListIndices[last_surfel_index] = list_index;
        _SurfelActiveListIndices[surfel_index] = 0xffffffffu;
        --active_count;
    }
    _SurfelState[kSurfelStateActiveListCount] = active_count;
}

[numthreads(64, 1, 1)]
void SurfelMaintenanceCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint surfel_index = dispatch_thread_id.x;
    uint high_watermark = min(_SurfelState[kSurfelStateHighWatermark], _MaxSurfelCount);
    if (surfel_index >= high_watermark)
        return;

    SurfelData surfel = _SurfelPoolWrite[surfel_index];
    if (!IsValidSurfel(surfel))
        return;

    // Keep retiring geometry and lighting stable until its query contribution reaches zero.
    if ((surfel._flags & kSurfelRetiring) != 0u)
    {
        if (_FrameIndex - surfel._last_touched_frame >= kSurfelTransitionFrames)
        {
            RecycleSurfelImmediate(surfel_index);
            InterlockedAdd(_SurfelFrameState[kSurfelFrameOverlapRecycleCount], 1u);
        }
        return;
    }

    surfel._flags &= ~(kSurfelNeedsOverlapResolve | kSurfelMigratedFiner | kSurfelMigratedCoarser);
    SurfelMigrationDecision decision = ComputeMigrationDecision(surfel);
    if (decision._mode == kMigrationRecycle)
    {
        _SurfelPoolWrite[surfel_index] = surfel;
        RecycleSurfelImmediate(surfel_index);
        return;
    }

    if (decision._mode == kMigrationFiner)
    {
        surfel._cascade_level = decision._target_cascade;
        surfel._radius = decision._target_radius;
        // Hysteresis can leave an existing sample in the destination cascade while a coarser
        // sample is spawned at the same position. Resolve their overlap after refinement too.
        surfel._flags |= kSurfelMigratedFiner | kSurfelNeedsOverlapResolve;
        _SurfelPoolWrite[surfel_index] = surfel;
        InterlockedAdd(_SurfelFrameState[kSurfelFrameMigratedFiner], 1u);
        return;
    }

    if (decision._mode == kMigrationCoarser)
    {
        surfel._cascade_level = decision._target_cascade;
        surfel._radius = decision._target_radius;
        surfel._flags |= kSurfelMigratedCoarser | kSurfelNeedsOverlapResolve;
        _SurfelPoolWrite[surfel_index] = surfel;
        InterlockedAdd(_SurfelFrameState[kSurfelFrameMigratedCoarser], 1u);
        return;
    }

    UpdateSameCascadeRadius(surfel_index, surfel, decision._target_radius);
}

[numthreads(64, 1, 1)]
void SurfelGridCountCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint surfel_index = dispatch_thread_id.x;
    uint high_watermark = min(_SurfelStateRead[kSurfelStateHighWatermark], _MaxSurfelCount);
    if (surfel_index >= high_watermark)
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
    uint high_watermark = min(_SurfelStateRead[kSurfelStateHighWatermark], _MaxSurfelCount);
    if (surfel_index >= high_watermark)
        return;

    SurfelData surfel = _SurfelPool[surfel_index];
    if (!IsValidSurfel(surfel))
        return;

    ForEachOverlappingCell(surfel, min(surfel._cascade_level, _CascadeCount - 1u), surfel_index, true);
}

[numthreads(64, 1, 1)]
void SurfelMigrationResolveCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint surfel_index = dispatch_thread_id.x;
    if (surfel_index >= _MaxSurfelCount)
        return;

    SurfelData surfel = _SurfelPool[surfel_index];
    if (!IsValidSurfel(surfel) || (surfel._flags & kSurfelRetiring) != 0u ||
        (surfel._flags & kSurfelNeedsOverlapResolve) == 0u)
        return;

    if (HasBetterOverlappingSurfel(surfel_index, surfel))
    {
        uint deferred_index;
        InterlockedAdd(_SurfelState[kSurfelStateDeferredFreeCount], 1u, deferred_index);
        if (deferred_index < _MaxSurfelCount)
            _SurfelDeferredFreeList[deferred_index] = surfel_index;
    }
}

[numthreads(64, 1, 1)]
void SurfelMigrationRecycleCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint deferred_index = dispatch_thread_id.x;
    uint deferred_count = min(_SurfelState[kSurfelStateDeferredFreeCount], _MaxSurfelCount);
    if (deferred_index >= deferred_count)
        return;

    uint surfel_index = _SurfelDeferredFreeListRead[deferred_index];
    if (surfel_index >= _MaxSurfelCount)
        return;

    SurfelData surfel = _SurfelPoolWrite[surfel_index];
    if (!IsValidSurfel(surfel))
        return;

    // This list contains retirement requests as well as already expired slots. Only invalid
    // slots leave the active list or enter the free list; valid retirees remain queryable.
    surfel._retire_start_weight = SurfelContributionWeight(surfel, _FrameIndex);
    surfel._last_touched_frame = _FrameIndex;
    surfel._flags |= kSurfelRetiring;
    _SurfelPoolWrite[surfel_index] = surfel;
}

float ComputeSurfelCoverageWeight(float3 position, float3 normal, SurfelData surfel)
{
    if ((surfel._flags & kSurfelRetiring) != 0u)
        return 0.0f;
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
    SurfelGridCell grid_cell = _GridCellsRead[cell_index];
    float coverage = 0.0f;
    for (uint index = 0u; index < grid_cell._count; ++index)
    {
        uint surfel_index = _GridIndicesRead[grid_cell._offset + index];
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

groupshared uint s_priority[256];
groupshared uint s_pixel_index[256];

[numthreads(16, 16, 1)]
void SurfelCandidateCS(uint3 dispatch_thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID)
{
    uint local_index = group_thread_id.y * 16u + group_thread_id.x;
    uint2 pixel_index = dispatch_thread_id.xy;
    bool in_bounds = pixel_index.x < (uint)_ScreenParams.z && pixel_index.y < (uint)_ScreenParams.w;
    float coverage = in_bounds ? _CoverageTexture[pixel_index] : kInvalidCoverage;
    uint priority = 0u;
    if (in_bounds && coverage < _CameraParams.z)
    {
        float coverage_deficit = saturate((_CameraParams.z - coverage) / max(_CameraParams.z, 1e-5f));
        float blue_noise = ReadBlueNoise(pixel_index) * _SpawnParams.z;
        uint coverage_key = (uint)(coverage_deficit * 65535.0f);
        uint noise_key = ((uint)(saturate(blue_noise) * 255.0f) << 8u) | (HashPixel(pixel_index) & 0xffu);
        priority = max((coverage_key << 16u) | noise_key, 1u);
    }

    s_priority[local_index] = priority;
    s_pixel_index[local_index] = in_bounds ? pixel_index.y * (uint)_ScreenParams.z + pixel_index.x : 0u;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 128u; stride > 0u; stride >>= 1u)
    {
        if (local_index < stride && s_priority[local_index + stride] > s_priority[local_index])
        {
            s_priority[local_index] = s_priority[local_index + stride];
            s_pixel_index[local_index] = s_pixel_index[local_index + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (local_index != 0u || s_priority[0] == 0u)
        return;

    uint width = (uint)_ScreenParams.z;
    uint2 candidate_pixel = uint2(s_pixel_index[0] % width, s_pixel_index[0] / width);
    float candidate_coverage = _CoverageTexture[candidate_pixel];
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

    uint candidate_index;
    InterlockedAdd(_CandidateState[0], 1u, candidate_index);
    if (candidate_index >= _CandidateCapacity)
    {
        InterlockedAdd(_CandidateState[0], 0xffffffffu);
        return;
    }

    SurfelCandidate candidate = (SurfelCandidate)0;
    candidate._position = world_position;
    candidate._radius = desired_radius;
    candidate._normal = normalize(world_normal);
    candidate._coverage = candidate_coverage;
    candidate._cascade_level = cascade_level;
    candidate._pixel_x = candidate_pixel.x;
    candidate._pixel_y = candidate_pixel.y;
    candidate._priority = s_priority[0];
    _SurfelCandidates[candidate_index] = candidate;
}

[numthreads(64, 1, 1)]
void SurfelCandidateClearCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint index = dispatch_thread_id.x;
    if (index < GetCellCount())
        _SpawnOccupancy[index] = 0u;
    if (index < 6u)
        _CandidateState[index] = 0u;
}

bool InheritSurfelLighting(SurfelCandidate candidate, out float3 irradiance, out uint inheritance_depth)
{
    irradiance = 0.0f.xxx;
    inheritance_depth = 0u;
    // Claim runs before Commit: pool, grid and lighting are immutable throughout this search.
    [loop]
    for (uint level = candidate._cascade_level; level < _CascadeCount; ++level)
    {
        const float4 grid_info = _GridInfo[level];
        const int3 center_cell = (int3)floor((candidate._position - grid_info.xyz) / grid_info.w);
        const float support_radius = 2.0f * grid_info.w;
        const float plane_limit = max(0.001f, grid_info.w * 0.05f);
        float3 irradiance_sum = 0.0f.xxx;
        float weight_sum = 0.0f;
        uint donor_depth = 0u;
        [loop]
        for (int z = -2; z <= 2; ++z)
        {
            [loop]
            for (int y = -2; y <= 2; ++y)
            {
                [loop]
                for (int x = -2; x <= 2; ++x)
                {
                    const int3 cell = center_cell + int3(x, y, z);
                    if (!IsInsideCell(cell))
                        continue;
                    const SurfelGridCell grid_cell = _GridCellsRead[GetCellIndex(level, cell)];
                    [loop]
                    for (uint i = 0u; i < grid_cell._count; ++i)
                    {
                        const uint index = _GridIndicesRead[grid_cell._offset + i];
                        if (index >= _MaxSurfelCount)
                            continue;
                        const SurfelData surfel = _SurfelPool[index];
                        if (!IsValidSurfel(surfel) || surfel._cascade_level != level ||
                            any((int3)floor((surfel._position - grid_info.xyz) / grid_info.w) != cell))
                            continue;
                        const SurfelLightingData lighting = _SurfelLighting[index];
                        if ((lighting._flags & kSurfelLightingValid) == 0u)
                            continue;
                        const bool mature = lighting._sample_count >= kSurfelMatureSampleCount;
                        // Relay only a trusted seed, never an immature Monte Carlo estimate.
                        if (!mature && ((lighting._flags & kSurfelLightingInherited) == 0u ||
                            lighting._inheritance_depth >= kSurfelMaxInheritanceDepth))
                            continue;
                        const float3 delta = candidate._position - surfel._position;
                        if (dot(candidate._normal, surfel._normal) < 0.95f ||
                            abs(dot(delta, candidate._normal)) > plane_limit ||
                            abs(dot(delta, surfel._normal)) > plane_limit)
                            continue;
                        const float relative_distance = length(delta) / support_radius;
                        const float falloff = saturate(1.0f - relative_distance);
                        const float weight = falloff * falloff * falloff * falloff *
                                             (1.0f + 4.0f * relative_distance);
                        if (weight <= 1e-5f)
                            continue;
                        irradiance_sum += (mature ? lighting._sampled_irradiance :
                                          lighting._inherited_irradiance) * weight;
                        donor_depth = max(donor_depth, mature ? 0u : lighting._inheritance_depth);
                        weight_sum += weight;
                    }
                }
            }
        }
        if (weight_sum >= 0.05f)
        {
            irradiance = irradiance_sum / weight_sum;
            inheritance_depth = donor_depth + 1u;
            return true;
        }
    }
    return false;
}

[numthreads(64, 1, 1)]
void SurfelCandidateClaimCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint candidate_index = dispatch_thread_id.x;
    uint candidate_count = min(_CandidateState[0], _CandidateCapacity);
    if (candidate_index >= candidate_count)
        return;

    SurfelCandidate candidate = _SurfelCandidates[candidate_index];
    if (ConflictsWithExistingSurfel(candidate))
    {
        InterlockedAdd(_CandidateState[2], 1u);
        _SurfelCandidates[candidate_index]._priority = 0u;
        return;
    }

    int3 cell = (int3)floor((candidate._position - _GridInfo[candidate._cascade_level].xyz) /
                            _GridInfo[candidate._cascade_level].w);
    if (!IsInsideCell(cell))
    {
        InterlockedAdd(_CandidateState[2], 1u);
        _SurfelCandidates[candidate_index]._priority = 0u;
        return;
    }

    float3 inherited_irradiance;
    uint inheritance_depth;
    InheritSurfelLighting(candidate, inherited_irradiance, inheritance_depth);
    _SurfelCandidates[candidate_index]._inherited_irradiance = inherited_irradiance;
    _SurfelCandidates[candidate_index]._inheritance_depth = inheritance_depth;

    uint occupancy_index = GetCellIndex(candidate._cascade_level, cell);
    // Store candidate_index + 1 (zero means empty), preserving priority and breaking ties by index.
    uint winner;
    InterlockedCompareExchange(_SpawnOccupancy[occupancy_index], 0u, 0u, winner);
    [allow_uav_condition]
    while (true)
    {
        if (winner != 0u)
        {
            uint winner_index = winner - 1u;
            uint winner_priority = _SurfelCandidates[winner_index]._priority;
            if (winner_index == candidate_index ||
                HasHigherCandidateRank(winner_priority, winner_index, candidate._priority, candidate_index))
                break;
        }

        uint previous_winner;
        InterlockedCompareExchange(_SpawnOccupancy[occupancy_index], winner, candidate_index + 1u, previous_winner);
        if (previous_winner == winner)
            break;
        winner = previous_winner;
    }
}

[numthreads(64, 1, 1)]
void SurfelCandidateCommitCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint candidate_index = dispatch_thread_id.x;
    uint candidate_count = min(_CandidateState[0], _CandidateCapacity);
    if (candidate_index >= candidate_count)
        return;

    SurfelCandidate candidate = _SurfelCandidates[candidate_index];
    if (candidate._priority == 0u)
        return;
    int3 cell = (int3)floor((candidate._position - _GridInfo[candidate._cascade_level].xyz) /
                            _GridInfo[candidate._cascade_level].w);
    if (!IsInsideCell(cell))
    {
        InterlockedAdd(_CandidateState[3], 1u);
        return;
    }
    if (_SpawnOccupancy[GetCellIndex(candidate._cascade_level, cell)] != candidate_index + 1u)
    {
        InterlockedAdd(_CandidateState[3], 1u);
        return;
    }
    if (ConflictsWithHigherRankedWinner(candidate, candidate_index))
    {
        InterlockedAdd(_CandidateState[3], 1u);
        return;
    }

    uint budget_index;
    InterlockedAdd(_CandidateState[5], 1u, budget_index);
    if (budget_index >= _MaxSpawnPerFrame)
    {
        InterlockedAdd(_CandidateState[4], 1u);
        return;
    }

    uint surfel_index;
    if (!AllocateSurfelSlot(surfel_index))
    {
        InterlockedAdd(_SurfelState[kSurfelStateOverflowCount], 1u);
        InterlockedAdd(_SurfelFrameState[kSurfelFrameOverflowCount], 1u);
        return;
    }

    SurfelData surfel;
    surfel._position = candidate._position;
    surfel._radius = candidate._radius;
    surfel._normal = candidate._normal;
    surfel._flags = kSurfelValid;
    surfel._cascade_level = candidate._cascade_level;
    surfel._last_touched_frame = _FrameIndex;
    surfel._object_id = 0u;
    surfel._retire_start_weight = 0.0f;
    surfel._spawn_pixel = uint2(candidate._pixel_x, candidate._pixel_y);
    _SurfelPoolWrite[surfel_index] = surfel;
    SurfelLightingData lighting = (SurfelLightingData)0;
    lighting._last_update_frame = _FrameIndex;
    if (candidate._inheritance_depth != 0u)
    {
        lighting._irradiance = candidate._inherited_irradiance;
        lighting._inherited_irradiance = candidate._inherited_irradiance;
        lighting._inheritance_depth = candidate._inheritance_depth;
        lighting._flags = kSurfelLightingValid | kSurfelLightingInherited;
    }
    _SurfelLightingWrite[surfel_index] = lighting;
    uint active_list_index;
    InterlockedAdd(_SurfelState[kSurfelStateActiveListCount], 1u, active_list_index);
    if (active_list_index < _MaxSurfelCount)
    {
        _ActiveSurfelIndices[active_list_index] = surfel_index;
        _SurfelActiveListIndices[surfel_index] = active_list_index;
    }
    InterlockedAdd(_SurfelState[kSurfelStateActiveCount], 1u);
    InterlockedAdd(_SurfelFrameState[kSurfelFrameSpawnCount], 1u);
    InterlockedAdd(_CandidateState[1], 1u);
}

[numthreads(64, 1, 1)]
void SurfelMigrationFinalizeCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint surfel_index = dispatch_thread_id.x;
    uint high_watermark = min(_SurfelStateRead[kSurfelStateHighWatermark], _MaxSurfelCount);
    if (surfel_index >= high_watermark)
        return;

    SurfelData surfel = _SurfelPoolWrite[surfel_index];
    if (!IsValidSurfel(surfel))
        return;

    surfel._flags &= ~(kSurfelNeedsOverlapResolve | kSurfelMigratedFiner | kSurfelMigratedCoarser);
    _SurfelPoolWrite[surfel_index] = surfel;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void SurfelPrepareDrawArgsCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x != 0u)
        return;

    _DrawArgs[0]._vertex_count_per_instance = 48u;
    _DrawArgs[0]._instance_count = min(_SurfelStateRead[kSurfelStateHighWatermark], _MaxSurfelCount);
    _DrawArgs[0]._start_vertex_location = 0u;
    _DrawArgs[0]._start_instance_location = 0u;
}
