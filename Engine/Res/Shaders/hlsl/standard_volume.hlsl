//info bein
//pass begin::
//name: Hidden/StandardVolume
//vert: VSMain
//pixel: PSMain
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//Blend: One,Src
//multi_compile _UseDensity_ON _UseDensity_OFF
//pass end::
//Properties
//{
//  _StepSize("StepSize",Range(0.01,1)) = 0.2
//  [HDR]_Absorption("Absorption",Color) = (1,1,1,-1)
//  [HDR]_Scattering("Scattering",Color) = (1,1,1,-1)
//  _Exposure("Exposure",Range(0,10)) = 1
//  _Fade("Fade",Range(0,1)) = 0.8
//  _DensityMultply("DensityMultply",Range(0,2)) = 1
//  _Threshold("Threshold",Range(0,1)) = 0.8
//  _G("g",Range(-1,1)) = -0.3
//  [Toggle]_UseDensity("UseDensity",Float) = 0
//}
//info end

#include "common.hlsli"

PerMaterialCBufferBegin
    float4 _AABBMax;
    float4 _AABBMin;
    float4 _AABBScale;
    float3 _Absorption;
    float _padding0;
    float3 _Scattering;
    float _padding1;
    float _Exposure;
    float _StepSize;
    float _G;
    float _Fade;
    float _DensityMultply;
    float _Threshold;
PerMaterialCBufferEnd

TEXTURE3D(_MainTex)
TEXTURE2D(_NoiseTex)
TEXTURECUBE(RadianceTex)

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

bool Intersect(Ray r, AABB aabb, out float t0, out float t1)
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
//#define BOX_FADE 1
float Fade(float3 pos)
{
#if defined(BOX_FADE)
    float fade_x = pos.x / _AABBMax.x;
    float fade_y = pos.y / _AABBMax.y;
    float fade_z = pos.z / _AABBMax.z;
    float fade = max(max(fade_x,fade_y),fade_z);
    float falloff = smoothstep(_Fade, 1, fade); // smooth transition from 0 to 1 as distance goes from 0.1 to 1
#else
    float dist = min(1.f,length(pos) / _AABBMax.x);
    float falloff = smoothstep(_Fade, 1, dist); // smooth transition from 0 to 1 as distance goes from 0.1 to 1
#endif
    return (1 - falloff);
}
float EvalDensity(float3 pos)
{
    float3 size = _AABBMax - _AABBMin;
    float3 uvw = (pos - _AABBMin.xyz) / size;
    uvw.x += _Time.y * 0.1f;
    //uvw.y +=  sin(_Time.x * 0.05f) * 0.3f;
    //uvw.z += -_SinTime.y * 0.03f;
    float noise = SAMPLE_TEXTURE3D(_MainTex,g_LinearWrapSampler,uvw).r;
    return smoothstep(_Threshold, 1, noise);
}
float BeerLambert(float3 sigma_a,float d)
{
    return exp(-sigma_a*d);
}
float Phase(float g,float vl)
{
    float g2 = g * g;
    float denom = 1 + g2 - 2 * g * vl;
    return 1 / (4 * PI) * (1 - g2) / pow(denom, 1.5);
    // float denom = 1 + g * g - 2 * g * vl;
    // return 1 / (4 * PI) * (1 - g * g) / (denom * sqrt(vl));
}

float SphereSDF(float3 p, float radius = 1.0f)
{
    //return smoothstep(0.0, 1.0, radius / (length(p)+0.0001f));
    //return EvalDensity(p);
    return (-(length(p) - radius) + EvalDensity(p)) * _DensityMultply;
}
float3 EvalNormal(float3 p)
{
    float2 e = float2(.1, 0);
  float3 n = SphereSDF(p) - float3(
    SphereSDF(p-e.xyy),
    SphereSDF(p-e.yxy),
    SphereSDF(p-e.yyx));

  return normalize(n);
}

#define USE_DENSITY 1
#define MAX_STEP_COUNT 32
#define MAX_STEPS_LIGHTS 8
#define DENSITY_THRESHOLD 0.001
#define GOLDEN_RATIO 1.61803398875
#define PYHCICS_CLOUD 0
#define SUN_LIGHT_POWER 200

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
    float area_radius = abs(_AABBMax.y - _AABBMin.y) * 0.5f * _Fade;
    float3 in_scattering = float3(0,0,0);
    float light_step_size =  _StepSize;//dist / MAX_STEPS_LIGHTS;
    float light_step_div = 1.0f / (float)MAX_STEPS_LIGHTS;
    float density = 0.0f;
    [loop]
    for (uint light_step = 0; light_step < MAX_STEPS_LIGHTS; light_step++)
    {
        float3 sample_light_pos = p + light_dir * (light_step + 0.5f) * light_step_size;
        float cur_density = SphereSDF(sample_light_pos,area_radius);
        if (cur_density > DENSITY_THRESHOLD)
            density += SphereSDF(sample_light_pos,area_radius) * light_step_div;
        //float light_density = SphereSDF(sample_light_pos,area_radius);
        // float light_transmittance = 1.0;
        // if (light_density > DENSITY_THRESHOLD)
        // {
        //     float light_absorption = BeerLambert(light_density * _Absorption, light_step_size);
        //     light_transmittance *= light_absorption;
        //     in_scattering += light_absorption * light_transmittance * light_density * LIGHT_STEP_DIV;
        // }
    }
    float3 transmittance = MultipleOctaves(density, cos_theta, dist, sigma_t);
    // Return product of Beer's law and powder effect depending on the 
    // view direction angle with the light direction.
	return lerp(transmittance * 2.0 * (1.0 - (BeerLambert(density * 2.0 * sigma_t,light_step_size))), 
               transmittance, 
               0.5 + 0.5 * cos_theta);
}

float4 PSMain(PSInput i) : SV_Target
{
    float4 dst = float4(0, 0, 0, 0);
    Ray ray;
    i.local_pos *= _AABBScale.rgb;
    ray.origin = i.local_pos;
    // world space direction to object space
    float3 dir = normalize(i.world_pos - GetCameraPositionWS());
    ray.dir = normalize(mul(_MatrixInvWorld, float4(dir,0)));
    AABB aabb;
    aabb.min = _AABBMin.xyz;
    aabb.max = _AABBMax.xyz;

    float t0;
    float t1;
    Intersect(ray, aabb, t0, t1);
    t0 = max(0.0, t0);
    float dist = length(t1 - t0);
    float step_size = _StepSize;// dist / MAX_STEP_COUNT;
    float step_div = 1.0f / (float)MAX_STEP_COUNT;
    float3 total_transmittance = float3(1,1,1);
    float3 accumulated_color = float3(0, 0, 0);
    //t same as e
    float sigma_t = max(_Absorption + _Scattering, 1e-6);
    float3 light_dir = _MainlightWorldPosition.xyz;
    float3 light_color = _DirectionalLights[0]._LightColor * SUN_LIGHT_POWER;
    float area_radius = abs(_AABBMax.y - _AABBMin.y) * 0.5f * _Fade;
    float noise = SAMPLE_TEXTURE2D(_NoiseTex,g_LinearWrapSampler,i.position.xy * _ScreenParams.xx).r;
    uint uFrame = uint(_Time.w);
    float offset = frac(noise + float(uFrame % 32) / GOLDEN_RATIO);
    float cos_theta = dot(dir, light_dir) * 0.5 + 0.5;
    float phase = lerp(Phase(_G, cos_theta),Phase(-_G, cos_theta),0.7);
    float ambient_intensity = 0.25;
    ray.origin += ray.dir * offset * 0.25;
    [loop]
    for (uint step = 0; step < MAX_STEP_COUNT; step++)
    {
        float3 sample_pos = ray.origin + ray.dir * (step + 0.5f) * step_size;
        float density = SphereSDF(sample_pos,area_radius);
        float3 sample_sigma_t = density * sigma_t;
        float3 sample_sigma_s = density * _Scattering;
        if (density > DENSITY_THRESHOLD)
        {
            float3 ambient = SAMPLE_TEXTURECUBE_LOD(RadianceTex,g_LinearWrapSampler,i.local_pos,0);
            //In-Scattering
            float3 in_scattering = float3(0,0,0);
            Ray light_ray;
            light_ray.origin = sample_pos;
            light_ray.dir = light_dir;
            float t2,t3;
            if (Intersect(light_ray,aabb,t2,t3))
            {
                float dist = length(t3 - t2);
                in_scattering = LightMarch(sample_pos,cos_theta,-light_dir,dist,sigma_t);
                float3 luminance = ambient_intensity * ambient + light_color * phase * in_scattering;
                luminance *= sample_sigma_s;
                float3 cur_transmittance = BeerLambert(sample_sigma_t, step_size);
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
    //return float4(alpha,alpha,alpha,1);
    return float4(accumulated_color * _Exposure, alpha); //
    /*
    // float3 start = ray.origin + ray.dir * t0;
    float3 start = ray.origin;
    float3 end = ray.origin + ray.dir * t1;
    float dist = length(t1 - t0);
    int setp_count = MAX_STEP_COUNT;//min(MAX_STEP_COUNT,dist / _StepSize);
    float step_size = dist / setp_count;
    float step_div = 1.0f / (float)setp_count;
    float step_light_div = 1.0f / (float)MAX_STEPS_LIGHTS;
    
    float3 ds = normalize(end - start) * step_size;
    float3 p = start;
    float transparency = 1;
    float3 light_color = _DirectionalLights[0]._LightColor;
    float3 light_dir = -_MainlightWorldPosition.xyz;
    float3 result = 0.0.xxx;
    float sigma_t = _Absorption+_Scattering;
    float4 res = float4(0,0,0,0);
    //float blueNoise = SAMPLE_TEXTURE2D(_NoiseTex,g_LinearWrapSampler,i.position.xy * _ScreenParams.xy).r;
    float blueNoise = SAMPLE_TEXTURE2D(_NoiseTex,g_LinearWrapSampler,i.uv * 5).r;
    //float offset = frac(noise);
    float radius = abs(_AABBMax.y - _AABBMin.y) * 0.5f * 0.8f;
    uint uFrame = uint(_Time.w);
    //float offset = frac(blueNoise + float(uFrame%32) / sqrt(0.5));
    float offset = frac(blueNoise);
    step_size *= offset;
    [loop]
    for (int iter = 0; iter < setp_count; iter++)
    {
        float3 sample_pos = ray.origin + ray.dir * (iter+0.5f) * step_size;
        float density = SphereSDF(sample_pos,radius);
        if (density > 0.001)
        {
        #if defined(PYHCICS_CLOUD)
            float light_transmittance = LightMarch(sample_pos,t1 - t0,offset);
            float lum = density;
            transparency *= light_transmittance;
            float3 lin = (float3(0.60,0.60,0.75) * 1.1 + 0.8 * float3(1.0,0.6,0.3)) * transparency * lum * step_div;
            res.rgb += lin;
            res.a += (density * (1.0-res.a)) * step_div;
        #else
            float3 n = EvalNormal(sample_pos);
            float diffuse = saturate(dot(n,-light_dir));
            float3 lin = float3(0.60,0.60,0.75) * 0.9 + 0.8 * float3(1.0,0.6,0.3) * diffuse;
            float4 color = float4(lerp(float3(1.0,1.0,1.0), float3(0.0, 0.0, 0.0), density), density );
            color.rgb *= lin;
            color.rgb *= color.a;
            res += color * (1.0 - res.a);
        #endif//PYHCICS_CLOUD
        }
    }
    return res;
    */
}