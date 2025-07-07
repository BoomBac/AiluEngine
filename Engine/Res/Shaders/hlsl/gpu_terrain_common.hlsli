#ifndef __GPU_TERRAIN_H__
#define __GPU_TERRAIN_H__

struct RenderPatch
{
    float3 _position;
    uint lod;
    float3 _aabb_ext;
    uint _sector;
};

uint PackEdgeAndLOD(uint lod, bool is_edge)
{
    return (lod << 1) | (is_edge ? 1 : 0);
}
void UnPackEdgeAndLOD(uint packed,out uint lod,out bool is_edge)
{
    is_edge = (packed & 1) != 0; 
    lod = packed >> 1;
}

uint PackSectorIndex(uint x,uint y)
{
    return (x & 0xFFFFu) | ((y & 0xFFFFu) << 16);
}
uint2 UnpackSectorIndex(uint sector)
{
    uint2 xy;
    xy.x = sector & 0xFFFFu;
    xy.y = (sector >> 16) & 0xFFFFu;
    return xy;
}
uint PackSectorLOD(uint4 ltrb)
{
    return (ltrb.r << 24) | (ltrb.g << 16) | (ltrb.b << 8) | ltrb.a;
}
//uint4 ltrb
uint4 UnpackSectorLOD(uint sector)
{
    uint4 ltrb;
    ltrb.x = (sector >> 24) & 0xFFu;
    ltrb.y = (sector >> 16) & 0xFFu;
    ltrb.z = (sector >> 8)  & 0xFFu;
    ltrb.w = sector         & 0xFFu;
    return ltrb;
}

#endif //__GPU_TERRAIN_H__