//info bein
//pass begin::
//name: ThresholdExtract
//vert: FullscreenVSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: DownSample
//vert: FullscreenVSMain
//pixel: DownSamplePS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: UpSample
//vert: FullscreenVSMain
//pixel: UpSamplePS
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//pass end::
//pass begin::
//name: Composite
//vert: FullscreenVSMain
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
#include "../fullscreen_quad.hlsli"
#include "../color_space_utils.hlsli"

FullScreenPSInput FullscreenVSMain(uint vertex_id : SV_VERTEXID);

float4 PSMain(FullScreenPSInput input) : SV_TARGET
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
    float4 _SunScreenPos;
    float4 _NoiseTex_TexelSize;
PerMaterialCBufferEnd

float4 DownSamplePS(FullScreenPSInput input) : SV_TARGET
{
	float3 c = DownSample(input.uv,_SampleParams.xy);
    return float4(c,0.5);
}
float4 UpSamplePS(FullScreenPSInput input) : SV_TARGET
{
    float aspect = _SampleParams.x / _SampleParams.y;
	float3 c = UpSample(input.uv,float2(_SampleParams.z * aspect,_SampleParams.z));
	return float4(c,0.5);
}

float4 BlurX(FullScreenPSInput input) : SV_TARGET
{	 
	float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb;
	float3 sum = 0.0.xxx;
	for (int i = -RADIUS; i<=RADIUS; i++)
	{
		float2 uv = input.uv;
		uv.x += i * _ScreenParams.x * RADIUS_SCALE;
		sum += weights[i + RADIUS] * _SourceTex.Sample(g_LinearClampSampler, uv).rgb;
	}
	return sum.xyzz;
}

float4 BlurY(FullScreenPSInput input) : SV_TARGET
{
	float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb;
	float3 sum = 0.0.xxx;
	for (int i = -RADIUS; i<=RADIUS; i++)
	{
		float2 uv = input.uv;
		uv.y += i * _ScreenParams.y * RADIUS_SCALE;
		sum += weights[i + RADIUS] * _SourceTex.Sample(g_LinearClampSampler, uv).rgb;
	}
	return sum.xyzz;
}
//----------------------------------------------------Composite Pass-----------------------------------------------------
TEXTURE2D(_NoiseTex)

float Noise(float t)
{
    return SAMPLE_TEXTURE2D(_NoiseTex,g_LinearWrapSampler,float2(t,0) * _NoiseTex_TexelSize.xy).r;
}

float Noise(float2 t)
{
    return SAMPLE_TEXTURE2D(_NoiseTex,g_LinearWrapSampler,t * _NoiseTex_TexelSize.xy).r;
}

float3 Lensflare(float2 uv, float2 pos)
{
    float2 main = uv-pos;
    float2 uvd = uv*length(uv);
    float ang = atan2(main.x, main.y);
    float dist = length(main);
    dist = pow(dist, 0.1);
    float n = Noise(float2(ang*16., dist*32.));
    float f0 = 1./(length(uv-pos)*32.+1.);
    f0 = f0+f0*(sin(Noise(sin(ang*2.+pos.x)*4.-cos(ang*3.+pos.y))*16.)*0.1+dist*0.1+0.1);
    float f1 = max(0.01-pow(length(uv+1.2*pos), 1.9), 0.)*7.;
    float f2 = max(1./(1.+32.*pow(length(uvd+0.8*pos), 2.)), 0.)*0.25;
    float f22 = max(1./(1.+32.*pow(length(uvd+0.85*pos), 2.)), 0.)*0.23;
    float f23 = max(1./(1.+32.*pow(length(uvd+0.9*pos), 2.)), 0.)*0.21;
    float2 uvx = lerp(uv, uvd, -0.5);
    float f4 = max(0.01-pow(length(uvx+0.4*pos), 2.4), 0.)*6.;
    float f42 = max(0.01-pow(length(uvx+0.45*pos), 2.4), 0.)*5.;
    float f43 = max(0.01-pow(length(uvx+0.5*pos), 2.4), 0.)*3.;
    uvx = lerp(uv, uvd, -0.4);
    float f5 = max(0.01-pow(length(uvx+0.2*pos), 5.5), 0.)*2.;
    float f52 = max(0.01-pow(length(uvx+0.4*pos), 5.5), 0.)*2.;
    float f53 = max(0.01-pow(length(uvx+0.6*pos), 5.5), 0.)*2.;
    uvx = lerp(uv, uvd, -0.5);
    float f6 = max(0.01-pow(length(uvx-0.3*pos), 1.6), 0.)*6.;
    float f62 = max(0.01-pow(length(uvx-0.325*pos), 1.6), 0.)*3.;
    float f63 = max(0.01-pow(length(uvx-0.35*pos), 1.6), 0.)*5.;
    float3 c = 0.0.xxx;
    c.r += f2+f4+f5+f6;
    c.g += f22+f42+f52+f62;
    c.b += f23+f43+f53+f63;
    c = c*1.3-((float3)length(uvd)*0.05);
    c += ((float3)f0);
    return c;
}
float3 cc(float3 color, float factor, float factor2)
{
    float w = color.x+color.y+color.z;
    return lerp(color, ((float3)w)*factor, w*factor2);
}

float4 Composite(FullScreenPSInput input) : SV_TARGET
{
	float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb;
    float4 cpos = _SunScreenPos;
	float3 bloom_color = _BloomTex.Sample(g_LinearClampSampler, input.uv).rgb;
    // if (cpos.z > 0)
    // {
    //     float2 uv = input.uv;
    //     float aspect = _ScreenParams.x / _ScreenParams.y;
    //     uv -= 0.5f;
    //     uv.x *= aspect;
    //     cpos.xy -= 0.5f;
    //     cpos.x *= aspect;
    //     float3 lens_flare = float3(1.3, 1.2, 1.1) * Lensflare(uv,float2(cpos.x,cpos.y));
    //     lens_flare -= Noise(input.uv)*0.015;
    //     //return float4(lens_flare,1.0f);
    //     color += lens_flare;
    // }
	return float4(ACESFilm(lerp(color,bloom_color,_SampleParams.w)),1.0f);
}
