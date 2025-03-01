#include "cs_common.hlsli"
#pragma kernel ShapeNoiseMain
#pragma kernel DetailNoiseMain
#pragma kernel CurlNoiseMain

/**
This tab contains all the necessary noise functions required to model a cloud shape.
*/

// Hash by David_Hoskins
#define UI0 1597334673U
#define UI1 3812015801U
#define UI2 uint2(UI0, UI1)
#define UI3 uint3(UI0, UI1, 2798796415U)
#define UIF (1.0 / float(0xffffffffU))

float3 hash33(float3 p)
{
	uint3 q = uint3(int3(p)) * UI3;
	q = (q.x ^ q.y ^ q.z)*UI3;
	return -1. + 2. * float3(q) * UIF;
}

float remap(float x, float a, float b, float c, float d)
{
    return (((x - a) / (b - a)) * (d - c)) + c;
}
float3 mod(float3 x, float3 y) 
{
    return x - y * floor(x / y);
}

// Gradient noise by iq (modified to be tileable)
float gradientNoise(float3 x, float freq = 10.0f)
{
    // grid
    float3 p = floor(x);
    float3 w = frac(x);
    
    // quintic interpolant
    float3 u = w * w * w * (w * (w * 6. - 15.) + 10.);
    
    // gradients
    float3 ga = hash33(mod(p + float3(0., 0., 0.), freq.xxx));
    float3 gb = hash33(mod(p + float3(1., 0., 0.), freq.xxx));
    float3 gc = hash33(mod(p + float3(0., 1., 0.), freq.xxx));
    float3 gd = hash33(mod(p + float3(1., 1., 0.), freq.xxx));
    float3 ge = hash33(mod(p + float3(0., 0., 1.), freq.xxx));
    float3 gf = hash33(mod(p + float3(1., 0., 1.), freq.xxx));
    float3 gg = hash33(mod(p + float3(0., 1., 1.), freq.xxx));
    float3 gh = hash33(mod(p + float3(1., 1., 1.), freq.xxx));
    
    // projections
    float va = dot(ga, w - float3(0., 0., 0.));
    float vb = dot(gb, w - float3(1., 0., 0.));
    float vc = dot(gc, w - float3(0., 1., 0.));
    float vd = dot(gd, w - float3(1., 1., 0.));
    float ve = dot(ge, w - float3(0., 0., 1.));
    float vf = dot(gf, w - float3(1., 0., 1.));
    float vg = dot(gg, w - float3(0., 1., 1.));
    float vh = dot(gh, w - float3(1., 1., 1.));
	
    // interpolation
    return va + 
           u.x * (vb - va) + 
           u.y * (vc - va) + 
           u.z * (ve - va) + 
           u.x * u.y * (va - vb - vc + vd) + 
           u.y * u.z * (va - vc - ve + vg) + 
           u.z * u.x * (va - vb - ve + vf) + 
           u.x * u.y * u.z * (-va + vb + vc - vd + ve - vf - vg + vh);
}

// Tileable 3D worley noise
float worleyNoise(float3 uv, float freq)
{    
    float3 id = floor(uv);
    float3 p = frac(uv);
    
    float minDist = 10000.;
    for (float x = -1.; x <= 1.; ++x)
    {
        for(float y = -1.; y <= 1.; ++y)
        {
            for(float z = -1.; z <= 1.; ++z)
            {
                float3 offset = float3(x, y, z);
            	float3 h = hash33(mod(float3(id + offset), freq.xxx)) * .5 + .5;
    			h += offset;
            	float3 d = p - h;
           		minDist = min(minDist, dot(d, d));
            }
        }
    }
    
    // inverted worley noise
    return 1. - minDist;
}

// Fbm for Perlin noise based on iq's blog
float perlinfbm(float3 p, float freq, int octaves)
{
    float G = exp2(-.85);
    float amp = 1.;
    float noise = 0.;
    for (int i = 0; i < octaves; ++i)
    {
        noise += amp * gradientNoise(p * freq, freq);
        freq *= 2.;
        amp *= G;
    }
    
    return noise;
}

// Tileable Worley fbm inspired by Andrew Schneider's Real-Time Volumetric Cloudscapes
// chapter in GPU Pro 7.
float worleyFbm(float3 p, float freq)
{
    return worleyNoise(p*freq, freq) * .625 +
        	 worleyNoise(p*freq*2., freq*2.) * .25 +
        	 worleyNoise(p*freq*4., freq*4.) * .125;
}
CBUFFER_START(NoiseParams)
    float4 _Size;
CBUFFER_END

RWTEXTURE3D(_OutNoise3D,float4)

[numthreads(8,8,8)]
void ShapeNoiseMain(CSInput input)
{
    float3 uvw = (float3)input.DispatchThreadID.xyz + 0.5;
    uvw /= _Size.xyz;
    float freq = 4.;
    float4 col = 0.0.xxxx;
    float pfbm= lerp(1.0, perlinfbm(uvw, 4., 7), .5);
    pfbm = abs(pfbm * 2. - 1.); // billowy perlin noise
    col.g += worleyFbm(uvw, freq);
    col.b += worleyFbm(uvw, freq*2.);
    col.a += worleyFbm(uvw, freq*4.);
    col.r += remap(pfbm, 0., 1., col.g, 1.); // perlin-worley
    _OutNoise3D[input.DispatchThreadID.xyz] = col;
}
[numthreads(8,8,8)]
void DetailNoiseMain(CSInput input)
{
    float3 uvw = (float3)input.DispatchThreadID.xyz + 0.5;
    uvw /= _Size.xyz;
    float freq = 4.;
    float4 col = 0.0.xxxx;
    float pfbm= lerp(1.0, perlinfbm(uvw, 4., 7), .5);
    col.r = lerp(1.0, perlinfbm(uvw, freq, 3), .5);
    col.g = lerp(1.0, perlinfbm(uvw, freq * 2, 7), .5);
    col.b = lerp(1.0, perlinfbm(uvw, freq * 4, 10), .5);
    // col.g += worleyFbm(uvw, freq);
    // col.b += worleyFbm(uvw, freq*2.);
    // col.a += worleyFbm(uvw, freq*4.);
    _OutNoise3D[input.DispatchThreadID.xyz] = col;
}


float2 Curl(float2 p,float delta)
{
#define FREQ 8
    float dx = gradientNoise(float3(p.x+delta,p.y,0) * FREQ,FREQ) - gradientNoise(float3(p.x-delta,p.y,0) * FREQ,FREQ);
    float dy = gradientNoise(float3(p.x,p.y + delta,0) * FREQ,FREQ) - gradientNoise(float3(p.x,p.y-delta,0) * FREQ,FREQ);
    return float2(dy,-dx) / (2*delta);
}

RWTEXTURE2D(_CurlOut,float4)
[numthreads(8,8,1)]
void CurlNoiseMain(CSInput input)
{
    float2 uv = (float2)input.DispatchThreadID.xy + 0.5;
    uv *= _Size.zw;
    _CurlOut[input.DispatchThreadID.xy] = float4(Curl(uv,_Size.z),0.0,1.0f) * 0.5 + 0.5;
}