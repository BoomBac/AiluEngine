//info begin
//pass begin::
//name: ObjectBoxBumpSmoke
//vert: FullscreenVSMain
//pixel: PSMain
//Cull: Off
//ZTest: Always
//ZWrite: Off
//pass end::
//Properties
//{
// _HeightTex("Height",Texture2D) = "white"
// _PlaneOrigin("Origin",Vector) = (0,0,0,0)
// _PlaneU("Plane U",Vector) = (1,0,0,0)
// _PlaneV("Plane V",Vector) = (0,1,0,0)
// _PlaneNormal("Plane Normal",Vector) = (0,0,1,0)
// _HeightParameters("Height Parameters",Vector) = (0.25,0.01,0.5,1)
//}
//info end
#include "../Engine/Res/Shaders/hlsl/common.hlsli"
#include "../Engine/Res/Shaders/hlsl/input.hlsli"
#include "../Engine/Res/Shaders/hlsl/fullscreen_quad.hlsli"
#include "../Engine/Res/Shaders/hlsl/object_box_bump.hlsli"
Texture2D _HeightTex : register(t0);
PerMaterialCBufferBegin
float4 _PlaneOrigin;
float4 _PlaneU;
float4 _PlaneV;
float4 _PlaneNormal;
float4 _HeightParameters;
PerMaterialCBufferEnd
float3 ConvertBasis(float3 value) { return float3(value.x, value.z, -value.y); }
float4 PSMain(FullScreenPSInput input) : SV_TARGET
{
    StandardPSInput surface = (StandardPSInput)0;
    float3 position = _PlaneOrigin.xyz + input.uv.x * _PlaneU.xyz + (1.0 - input.uv.y) * _PlaneV.xyz;
    surface.object_pos = ConvertBasis(position);
    surface.object_normal = ConvertBasis(_PlaneNormal.xyz);
    surface.world_pos = surface.object_pos;
    surface.normal = surface.object_normal;
#ifdef VALIDATE_BOX_COLOR
    return float4(EvaluateObjectBoxColor(_HeightTex, g_LinearWrapSampler, surface, _HeightParameters.z, 0.0), 1.0);
#else
    float3 normal = EvaluateObjectBoxBump(_HeightTex, g_LinearWrapSampler, surface, _HeightParameters, 0.0);
    return float4(float3(normal.x, -normal.z, normal.y) * 0.5 + 0.5, 1.0);
#endif
}
