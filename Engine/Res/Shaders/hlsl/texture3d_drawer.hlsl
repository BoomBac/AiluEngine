//info bein
//pass begin::
//name: Hidden/Texture3dDrawer
//vert: VSMain
//pixel: PSMain
//Cull: Off
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
//  _SliceAxis("SliceAxis",Range(0,2)) = 2
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
    float _SliceAxis;
    float3 _camera_pos;
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
#define ITERATIONS 16

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

float Flux(float3 c)
{
    return saturate(dot(c, float3(0.299, 0.587, 0.114)));
}

float4 PSMain(PSInput i) : SV_Target
{
    float4 dst = float4(0, 0, 0, 0);
#if defined(_DrawMode_Slice)
    float3 uvw;
    // _SliceAxis: 0 = X, 1 = Y, 2 = Z
    if (_SliceAxis < 0.5)
        uvw = float3(_Slice, i.uv.x, i.uv.y);
    else if (_SliceAxis < 1.5)
        uvw = float3(i.uv.x, _Slice, i.uv.y);
    else
        uvw = float3(i.uv, _Slice);
    dst = SAMPLE_TEXTURE3D_LOD(_MainTex,g_LinearWrapSampler,uvw,_Mipmap);
    dst.a = max(Flux(dst.rgb),0.2f);
    //dst.rgb = uvw.xyz;
#else
    Ray ray;
    ray.origin = mul(_MatrixInvWorld, float4(_camera_pos,1)).xyz;
    ray.dir = normalize(i.local_pos - ray.origin);
    float ext = 1.0;
    AABB aabb;
    aabb.min = float3(-ext,-ext,-ext);
    aabb.max = float3( ext, ext, ext);

    float t0, t1;
    if (!intersect(ray, aabb, t0, t1))
        return 0;

    t0 = max(t0, 0.0);

    float dist = t1 - t0;
    float step = dist / ITERATIONS;

    float3 p = ray.origin + ray.dir * t0;

    for (int i = 0; i < ITERATIONS; i++)
    {
        float3 uv = (p - aabb.min) / (aabb.max - aabb.min);
        float4 src = SAMPLE_TEXTURE3D(_MainTex, g_LinearClampSampler, uv);
        float density = src.a;
        float alpha = 1 - exp(-density * step * 2); // 关键
        src.rgb *= alpha;
        src.a = alpha;

        dst.rgb += (1 - dst.a) * src.rgb;
        dst.a   += (1 - dst.a) * src.a;
        if (dst.a > 0.95)
            break;

        p += ray.dir * step;
    }
#endif
    dst *= _Color;
    return saturate(dst);
}