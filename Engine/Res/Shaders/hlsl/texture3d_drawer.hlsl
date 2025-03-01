//info bein
//pass begin::
//name: Hidden/Texture3dDrawer
//vert: VSMain
//pixel: PSMain
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//multi_compile _ _DrawMode_Slice _DrawMode_Volume
//pass end::
//Properties
//{
//  _Color("Color",Color) = (1,1,1,1)
//  _Mipmap("Mipmap",Range(0,9)) = 0
//  _Speed("Speed",Range(0,0.5)) = 1
//  _Scale("Scale",Range(0,10)) = 1
//  _Slice("Slice",Range(0,1)) = 0
//  [Enum(Slice,0,Volume,1)] _DrawMode("DrawMode",Float) = 0
//}
//info end

#include "common.hlsli"

PerMaterialCBufferBegin
	float4 _Color;
    float _Mipmap;
    float _Speed;
    float _Scale;
    float _Slice;
PerMaterialCBufferEnd

TEXTURE3D(_MainTex)
//TEXTURE2D(_MainTex)

struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 local_pos : TEXCOORD1;
    float3 world_pos : TEXCOORD2;
};

PSInput VSMain(VSInput v)
{
    PSInput ret;
    ret.position = TransformToClipSpace(v.position);
    ret.local_pos = v.position.xyz;
    ret.world_pos = TransformObjectToWorld(v.position);
    ret.uv = v.uv;
    return ret;
}
struct Ray
{
    float3 origin;
    float3 dir;
};

struct AABB
{
    float3 min;
    float3 max;
};
#define ITERATIONS 20

bool intersect(Ray r, AABB aabb, out float t0, out float t1)
{
    float3 invR = 1.0 / r.dir;
    float3 tbot = invR * (aabb.min - r.origin);
    float3 ttop = invR * (aabb.max - r.origin);
    float3 tmin = min(ttop, tbot);
    float3 tmax = max(ttop, tbot);
    float2 t = max(tmin.xx, tmin.yz);
    t0 = max(t.x, t.y);
    t = min(tmax.xx, tmax.yz);
    t1 = min(t.x, t.y);

    return t0 <= t1;
}

float3 get_uv(float3 p)
{
    return (p+1) * 0.5;
}

float4 PSMain(PSInput i) : SV_Target
{
    float4 dst = float4(0, 0, 0, 0);
    // float2 uv = i.uv;
    // uv *= _Scale;
    // uv.x += _Time.x * _Speed;
    // dst = SAMPLE_TEXTURE2D_LOD(_MainTex,g_LinearWrapSampler,uv,_Mipmap);
    // dst.a=1; 
#if defined(_DrawMode_Slice)
    float3 uvw =  float3(i.uv,_Slice);
    dst = SAMPLE_TEXTURE3D_LOD(_MainTex,g_LinearWrapSampler,uvw,_Mipmap);
    dst.a = 1.0;
#else
    Ray ray;
    ray.origin = i.local_pos;
    // world space direction to object space
    float3 dir = normalize(i.world_pos - GetCameraPositionWS());
    ray.dir = normalize(mul(_MatrixInvWorld, float4(dir,0)));
    float ext = 1;
    AABB aabb;
    aabb.min = float3(-ext,-ext,-ext);
    aabb.max = float3(ext,ext,ext);

    float tnear;
    float tfar;
    intersect(ray, aabb, tnear, tfar);
    tnear = max(0.0, tnear);

    // float3 start = ray.origin + ray.dir * tnear;
    float3 start = ray.origin;
    float3 end = ray.origin + ray.dir * tfar;
    float dist = length(tfar - tnear);
    float step_size = dist / float(ITERATIONS);
    float3 ds = normalize(end - start) * step_size;
    float3 p = start;

    [loop]
    for (int iter = 0; iter < ITERATIONS; iter++)
    {
        float3 uv = get_uv(p) * _Scale;
        uv.x += _Time.x * _Speed * _Speed;
        float4 src = SAMPLE_TEXTURE3D_LOD(_MainTex,g_LinearWrapSampler,uv,_Mipmap);

        src.a *= 0.5;
        src.rgb *= src.a;

        // blend
        dst = (1.0 - dst.a) * src + dst;
        p += ds;
        // if(src.a > 0.75)
        //     break;
    }
#endif
    dst *= _Color;
    return saturate(dst);
}