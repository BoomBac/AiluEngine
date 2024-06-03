//info bein
//pass begin::
//name: ThresholdExtract
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: BlurX
//vert: VSMain
//pixel: BlurX
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: BlurY
//vert: VSMain
//pixel: BlurY
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: Composite
//vert: VSMain
//pixel: Composite
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//info end
Texture2D _SourceTex : register(t0);
Texture2D _BloomTex : register(t1);

SamplerState g_LinearClampSampler : register(s1);
struct VSInput
{
	float3 position : POSITION;
	float2 uv : TEXCOORD;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = float4(v.position.xy, 0.0, 1.0);
	result.uv = v.uv;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb;
	color *= step(1.8,(color.r + color.b + color.g));
	return color.xyzz;
}
#define RADIUS 3
#define RADIUS_SCALE 16
const static float weights[7] = {0.05,0.1,0.15,0.4,0.15,0.1,0.05};

float4 BlurX(PSInput input) : SV_TARGET
{	 
	float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb;
	float3 sum = 0.0.xxx;
	for (int i = -RADIUS; i<=RADIUS; i++)
	{
		float2 uv = input.uv;
		uv.x += i * 0.000625 * RADIUS_SCALE;
		sum += weights[i + RADIUS] * _SourceTex.Sample(g_LinearClampSampler, uv).rgb;
	}
	return sum.xyzz;
}

float4 BlurY(PSInput input) : SV_TARGET
{
	float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb;
	float3 sum = 0.0.xxx;
	for (int i = -RADIUS; i<=RADIUS; i++)
	{
		float2 uv = input.uv;
		uv.y += i * 0.001111 * RADIUS_SCALE;
		sum += weights[i + RADIUS] * _SourceTex.Sample(g_LinearClampSampler, uv).rgb;
	}
	return sum.xyzz;
}
float4 Composite(PSInput input) : SV_TARGET
{
	float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb;
	float3 bloom_color = _BloomTex.Sample(g_LinearClampSampler, input.uv).rgb;
	color += bloom_color;
	return color.xyzz; 
}
