//info bein
//pass begin::
//name: default_font
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Transparent
//Blend: Src,OneMinusSrc
//ZTest: Always
//multi_compile: _ _MSDF
//ColorMask: RGB
//pass end::
//info end

#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct PSInput
{
	float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};
float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}
static const float4 kBgColor = (0, 0, 0, 0);
static const float4 kFgColor = (1, 1, 1, 1);
TEXTURE2D(_MainTex)
PSInput VSMain(VSInput v,uint vert_id : SV_VertexID)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
    result.color = v.color;
    result.uv = v.uv;
	return result;
}

float4 PSMain(PSInput input):SV_TARGET
{
#if defined(_MSDF)
    // float4 msd = SAMPLE_TEXTURE2D_LOD(_MainTex, g_LinearClampSampler, input.uv, 0);
    // float sd = median(msd.r, msd.g, msd.b);
    // float screenPxDistance = ScreenPxRange() * (sd - 0.5);
    // float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    // return lerp(kBgColor, kFgColor, opacity);
    float3 sdf = SAMPLE_TEXTURE2D_LOD(_MainTex, g_LinearClampSampler, input.uv, 0).rgb;
    float dist = median(sdf.r, sdf.g, sdf.b);
    float screen_px_distance = fwidth(dist);
    float edge_width = (screen_px_distance * 14) / 8.0f * 0.5;
    float alpha = smoothstep(0.5 - edge_width, 0.5 + edge_width, dist);
    return float4(1, 1, 1, alpha);
#else
    float4 c = SAMPLE_TEXTURE2D_LOD(_MainTex,g_LinearClampSampler,input.uv,0) * input.color;
    return c;
#endif
}