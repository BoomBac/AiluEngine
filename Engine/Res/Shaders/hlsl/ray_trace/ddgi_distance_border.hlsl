#pragma kernel ProbeDistanceBorder

#include "ddgi_common.hlsli"

RWTexture2D<float2> _distance_texture;

CBUFFER_START(ComputeCB)
    uint _probe_count_x;
    uint _probe_count_y;
    uint _probe_count_z;
    uint _ddgi_padding;
CBUFFER_END

[numthreads(16, 16, 1)]
void ProbeDistanceBorder(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    const uint3 probe_count = uint3(_probe_count_x, _probe_count_y, _probe_count_z);
    const uint probe_index = group_id.x;
    const uint probe_count_total = probe_count.x * probe_count.y * probe_count.z;
    if (probe_index >= probe_count_total)
        return;

    const uint2 local_pixel = group_thread_id.xy;
    const uint last_pixel = kDistancePhysicalResolution - 1u;
    if (local_pixel.x > 0u && local_pixel.x < last_pixel && local_pixel.y > 0u && local_pixel.y < last_pixel)
        return;

    const uint3 probe_coord = DDGIProbeCoord(probe_index, probe_count);
    const uint2 tile_origin = uint2(probe_coord.x, probe_coord.y + probe_coord.z * probe_count.y) *
                              kDistancePhysicalResolution;

    uint2 source_pixel;
    if (local_pixel.x == 0u && local_pixel.y == 0u)
        source_pixel = uint2(kDistanceInteriorResolution, kDistanceInteriorResolution);
    else if (local_pixel.x == last_pixel && local_pixel.y == 0u)
        source_pixel = uint2(kDistanceBorderSize, kDistanceInteriorResolution);
    else if (local_pixel.x == 0u && local_pixel.y == last_pixel)
        source_pixel = uint2(kDistanceInteriorResolution, kDistanceBorderSize);
    else if (local_pixel.x == last_pixel && local_pixel.y == last_pixel)
        source_pixel = uint2(kDistanceBorderSize, kDistanceBorderSize);
    else if (local_pixel.y == 0u)
        source_pixel = uint2(last_pixel - local_pixel.x, kDistanceBorderSize);
    else if (local_pixel.y == last_pixel)
        source_pixel = uint2(last_pixel - local_pixel.x, kDistanceInteriorResolution);
    else if (local_pixel.x == 0u)
        source_pixel = uint2(kDistanceBorderSize, last_pixel - local_pixel.y);
    else
        source_pixel = uint2(kDistanceInteriorResolution, last_pixel - local_pixel.y);

    const uint2 destination = tile_origin + local_pixel;
    const uint2 source = tile_origin + source_pixel;
    _distance_texture[destination] = _distance_texture[source];
}
