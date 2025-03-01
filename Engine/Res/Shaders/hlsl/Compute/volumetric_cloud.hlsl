#include "cs_common.hlsli"
#include "../geometry.hlsli"
#include "../atmosphere_common.hlsli"
#pragma kernel CloudMain
#pragma multi_compile CloudMain _QUALITY_LOW _QUALITY_HIGH
#pragma multi_compile CloudMain _ _TILE_RENDER
#pragma kernel CloudReprojection

#define THREAD_NUM 8

CBUFFER_START(CloudInfo)
    float4 _CloudTex_TexelSize;
    float3 _BaseWorldPos;
    float _height;
    float4 _Params;
    float3 _Absorption;
    float _Exposure;
    float3 _Scattering;
    float _DensityMultply;
    float _Threshold;
    float _thickness;
    uint2 _pixel_offset;
CBUFFER_END

TEXTURE3D(_ShapeNoise)
TEXTURE3D(_DetailNoise)
TEXTURE2D(_NoiseTex)
TEXTURE2D(_CameraDepthTexture)
TEXTURE2D(_WeatherMap)

RWTEXTURE2D(_CloudTex,float4)

#define TYPE_OFFSET _Params.w
#define _Speed     _Params.z
#define _CloudZone float2(_height,_height + _thickness)


#if defined(_QUALITY_HIGH)
    #define MAX_STEP_COUNT 128
    #define MAX_STEPS_LIGHTS 8
#else
    #define MAX_STEP_COUNT 32
    #define MAX_STEPS_LIGHTS 8
#endif

#define BLOCK_WIDTH 4
#define LIGHT_RAY_STEP_SIZE 200
#define CLOUD_BASE_FREQ .00003
#define CLOUD_DETAIL_FREQ .0019
#define EARTH_RADIUS 6371000
#define G -0.3f
#define DENSITY_THRESHOLD 0.0
#define GOLDEN_RATIO 1.61803398875
#define PYHCICS_CLOUD 0
#define SUN_LIGHT_POWER 300
#define MAX_CLOUD_DIST 20000

#define MAIN_RAY_THINKNESS 0.07
#define LIGHT_RAY_THINKNESS 0.07

/* Ref
https://www.shadertoy.com/view/ttcSD8
https://www.shadertoy.com/view/3sffzj single_cloud
*/

 // Utility function that maps a value from one range to another.
 //GPU Pro7 2.4
float Remap( float original_value, float original_min,float original_max, float new_min, float new_max)
{
    return new_min + (((original_value - original_min) /
        ( original_max - original_min)) * (new_max - new_min));
}

float HeightDensityCurve(float h,float offset,float min0,float max0,float min1,float max1,float multiply,float pow_multiply)
{
    float a = smoothstep(min0,max0,h + offset);
    float b = smoothstep(min1,max1,1-h+ offset);
    return saturate(pow(a*b*multiply,pow_multiply));
}

float GetDensityHeightGradientForPoint(float height_frac,float3 weather)
{
    float type = saturate(weather.z + TYPE_OFFSET);
    //层云
    float stratus = HeightDensityCurve(height_frac,0.0,0.022,0.413,0.869,0.909,12.6,2);
    //积云
    float cumulus = HeightDensityCurve(height_frac,0.097,0.039,0.942,0.516,1,6.6,1.6);
    //积雨云
    float cumulonimbus = HeightDensityCurve(height_frac,0.026,0.025,0.321,0,0.572,2.5,1.2);
    float t1 = smoothstep(0.0, 0.5, type);
    float t2 = smoothstep(0.5, 1.0, type);
    float final_density = lerp(stratus,cumulus,t1);
    final_density = lerp(cumulus,cumulonimbus,t2);
    return final_density;
}

float GetHeightFrac(float3 p,float2 cloud_zone)
{
    return (p.y - EARTH_RADIUS - cloud_zone.x) / (cloud_zone.y - cloud_zone.x);
}

float EvalDensity(float3 pos,float mip_map,bool is_detail)
{
    float3 wind_dir = float3(1.0,0.0,0.0);
    float cloud_top_offset = 1300.0;
    float height_frac = GetHeightFrac(pos,_CloudZone);
    float3 weather_data = SAMPLE_TEXTURE2D_LOD(_WeatherMap,g_LinearWrapSampler,pos.xz* CLOUD_BASE_FREQ,mip_map).rgb;
    float density_height_gradient = GetDensityHeightGradientForPoint(height_frac,weather_data);
    pos += height_frac * wind_dir * cloud_top_offset;
    pos += (wind_dir + float3(0.0,0.2,0.0)) * _Time.y * _Speed * 100;
    float4 noise = SAMPLE_TEXTURE3D_LOD(_ShapeNoise,g_LinearWrapSampler,pos * CLOUD_BASE_FREQ,mip_map);
    //noise = pow(noise,0.45);
    float wfbm = noise.y * 0.625 + noise.z * 0.25 + noise.w * 0.125;
    float base_cloud = Remap(noise.x, wfbm-1.0, 1.0,0.0,1.0);
    base_cloud = Remap(base_cloud,0.85,1.0,0.0,1.0);
    base_cloud *= density_height_gradient;
    float cloud_coverage = 1.0;
    float base_cloud_coverage = Remap(base_cloud,1 - cloud_coverage ,1.0,0.0,1.0);
    base_cloud_coverage *= cloud_coverage;
    float final_cloud = base_cloud_coverage;
    if (is_detail)
    {
        float3 detail_uvw = pos * CLOUD_DETAIL_FREQ;
        detail_uvw.z +=_Time.y * -1.5 * 0.4;
        detail_uvw.y -=_Time.y *  2.3 * 0.4;
        float3 high_freq_noise = SAMPLE_TEXTURE3D_LOD(_DetailNoise,g_LinearWrapSampler,detail_uvw,mip_map).rgb;
        float high_freq_fbm = high_freq_noise.r * 0.625 + high_freq_noise.g * 0.25 + high_freq_noise.b * 0.125;
        float high_freq_noise_modifier = lerp(high_freq_fbm,1.0 - high_freq_fbm,saturate(height_frac * 10.0));
        final_cloud = Remap(base_cloud_coverage,high_freq_noise_modifier*0.2,1.0,0.0,1.0);
    }
    return Remap(final_cloud,_Threshold,1,0,_DensityMultply);
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

float3 AdjustSaturation(float3 color, float saturation)
{
    float gray = dot(color, float3(0.3, 0.59, 0.11));
    return lerp(float3(gray, gray, gray), color, saturation);
}

float3 LightMarch(float3 p,float cos_theta,float3 light_dir,float dist,float3 sigma_t)
{
    float3 in_scattering = float3(0,0,0);
    float light_step_div = 1.0f / (float)MAX_STEPS_LIGHTS;
    float total_length = LIGHT_RAY_STEP_SIZE * MAX_STEPS_LIGHTS;
    float density = 0.0f;
    float total_dis = 0.0f;
    [loop]
    for (uint light_step = 0; light_step < MAX_STEPS_LIGHTS; light_step++)
    {
        float3 sample_light_pos = p + light_dir * (light_step + 0.5f) * LIGHT_RAY_STEP_SIZE;
        float cur_density = EvalDensity(sample_light_pos,light_step * 0.5f,false);
        if (cur_density > DENSITY_THRESHOLD)
            density += cur_density * light_step_div;
    }
    float3 transmittance = MultipleOctaves(density, cos_theta, LIGHT_RAY_THINKNESS, sigma_t);
    // Return product of Beer'is law and powder effect depending on the 
    // view direction angle with the light direction.
	return lerp(transmittance * 2.0 * (1.0 - (BeerLambert(density * 2.0 * sigma_t,LIGHT_RAY_THINKNESS))), 
               transmittance, 
               0.5 + 0.5 * cos_theta);
}
float3 ACESFilm(float3 x){
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
[numthreads(THREAD_NUM,THREAD_NUM,1)]
void CloudMain(CSInput input)
{
#if defined(_TILE_RENDER)
    //4x4 block,update 1 pixels at one frame,so we need 16 frame to update all pixels
    uint2 pixel_index = input.DispatchThreadID.xy * BLOCK_WIDTH + _pixel_offset;
#else
    uint2 pixel_index = input.DispatchThreadID.xy;
#endif
    if (pixel_index.x >= _CloudTex_TexelSize.z || pixel_index.y >= _CloudTex_TexelSize.w)
        return;
    float2 uv = (pixel_index + 0.5.xx) * _CloudTex_TexelSize.xy;
    float depth = SAMPLE_TEXTURE2D_DEPTH_LOD(_CameraDepthTexture,g_LinearClampSampler,uv,0);
    if (depth < 0.99)
    {
        _CloudTex[pixel_index] = float4(0,0,0,1);
        return;
    }
    float3 cam_dir = lerp(_LT,_RT,uv.x);
    cam_dir = lerp(cam_dir,lerp(_LB,_RB,uv.x),uv.y);
    cam_dir = normalize(cam_dir - _CameraPos.xyz);
    float3 cam_pos = _CameraPos.xyz;
    cam_pos.y += EARTH_RADIUS;
    Ray ray;
    ray.o = cam_pos;
    ray.d = cam_dir;
    float3 p;
    Sphere is;
    is.c = 0.0.xxx;
    is.r = EARTH_RADIUS + _CloudZone.x;
    Sphere os;
    os.c = 0.0.xxx;
    os.r = EARTH_RADIUS + _CloudZone.x + _CloudZone.y;
    float step_div = 1.0f / (float)MAX_STEP_COUNT;
    float3 total_transmittance = float3(1,1,1);
    float3 accumulated_color = float3(0, 0, 0);
    //t same as e
    float sigma_t = max(_Absorption + _Scattering, 1e-6);
    float3 light_dir = _MainlightWorldPosition.xyz;
    float sun_atten = dot(float3(0,1,0),-light_dir) * 0.5 + 0.5;
    //sun_atten = smoothstep(-1,1.0,sun_atten);
    float3 light_color = _DirectionalLights[0]._LightColor * SUN_LIGHT_POWER;
    float noise = SAMPLE_TEXTURE2D_LOD(_NoiseTex,g_LinearWrapSampler,uv,0).r;
    uint uFrame = uint(_Time.w);
    float offset = frac(noise + float(uFrame % 1023) / GOLDEN_RATIO);
    float cos_theta = dot(cam_dir, light_dir) * 0.5 + 0.5;
    float phase = lerp(Phase(G, cos_theta),Phase(-G, cos_theta),0.7);
    float ambient_intensity = 0.25;
    float step_size = 75;
    bool is_in_cloud = false;
    if (cam_pos.y < EARTH_RADIUS + _CloudZone.x) //云层下方观察
    {
        float3 out_p,in_p;
        RaySphereIntersection(ray,os,out_p);
        RaySphereIntersection(ray,is,in_p);
        float dis = distance(in_p,out_p);
        step_size = min(dis,4 * _thickness) / MAX_STEP_COUNT;
        ray.o = in_p + ray.d * offset * step_size;
        if (dot(in_p - ray.o,float3(0,1,0)) > 0)
        {
            _CloudTex[pixel_index] = float4(0,0,0,1);
            return;
        }
    }
    else //云中
    {
        is_in_cloud = true;
        float3 in_p;
        RaySphereIntersection(ray,is,in_p);
        //step_size = max(step_size,distance(in_p,ray.o) / MAX_STEP_COUNT);
        ray.o = cam_pos + ray.d * offset * step_size;
    }
    float cloud_test = 0.0;
    int zero_density_sample_count = 0;
    float mip_map = 0.0;
    [loop]
    for (uint step = 0; step < MAX_STEP_COUNT; step++)
    {
        float3 sample_pos = ray.o + ray.d * step * step_size;
        if (is_in_cloud)
            step_size = step_size * exp(0.0006 * step);
        if (sample_pos.y > os.r)
            break;
        //float density = 0.0;
        // if (cloud_test > 0.0)
        // {
        //     density = EvalDensity(sample_pos,mip_map,true);
        //     if (density < 0.0)
        //     {
        //         zero_density_sample_count++;
        //     }
        //     if (zero_density_sample_count == 6)
        //     {
        //         cloud_test = 0.0;
        //         zero_density_sample_count = 0;
        //     }
        // }
        // else
        // {
        //     density = EvalDensity(sample_pos,mip_map,false);
        //     cloud_test = density;
        // }
        float density = EvalDensity(sample_pos,mip_map,true);
        float3 sample_sigma_t = density * sigma_t;
        float3 sample_sigma_s = density * _Scattering;
        if (density > DENSITY_THRESHOLD)
        {
            float3 ambient = 0.0.xxx;
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
                float3 end_p = normalize(light_ray.o + light_dir * 600000);
                end_p.y = max(0,end_p.y);
                end_p.y = lerp(0,0.005,end_p.y);
                light_color = getValFromSkyLUT(normalize(end_p),light_dir) * SUN_LIGHT_POWER * _DirectionalLights[0]._LightColor * sun_atten;
                light_color = AdjustSaturation(light_color,1-sun_atten);
                in_scattering = LightMarch(sample_pos,cos_theta,-light_dir,dist,sigma_t);
                float3 luminance = ambient_intensity * ambient + light_color * phase * in_scattering;
                luminance *= sample_sigma_s;
                float3 cur_transmittance = BeerLambert(sample_sigma_t,MAIN_RAY_THINKNESS);
                // Better energy conserving integration
                // "From Physically based sky, atmosphere and cloud rendering in Frostbite" 5.6
                // by Sebastian Hillaire.
                accumulated_color += total_transmittance * (luminance - luminance * cur_transmittance) / sample_sigma_t;
                total_transmittance *= cur_transmittance;
                if (length(total_transmittance) < 0.1)
                {
                    total_transmittance = float3(0, 0, 0);
                    break;
                }
            }
        }
    }
    float alpha = saturate(length(total_transmittance));
    //accumulated_color.r = Remap(accumulated_color.r,0,1,0,_Exposure);
    //accumulated_color.g = Remap(accumulated_color.g,0,1,0,_Exposure);
    //accumulated_color.b = Remap(accumulated_color.b,0,1,0,_Exposure);
    accumulated_color = pow(accumulated_color,_Exposure);
    _CloudTex[pixel_index] = float4(accumulated_color, alpha);
}

TEXTURE2D(_CloudHistoryTex)
TEXTURE2D(_MotionVectorTexture)

[numthreads(THREAD_NUM,THREAD_NUM,1)]
void CloudReprojection(CSInput input)
{
    uint2 pixel_index = input.DispatchThreadID.xy;
    if (pixel_index.x >= _CloudTex_TexelSize.z || pixel_index.y >= _CloudTex_TexelSize.w)
        return;
    if (pixel_index.x % BLOCK_WIDTH == _pixel_offset.x && pixel_index.y % BLOCK_WIDTH == _pixel_offset.y)
        return;
    float2 uv = (pixel_index + 0.5.xx) * _CloudTex_TexelSize.xy;
    float2 mv = _MotionVectorTexture[pixel_index].xy;
    float2 prev_uv = uv - mv;
    if (any(prev_uv != saturate(prev_uv)))
    {
        _CloudTex[input.DispatchThreadID.xy] = float4(0,0,0,1);
        return;
    }
    _CloudTex[input.DispatchThreadID.xy] = SAMPLE_TEXTURE2D_LOD(_CloudHistoryTex,g_LinearClampSampler,prev_uv,0);
}