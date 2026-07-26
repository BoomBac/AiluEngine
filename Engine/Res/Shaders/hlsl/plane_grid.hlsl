//info bein
//pass begin::
//name: plane_grid
//vert: VSMain
//pixel: PSMain
//Cull: Off
//ZWrite: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//Fill: Solid
//pass end::
//info end

#include "cbuffer.hlsli"
#include "common.hlsli"
//这个shader中指定的pso状态不会应用，其在D3DContext line124设置。
struct VSInput
{
	float3 position : POSITION;
};

struct PSInput
{
	float4 position : SV_POSITION;
    float3 world_pos : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
};

PerMaterialCBufferBegin
    float _grid_alpha;
    float4 _grid_axis_mode;
PerMaterialCBufferEnd

PSInput VSMain(VSInput v)
{
	PSInput result;
    float3 wpos = TransformObjectToWorld(v.position);
    int axis_mode = (int)_grid_axis_mode.x;
    if (axis_mode == 2)
    {
        wpos.y += ((int)_CameraPos.y / 10) * 10;
        wpos.z += ((int)_CameraPos.z / 10) * 10;
    }
    else if (axis_mode == 1)
    {
        wpos.x += ((int)_CameraPos.x / 10) * 10;
        wpos.y += ((int)_CameraPos.y / 10) * 10;
    }
    else
    {
        wpos.x += ((int)_CameraPos.x / 10) * 10;
        wpos.z += ((int)_CameraPos.z / 10) * 10;
    }
	result.position = TransformWorldToHClipNoJitter(wpos);
	result.world_pos = wpos;
    result.world_normal = float3(axis_mode, 0.0, 0.0);
	return result;
}

float2 GetGridCoord(float3 world_pos, float3 world_normal)
{
    int axis_mode = (int)world_normal.x;
    if (axis_mode == 2)
        return world_pos.yz;
    if (axis_mode == 1)
        return world_pos.xy;
    return world_pos.xz;
}

float GetCameraPlaneDistance(float3 world_pos, float3 world_normal)
{
    int axis_mode = (int)world_normal.x;
    if (axis_mode == 2)
        return abs(GetCameraPositionWS().x - world_pos.x);
    if (axis_mode == 1)
        return abs(GetCameraPositionWS().z - world_pos.z);
    return abs(GetCameraPositionWS().y - world_pos.y);
}

float4 Grid(float3 fragPos3D, float3 world_normal, float scale,float axis_scale) 
{
    float2 plane_coord = GetGridCoord(fragPos3D, world_normal);
    float2 coord = plane_coord * scale; // use the scale variable to set the distance between the lines
    float2 derivative = fwidth(coord);
    float2 grid = abs(frac(coord - 0.5) - 0.5) / derivative;
    float a = min(grid.x, grid.y);
    float minimumz = min(derivative.y, 1) * axis_scale;
    float minimumx = min(derivative.x, 1) * axis_scale;
    float4 color = float4(0.2, 0.2, 0.2, 1.0 - min(a, 1.0));
    int axis_mode = (int)world_normal.x;
    float3 axis0_color = axis_mode == 1 ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
    float3 axis1_color = axis_mode == 2 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    if(plane_coord.x > -0.1 * minimumx && plane_coord.x < 0.1 * minimumx)
    {
        color.rgb = axis0_color;
        color.a *= 4.0;
    }
    if(plane_coord.y > -0.1 * minimumz && plane_coord.y < 0.1 * minimumz)
    {
        color.rgb = axis1_color;
        color.a *= 4.0;
    }
    return color;
}

const static float _GridWidth = 10.0f;
const static float _GridAlpha = 0.25f;

float4 PSMain(PSInput i) : SV_TARGET
{
    float camera_height = GetCameraPlaneDistance(i.world_pos, i.world_normal);
    float vis_small_grid = saturate(camera_height / 30.0f);
    float vis_mid_grid =   saturate(camera_height / 100.0f);
    float4 grid_s = Grid(i.world_pos, i.world_normal,1,_GridWidth);
    float4 grid_m = Grid(i.world_pos, i.world_normal,0.1,_GridWidth * 10);
    float4 grid_l = Grid(i.world_pos, i.world_normal,0.01,_GridWidth * 100);
    float4 grid_color = lerp(lerp(grid_s,grid_m,vis_small_grid),grid_l,vis_mid_grid);
    grid_color.a *= lerp(_GridAlpha,0.0,saturate(distance(GetCameraPositionWS(),i.world_pos) / lerp(lerp(30,200,vis_small_grid),3000,vis_mid_grid * vis_mid_grid)));
    grid_color.a *= _grid_alpha;
    return grid_color;
}
