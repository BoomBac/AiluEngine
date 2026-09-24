//info begin
//pass begin::
//name: SurfelTileGrid
//vert: FullscreenVSMain
//pixel: PSMain
//Cull: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//ZTest: Always
//ZWrite: Off
//pass end::
//info end

#include "fullscreen_quad.hlsli"

float4 PSMain(FullScreenPSInput input) : SV_TARGET
{
    float2 tile_position = fmod(input.position.xy, 16.0f);
    if (tile_position.x >= 1.0f && tile_position.y >= 1.0f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    return float4(0.85f, 0.1f, 1.0f, 0.85f);
}
