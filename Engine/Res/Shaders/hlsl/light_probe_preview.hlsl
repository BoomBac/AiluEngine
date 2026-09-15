//info begin
//pass begin::
//name: light_probe_preview
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//ZTest: LEqual
//ZWrite: Off
//multi_compile _ DEBUG_CUBEMAP
//multi_compile _ DDGI_PROBE_PREVIEW
//pass end::
//Properties
//{
//}
//info end

#include "common.hlsli"
#include "constants.hlsli"
#include "spherical_harmonics.hlsli"

TextureCube _ProbeCubemap : register(t0);
#if defined(DDGI_PROBE_PREVIEW)
StructuredBuffer<float4x4> _ProbeInstanceData;
#endif

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
#if defined(DDGI_PROBE_PREVIEW)
    uint instance_id : SV_InstanceID;
#endif
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal_ws : NORMAL;
};

PerMaterialCBufferBegin
    float4 _SH0;
    float4 _SH1;
    float4 _SH2;
    float4 _SH3;
    float4 _SH4;
    float4 _SH5;
    float4 _SH6;
    float4 _SH7;
    float4 _SH8;
PerMaterialCBufferEnd

PSInput VSMain(VSInput input)
{
    PSInput output;
#if defined(DDGI_PROBE_PREVIEW)
    const float4x4 instance_matrix = _ProbeInstanceData[input.instance_id];
    const float3 world_position = mul(instance_matrix, float4(input.position, 1.0f)).xyz;
    output.position = TransformWorldToHClip(world_position);
    output.normal_ws = normalize(mul((float3x3)transpose(instance_matrix), input.normal));
#else
    output.position = TransformToClipSpace(input.position);
    output.normal_ws = TransformNormal(input.normal);
#endif
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
#if defined(DDGI_PROBE_PREVIEW)
    const float3 normal_ws = normalize(input.normal_ws);
    return float4(normal_ws * 0.5f + 0.5f, 1.0f);
#else
    float3 radiance;
#if defined(DEBUG_CUBEMAP)
    radiance = max(_ProbeCubemap.SampleLevel(g_LinearClampSampler, normalize(input.normal_ws), 0.0f).rgb,
                   0.0f.xxx);
#else
    float4 coefficients[SH_COEFFICIENT_COUNT] =
    {
        _SH0, _SH1, _SH2, _SH3, _SH4, _SH5, _SH6, _SH7, _SH8
    };
    radiance = max(EvaluateSH9(coefficients, normalize(input.normal_ws)), 0.0.xxx);
#endif
    float3 color = 1.0.xxx - exp(-radiance);
    color = pow(max(color, 0.0.xxx), 1.0.xxx / 2.2.xxx);
    return float4(color, 1.0);
#endif
}
