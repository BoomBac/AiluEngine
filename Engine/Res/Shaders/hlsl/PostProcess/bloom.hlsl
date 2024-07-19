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
//name: DownSample
//vert: VSMain
//pixel: DownSamplePS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: UpSample
//vert: VSMain
//pixel: UpSamplePS
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
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

#include "../common.hlsli"

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
	float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));
	color *= step(1.0, brightness);
	return color.xyzz;
}
//texel_size: float2(1.0,1.0) / SrcTex.size
float3 DownSample(float2 center,float2 texel_size)
{
	float3 result = 0.0.xxx;
	float x = texel_size.x;
	float y = texel_size.y;
	// Take 13 samples around current texel:
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    // === ('e' is the current texel) ===
    float3 a = _SourceTex.Sample(g_LinearClampSampler, float2(center.x - 2*x, center.y + 2*y)).rgb;
    float3 b = _SourceTex.Sample(g_LinearClampSampler, float2(center.x,       center.y + 2*y)).rgb;
    float3 c = _SourceTex.Sample(g_LinearClampSampler, float2(center.x + 2*x, center.y + 2*y)).rgb;
    float3 d = _SourceTex.Sample(g_LinearClampSampler, float2(center.x - 2*x, center.y)).rgb;
    float3 e = _SourceTex.Sample(g_LinearClampSampler, float2(center.x,       center.y)).rgb;
    float3 f = _SourceTex.Sample(g_LinearClampSampler, float2(center.x + 2*x, center.y)).rgb;
    float3 g = _SourceTex.Sample(g_LinearClampSampler, float2(center.x - 2*x, center.y - 2*y)).rgb;
    float3 h = _SourceTex.Sample(g_LinearClampSampler, float2(center.x,       center.y - 2*y)).rgb;
    float3 i = _SourceTex.Sample(g_LinearClampSampler, float2(center.x + 2*x, center.y - 2*y)).rgb;
    float3 j = _SourceTex.Sample(g_LinearClampSampler, float2(center.x - x, center.y + y)).rgb;
    float3 k = _SourceTex.Sample(g_LinearClampSampler, float2(center.x + x, center.y + y)).rgb;
    float3 l = _SourceTex.Sample(g_LinearClampSampler, float2(center.x - x, center.y - y)).rgb;
    float3 m = _SourceTex.Sample(g_LinearClampSampler, float2(center.x + x, center.y - y)).rgb;

    // Apply weighted distribution:
    // 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
    // a,b,d,e * 0.125
    // b,c,e,f * 0.125
    // d,e,g,h * 0.125
    // e,f,h,i * 0.125
    // j,k,l,m * 0.5
    // This shows 5 square areas that are being sampled. But some of them overlap,
    // so to have an energy preserving downsample we need to make some adjustments.
    // The weights are the distributed, so that the sum of j,k,l,m (e.g.)
    // contribute 0.5 to the final color output. The code below is written
    // to effectively yield this sum. We get:
    // 0.125*5 + 0.03125*4 + 0.0625*4 = 1
    result = e*0.125;
    result += (a+c+g+i)*0.03125;
    result += (b+d+f+h)*0.0625;
    result += (j+k+l+m)*0.125;
	return result;
}
float3 UpSample(float2 center,float2 filter_radius)
{
	// The filter kernel is applied with a radius, specified in texture
    // coordinates, so that the radius will vary across mip resolutions.
	float3 result = 0.0.xxx;
	float x = filter_radius;
	float y = filter_radius;

    // Take 9 samples around current texel:
    // a - b - c
    // d - e - f
    // g - h - i
    // === ('e' is the current texel) ===
    float3 a = _SourceTex.Sample(g_LinearClampSampler, float2(center.x - x, center.y + y)).rgb;
    float3 b = _SourceTex.Sample(g_LinearClampSampler, float2(center.x,     center.y + y)).rgb;
    float3 c = _SourceTex.Sample(g_LinearClampSampler, float2(center.x + x, center.y + y)).rgb;
    float3 d = _SourceTex.Sample(g_LinearClampSampler, float2(center.x - x, center.y)).rgb;
    float3 e = _SourceTex.Sample(g_LinearClampSampler, float2(center.x,     center.y)).rgb;
    float3 f = _SourceTex.Sample(g_LinearClampSampler, float2(center.x + x, center.y)).rgb;
    float3 g = _SourceTex.Sample(g_LinearClampSampler, float2(center.x - x, center.y - y)).rgb;
    float3 h = _SourceTex.Sample(g_LinearClampSampler, float2(center.x,     center.y - y)).rgb;
    float3 i = _SourceTex.Sample(g_LinearClampSampler, float2(center.x + x, center.y - y)).rgb;

    // Apply weighted distribution, by using a 3x3 tent filter
    //  1   | 1 2 1 |
    // -- * | 2 4 2 |
    // 16   | 1 2 1 |
    result = e*4.0;
    result += (b+d+f+h)*2.0;
    result += (a+c+g+i);
    result *= 1.0 / 16.0;
	return result;
}

#define RADIUS 3
#define RADIUS_SCALE 3
const static float weights[7] = {0.05,0.1,0.15,0.4,0.15,0.1,0.05};

PerMaterialCBufferBegin
	float4 _SampleParams;
PerMaterialCBufferEnd

float4 DownSamplePS(PSInput input) : SV_TARGET
{
	float3 c = DownSample(input.uv,_SampleParams.xy);
    return float4(c,0.5);
}
float4 UpSamplePS(PSInput input) : SV_TARGET
{
    float aspect = _SampleParams.x / _SampleParams.y;
	float3 c = UpSample(input.uv,float2(_SampleParams.z * aspect,_SampleParams.z));
	return float4(c,0.5);
}

float4 BlurX(PSInput input) : SV_TARGET
{	 
	float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb;
	float3 sum = 0.0.xxx;
	for (int i = -RADIUS; i<=RADIUS; i++)
	{
		float2 uv = input.uv;
		uv.x += i * _ScreenParams.z * RADIUS_SCALE;
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
		uv.y += i * _ScreenParams.w * RADIUS_SCALE;
		sum += weights[i + RADIUS] * _SourceTex.Sample(g_LinearClampSampler, uv).rgb;
	}
	return sum.xyzz;
}
float4 Composite(PSInput input) : SV_TARGET
{
	float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb;
	float3 bloom_color = _BloomTex.Sample(g_LinearClampSampler, input.uv).rgb;
	color = lerp(color,bloom_color,_SampleParams.w);
	return color.xyzz; 
}
