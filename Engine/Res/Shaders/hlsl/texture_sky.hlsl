//info begin
//pass begin::
//name: TextureSky
//vert: VSMain
//pixel: PSMain
//Cull: Front
//ZTest: Equal
//ZWrite: Off
//ZTest: Equal
//ZWrite: Off
//Queue: Opaque
//Stencil: {Ref:0,Comp:Equal,Pass:Keep}
//pass end::
//Properties
//{
//  _EnvironmentSky("Environment",Texture2D) = white
//  _EnvironmentIntensity("Intensity",Range(0,8)) = 1
//  _EnvironmentRotation("Rotation",Range(-6.283,6.283)) = 0
//}
//info end

#include "common.hlsli"
#include "constants.hlsli"

TextureCube _EnvironmentSky : register(t0);
Texture2D _EnvironmentBackground : register(t1);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 direction : NORMAL;
};

PerMaterialCBufferBegin
    float _EnvironmentIntensity;
    float _EnvironmentRotation;
    int _UseSeparateBackground;
PerMaterialCBufferEnd

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = TransformToClipSpace(input.position);
    output.position.z = output.position.w;
    output.direction = input.position;
    return output;
}

float3 RotateEnvironment(float3 direction, float rotation)
{
    float s = sin(rotation);
    float c = cos(rotation);
    return float3(c * direction.x - s * direction.z, direction.y, s * direction.x + c * direction.z);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 direction = RotateEnvironment(normalize(input.direction), _EnvironmentRotation);
    if (_UseSeparateBackground != 0)
    {
        float2 uv = float2(atan2(direction.x, direction.z) / (2.0 * PI), asin(direction.y) / PI) + 0.5;
        uint width, height;
        _EnvironmentBackground.GetDimensions(width, height);
        uv.y = clamp(uv.y, 0.5 / height, 1.0 - 0.5 / height);
        return float4(_EnvironmentBackground.SampleLevel(g_LinearWrapSampler, uv, 0).rgb *
                      (_EnvironmentIntensity * GetPreExposure()), 1.0);
    }
    return float4(_EnvironmentSky.SampleLevel(g_LinearClampSampler, direction, 0).rgb *
                  (_EnvironmentIntensity * GetPreExposure()), 1.0);
}
