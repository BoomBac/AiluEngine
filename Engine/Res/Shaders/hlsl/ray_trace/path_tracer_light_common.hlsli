#ifndef PATH_TRACER_LIGHT_COMMON_HLSLI
#define PATH_TRACER_LIGHT_COMMON_HLSLI

#include "rt_common.hlsli"
#include "sampling.hlsli"
#include "../geometry.hlsli"
#include "../bindless.hlsli"
#include "../sampler.hlsli"
#include "../material_sampling.hlsli"
#include "gpu_scene.hlsli"
#include "path_tracer_common.hlsli"

#if defined(DXR)
StructuredBuffer<uint> _PathLightLookup : register(t9, space0);
#else
StructuredBuffer<uint> _PathLightLookup;
#endif

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

    ByteAddressBuffer index_buffer = g_bindless_index_buffer[NonUniformResourceIndex(idx_buffer)];
    uint3 tri_indices = index_buffer.Load3(tri_idx * 12u);

    ByteAddressBuffer position_buffer = g_bindless_vertex_buffer[NonUniformResourceIndex(vert_buffer)];
    tri.v0 = LoadBindlessFloat3(position_buffer, tri_indices.x * 12u);
    tri.v1 = LoadBindlessFloat3(position_buffer, tri_indices.y * 12u);
    tri.v2 = LoadBindlessFloat3(position_buffer, tri_indices.z * 12u);

    if (valid_bindless_buffer(nor_buffer))
    {
        ByteAddressBuffer normal_buffer = g_bindless_vertex_buffer[NonUniformResourceIndex(nor_buffer)];
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
        ByteAddressBuffer uv_data = g_bindless_vertex_buffer[NonUniformResourceIndex(uv_buffer)];
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

bool UnifiedLightCastsShadow(uint light_index)
{
    return LoadUnifiedLight(light_index)._shadow_index >= 0;
}

float GetUnifiedLightSelectionPMF(uint light_count)
{
    return light_count > 0u ? rcp((float)light_count) : 0.0;
}

#if defined(PATH_TRACER_POWER_LIGHT_SAMPLING)
#if defined(DXR)
StructuredBuffer<UnifiedLightSamplingData> _UnifiedLightSamplingTable : register(t8, space0);
#else
StructuredBuffer<UnifiedLightSamplingData> _UnifiedLightSamplingTable;
#endif
#endif

float GetPathUnifiedLightSelectionPMF(uint light_index, uint light_count)
{
#if defined(PATH_TRACER_POWER_LIGHT_SAMPLING)
    if (light_index >= light_count)
        return 0.0f;
    return _UnifiedLightSamplingTable[light_index]._pmf;
#else
    return GetUnifiedLightSelectionPMF(light_count);
#endif
}

uint GetPathTriangleLightIndex(uint primitive_index, uint triangle_index)
{
    if (primitive_index >= _PathLightLookup[0] ||
        triangle_index >= g_primitive_data[primitive_index]._submesh_triangle_count)
        return 0xffffffffu;
    const uint triangle_start = _PathLightLookup[_PathLightLookup[3] + primitive_index];
    return triangle_start == 0xffffffffu ? 0xffffffffu : _PathLightLookup[triangle_start + triangle_index];
}

uint SamplePathUnifiedLightIndex(float random_value, uint light_count, out float selection_pmf)
{
    selection_pmf = 0.0f;
    if (light_count == 0u)
        return 0u;

#if defined(PATH_TRACER_POWER_LIGHT_SAMPLING)
    const float safe_random = min(random_value, 0.99999994f);
    const float scaled_random = safe_random * float(light_count);
    const uint bucket_index = min((uint)scaled_random, light_count - 1u);
    const float alias_random = frac(scaled_random);
    const UnifiedLightSamplingData bucket = _UnifiedLightSamplingTable[bucket_index];
    const uint light_index = alias_random < bucket._alias_probability ? bucket_index : bucket._alias_index;
    selection_pmf = GetPathUnifiedLightSelectionPMF(light_index, light_count);
    return light_index;
#else
    const float safe_random = min(max(random_value, 0.0f), 0.99999994f);
    const uint light_index = min((uint)(safe_random * float(light_count)), light_count - 1u);
    selection_pmf = GetPathUnifiedLightSelectionPMF(light_index, light_count);
    return light_index;
#endif
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
    if (valid_bindless_handle(light._emissive_map) && light._instance_index != 0xffffffffu)
    {
        const uint material_index = g_primitive_data[light._instance_index]._material_id;
#if defined(DXR)
        const MaterialData material = g_material_data[material_index];
#else
        const MaterialData material = g_material_buffer[material_index];
#endif
        radiance *= SampleMaterialTextureLevel(g_bindless_texture2d[NonUniformResourceIndex(light._emissive_map)],
            material._emission_sampler, uv, 0.0f, MATERIAL_MANUAL_MIP_BIAS).rgb;
    }
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

    float area = 0.5 * length(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    result._pdf_area = rcp(max(area, 1e-6));
    result._pdf = cos_theta_l > FLOAT_EPSILON ? PdfSolidAngleFromArea(result._pdf_area, r2, cos_theta_l) : 0.0f;
    result._wi = wi;
    result._t = r;
    result._normal = light_normal;
    result._position = y;
    result._uv = tri.uv0 * bary0 + tri.uv1 * bary1 + tri.uv2 * bary2;
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

    float area = 0.5 * length(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    result._pdf_area = rcp(max(area, 1e-6));
    result._pdf = cos_theta_l > FLOAT_EPSILON ? PdfSolidAngleFromArea(result._pdf_area, r2, cos_theta_l) : 0.0f;
    result._wi = wi;
    result._t = r;
    result._normal = light_normal;
    result._position = y;
    result._uv = tri.uv0 * bary0 + tri.uv1 * bary1 + tri.uv2 * bary2;
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
    float irradiance = PointLightDistanceAttenuation(light._position, light._range, x, light._radius_or_sun_angle);
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
        return light._color * LightRangeAttenuation(distance(light._position, x), light._range);
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

void ResolvePathSurfaceEmission(inout PathHit hit, float3 ray_direction, float2 uv, uint light_count)
{
    hit._light_index = GetPathTriangleLightIndex(hit._primitive_index, hit._triangle_index);
    if (hit._light_index == 0xffffffffu)
        return;
    const SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(hit._light_index));
    const TriangleData triangle_data = LoadTriangleAreaLightData(light);
    const float3 normal = GetTriangleAreaLightNormal(triangle_data);
    const float light_cos = light._is_double_sided ? abs(dot(normal, -ray_direction)) : dot(normal, -ray_direction);
    const float area = 0.5f * length(cross(triangle_data.v1 - triangle_data.v0, triangle_data.v2 - triangle_data.v0));
    hit._light_pdf = light_cos > FLOAT_EPSILON && area > FLOAT_EPSILON ?
        GetPathUnifiedLightSelectionPMF(hit._light_index, light_count) * hit._hit_t * hit._hit_t / (area * light_cos) : 0.0f;
    hit._light_radiance = light_cos > 0.0f ? EvaluateTriangleLightRadiance(light, uv) : 0.0.xxx;
}

float PdfDirectionalLight(SampleLightData light, float3 wi)
{
    float3 light_direction = normalize(light._direction);
    float3 tangent_direction = cross(light_direction, wi);
    float sine_radius = sin(DirectionalLightAngularRadius(light));
    if (dot(light_direction, wi) <= 0.0f || dot(tangent_direction, tangent_direction) > sine_radius * sine_radius)
        return 0.0f;
    return 1.0f / DirectionalLightSolidAngle(light);
}

float PowerHeuristicThree(float technique_pdf, float other_pdf, float third_pdf)
{
    const float scale = max(technique_pdf, max(other_pdf, third_pdf));
    if (scale <= 0.0f)
        return 0.0f;
    const float3 relative_pdf = float3(technique_pdf, other_pdf, third_pdf) / scale;
    return relative_pdf.x * relative_pdf.x / dot(relative_pdf, relative_pdf);
}

// Directional lights live at infinity, so BSDF rays evaluate them only after a scene miss.
float3 EvaluateDirectionalLightMiss(uint light_count, float3 wi, float bsdf_pdf, bool carry_mis)
{
    float3 radiance = 0.0.xxx;
    for (uint index = 0u; index < _PathLightLookup[1]; ++index)
    {
        const uint light_index = _PathLightLookup[4u + index];
        SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
        if (light._type != LIGHT_TYPE_DIRECTIONAL)
            continue;
        float light_pdf = PdfDirectionalLight(light, wi);
        if (light_pdf <= 0.0f)
            continue;
        const float selection_pmf = GetPathUnifiedLightSelectionPMF(light_index, light_count);
        // The sun has its own NEE sample in addition to unified-light selection and BSDF sampling.
        float mis_weight = carry_mis ? PowerHeuristicThree(bsdf_pdf, light_pdf * selection_pmf, light_pdf) : 1.0f;
        radiance += light._color * light_pdf * mis_weight;
    }
    return radiance;
}

float PdfUnifiedLight(uint light_index, float3 x, float3 wi)
{
    SampleLightData light = DecodeUnifiedLightEntry(LoadUnifiedLight(light_index));
    if (light._type == LIGHT_TYPE_DIRECTIONAL)
        return PdfDirectionalLight(light, wi);
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
    light_sample._position = hit_pos;
    if (light._type == LIGHT_TYPE_TRIANGLE_AREA)
    {
        TriangleData tri = LoadTriangleAreaLightData(light);
        //hit_uv = GetTriangleLightUV(tri, uv);
        light_sample._normal = GetTriangleAreaLightNormal(tri);
    }
    light_sample._radiance = EvaluateFiniteLightRadiance(light, ray_origin, light_sample._uv);
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

bool FindClosestPathAnalyticLightHit(uint light_count, float3 ray_origin, float3 ray_direction, float max_t,
                                    out uint light_index, out float light_t, out LightSample light_sample)
{
    bool found = false;
    light_index = 0u;
    light_t = max_t;
    light_sample = (LightSample)0;
    const uint list_start = 4u + _PathLightLookup[1];
    for (uint index = 0u; index < _PathLightLookup[2]; ++index)
    {
        const uint candidate_index = _PathLightLookup[list_start + index];
        float candidate_t;
        LightSample candidate_sample;
        if (!EvaluateUnifiedLightHit(light_count, candidate_index, ray_origin, ray_direction, candidate_t, candidate_sample) ||
            candidate_t >= light_t)
            continue;
        candidate_sample._pdf = PdfUnifiedLight(candidate_index, ray_origin, ray_direction) *
                                GetPathUnifiedLightSelectionPMF(candidate_index, light_count);
        found = true;
        light_index = candidate_index;
        light_t = candidate_t;
        light_sample = candidate_sample;
    }
    return found;
}

float DirectLightCosTheta(Material mat, float3 n, float3 wi)
{
    float ndotl = dot(n, wi);
    return mat._is_glass ? abs(ndotl) : saturate(ndotl);
}

#endif
