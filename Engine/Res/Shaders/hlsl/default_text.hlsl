//info bein
//pass begin::
//name: default_font
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//ZTest: Always
//multi_compile _ _MSDF
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

PerMaterialCBufferBegin
    float _MsdfPxRange;
PerMaterialCBufferEnd

TEXTURE2D(_MainTex)

float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}
float ScreenPxRange(float2 uv)
{
    uint tex_w, tex_h;
    _MainTex.GetDimensions(tex_w, tex_h);
    float px_range = max(_MsdfPxRange, 1.0f);
    float2 unit_range = float2(px_range, px_range) / float2(tex_w, tex_h);
    float2 screen_tex_size = 1.0f / max(fwidth(uv), float2(1e-6f, 1e-6f));
    return max(0.5f * dot(unit_range, screen_tex_size), 1.0f);
}
static const float4 kBgColor = float4(0, 0, 0, 0);
static const float4 kFgColor = float4(1, 1, 1, 1);

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
    float3 sdf = SAMPLE_TEXTURE2D_LOD(_MainTex, g_LinearClampSampler, input.uv, 0).rgb;
    float dist = median(sdf.r, sdf.g, sdf.b);
    float screen_px_distance = ScreenPxRange(input.uv) * (dist - 0.5f);
    float opacity = clamp(screen_px_distance + 0.5f, 0.0f, 1.0f);
    return float4(input.color.rgb, input.color.a * opacity);
#else
    float4 c = SAMPLE_TEXTURE2D_LOD(_MainTex,g_LinearClampSampler,input.uv,0) * input.color;
    return c;
#endif
}