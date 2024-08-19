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
};

PSInput VSMain(VSInput v)
{
	PSInput result;
    float3 wpos = TransformToWorldSpace(v.position);
    wpos.x += ((int)_CameraPos.x / 10) * 10;
    wpos.z += ((int)_CameraPos.z / 10) * 10;
	result.position = TransformFromWorldToClipSpace(wpos);
	result.world_pos = wpos;
	return result;
}

float4 Grid(float3 fragPos3D, float scale,float axis_scale) 
{
    float2 coord = fragPos3D.xz * scale; // use the scale variable to set the distance between the lines
    float2 derivative = fwidth(coord);
    float2 grid = abs(frac(coord - 0.5) - 0.5) / derivative;
    float a = min(grid.x, grid.y);
    float minimumz = min(derivative.y, 1) * axis_scale;
    float minimumx = min(derivative.x, 1) * axis_scale;
    float4 color = float4(0.2, 0.2, 0.2, 1.0 - min(a, 1.0));
    // z axis
    if(fragPos3D.x > -0.1 * minimumx && fragPos3D.x < 0.1 * minimumx)
    {
        color.z = 1.0;
        color.a *= 4.0;
    }
    // x axis
    if(fragPos3D.z > -0.1 * minimumz && fragPos3D.z < 0.1 * minimumz)
    {
        color.x = 1.0;
        color.a *= 4.0;
    }
    return color;
}

const static float _GridWidth = 10.0f;
const static float _GridAlpha = 0.25f;

float4 PSMain(PSInput i) : SV_TARGET
{
    float camera_height = GetCameraPositionWS().y;
    float vis_small_grid = saturate(camera_height / 30.0f);
    float vis_mid_grid =   saturate(camera_height / 100.0f);
    float4 grid_s = Grid(i.world_pos,1,_GridWidth);
    float4 grid_m = Grid(i.world_pos,0.1,_GridWidth * 10);
    float4 grid_l = Grid(i.world_pos,0.01,_GridWidth * 100);
    float4 grid_color = lerp(lerp(grid_s,grid_m,vis_small_grid),grid_l,vis_mid_grid);
    grid_color.a *= lerp(_GridAlpha,0.0,saturate(distance(GetCameraPositionWS(),i.world_pos) / lerp(lerp(30,200,vis_small_grid),3000,vis_mid_grid * vis_mid_grid)));
    return grid_color;
}