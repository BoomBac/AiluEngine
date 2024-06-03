//info bein
//pass begin::
//name: filter_irradiance
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//pass end::
//Properties
//{
//}
//info end

#include "common.hlsli"
#include "constants.hlsli"

TextureCube EnvMap : register(t0);
SamplerState g_LinearSampler : register(s0);

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 wnormal : NORMAL;
};

static const float2 inv_atan = { 0.1591f, 0.3183f };

float3 FilterCubeMap(float3 normal)
{
	float3 irradiance = { 0.f, 0.f, 0.f };
	float3 up = { 0.f, 1.f, 0.f };
	float3 right = normalize(cross(up, normal));
	up = normalize(cross(normal, right));
	float nrSamples = 0.0;
	float sampleDelta = 0.025f;
	for (float phi = 0.0; phi < DoublePI; phi += sampleDelta)
	{
		for (float theta = 0.0; theta < HalfPI; theta += sampleDelta)
		{
            // spherical to cartesian (in tangent space)
			float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // tangent space to world
			float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;
			irradiance += EnvMap.Sample(g_LinearSampler, sampleVec).rgb * cos(theta) * sin(theta);
			nrSamples++;
		}
	}
	irradiance = PI * irradiance * (1.0 / float(nrSamples));
	return irradiance;
}


PSInput VSMain(VSInput v)
{
	PSInput result;
	float4x4 mvp = mul(_PassMatrixVP, _MatrixWorld);
	result.position = mul(mvp, float4(v.position, 1.0f));
	result.wnormal = normalize(v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(FilterCubeMap(normalize(normalize(input.wnormal))), 1.0);
}
