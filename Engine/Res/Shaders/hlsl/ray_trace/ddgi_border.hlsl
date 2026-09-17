#pragma kernel ProbeBorder

#include "ddgi_common.hlsli"

RWTexture2D<float4> _radiance_texture;

CBUFFER_START(ComputeCB)
    uint _probe_count_x;
    uint _probe_count_y;
    uint _probe_count_z;
    uint _ddgi_padding;
CBUFFER_END

// numthreads 必须等于 kRadiancePhysicalResolution（一个线程一个 texel，interior 的线程直接返回）。
[numthreads(16, 16, 1)]
void ProbeBorder(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    const uint3 probe_count = uint3(_probe_count_x, _probe_count_y, _probe_count_z);
    const uint probe_index = group_id.x;
    const uint probe_count_total = probe_count.x * probe_count.y * probe_count.z;
    if (probe_index >= probe_count_total)
        return;

    const uint2 local_pixel = group_thread_id.xy;
    const uint last_pixel = kRadiancePhysicalResolution - 1u;
    if (local_pixel.x > 0u && local_pixel.x < last_pixel && local_pixel.y > 0u && local_pixel.y < last_pixel)
        return;

    const uint3 probe_coord = DDGIProbeCoord(probe_index, probe_count);
    const uint2 tile_origin = uint2(probe_coord.x, probe_coord.y + probe_coord.z * probe_count.y) *
                              kRadiancePhysicalResolution;

    uint2 source_pixel;
    if (local_pixel.x == 0u && local_pixel.y == 0u)
        source_pixel = uint2(kRadianceInteriorResolution, kRadianceInteriorResolution);
    else if (local_pixel.x == last_pixel && local_pixel.y == 0u)
        source_pixel = uint2(kRadianceBorderSize, kRadianceInteriorResolution);
    else if (local_pixel.x == 0u && local_pixel.y == last_pixel)
        source_pixel = uint2(kRadianceInteriorResolution, kRadianceBorderSize);
    else if (local_pixel.x == last_pixel && local_pixel.y == last_pixel)
        source_pixel = uint2(kRadianceBorderSize, kRadianceBorderSize);
    else if (local_pixel.y == 0u)
        source_pixel = uint2(last_pixel - local_pixel.x, kRadianceBorderSize);
    else if (local_pixel.y == last_pixel)
        source_pixel = uint2(last_pixel - local_pixel.x, kRadianceInteriorResolution);
    else if (local_pixel.x == 0u)
        source_pixel = uint2(kRadianceBorderSize, last_pixel - local_pixel.y);
    else
        source_pixel = uint2(kRadianceInteriorResolution, last_pixel - local_pixel.y);

    const uint2 destination = tile_origin + local_pixel;
    const uint2 source = tile_origin + source_pixel;
    _radiance_texture[destination] = _radiance_texture[source];
}
