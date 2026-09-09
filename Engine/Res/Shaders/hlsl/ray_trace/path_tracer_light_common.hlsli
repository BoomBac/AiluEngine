#ifndef PATH_TRACER_LIGHT_COMMON_HLSLI
#define PATH_TRACER_LIGHT_COMMON_HLSLI

#include "rt_common.hlsli"
#include "sampling.hlsli"
#include "../geometry.hlsli"
#include "../bindless.hlsli"
#include "../sampler.hlsli"
#include "gpu_scene.hlsli"

float3 LoadBindlessFloat3(ByteAddressBuffer buffer, uint byte_offset)
{
    return asfloat(buffer.Load3(byte_offset));
}

float2 LoadBindlessFloat2(ByteAddressBuffer buffer, uint byte_offset)
{
    return asfloat(buffer.Load2(byte_offset));
}


bool LoadHitTriangleData(uint tri_idx,int vert_buffer,int nor_buffer,int uv_buffer,int idx_buffer,out TriangleData tri)
{
    tri = (TriangleData)0;

    if (!valid_bindless_buffer(vert_buffer) || !valid_bindless_buffer(nor_buffer) || !valid_bindless_buffer(uv_buffer) || !valid_bindless_buffer(idx_buffer))
        return false;

    ByteAddressBuffer index_buffer = g_bindless_index_buffer[idx_buffer];
    uint3 tri_indices = index_buffer.Load3(tri_idx * 12u);

    ByteAddressBuffer position_buffer = g_bindless_vertex_buffer[vert_buffer];
    tri.v0 = LoadBindlessFloat3(position_buffer, tri_indices.x * 12u);
    tri.v1 = LoadBindlessFloat3(position_buffer, tri_indices.y * 12u);
    tri.v2 = LoadBindlessFloat3(position_buffer, tri_indices.z * 12u);

    if (valid_bindless_buffer(nor_buffer))
    {
        ByteAddressBuffer normal_buffer = g_bindless_vertex_buffer[nor_buffer];
        tri.n0 = LoadBindlessFloat3(normal_buffer, tri_indices.x * 12u);
        tri.n1 = LoadBindlessFloat3(normal_buffer, tri_indices.y * 12u);
        tri.n2 = LoadBindlessFloat3(normal_buffer, tri_indices.z * 12u); 
    }
    else
    {
        float3 geometric_normal = normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
        tri.n0 = geometric_normal;
        tri.n1 = geometric_normal;
        tri.n2 = geometric_normal;
    }

    if (valid_bindless_buffer(uv_buffer))
    {
        ByteAddressBuffer uv_data = g_bindless_vertex_buffer[uv_buffer];
        tri.uv0 = LoadBindlessFloat2(uv_data, tri_indices.x * 8u);
        tri.uv1 = LoadBindlessFloat2(uv_data, tri_indices.y * 8u);
        tri.uv2 = LoadBindlessFloat2(uv_data, tri_indices.z * 8u);
    }

    return true;
}


UnifiedLightData LoadUnifiedLight(uint index)
{
    return _UnifiedLights[index];
}

float GetUnifiedLightSelectionPMF(uint light_count)
{
    return light_count > 0u ? rcp((float)light_count) : 0.0;
}

float GetLightShadowRayTMax(LightSample light_sample)
{
    return light_sample._t > FLOAT_EPSILON ? light_sample._t : 1e20;
}

bool IsSampleLightDoubleSided(SampleLightData light)
{
    return light._is_double_sided;
}

bool IsFiniteUnifiedLightType(uint light_type)
{
    return light_type != LIGHT_TYPE_DIRECTIONAL;
}

SampleLightData DecodeUnifiedLightEntry(UnifiedLightData light)
{
    SampleLightData decoded = (SampleLightData)0;
    decoded._color = light._radiance;
    decoded._type = light._type;
    decoded._position = light._position;
    decoded._radius_or_sun_angle = light._source_radius;
    decoded._range = light._range;
    decoded._direction = light._direction;
    decoded._light_u = light._type == LIGHT_TYPE_SPOT ? float3(light._spot_angle_scale, light._spot_angle_offset, 0.0) : light._shape_u;
    decoded._light_v = light._shape_v;
    decoded._instance_index = light._type == LIGHT_TYPE_TRIANGLE_AREA && light._shadow_index >= 0 ? (uint)light._shadow_index : 0xffffffffu;
    decoded._tri_index = light._tri_index;
    decoded._emissive_map = (uint)light._emissive_map;
    decoded._is_double_sided = (light._flags & AL_UNIFIED_LIGHT_FLAG_TWO_SIDED) != 0u;
    return decoded;
}

TriangleData LoadTriangleAreaLightData(SampleLightData light)
{
    if (light._instance_index == 0xffffffffu)
    //if (light._instance_index == 0xffffffffu || light._instance_index >= _inst_count)
        return (TriangleData)0;

    PrimitiveData inst = g_primitive_data[light._instance_index];
    TriangleData tri;
    LoadHitTriangleData(light._tri_index,inst._position_bindless_idx,inst._normal_bindless_idx,inst._uv_bindless_idx,inst._index_bindless_idx, tri);
    tri.v0 = mul(inst._local_to_world, float4(tri.v0, 1.0)).xyz;
    tri.v1 = mul(inst._local_to_world, float4(tri.v1, 1.0)).xyz;
    tri.v2 = mul(inst._local_to_world, float4(tri.v2, 1.0)).xyz;
    tri.n0 = normalize(mul(inst._local_to_world, float4(tri.n0, 0.0)).xyz);
    tri.n1 = normalize(mul(inst._local_to_world, float4(tri.n1, 0.0)).xyz);
    tri.n2 = normalize(mul(inst._local_to_world, float4(tri.n2, 0.0)).xyz);
    return tri;
}

float3 GetTriangleAreaLightNormal(TriangleData tri)
{
    return normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
}

float2 GetTriangleLightUV(TriangleData tri, float2 bary)
{
    float bary0 = 1.0 - bary.x - bary.y;
    return tri.uv0 * bary0 + tri.uv1 * bary.x + tri.uv2 * bary.y;
}

float3 EvaluateTriangleLightRadiance(SampleLightData light, float2 uv)
{
    float3 radiance = light._color;
    if (valid_bindless_handle(light._emissive_map))
        radiance *= SAMPLE_TEXTURE2D_LOD(g_bindless_texture2d[light._emissive_map], g_LinearClampSampler, uv, 0).rgb;
    return radiance;
}

LightSample SampleTriangleAreaLight(SampleLightData light, float3 x, inout RandomCtx ctx)
{
    LightSample result = (LightSample)0;
    TriangleData tri = LoadTriangleAreaLightData(light);
    float2 random_uv = rand2(ctx);
    float sqrt_r0 = sqrt(random_uv.x);
    float bary0 = 1.0 - sqrt_r0;
    float bary1 = sqrt_r0 * (1.0 - random_uv.y);
    float bary2 = sqrt_r0 * random_uv.y;

    float3 y = tri.v0 * bary0 + tri.v1 * bary1 + tri.v2 * bary2;
    float3 wi = y - x;
    float r2 = dot(wi, wi);
    if (r2 <= FLOAT_EPSILON)
        return result;

    float r = sqrt(r2);
    wi /= r;

    float3 light_normal = GetTriangleAreaLightNormal(tri);
    float cos_theta_l = dot(light_normal, -wi);
    if (light._is_double_sided)
        cos_theta_l = abs(cos_theta_l);
    if (cos_theta_l <= FLOAT_EPSILON)
        return result;

    float area = 0.5 * length(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    result._pdf = PdfSolidAngleFromArea(rcp(max(area, 1e-6)), r2, cos_theta_l);
    result._wi = wi;
    result._t = r;
    result._normal = light_normal;
    result._radiance = EvaluateTriangleLightRadiance(light, tri.uv0 * bary0 + tri.uv1 * bary1 + tri.uv2 * bary2);
    return result;
}

LightSample SampleTriangleAreaLight(SampleLightData light, float3 x, float2 random_uv)
{
    LightSample result = (LightSample)0;
    TriangleData tri = LoadTriangleAreaLightData(light);
    float sqrt_r0 = sqrt(random_uv.x);
    float bary0 = 1.0 - sqrt_r0;
    float bary1 = sqrt_r0 * (1.0 - random_uv.y);
    float bary2 = sqrt_r0 * random_uv.y;

    float3 y = tri.v0 * bary0 + tri.v1 * bary1 + tri.v2 * bary2;
    float3 wi = y - x;
    float r2 = dot(wi, wi);
    if (r2 <= FLOAT_EPSILON)
        return result;

    float r = sqrt(r2);
    wi /= r;

    float3 light_normal = GetTriangleAreaLightNormal(tri);
    float cos_theta_l = dot(light_normal, -wi);
    if (light._is_double_sided)
        cos_theta_l = abs(cos_theta_l);
    if (cos_theta_l <= FLOAT_EPSILON)
        return result;

    float area = 0.5 * length(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    result._pdf = PdfSolidAngleFromArea(rcp(max(area, 1e-6)), r2, cos_theta_l);
    result._wi = wi;
    result._t = r;
    result._normal = light_normal;
    result._radiance = EvaluateTriangleLightRadiance(light, tri.uv0 * bary0 + tri.uv1 * bary1 + tri.uv2 * bary2);
    return result;
}

bool HitAreaLight(SampleLightData light, float3 ray_origin, float3 ray_dir, inout float t)
{
    float3 p0 = light._position - 0.5 * light._light_u - 0.5 * light._light_v;
    float3 e1 = light._light_u;
    float3 e2 = light._light_v;
    float3 n = -normalize(cross(e1, e2));
    float denom = dot(ray_dir, n);

    if (!IsSampleLightDoubleSided(light) && denom >= -FLOAT_EPSILON)
        return false;

    if (abs(denom) < FLOAT_EPSILON)
        return false;

    float light_t = dot(p0 - ray_origin, n) / denom;
    if (light_t <= FLOAT_EPSILON)
        return false;

    float3 hit = ray_origin + light_t * ray_dir;
    float3 rel = hit - p0;
    float uu = dot(e1, e1);
    float vv = dot(e2, e2);
    float uv = dot(e1, e2);
    float ru = dot(rel, e1);
    float rv = dot(rel, e2);
    float det = uu * vv - uv * uv;
    if (abs(det) < FLOAT_EPSILON)
        return false;

    float inv_det = 1.0 / det;
    float a = (ru * vv - rv * uv) * inv_det;
    float b = (rv * uu - ru * uv) * inv_det;

    if (a < 0.0 || a > 1.0 || b < 0.0 || b > 1.0)
        return false;

    t = light_t;
    return true;
}

bool HitPointLight(SampleLightData light, float3 ray_origin, float3 ray_dir, inout float t)
{
    float3 center = light._position;
    float radius = light._radius_or_sun_angle;
    Ray ray = (Ray)0;
    ray.o = ray_origin;
    ray.d = ray_dir;
    Sphere sphere = (Sphere)0;
    sphere.c = center;
    sphere.r = radius;
    float3 hit;
    bool is_hit = RaySphereIntersection(ray, sphere, hit);
    t = is_hit ? length(hit - ray_origin) : 0;
    return is_hit;
}

bool HitTriangleAreaLight(SampleLightData light, float3 ray_origin, float3 ray_dir, out float t,out float2 uv)
{
    TriangleData tri = LoadTriangleAreaLightData(light);
    Ray ray = (Ray)0;
    ray.o = ray_origin;
    ray.d = ray_dir;
    uint cull_mode = light._is_double_sided ? CULL_NONE : CULL_BACK_FACE;
    bool is_hit = RayTriangleIntersection(ray, tri.v0, tri.v1, tri.v2, t, uv.x, uv.y, cull_mode);
    if (is_hit)
        uv = GetTriangleLightUV(tri,uv);
    return is_hit;
}

float PowerHeuristic(float pdfA, float pdfB)
{
    float a2 = pdfA * pdfA;
    float b2 = pdfB * pdfB;
    return a2 / max(a2 + b2, 1e-6);
}

float3 PointLightRadiance(SampleLightData light, float3 x)
{
    float irradiance = PointLightDistanceAttenuation(light._position, light._range, x);
    float solid_angle = PointLightSolidAngle(light._position, light._radius_or_sun_angle, x);
    return light._color * irradiance / max(solid_angle, 1e-6);
}

float SpotLightAngleAttenuationAtPoint(SampleLightData light, float3 x)
{
    return SpotLightAngleAttenuation(light._position, light._direction, light._light_u.x, light._light_u.y, x);
}

float3 EvaluateFiniteLightRadiance(SampleLightData light, float3 x, float2 uv)
{
    if (light._type == LIGHT_TYPE_RECT_AREA)
        return light._color;
    if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
        return EvaluateTriangleLightRadiance(light, uv);
    if (light._type == LIGHT_TYPE_SPOT)
        return PointLightRadiance(light, x) * SpotLightAngleAttenuationAtPoint(light, x);
    if (light._type == LIGHT_TYPE_POINT)
        return PointLightRadiance(light, x);
    return 0.0.xxx;
}

float PdfAreaLight(SampleLightData light, float3 x, float3 wi)
{
    float3 p0 = light._position - 0.5 * light._light_u - 0.5 * light._light_v;
    float3 light_u = light._light_u;
    float3 light_v = light._light_v;
    float3 light_normal = -normalize(cross(light_u, light_v));

    float denom = dot(wi, light_normal);
    if (!IsSampleLightDoubleSided(light) && denom >= -FLOAT_EPSILON)
        return 0;
    if (abs(denom) < FLOAT_EPSILON)
        return 0;

    float t = dot(p0 - x, light_normal) / denom;
    if (t <= FLOAT_EPSILON)
        return 0;

    float3 y = x + wi * t;
    float3 rel = y - p0;
    float uu = dot(light_u, light_u);
    float vv = dot(light_v, light_v);
    float uv = dot(light_u, light_v);
    float ru = dot(rel, light_u);
    float rv = dot(rel, light_v);
    float det = uu * vv - uv * uv;
    if (abs(det) < FLOAT_EPSILON)
        return 0;

    float inv_det = 1.0 / det;
    float a = (ru * vv - rv * uv) * inv_det;
    float b = (rv * uu - ru * uv) * inv_det;
    if (a < 0.0 || a > 1.0 || b < 0.0 || b > 1.0)
        return 0;

    float cos_theta_l = dot(light_normal, -wi);
    if (IsSampleLightDoubleSided(light))
        cos_theta_l = abs(cos_theta_l);
    if (cos_theta_l <= 0)
        return 0;

    float area = length(cross(light_u, light_v));
    return PdfSolidAngleFromArea(1.0 / max(area, 1e-6), t * t, cos_theta_l);
}

float PdfPointLight(SampleLightData light, float3 x, float3 wi)
{
    float radius = max(light._radius_or_sun_angle, 1e-6);
    float3 to_center = light._position - x;
    float dist2_center = dot(to_center, to_center);
    if (dist2_center <= radius * radius)
        return 1.0 / (4.0 * PI);

    float light_t;
    if (!HitPointLight(light, x, wi, light_t))
        return 0.0;

    float3 y = x + light_t * wi;
    float3 light_normal = normalize(y - light._position);
    float cos_theta_l = dot(light_normal, -wi);
    if (cos_theta_l <= 0.0)
        return 0.0;

    float sin_theta_max2 = saturate(radius * radius / dist2_center);
    float cos_theta_max = sqrt(max(0.0, 1.0 - sin_theta_max2));
    return 1.0 / max(2.0 * PI * (1.0 - cos_theta_max), 1e-6);
}

float PdfMeshLight(SampleLightData light, float3 x, float3 wi)
{
    TriangleData tri = LoadTriangleAreaLightData(light);
    float light_t = 0.0;
    float2 bary = 0.0.xx;
    if (!HitTriangleAreaLight(light, x, wi, light_t, bary))
        return 0.0;

    float3 light_normal = GetTriangleAreaLightNormal(tri);
    float cos_theta_l = dot(light_normal, -wi);
    if (light._is_double_sided)
        cos_theta_l = abs(cos_theta_l);
    if (cos_theta_l <= FLOAT_EPSILON)
        return 0.0;

    float area = 0.5 * length(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    return PdfSolidAngleFromArea(rcp(max(area, 1e-6)), light_t * light_t, cos_theta_l);
}

float PdfUnifiedLight(uint light_index, float3 x, float3 wi)
{
    SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
    if (light._type == LIGHT_TYPE_RECT_AREA)
        return PdfAreaLight(light, x, wi);
    if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
        return PdfMeshLight(light, x, wi);
    if (light._type == LIGHT_TYPE_POINT)
        return PdfPointLight(light, x, wi);
    if (light._type == LIGHT_TYPE_SPOT)
    {
        float light_t = 0.0;
        if (!HitPointLight(light, x, wi, light_t))
            return 0.0;
        float3 hit_pos = x + light_t * wi;
        float angle_atten = SpotLightAngleAttenuationAtPoint(light, hit_pos);
        return angle_atten > 0.0 ? PdfPointLight(light, x, wi) : 0.0;
    }
    return 0.0;
}

LightSample SampleUnifiedLight(uint light_index, float3 x, float3 n, inout RandomCtx ctx)
{
    SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
    if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
        return SampleTriangleAreaLight(light, x, ctx);
    return SampleLight(light, x, n, ctx);
}

LightSample SampleUnifiedLight(uint light_index, float3 x, float3 n, float2 random_uv)
{
    SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
    if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
        return SampleTriangleAreaLight(light, x, random_uv);
    return SampleLight(light, x, n, random_uv);
}

bool EvaluateUnifiedLightHit(uint light_num,uint light_index, float3 ray_origin, float3 ray_dir, out float light_t, out LightSample light_sample)
{
    light_t = 0.0;
    light_sample = (LightSample)0;
    SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
    if (!IsFiniteUnifiedLightType(light._type))
        return false;
    float2 uv;
    bool is_hit = false;
    if (light._type == LIGHT_TYPE_RECT_AREA)
        is_hit = HitAreaLight(light, ray_origin, ray_dir, light_t);
    else if (light._type == LIGHT_TYPE_POINT || light._type == LIGHT_TYPE_SPOT)
        is_hit = HitPointLight(light, ray_origin, ray_dir, light_t);
    else if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
        is_hit = HitTriangleAreaLight(light, ray_origin, ray_dir, light_t, light_sample._uv);
    else
        return false;

    if (!is_hit)
        return false;

    float3 hit_pos = ray_origin + light_t * ray_dir;
    //float2 hit_uv = 0.0.xx;
    light_sample._wi = ray_dir;
    light_sample._pdf = PdfUnifiedLight(light_index, ray_origin, ray_dir) * GetUnifiedLightSelectionPMF(light_num);
    light_sample._t = light_t;
    if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
    {
        TriangleData tri = LoadTriangleAreaLightData(light);
        //hit_uv = GetTriangleLightUV(tri, uv);
        light_sample._normal = GetTriangleAreaLightNormal(tri);
    }
    light_sample._radiance = EvaluateFiniteLightRadiance(light, hit_pos, light_sample._uv);
    if (light._type == LIGHT_TYPE_RECT_AREA)
        light_sample._normal = -normalize(cross(light._light_u, light._light_v));
    else if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
        light_sample._normal = light_sample._normal;
    else
        light_sample._normal = normalize(hit_pos - light._position);

    return light_sample._pdf > 0.0 && any(light_sample._radiance > 0.0.xxx);
}

float3 ReconstructLightNormal(uint light_index, float3 hit_pos)
{
    SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
    if (light._type == LIGHT_TYPE_RECT_AREA)
        return -normalize(cross(light._light_u, light._light_v));
    else if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
        return GetTriangleAreaLightNormal(LoadTriangleAreaLightData(light));
    else if (light._type == LIGHT_TYPE_POINT)
        return normalize(hit_pos - light._position);
    else
        return normalize(hit_pos);
}

bool FindClosestUnifiedLightHit(uint light_num,float3 ray_origin, float3 ray_dir, float max_t, out uint light_index, out float light_t, out LightSample light_sample)
{
    bool hit_anything = false;
    light_index = 0u;
    light_t = max_t;
    light_sample = (LightSample)0;

    [loop]
    for (uint index = 0u; index < light_num; ++index)
    {
        float candidate_t = 0.0;
        LightSample candidate_sample = (LightSample)0;
        if (!EvaluateUnifiedLightHit(light_num, index, ray_origin, ray_dir, candidate_t, candidate_sample))
            continue;
        if (candidate_t >= light_t)
            continue;

        hit_anything = true;
        light_index = index;
        light_t = candidate_t;
        light_sample = candidate_sample;
    }

    return hit_anything;
}

float3 ProceduralSky(float3 d)
{
    float h = saturate(d.y);

    float3 zenith = float3(0.22, 0.35, 0.95);
    float3 horizon = float3(0.8, 0.85, 0.9);
    float3 ground = float3(0.1, 0.1, 0.1);

    float3 sky = lerp(horizon, zenith, pow(h, 0.2));
    float horizon_boost = exp(-abs(d.y) * 20.0);
    sky += horizon * horizon_boost * 0.2;

    float horizon_width = 0.05;
    float t = smoothstep(-horizon_width, horizon_width, d.y);
    return lerp(ground, sky, t);
}

float DirectLightCosTheta(Material mat, float3 n, float3 wi)
{
    float ndotl = dot(n, wi);
    return mat._is_glass ? abs(ndotl) : saturate(ndotl);
}

#endif
