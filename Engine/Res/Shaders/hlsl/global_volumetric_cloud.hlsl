//info bein
//pass begin::
//name: CloudCompute
//vert: FullscreenVSMain
//pixel: ComputePSMain
//Cull: Back
//ZWrite: Off
//Stencil: {Ref:0,Comp:Equal,Pass:Keep}
//pass end::
//pass begin::
//name: CloudBlit
//vert: FullscreenVSMain
//pixel: BlitPSMain
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//Stencil: {Ref:0,Comp:Equal,Pass:Keep}
//Blend: One,Src
//pass end::
//info end

#include "common.hlsli"
#include "geometry.hlsli"
#include "fullscreen_quad.hlsli"

PerMaterialCBufferBegin
    float4 _Params;
    float3 _Absorption;
    float _padding0;
    float3 _Scattering;
    float _padding1;
    float _Exposure;
    float _StepSize;
    float _DensityMultply;
    float _Threshold;
    uint _CloudStartHeight;
PerMaterialCBufferEnd

TEXTURE3D(_MainTex)
TEXTURE2D(_NoiseTex)
TEXTURECUBE(RadianceTex)

#define _Speed _Params.z
#define _CloudZone _Params.xy

#define _G -0.3f
#define USE_DENSITY 1
#define MAX_STEP_COUNT 32
#define MAX_STEPS_LIGHTS 16
#define DENSITY_THRESHOLD 0.0001
#define GOLDEN_RATIO 1.61803398875
#define PYHCICS_CLOUD 0
#define SUN_LIGHT_POWER 200
#define MAX_CLOUD_DIST 200000
#define INV_CLOUD_THINKNESS 5e-05

FullScreenPSInput FullscreenVSMain(uint vertex_id : SV_VERTEXID);

struct AABB
{
    float3 min;
    float3 max;
};

bool Intersect(Ray r, AABB aabb, out float t0, out float t1)
{
    float3 invR = 1.0 / r.d;
    float3 tbot = invR * (aabb.min - r.o);
    float3 ttop = invR * (aabb.max - r.o);
    float3 tmin = min(ttop, tbot);
    float3 tmax = max(ttop, tbot);
    float2 t = max(tmin.xx, tmin.yz);
    t0 = max(t.x, t.y);
    t = min(tmax.xx, tmax.yz);
    t1 = min(t.x, t.y);

    return t0 <= t1;
}
float Remap(float x, float a, float b, float c, float d)
{
    return (((x - a) / (b - a)) * (d - c)) + c;
}

float3 ToCloudSpace(float3 world_pos)
{
    static const int kGridSize = 1000;
    //snap to grid
    float3 center = ((int3)_CameraPos.xyz / kGridSize) * kGridSize + kGridSize / 2;
    center.y = 0;
    float3 pos = (world_pos - center) / (_CloudZone.y * 0.5);
    return (pos * 0.5 + 0.5);
}
float EvalDensity(float3 pos,float atten=1.0f)
{
    pos.x += _Speed * _Time.x * 20000;
    float3 uvw = ToCloudSpace(pos);
    float4 noise = SAMPLE_TEXTURE3D_LOD(_MainTex,g_LinearWrapSampler,uvw,1);
    float wfbm = noise.y * 0.625 + noise.z * 0.25 + noise.w * 0.125;
    float cloud = Remap(noise.x, wfbm-1.0, 1.0,0.0,1.0);
    cloud = Remap(cloud,0.85,1.0,0.0,1.0);
    cloud = smoothstep(_Threshold,1.0,cloud);
    atten *= _DensityMultply;
    return cloud * atten;
}
float BeerLambert(float3 sigma_a,float d)
{
    return exp(-sigma_a * d);
}
float Phase(float g,float vl)
{
    float g2 = g * g;
    float denom = 1 + g2 - 2 * g * vl;
    return 1 / (4 * PI) * (1 - g2) / pow(denom, 1.5);
    // float denom = 1 + g * g - 2 * g * vl;
    // return 1 / (4 * PI) * (1 - g * g) / (denom * sqrt(vl));
}

float3 EvalNormal(float3 p)
{
    float2 e = float2(.1, 0);
  float3 n = EvalDensity(p) - float3(
    EvalDensity(p-e.xyy),
    EvalDensity(p-e.yxy),
    EvalDensity(p-e.yyx));

  return normalize(n);
}

// https://twitter.com/FewesW/status/1364629939568451587/photo/1
float3 MultipleOctaves(float extinction, float mu, float stepL,float3 sigma_t)
{
    float3 luminance = float3(0,0,0);
    const float octaves = 4.0;
    
    // Attenuation
    float a = 1.0;
    // Contribution
    float b = 1.0;
    // Phase attenuation
    float c = 1.0;
    
    float phase;
    for(float i = 0.0; i < octaves; i++)
    {
        // Two-lobed HG
        phase = lerp(Phase(-0.1 * c, mu), Phase(0.3 * c, mu), 0.7);
        luminance += b * phase * BeerLambert(extinction * sigma_t * a,stepL);// exp(-stepL * extinction * sigma_t * a);
        // Lower is brighter
        a *= 0.2;
        // Higher is brighter
        b *= 0.5;
        c *= 0.5;
    }
    return luminance;
}

float3 LightMarch(float3 p,float cos_theta,float3 light_dir,float dist,float3 sigma_t)
{
    float3 in_scattering = float3(0,0,0);
    float light_step_size = dist / MAX_STEPS_LIGHTS;
    float light_step_div = 1.0f / (float)MAX_STEPS_LIGHTS;
    float density = 0.0f;
    float total_dis = 0.0f;
    [loop]
    for (uint light_step = 0; light_step < MAX_STEPS_LIGHTS; light_step++)
    {
        float3 sample_light_pos = p + light_dir * (light_step + 0.5f) * light_step_size;
        float cur_density = EvalDensity(sample_light_pos);
        if (cur_density > DENSITY_THRESHOLD)
            density += cur_density * light_step_div;
    }
    float3 transmittance = MultipleOctaves(density, cos_theta, light_step_size * MAX_STEPS_LIGHTS * INV_CLOUD_THINKNESS, sigma_t);
    // Return product of Beer'is law and powder effect depending on the 
    // view direction angle with the light direction.
	return lerp(transmittance * 2.0 * (1.0 - (BeerLambert(density * 2.0 * sigma_t,light_step_size * INV_CLOUD_THINKNESS))), 
               transmittance, 
               0.5 + 0.5 * cos_theta);
}
float4 ComputePSMain(FullScreenPSInput input) : SV_Target
{
    float3 cam_dir = lerp(_LT,_RT,input.uv.x);
    cam_dir = lerp(cam_dir,lerp(_LB,_RB,input.uv.x),input.uv.y);
    cam_dir = normalize(cam_dir - _CameraPos.xyz);
    Ray ray;
    ray.o = _CameraPos.xyz;
    ray.d = cam_dir;
    float3 p;
    Sphere is;
    is.c = _CameraPos.xyz;
    is.r = _CloudZone.x;
    Sphere os;
    os.c = _CameraPos.xyz;
    os.r = _CloudZone.y;
    if (RaySphereIntersection(ray,is,p) && dot(p-is.c, float3(0,1,0)) > 0)
    {
        float pot = dot(normalize(p-is.c), float3(0,1,0));
        float dist = distance(p,ray.o);
        if (dist > MAX_CLOUD_DIST)
            return float4(0,0,0,1);
        float4 dst = float4(0, 0, 0, 0);
        float step_size = (_CloudZone.y - _CloudZone.x) / MAX_STEP_COUNT;
        float step_div = 1.0f / (float)MAX_STEP_COUNT;
        float3 total_transmittance = float3(1,1,1);
        float3 accumulated_color = float3(0, 0, 0);
        //t same as e
        float sigma_t = max(_Absorption + _Scattering, 1e-6);
        float3 light_dir = _MainlightWorldPosition.xyz;
        float3 light_color = _DirectionalLights[0]._LightColor * SUN_LIGHT_POWER;
        float noise = SAMPLE_TEXTURE2D(_NoiseTex,g_LinearWrapSampler,input.uv).r;
        uint uFrame = uint(_Time.w);
        float offset = frac(noise + float(uFrame % 32) / GOLDEN_RATIO);
        float cos_theta = dot(cam_dir, light_dir) * 0.5 + 0.5;
        float phase = lerp(Phase(_G, cos_theta),Phase(-_G, cos_theta),0.7);
        float ambient_intensity = 0.25;
        ray.o = p + ray.d * offset * step_size;
        float3 virtual_center = _CameraPos.xyz;
        virtual_center.y += _CloudZone.x * 2;
        [loop]
        for (uint step = 0; step < MAX_STEP_COUNT; step++)
        {
            float3 sample_pos = ray.o + ray.d * (step + 0.5f) * step_size;
            float density = EvalDensity(sample_pos,smoothstep(0.0,1.0,saturate(pot)));
            float3 sample_sigma_t = density * sigma_t;
            float3 sample_sigma_s = density * _Scattering;
            if (density > DENSITY_THRESHOLD)
            {
                float3 ambient = SAMPLE_TEXTURECUBE_LOD(RadianceTex,g_LinearWrapSampler,normalize(sample_pos - virtual_center),0);
                //In-Scattering
                float3 in_scattering = float3(0,0,0);
                Ray light_ray;
                light_ray.o = sample_pos;
                light_ray.d = light_dir;
                float t2,t3;
                float3 p2;
                if (RaySphereIntersection(light_ray,os,p2))
                {
                    float dist = length(p2 - light_ray.o);
                    in_scattering = LightMarch(sample_pos,cos_theta,-light_dir,dist,sigma_t);
                    float3 luminance = ambient_intensity * ambient + light_color * phase * in_scattering;
                    luminance *= sample_sigma_s;
                    float3 cur_transmittance = BeerLambert(sample_sigma_t, step_size * INV_CLOUD_THINKNESS);
                    // Better energy conserving integration
                    // "From Physically based sky, atmosphere and cloud rendering in Frostbite" 5.6
                    // by Sebastian Hillaire.
                    accumulated_color += total_transmittance * (luminance - luminance * cur_transmittance) / sample_sigma_t;
                    total_transmittance *= cur_transmittance;
                    if (length(total_transmittance) < 0.001)
                    {
                        total_transmittance = float3(0, 0, 0);
                        break;
                    }
                }
            }
        }
        float alpha = saturate(length(total_transmittance));
        return float4(accumulated_color * _Exposure, alpha);
    }
    return float4(0,0,0,1);
}

TEXTURE2D(_CloudTex);
TEXTURE2D(_CameraDepthTexture)
float4 BlitPSMain(FullScreenPSInput input) : SV_Target
{
    float d = SAMPLE_TEXTURE2D_DEPTH(_CameraDepthTexture,g_LinearWrapSampler,input.uv);
    if (d != 1.0)
        return float4(0,0,0,1);
    return SAMPLE_TEXTURE2D_LOD(_CloudTex,g_LinearClampSampler,input.uv,0);
}