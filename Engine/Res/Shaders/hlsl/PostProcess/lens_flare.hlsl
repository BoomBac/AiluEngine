//info bein
//pass begin::
//name: len_flare
//vert: FullscreenVSMain
//pixel: PSMain
//Cull: Back
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//pass end::
//info end
#include "../common.hlsli"
#include "../fullscreen_quad.hlsli"

TEXTURE2D(_NoiseTex)

PerMaterialCBufferBegin
    float4 _SunScreenPos;
    float4 _NoiseTex_TexelSize;
PerMaterialCBufferEnd

FullScreenPSInput FullscreenVSMain(FullScreenVSInput v);

float Noise(float2 t)
{
    return SAMPLE_TEXTURE2D(_NoiseTex,g_LinearClampSampler,t * _NoiseTex_TexelSize.xy).r;
}

float3 Lensflare(float2 uv, float2 pos)
{
    float2 main = uv-pos;
    float2 uvd = uv*length(uv);
    float ang = atan2(main.x, main.y);
    float dist = length(main);
    dist = pow(dist, 0.1);
    float n = Noise(float2(ang*16., dist*32.));
    float f0 = 1./(length(uv-pos)*16.+1.);
    f0 = f0+f0*(sin(Noise(sin(ang*2.+pos.x)*4.-cos(ang*3.+pos.y))*16.)*0.1+dist*0.1+0.8);
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

float4 PSMain(FullScreenPSInput input) : SV_TARGET
{
    return float4(Lensflare(input.uv,0.5.xx),1.0);
}