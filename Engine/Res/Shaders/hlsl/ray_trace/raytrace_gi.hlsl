#include "rt_common.hlsli"
#include "../Compute/cs_common.hlsli"
#include "../bindless.hlsli"
#include "../color_space_utils.hlsli"
#include "sampling.hlsli"
#include "hit.hlsli"
#include "restir_di.hlsli"

// ===== Ray Trace GI Debug 功能使用说明 =====
//
// 在编译时定义 DEBUG_MODE 宏来选择不同的调试输出：
//
// DEBUG_MODE = 0  (DEBUG_MODE_NONE)         - 无 debug 输出，正常渲染
// DEBUG_MODE = 1  (DEBUG_MODE_PDF)          - 可视化 PDF 值 (热力图)
// DEBUG_MODE = 2  (DEBUG_MODE_BRDF)         - 可视化 BRDF 值 (f)
// DEBUG_MODE = 3  (DEBUG_MODE_NORMAL)       - 可视化法线方向
// DEBUG_MODE = 4  (DEBUG_MODE_WI)           - 可视化入射光方向
// DEBUG_MODE = 5  (DEBUG_MODE_WO)           - 可视化出射光方向
// DEBUG_MODE = 6  (DEBUG_MODE_D_GTR2)       - 可视化 D 项 (GTR2 法线分布)
// DEBUG_MODE = 7  (DEBUG_MODE_G_SMITH)      - 可视化 G 项 (Smith 几何遮蔽)
// DEBUG_MODE = 8  (DEBUG_MODE_F_FRESNEL)    - 可视化 F 项 (Fresnel 反射)
// DEBUG_MODE = 9  (DEBUG_MODE_COS_THETA)    - 可视化 cos(theta) = dot(n, v)
// DEBUG_MODE = 10 (DEBUG_MODE_ROUGHNESS)   - 可视化粗糙度
// DEBUG_MODE = 11 (DEBUG_MODE_THROUGHPUT)   - 可视化 throughput 衰减
// DEBUG_MODE = 12 (DEBUG_MODE_EMISSION)     - 可视化自发光
// DEBUG_MODE = 99 (DEBUG_MODE_ALL)          - 完整分析模式 (RGB 分别显示 D/G/PDF)
//
// 使用示例 (在编译时添加宏定义)：
//   dxc -D DEBUG_MODE=1 raytrace_gi.hlsl ...
//
// 对于 pick 像素 (_PickPixel)，还会在 _debug_rays 缓冲区输出详细的射线可视化数据
// ============================================

#pragma kernel RayGen
#pragma kernel PrimaryRay
#pragma kernel Denoise

#define MAX_ACCUMULATED_FRAMES 4096
#define ADDITIONAL_SAMPLING 1
#define PER_FRAME_SAMPLES 4

RWTEXTURE2D(_GI_Texture,float4)

CBUFFER_START(ComputeCB)
    int2 _PickPixel;
    int2 _GI_TileOffset;
    int2 _GI_TileSize;
    uint _inst_count;
    uint _tlas_count;
    uint _blas_count;
    uint _tri_count;
    uint _debug_hit_box_idx;
    uint _frame_index;
    bool _show_debug;
    uint _light_count;
    uint _scene_bindless_idx;
    bool _enable_ris;
    bool _enable_resampling;
    uint _prev_surface_buffer_idx;
    uint _curr_surface_buffer_idx;
CBUFFER_END

// ===== Debug 宏定义 =====
// 使用方法：在编译前定义对应宏，例如 #define DEBUG_PDF 1
// DEBUG_PDF           - 输出 PDF 值
// DEBUG_BRDF          - 输出 BRDF 值 (f)
// DEBUG_NORMAL        - 输出法线可视化
// DEBUG_WI            - 输出入射光方向
// DEBUG_WO            - 输出出射光方向
// DEBUG_D_GTR2        - 输出 D 项 (GTR2 分布)
// DEBUG_G_SMITH       - 输出 G 项 (Smith 几何)
// DEBUG_F_FRESNEL     - 输出 F 项 (Fresnel)
// DEBUG_COS_THETA     - 输出 cos(theta) = dot(n, v)
// DEBUG_ROUGHNESS     - 输出粗糙度
// DEBUG_THROUGHPUT    - 输出 throughput 衰减
// DEBUG_EMITTION      - 输出自发光
// DEBUG_ALL           - 输出所有数据 (用于单像素详细分析)

#define DEBUG_MODE_NONE         0
#define DEBUG_MODE_PDF          1
#define DEBUG_MODE_BRDF         2
#define DEBUG_MODE_NORMAL       3
#define DEBUG_MODE_WI           4
#define DEBUG_MODE_WO           5
#define DEBUG_MODE_D_GTR2       6
#define DEBUG_MODE_G_SMITH      7
#define DEBUG_MODE_F_FRESNEL    8
#define DEBUG_MODE_COS_THETA    9
#define DEBUG_MODE_ROUGHNESS    10
#define DEBUG_MODE_THROUGHPUT   11
#define DEBUG_MODE_EMISSION     12
#define DEBUG_MODE_ALL          99

//#define DEBUG_MODE DEBUG_MODE_NORMAL
#define RAYTRACE_GI_HIT_USE_BINDLESS_TRIANGLE_BUFFER 1
#define RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS 1

#ifndef DEBUG_MODE
    #define DEBUG_MODE DEBUG_MODE_NONE
#endif
#ifndef RAYTRACE_GI_HIT_USE_BINDLESS_TRIANGLE_BUFFER
    #define RAYTRACE_GI_HIT_USE_BINDLESS_TRIANGLE_BUFFER 0
#endif
#ifndef RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS
    #define RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS 0
#endif


AppendStructuredBuffer<DebugRay>   _debug_rays;
AppendStructuredBuffer<uint>   _debug_ray_indices;

StructuredBuffer<Reservoir>    g_prev_reservoir;
RWStructuredBuffer<Reservoir>  g_curr_reservoir;


StructuredBuffer<MaterialData> g_material_buffer;
TEXTURE2D(_MotionVectorTexture)
//ConstantBuffer<UnifiedLightBufferConfig> _UnifiedLightConfig;


uint GetUnifiedLightCount()
{
    return _light_count;
}

#include "path_tracer_light_common.hlsli"

// 固定的每层颜色
static const float3 kDepthColors[kMaxDepth] =
{
    float3(0.5, 0.0, 1.0),    // depth = 7  (紫)
    float3(0.0, 1.0, 0.0),   // depth = 1  (绿)
    float3(0.0, 0.0, 1.0),   // depth = 2  (蓝)
    float3(1.0, 1.0, 0.0),   // depth = 3  (黄)
    float3(1.0, 0.0, 1.0),   // depth = 4  (品红)
    float3(0.0, 1.0, 1.0),   // depth = 5  (青)
    float3(1.0, 0.5, 0.0),   // depth = 6  (橙)
    float3(0.5, 0.0, 1.0),    // depth = 7  (紫)
    float3(1.0, 0.0, 0.0)    // miss
};


uint PixelToLinearIndex(uint2 pixel)
{
    return pixel.y * (uint)_ScreenParams.z + pixel.x;
}

static float reflectance(float cosine, float refraction_index) 
{
    // Use Schlick's approximation for reflectance.
    float r0 = (1 - refraction_index) / (1 + refraction_index);
    r0 = r0*r0;
    float a = (1 - cosine);
    return r0 + (1-r0)* Pow4(a) * a;
}


void DebugDrawAABB(float3 bmin, float3 bmax, float scale,uint idx,float4 color = float4(1,0,0,1))
{
    // AABB 真实中心点
    float3 center = (bmin + bmax) * 0.5;

    // 以中心缩放偏移量
    float3 half_size = (bmax - bmin) * 0.5 * scale;
    float3 smin = center - half_size;
    float3 smax = center + half_size;

    float3 corners[8];
    corners[0] = float3(smin.x, smin.y, smin.z);
    corners[1] = float3(smax.x, smin.y, smin.z);
    corners[2] = float3(smax.x, smax.y, smin.z);
    corners[3] = float3(smin.x, smax.y, smin.z);
    corners[4] = float3(smin.x, smin.y, smax.z);
    corners[5] = float3(smax.x, smin.y, smax.z);
    corners[6] = float3(smax.x, smax.y, smax.z);
    corners[7] = float3(smin.x, smax.y, smax.z);

    int2 edges[12] = {
        int2(0,1), int2(1,2), int2(2,3), int2(3,0),
        int2(4,5), int2(5,6), int2(6,7), int2(7,4),
        int2(0,4), int2(1,5), int2(2,6), int2(3,7)
    };

    for (int e = 0; e < 12; ++e)
    {
        DebugRay dr = (DebugRay)0;
        dr.pos = corners[edges[e].x];
        dr.color = PackFloat4(color);
        dr.aabb_idx = idx;
        _debug_rays.Append(dr);

        dr.pos = corners[edges[e].y];
        dr.color = PackFloat4(color);
        _debug_rays.Append(dr);
    }
}


void DebugDrawRay(float3 origin, float3 dir,float4 color = float4(1,0,0,1))
{
    DebugRay dr = (DebugRay)0;
    dr.pos = origin;
    dr.aabb_idx = -1;
    dr.color = PackFloat4(color);
    _debug_rays.Append(dr);
    dr.pos = origin + dir * 1000.0f;
    _debug_rays.Append(dr);
}

void DebugDrawTriangle(float3 v0, float3 v1, float3 v2,float4 color = float4(1,0,0,1))
{
    DebugRay dr = (DebugRay)0;
    dr.pos = v0;
    dr.aabb_idx = -1;
    dr.color = PackFloat4(color);
    _debug_rays.Append(dr);
    dr.pos = v1;
    _debug_rays.Append(dr);
    
    dr.pos = v1;
    _debug_rays.Append(dr);
    dr.pos = v2;
    _debug_rays.Append(dr);
    
    dr.pos = v2;
    _debug_rays.Append(dr);
    dr.pos = v0;
    _debug_rays.Append(dr);
}

void AppendDebugLine(float3 start, float3 end, float4 color)
{
    DebugRay dr = (DebugRay)0;
    dr.pos = start;
    dr.color = PackFloat4(color);
    dr.aabb_idx = -1;
    _debug_rays.Append(dr);
    dr.pos = end;
    _debug_rays.Append(dr);
}

//返回指向光源的入射方向wi，pdf为该方向的概率密度
float3 SampleAreaLight(ShaderArealLightData light,float3 x,float3 n,RandomCtx ctx,out float pdf,bool is_debug = false)
{
    float2 u = rand2(ctx);
    float3 light_u = light._points[1].xyz - light._points[0].xyz;
    float3 light_v = light._points[3].xyz - light._points[0].xyz;
    float3 light_normal = -normalize(cross(light_u, light_v));
    float3 light_pos = light._points[0].xyz + 0.5 * light_u + 0.5 * light_v; // 面光源中心点
    float3 y =light_pos+ (u.x - 0.5f) * light_u+ (u.y - 0.5f) * light_v;
    float3 wi = y - x;
    float r2 = dot(wi, wi);
    float r = sqrt(r2);
    wi = normalize(wi);
    //wi /= r;
    float cos_theta_l = dot(light_normal, -wi);
    if (light._is_twosided != 0)
        cos_theta_l = abs(cos_theta_l);
    if (cos_theta_l <= 0)
    {
        pdf = 0;
        return 0;
    }
    if (is_debug)
    {
        AppendDebugLine(light._points[0].xyz,light._points[1].xyz, float4(1, 0, 0, 1));
        AppendDebugLine(light._points[1].xyz,light._points[2].xyz, float4(1, 0, 0, 1));
        AppendDebugLine(light._points[2].xyz,light._points[3].xyz, float4(1, 0, 0, 1));
        AppendDebugLine(light._points[3].xyz,light._points[0].xyz, float4(1, 0, 0, 1));
    }
    float area = length(cross(light_u, light_v));

    pdf = 1.0 / max(area, 1e-6);
    return wi;
}

float3 SampleSphereLight(ShaderDirectionalAndPointLightData light,float3 x,float3 n,RandomCtx ctx,out float pdf,bool is_debug = false)
{
    float2 u = rand2(ctx);

    float3 center = light._LightPosOrDir;
    float radius = light._LightParam1;
    float sphere_area = 4.0 * PI * radius * radius;

    float3 wc = center - x;
    float dist2 = dot(wc, wc);
    float dist = sqrt(dist2);

    // inside sphere fallback
    if (dist <= radius)
    {
        float z = 1.0 - 2.0 * u.x;
        float xy = sqrt(max(0.0, 1.0 - z * z));
        float phi = 2.0 * PI * u.y;

        float3 light_normal = float3(xy * cos(phi), xy * sin(phi), z);
        float3 y = center + radius * light_normal;
        float3 wi = normalize(y - x);

        pdf = 1.0 / max(sphere_area, 1e-6);
        return wi;
    }

    // ===== solid angle sampling =====

    float3 w = wc / dist;

    float sin_theta_max2 = radius * radius / dist2;
    float cos_theta_max = sqrt(max(0.0, 1.0 - sin_theta_max2));

    // sample θ
    float cos_theta = 1.0 - u.x * (1.0 - cos_theta_max);
    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));

    float phi = 2.0 * PI * u.y;

    // local direction
    float3 local_dir = float3(
        cos(phi) * sin_theta,
        sin(phi) * sin_theta,
        cos_theta);
    // build ONB
    float3 up = abs(w.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 tangent = normalize(cross(up, w));
    float3 bitangent = cross(w, tangent);
    // float3 tangent, bitangent;
    // build_onb(w, tangent, bitangent);

    float3 wi = 
        local_dir.x * tangent +
        local_dir.y * bitangent +
        local_dir.z * w;

    wi = normalize(wi);

    // ===== ray-sphere intersection =====

    float3 oc = x - center;

    float b = dot(wi, oc);
    float c = dot(oc, oc) - radius * radius;

    float h = b * b - c;
    if (h < 0.0)
    {
        pdf = 0;
        return 0;
    }

    float t = -b - sqrt(h); // 最近交点
    float3 y = x + t * wi;

    float3 light_normal = normalize(y - center);

    float cos_theta_l = dot(light_normal, -wi);
    if (cos_theta_l <= 0.0)
    {
        pdf = 0;
        return 0;
    }

    // ===== PDF =====

    // solid angle pdf
    float pdf_omega = 1.0 / (2.0 * PI * (1.0 - cos_theta_max));

    // convert to area pdf（关键）
    float dist_y2 = t * t;
    pdf = PdfAreaFromSolidAngle(pdf_omega, dist_y2, cos_theta_l);

    return wi;
}


bool HitWorld(float3 ray_origin,float3 ray_dir,bool is_debug,out HitRecord rec,out LightSample light_sample,out float3 debug_color)
{
    rec = (HitRecord)0;
    rec.t = 1e20;
    rec.is_light = false;
    rec.emission = 0;
    rec.pdf = 1.0;
    rec.light_idx = 0;
    debug_color = 0;
    bool hit_anything = false;

    for (uint light_index = 0; light_index < _light_count; ++light_index)
    {
        float light_t = 0.0;
        LightSample hit_light_sample = (LightSample)0;
        if (EvaluateUnifiedLightHit(_light_count,light_index, ray_origin, ray_dir, light_t, hit_light_sample) && light_t < rec.t)
        {
            rec.t = light_t;
            rec.p = ray_origin + rec.t * ray_dir;
            rec.normal = hit_light_sample._normal;
            rec.front_face = dot(ray_dir, rec.normal) < 0.0;
            rec.uv = 0;
            rec.material_type = 0;
            rec.material_idx = 0;
            rec.is_light = true;
            rec.light_idx = light_index;
            rec.emission = hit_light_sample._radiance;
            hit_anything = true;
            light_sample = hit_light_sample;
            debug_color = float3(1.0, 0.8, 0.2);
            rec.pdf = light_sample._pdf; 
        }
    }
    if (hit_anything)
        return true;
    TriangleHitCandidate best_hit;
    PrimitiveData best_inst;
    if (!TraverseSceneClosest(ray_origin, ray_dir, rec, best_hit, best_inst))
        return hit_anything;

    TriangleData tri;
    LoadHitTriangleData(best_hit.tri_idx,best_inst._position_bindless_idx,best_inst._normal_bindless_idx,best_inst._uv_bindless_idx,best_inst._index_bindless_idx, tri); 

    float3 n_local = normalize(
        tri.n0 * (1 - best_hit.bary.x - best_hit.bary.y) +
        tri.n1 * best_hit.bary.x +
        tri.n2 * best_hit.bary.y);
    float3 n_world = normalize(mul(best_inst._local_to_world, float4(n_local, 0)).xyz);
    rec.front_face = dot(ray_dir, n_world) < 0;
    rec.normal = rec.front_face ? n_world : -n_world;
    rec.material_idx = best_inst._material_id;
    rec.material_type = 0;
    rec.emission = g_material_buffer[rec.material_idx]._emission;
    rec.is_light = false;
    rec.uv = tri.uv0 * (1 - best_hit.bary.x - best_hit.bary.y) +
              tri.uv1 * best_hit.bary.x +
              tri.uv2 * best_hit.bary.y;
    debug_color = float3(rec.uv,0.0);
    return true;
}

bool HitAnyWorld(float3 ray_origin, float3 ray_dir, float max_t)
{
    float ray_tmax_world = max_t - 1e-3;
    if (ray_tmax_world <= 0.0)
        return false;

    return TraverseSceneAny(ray_origin, ray_dir, ray_tmax_world);
}

bool IsMaterialTransmissive(MaterialData mat_data)
{
    return mat_data._transmission > 0.0 && mat_data._metallic < 1.0;
}

bool IsOccluded(float3 origin, float3 dir, float max_t)
{
    float remaining_t = max_t;
    float3 ray_origin = origin;

    [loop]
    for (uint step = 0; step < 8; ++step)
    {
        HitRecord rec = (HitRecord)0;
        rec.t = remaining_t;

        TriangleHitCandidate best_hit;
        PrimitiveData best_inst;
        if (!TraverseSceneClosest(ray_origin, dir, rec, best_hit, best_inst))
            return false;

        MaterialData mat_data = g_material_buffer[best_inst._material_id];
        if (!IsMaterialTransmissive(mat_data))
            return true;

        float hit_t = best_hit.world_t;
        float adaptive_offset = max(0.001, hit_t * 1e-4);
        ray_origin = best_hit.world_pos + adaptive_offset * dir;
        remaining_t -= hit_t + adaptive_offset;

        if (remaining_t <= 0.0)
            return false;
    }

    return false;
}

// 将方向向量转换为颜色 (用于可视化 wi, wo 等方向)
float3 DirectionToColor(float3 dir)
{
    // 将 [-1,1] 映射到 [0,1]
    return dir * 0.5 + 0.5;
}

// 将标量值映射到颜色 (用于可视化 PDF, D, G, F 等标量)
float3 ScalarToColor(float value, float minVal, float maxVal)
{
    float t = saturate((value - minVal) / (maxVal - minVal));
    // 使用热力图颜色：蓝 -> 青 -> 绿 -> 黄 -> 红
    float3 color;
    if (t < 0.25)
        color = lerp(float3(0, 0, 1), float3(0, 1, 1), t * 4);
    else if (t < 0.5)
        color = lerp(float3(0, 1, 1), float3(0, 1, 0), (t - 0.25) * 4);
    else if (t < 0.75)
        color = lerp(float3(0, 1, 0), float3(1, 1, 0), (t - 0.5) * 4);
    else
        color = lerp(float3(1, 1, 0), float3(1, 0, 0), (t - 0.75) * 4);
    return color;
}

struct DebugData
{
    float pdf;
    float3 brdf_value;
    float D;  // GTR2 分布
    float G;  // Smith 几何
    float3 F; // Fresnel
    float cos_theta;  // dot(n, v)
    float nl;  // dot(n, l)
    float nv;  // dot(n, v)
    float nh;  // dot(n, h)
    float vh;  // dot(v, h)
    float roughness;
    float3 throughput;
    float3 wi;  // 入射方向
    float3 wo;  // 出射方向
    float3 normal;
    float3 emission;
    bool has_hit;
};

// 调试打印函数：在 pick 像素处输出详细数据到 debug buffer

void DebugPrint(uint2 pixel, DebugData data)
{
    // 只在 pick 像素处输出
    if (pixel.x != _PickPixel.x || pixel.y != _PickPixel.y)
        return;

    // 使用射线可视化输出关键数据
    float3 origin = _CameraPos.xyz;
    float scale = 0.1; // 缩放系数

    // R 通道 - PDF
    AppendDebugLine(origin, origin + float3(data.pdf * scale, 0, 0), float4(1, 0, 0, 1));

    // G 通道 - D 项
    AppendDebugLine(origin, origin + float3(0, data.D * scale * 0.1, 0), float4(0, 1, 0, 1));

    // B 通道 - G 项
    AppendDebugLine(origin, origin + float3(0, 0, data.G * scale), float4(0, 0, 1, 1));

    // 绘制 wi 方向 (黄色)
    AppendDebugLine(origin, origin + data.wi * scale * 10, float4(1, 1, 0, 1));

    // 绘制 wo 方向 (青色)
    AppendDebugLine(origin, origin + data.wo * scale * 10, float4(0, 1, 1, 1));

    // 绘制法线方向 (白色)
    AppendDebugLine(origin, origin + data.normal * scale * 10, float4(1, 1, 1, 1));
}

float3 DirectLight(TraceContext trace_ctx,float3 ray_dir,float3 p,float3 n,Material mat,inout RandomCtx ctx)
{
    return 0.0.xxx;
}

struct MisContext
{
    float _pl;
    float _pb;
};

bool EvaluateRISDirectCandidate(
    TraceContext trace_ctx,
    Material mat,
    float3 p,
    float3 n,
    float3 wo,
    LightSample light_sample,
    out float3 numerator,
    out float target)
{
    numerator = 0.0.xxx;
    target = 0.0;

    if (light_sample._pdf <= 1e-6 || !any(light_sample._radiance > 0.0.xxx))
        return false;
    // float3 shadow_origin = p + light_sample._wi * 0.002;
    // float shadow_ray_tmax = GetLightShadowRayTMax(light_sample) * 0.98;
    // bool is_occluded = IsOccluded(shadow_origin, light_sample._wi, shadow_ray_tmax);
    // if (is_occluded)
    //     return false;

    float cos_theta = DirectLightCosTheta(mat, n, light_sample._wi);
    if (cos_theta <= 1e-6)
        return false;

    float3 h = normalize(light_sample._wi + wo);
    float pdf_brdf = 0.0;
    float3 f = EvalBRDF(trace_ctx, n, h, wo, light_sample._wi, pdf_brdf);
    numerator = light_sample._radiance * f * cos_theta;
    target = max(Luminance(numerator), 0);
    return target > 0.0;
}

float ComputeReSTIRPowerHeuristicMIS(float technique_pdf, uint technique_candidate_count, float other_pdf, uint other_candidate_count)
{
    float weighted_technique_pdf = (float)technique_candidate_count * max(technique_pdf, 0.0);
    float weighted_other_pdf = (float)other_candidate_count * max(other_pdf, 0.0);
    float technique_pdf2 = weighted_technique_pdf * weighted_technique_pdf;
    float other_pdf2 = weighted_other_pdf * weighted_other_pdf;
    return technique_pdf2 / max(technique_pdf2 + other_pdf2, 1e-6);
}

float ComputeReSTIREffectivePDF(float technique_pdf, uint technique_candidate_count, float other_pdf, uint other_candidate_count)
{
    float mis_weight = ComputeReSTIRPowerHeuristicMIS(technique_pdf, technique_candidate_count, other_pdf, other_candidate_count);
    return technique_pdf / max(mis_weight, 1e-6);
}

Material BuildSurfaceMaterial(SurfaceData surface_data)
{
    Material mat = (Material)0;
    mat._albedo = surface_data.albedo.rgb;
    mat._roughness = max(surface_data.roughness,0.04);
    mat._metallic = surface_data.metallic;
    mat._anisotropy = surface_data.anisotropy;
    mat._ior = 1.5;
    mat._transmission = 0.0;
    mat._emission = surface_data.emssive;
    mat._is_glass = false;
    return mat;
}

bool IsValidPixel(int2 pixel)
{
    return pixel.x >= 0 && pixel.y >= 0 && pixel.x < _ScreenParams.z && pixel.y < _ScreenParams.w;
}

bool IsValidSurface(Surface s)
{
#if defined(_REVERSED_Z)
    return s._linear_depth > kZFar;
#else
    return s._linear_depth < kZFar;
#endif
}

Surface GetPrevSurface(int2 pixel)
{
    Surface s;
    if (!IsValidPixel(pixel) || !valid_bindless_handle(_prev_surface_buffer_idx))
    {
        s._linear_depth = kZFar;
        return s;
    }
    uint history_index = PixelToLinearIndex(uint2(pixel));
    s = BINDLESS_RWBUFFER_LOAD_INDEX(Surface,_prev_surface_buffer_idx, history_index);
    return s;
}

void StoreCurrentSurface(uint2 pixel, Surface s)
{
    if (!valid_bindless_handle(_curr_surface_buffer_idx))
        return;

    uint surface_index = PixelToLinearIndex(pixel);
    BINDLESS_RWBUFFER_STORE_INDEX(Surface, _curr_surface_buffer_idx, surface_index, s);
}

Material BuildStoredSurfaceMaterial(Surface surface)
{
    Material mat = (Material)0;
    mat._albedo = surface._albedo;
    mat._roughness = surface._roughness;
    mat._metallic = surface._metallic;
    mat._anisotropy = surface._padding;
    mat._ior = 1.5;
    mat._transmission = 0.0;
    mat._emission = 0.0.xxx;
    mat._is_glass = false;
    return mat;
}

TraceContext BuildSurfaceTraceContext(Material mat)
{
    TraceContext trace_ctx = (TraceContext)0;
    trace_ctx._mat = mat;
    trace_ctx._ior_i = 1.0;
    trace_ctx._ior_t = mat._ior;
    trace_ctx._eta = trace_ctx._ior_i / trace_ctx._ior_t;
    return trace_ctx;
}

bool EvaluateReservoirTargetAtSurface(TinyLightSample sample, Surface surface, out float target)
{
    target = 0.0;

    if (!any(sample._le > 0.0.xxx))
        return false;

    float3 to_light = sample._position - surface._position;
    float dist2 = dot(to_light, to_light);
    if (dist2 <= 1e-6)
        return false;

    float3 wi = to_light * rsqrt(dist2);
    Material mat = BuildStoredSurfaceMaterial(surface);
    float cos_theta = DirectLightCosTheta(mat, surface._normal, wi);
    if (cos_theta <= 1e-6)
        return false;

    float3 wo = normalize(_CameraPos.xyz - surface._position);
    float3 h = normalize(wi + wo);
    if (all(h == 0.0.xxx))
        return false;

    float pdf_brdf = 0.0;
    TraceContext trace_ctx = BuildSurfaceTraceContext(mat);
    float3 f = EvalBRDF(trace_ctx, surface._normal, h, wo, wi, pdf_brdf);
    if (all(f == 0.0.xxx))
        return false;

    target = max(Luminance(sample._le * f * cos_theta / pdf_brdf), 0.0);
    return target > 1e-8;
}

bool IsValidNeighbor(float3 normal,float depth01,float3 neighbor_normal,float neighbor_depth01,float depth_threshold,float normal_threshold)
{
    float normal_similarity = dot(normal, neighbor_normal);
    float depth_delta = abs(depth01 - neighbor_depth01);
    return normal_similarity > normal_threshold && depth_delta < depth_threshold;
}

bool IsValidTemporalNeighbor(Surface current, Surface history, float expected01_depth)
{
    if (!IsValidSurface(history))
        return false;

    // float3 position_delta = current._position - history._position;
    // const float kMaxPositionError = 0.1;
    // if (dot(position_delta, position_delta) > kMaxPositionError * kMaxPositionError)
    //     return false;

    const float kDepthThreshold = 0.02;
    const float kNormalThreshold = 0.95;
    return IsValidNeighbor(current._normal, expected01_depth, history._normal,
        history._linear_depth, kDepthThreshold, kNormalThreshold);
}

Reservoir TemporalReuse(uint2 pixel,Reservoir r,Surface surface,inout RandomCtx ctx,out float3 debug_color)
{
    if (!valid_bindless_handle(_prev_surface_buffer_idx) || r.M == 0u)
        return r;
    debug_color = float3(0, 0, 0);
    Reservoir state = InitReservoir();
    CombineDIReservoirs(state, r, rand(ctx), r.target);

    float3 motion = _MotionVectorTexture[pixel].rgb;
    float2 uv = (float2(pixel) + 0.5.xx) * _ScreenParams.xy;
    float2 history_uv = uv - motion.xy;
    float expected01_depth = surface._linear_depth - motion.z;
    bool is_valid_history = all(history_uv >= 0) && all(history_uv < 1.0.xx);
    if (!is_valid_history)
        return r;

    int2 history_pixel = int2(history_uv * _ScreenParams.zw);
    int2 history_max = int2(_ScreenParams.zw) - int2(1, 1);
    history_pixel = clamp(history_pixel, int2(0, 0), history_max);

    Surface history_surface = GetPrevSurface(history_pixel);
    if (!IsValidTemporalNeighbor(surface, history_surface, expected01_depth))
    {
        debug_color = float3(1, 0, 0);
        return r;
    }

    Reservoir history_r = g_prev_reservoir[PixelToLinearIndex(uint2(history_pixel))];
    if (history_r.M == 0)
    {
        debug_color = float3(1, 0, 0);
        return r;
    }
    float history_target = 0.0;
    if (!EvaluateReservoirTargetAtSurface(history_r.y, surface, history_target))
    {
        debug_color = float3(1, 0, 0);
        return r;
    }
    const uint kMaxHistoryFrames = 14;
    history_r.M = min(history_r.M, kMaxHistoryFrames);
    CombineDIReservoirs(state, history_r, rand(ctx), history_target);

    const int kSpatialNeighborCount = 5;
    const float kSpatialRadius = 14.0;
    for (int i = 0; i < kSpatialNeighborCount; i++)
    {
        int2 offset = int2((rand2(ctx) - 0.5) * kSpatialRadius);
        int2 neighbor_pixel = clamp(history_pixel + offset, int2(0, 0), history_max);
        if (all(neighbor_pixel == history_pixel))
            continue;

        Surface neighbor_surface = GetPrevSurface(neighbor_pixel);
        if (!IsValidTemporalNeighbor(surface, neighbor_surface, expected01_depth))
            continue;

        Reservoir neighbor_r = g_prev_reservoir[PixelToLinearIndex(uint2(neighbor_pixel))];
        if (neighbor_r.M == 0)
            continue;
        float neighbor_target = 0.0;
        if (!EvaluateReservoirTargetAtSurface(neighbor_r.y, surface, neighbor_target))
            continue;
        neighbor_r.M = min(neighbor_r.M, kMaxHistoryFrames);
        CombineDIReservoirs(state, neighbor_r, rand(ctx), neighbor_target);
    }

    FinalizeResampling(state, 1.0, (float)state.M);
    return state;
}

float4 EvaluateSurfaceReSTIRDI(uint2 pixel,float3 world_pos,SurfaceData surface_data,Surface surface,float3 wo,bool is_debug,inout RandomCtx ctx,inout DebugData debug_data)
{
    Material mat = BuildSurfaceMaterial(surface_data);
    float3 n = surface_data.wnormal;

    TraceContext trace_ctx = (TraceContext)0;
    trace_ctx._mat = mat;
    trace_ctx._ior_i = 1.0;
    trace_ctx._ior_t = mat._ior;
    trace_ctx._eta = trace_ctx._ior_i / trace_ctx._ior_t;

    debug_data = (DebugData)0;
    debug_data.wo = wo;
    debug_data.normal = -1;//n;
    debug_data.roughness = mat._roughness;
    debug_data.emission = mat._emission;
    debug_data.throughput = 1.0.xxx;
    debug_data.has_hit = true;

    float4 radiance = float4(mat._emission,1.0);
    if (_light_count == 0u)
        return radiance;
    if (_enable_ris)
    {
        const uint brdf_candidate_count = 4u;
        MisContext mis_ctx = (MisContext)0;
        float sample_count = float(PER_FRAME_SAMPLES + brdf_candidate_count);
        mis_ctx._pl = float(PER_FRAME_SAMPLES) / sample_count;
        mis_ctx._pb = float(brdf_candidate_count) / sample_count;

        float select_pmf = GetUnifiedLightSelectionPMF(_light_count);
        uint reservoir_index = PixelToLinearIndex(pixel);
        Reservoir r = InitReservoir();
        Reservoir local_r = InitReservoir();
        for (uint i = 0; i < PER_FRAME_SAMPLES; ++i)
        {
            float select_r = rand(ctx);
            uint sampled_light_idx = min((uint)(select_r * _light_count), _light_count - 1u);
            LightSample light_sample = SampleUnifiedLight(sampled_light_idx, world_pos, n, ctx);
            float proposal_pdf = light_sample._pdf * select_pmf;

            float3 numerator = 0.0.xxx;
            float target = 0.0;
            bool valid = EvaluateRISDirectCandidate(trace_ctx, mat, world_pos, n, wo, light_sample, numerator, target);
            float pdf_brdf = 0.0;
            if (valid)
            {
                float3 h = normalize(light_sample._wi + wo);
                EvalBRDF(trace_ctx, n, h, wo, light_sample._wi, pdf_brdf);
            }
            TinyLightSample ts = (TinyLightSample)0;
            ts._light_idx = sampled_light_idx;
            ts._position = world_pos + light_sample._wi * light_sample._t;
            ts._le = light_sample._radiance;
            float inv_pdf = 1.0 / max(mis_ctx._pl * proposal_pdf + mis_ctx._pb * pdf_brdf, 1e-6);
            UpdateReservoir(local_r, ts, target, rand(ctx), inv_pdf);
        }
        FinalizeResampling(local_r,1.0,sample_count);
        local_r.M = 1;
        CombineDIReservoirs(r, local_r,0.5,local_r.target);

        Reservoir brdf_r = InitReservoir();
        for (uint i = 0; i < brdf_candidate_count; ++i)
        {
            float3 r3 = float3(rand(ctx), rand(ctx), rand(ctx));
            SampleBRDFResult result = SampleBRDF(trace_ctx,n,wo,r3.x,r3.y,r3.z);
            if (all(result.wi == 0.0.xxx))
                continue;

            float cos_theta = DirectLightCosTheta(mat, n, result.wi);
            if (cos_theta <= 1e-6)
                continue;

            float pdf_brdf = 0.0;
            float3 f = EvalBRDF(trace_ctx, n, result.h, wo, result.wi, pdf_brdf);
            if (pdf_brdf <= 1e-6 || all(f == 0.0.xxx))
                continue;

            float3 brdf_origin = world_pos + result.wi * 0.002;

            uint hit_light_idx = 0u;
            float hit_light_t = 0.0;
            LightSample hit_light_sample = (LightSample)0;
            if (!FindClosestUnifiedLightHit(_light_count, brdf_origin, result.wi, 1e5, hit_light_idx, hit_light_t, hit_light_sample))
                continue;

            float light_pdf = PdfUnifiedLight(hit_light_idx, world_pos, result.wi) * select_pmf;
            float3 numerator = hit_light_sample._radiance * f * cos_theta;
            float target = max(Luminance(numerator), 0.0);
            if (target <= 1e-8)
                continue;

            TinyLightSample ts = (TinyLightSample)0;
            ts._light_idx = hit_light_idx;
            float inv_pdf = 1.0 / max(mis_ctx._pl * light_pdf + mis_ctx._pb * pdf_brdf, 1e-6);
            ts._position = brdf_origin + result.wi * hit_light_t;
            ts._le = hit_light_sample._radiance;
            UpdateReservoir(brdf_r, ts, target, rand(ctx), inv_pdf);
        }
        FinalizeResampling(brdf_r,1.0,sample_count);
        brdf_r.M = 1;
        CombineDIReservoirs(r, brdf_r, rand(ctx), brdf_r.target);
        FinalizeResampling(r,1.0,1.0);
        r.M = 1;
        float3 debug_color = 0;
        if (_enable_resampling)
            //r = TemporalSpatialResampling(pixel, r, surface, ctx,debug_color);
            r = TemporalReuse(pixel, r, surface, ctx,debug_color);
        g_curr_reservoir[reservoir_index] = r;
        TinyLightSample selected_sample = r.y;
        if (r.M > 0u && r.w_sum > 1e-6)
        {
            float3 wi = normalize(selected_sample._position - world_pos);
            float cos_theta = DirectLightCosTheta(mat, n, wi);
            debug_data.nl = cos_theta;
            debug_data.wi = wi;

            float3 shadow_origin = world_pos + wi * 0.002;
            float shadow_ray_tmax = length(selected_sample._position - world_pos) * 0.98;
            bool is_occluded = IsOccluded(shadow_origin, wi, shadow_ray_tmax);
            if (is_debug)
                AppendDebugLine(shadow_origin, shadow_origin + wi * 20, float4(1, 0, 0, 1));

            if (!is_occluded)
            {
                float3 h = normalize(wi + wo);
                float pdf_brdf = 0.0;
                float3 f = EvalBRDF(trace_ctx, n, h, wo, wi, pdf_brdf);
                radiance.rgb += selected_sample._le * f * cos_theta * r.w_sum;
                radiance.a = r.w_sum;
            }
        }
        //radiance = float4(debug_color,1.0);
    }
    else
    {
        float select_r = rand(ctx);
        uint sampled_light_idx = min((uint)(select_r * _light_count), _light_count - 1u);
        LightSample light_sample = SampleUnifiedLight(sampled_light_idx, world_pos, n, ctx);
        light_sample._pdf *= GetUnifiedLightSelectionPMF(_light_count);
        float cos_theta = DirectLightCosTheta(mat, n, light_sample._wi);
        debug_data.nl = cos_theta;
        debug_data.wi = light_sample._wi;
        float3 shadow_origin = world_pos + light_sample._wi * 0.002;
        float shadow_ray_tmax = GetLightShadowRayTMax(light_sample) * 0.98;
        bool is_occluded = IsOccluded(shadow_origin, light_sample._wi, shadow_ray_tmax);
        if (is_debug)
            AppendDebugLine(shadow_origin, shadow_origin + light_sample._wi * shadow_ray_tmax, float4(1, 0, 0, 1));
        if (!is_occluded && light_sample._pdf > 1e-6 && cos_theta > 1e-6 && any(light_sample._radiance > 0.0.xxx))
        {
            float3 h = normalize(light_sample._wi + wo);
            float pdf_brdf_for_light = 0.0;
            float3 f = EvalBRDF(trace_ctx, n, h, wo, light_sample._wi, pdf_brdf_for_light);
            float weight = PowerHeuristic(light_sample._pdf, pdf_brdf_for_light);
            radiance.rgb += light_sample._radiance * f * cos_theta / light_sample._pdf;
        }
    }

    //return max(radiance, 0.0.xxx);
    return radiance;
}


float3 Trace(uint2 pixel,float3 ray_dir, float3 ray_origin, uint max_depth,bool is_debug,inout RandomCtx ctx,inout float4 debug_color, inout DebugData debug_data)
{
    float3 radiance = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    HitRecord temp_rec;
    float3 c;
    LightSample g_light_sample;
    uint unified_light_count = _light_count;
    float prev_bsdf_pdf = 0.0;
    bool has_prev_bsdf_sample = false;

    // 初始化 debug 数据
    debug_data = (DebugData)0;
    debug_data.throughput = throughput;
    bool hit_anything = false;
    TraceContext trace_ctx = (TraceContext)0;

    for (uint depth = 0; depth < max_depth; ++depth)
    {
        hit_anything = HitWorld(ray_origin, ray_dir,is_debug, temp_rec, g_light_sample, c); 
        debug_data.has_hit = hit_anything;

        if (is_debug)
        {
            uint d = hit_anything ? depth : kMaxDepth-1;
            AppendDebugLine(ray_origin, hit_anything ? temp_rec.p : ray_origin + 100 * ray_dir,float4(kDepthColors[d],1.0));
            //AppendDebugLine(ray_origin, ray_origin + 100 * ray_dir,float4(kDepthColors[d],1.0));
        }
        //if (!hit_anything || depth == max_depth - 1)
        if (!hit_anything)
        {
            bool is_glass = g_material_buffer[temp_rec.material_idx]._transmission > 0.0;
            //ray_dir = hit_anything? (is_glass? ray_dir : reflect(ray_dir, temp_rec.normal)) : ray_dir; // 如果最后一次击中，反射方向用于环境光照；否则保持原方向
            if (is_debug)
                AppendDebugLine(ray_origin, ray_origin + ray_dir * 10, float4(0, 0, 0, 1)); // 黑色线表示环境光照射线
            float3 env_Li = ProceduralSky(ray_dir);
            //radiance += env_Li * throughput;
            debug_data.normal = ray_dir;
            break;
        }
        if (temp_rec.is_light)
        {
            float mis_weight = 1.0;
            float3 emission = temp_rec.emission;
            float mis_weight_bsdf = has_prev_bsdf_sample ? PowerHeuristic(prev_bsdf_pdf, g_light_sample._pdf) : 1.0;
            radiance += emission * throughput * mis_weight_bsdf;
            break;
        }
        float3 p = temp_rec.p;
        float3 n = temp_rec.normal;
        float3 wo = -ray_dir; // 出射方向（指向相机）

        // 存储 debug 数据
        debug_data.wo = wo;
        debug_data.normal = n;
        debug_data.roughness = g_material_buffer[temp_rec.material_idx]._roughness;
        debug_data.emission = temp_rec.emission;

        MaterialData mat_data = g_material_buffer[temp_rec.material_idx];
        Material mat;
        mat._albedo = mat_data._base_color.rgb;
        if(valid_bindless_handle(mat_data._base_color_tex))
        {
            mat._albedo *= pow(SAMPLE_TEXTURE2D_LOD(g_bindless_texture2d[mat_data._base_color_tex], g_LinearClampSampler, temp_rec.uv, 0).rgb, 2.2);
        }
        mat._anisotropy = mat_data._anisotropy;
        mat._roughness = mat_data._roughness;
        mat._metallic = mat_data._metallic;
        mat._ior = mat_data._ior;
        mat._transmission = mat_data._transmission;
        mat._is_glass = IsMaterialTransmissive(mat_data);
        trace_ctx._mat = mat;
        trace_ctx._ior_i = temp_rec.front_face ? 1.0 : mat._ior; // 入射介质的 IOR
        trace_ctx._ior_t = temp_rec.front_face ? mat._ior : 1.0; // 出射介质的 IOR
        trace_ctx._eta = trace_ctx._ior_i / trace_ctx._ior_t;

#ifdef ADDITIONAL_SAMPLING
    if (_light_count > 0u)
    {
        {
            float select_r = rand(ctx);
            uint sampled_light_idx = min((uint)(select_r * _light_count), _light_count - 1u);
            LightSample light_sample = SampleUnifiedLight(sampled_light_idx, p, n, ctx);
            light_sample._pdf *= GetUnifiedLightSelectionPMF(_light_count);
            float cos_theta = DirectLightCosTheta(mat, n, light_sample._wi);
            debug_data.nl = cos_theta;
            debug_data.wi = light_sample._wi;
            float3 shadow_origin = p + light_sample._wi * 0.002;
            float shadow_ray_tmax = GetLightShadowRayTMax(light_sample) * 0.98;
            bool is_occluded = IsOccluded(shadow_origin, light_sample._wi, shadow_ray_tmax);
            if (is_debug)
                AppendDebugLine(shadow_origin, shadow_origin + light_sample._wi * shadow_ray_tmax, float4(1, 0, 0, 1));
            if (!is_occluded && light_sample._pdf > 1e-6 && cos_theta > 1e-6 && any(light_sample._radiance > 0.0.xxx))
            {
                float3 h = normalize(light_sample._wi + wo);
                float pdf_brdf_for_light = 0.0;
                float3 f = EvalBRDF(trace_ctx, n, h, wo, light_sample._wi, pdf_brdf_for_light);
                float weight = PowerHeuristic(light_sample._pdf, pdf_brdf_for_light);
                radiance += throughput * light_sample._radiance * f * cos_theta * weight / light_sample._pdf;
                //radiance = kDepthColors[sampled_light_idx];
            }
        }
    }
#endif
        float3 random_values = float3(rand(ctx), rand(ctx), rand(ctx));
        SampleBRDFResult brdf_sample = SampleBRDF(trace_ctx,n,wo,random_values.x,random_values.y,random_values.z); 
        float3 wi = brdf_sample.wi;
        float3 h = brdf_sample.h;
        float pdf_bsdf = 0.0;
        float3 f = EvalBRDF(trace_ctx, n, h, wo, wi, pdf_bsdf); 

        if (all(wi == 0.0.xxx))
        {
            break;
        }

        //debug_color.rgb = SAMPLE_TEXTURE2D_LOD(g_bindless_texture2d[mat_data._base_color_tex], g_LinearClampSampler, temp_rec.uv, 0).rgb;

        float cos_theta_i = abs(dot(n, wi));
        if (pdf_bsdf < 1e-6)
        {
            break; 
        }
        throughput *= f * (cos_theta_i / pdf_bsdf);
        prev_bsdf_pdf = pdf_bsdf;
        has_prev_bsdf_sample = true; 

        // 更新射线，使用自适应偏移避免 self-intersection
        float hit_t = temp_rec.t;
        float adaptive_offset = max(0.001, hit_t * 1e-4);
        ray_dir = wi; 
        ray_origin = p + adaptive_offset * ray_dir;
        //debug_color = lerp(float4(1, 0, 0, 1), float4(0, 1, 0, 1), ComputeBRDFSpecularSamplingProb(mat._albedo,mat._metallic));
    }

    return max(radiance,0);
}

void HandleDebugOutput(DebugData data, float3 debug_color, float3 color,out float3 debug_output)
{
    #if DEBUG_MODE == DEBUG_MODE_NONE
        // 无 debug 输出，使用默认行为
        debug_output = _show_debug ? debug_color : color;

    #elif DEBUG_MODE == DEBUG_MODE_PDF
        // 输出 PDF 值
        debug_output = ScalarToColor(data.pdf, 0.0, 10.0);

    #elif DEBUG_MODE == DEBUG_MODE_BRDF
        // 输出 BRDF 值
        debug_output = data.brdf_value;

    #elif DEBUG_MODE == DEBUG_MODE_NORMAL
        // 输出法线可视化
        debug_output = DirectionToColor(data.normal);

    #elif DEBUG_MODE == DEBUG_MODE_WI
        // 输出入射光方向
        debug_output = DirectionToColor(data.wi);

    #elif DEBUG_MODE == DEBUG_MODE_WO
        // 输出出射光方向
        debug_output = DirectionToColor(data.wo);

    #elif DEBUG_MODE == DEBUG_MODE_D_GTR2
        // 输出 D 项 (GTR2 分布)
        debug_output = ScalarToColor(data.D, 0.0, 300000.0);

    #elif DEBUG_MODE == DEBUG_MODE_G_SMITH
        // 输出 G 项 (Smith 几何)
        debug_output = ScalarToColor(data.G, 0.0, 1.0);

    #elif DEBUG_MODE == DEBUG_MODE_F_FRESNEL
        // 输出 F 项 (Fresnel)
        debug_output = data.F;

    #elif DEBUG_MODE == DEBUG_MODE_COS_THETA
        // 输出 cos(theta) = dot(n, v)
        debug_output = ScalarToColor(data.nv, -1.0, 1.0);

    #elif DEBUG_MODE == DEBUG_MODE_ROUGHNESS
        // 输出粗糙度
        debug_output = ScalarToColor(data.roughness, 0.0, 1.0);

    #elif DEBUG_MODE == DEBUG_MODE_THROUGHPUT
        debug_output = data.throughput;

    #elif DEBUG_MODE == DEBUG_MODE_EMISSION
        // 输出自发光
        debug_output = data.emission;

    #elif DEBUG_MODE == DEBUG_MODE_ALL
        debug_output = color;
    #endif
}

TEXTURE2D(_GBuffer0)
TEXTURE2D(_GBuffer1)
TEXTURE2D(_GBuffer2)
TEXTURE2D(_GBuffer3)
TEXTURE2D(_CameraDepthTexture)

[shader("compute")]
[numthreads(16,16,1)]
void RayGen(CSInput input)
{
    uint2 pixel = input.DispatchThreadID.xy + uint2(_GI_TileOffset);
    if (pixel.x >= (_GI_TileOffset.x + _GI_TileSize.x) || pixel.y >= (_GI_TileOffset.y + _GI_TileSize.y))
        return;
    RandomCtx ctx;
    InitSeed(ctx, pixel + _FrameIndex, _FrameIndex);
    float2 uv = (pixel + 0.5) * _ScreenParams.xy;
    float2 jitter = rand2(ctx); 
    jitter = (jitter * 2.0 - 1.0) * 0.5;
    //uv += jitter * _ScreenParams.xy;
    float2 gbuf0 = SAMPLE_TEXTURE2D_LOD(_GBuffer0, g_LinearClampSampler, uv,0).xy;
	float4 gbuf1 = SAMPLE_TEXTURE2D_LOD(_GBuffer1, g_LinearClampSampler, uv,0);
	float4 gbuf2 = SAMPLE_TEXTURE2D_LOD(_GBuffer2, g_LinearClampSampler, uv,0);
	half4 emssion = SAMPLE_TEXTURE2D_LOD(_GBuffer3, g_LinearClampSampler, uv,0);
	float depth = SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture,g_LinearClampSampler,uv,0).r;
    if (depth >= 1.0)
    {
        Surface invalid_surface = (Surface)0;
        invalid_surface._linear_depth = kZFar;
        StoreCurrentSurface(pixel, invalid_surface);
        g_curr_reservoir[PixelToLinearIndex(pixel)] = InitReservoir();
        _GI_Texture[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
	float3 world_pos = ComputeWorldSpacePosition(uv,depth,_MatrixIVP);
	//float3 world_pos = _CameraPos.xyz + UnprojectByCameraRay(input.uv,depth);
	SurfaceData surface_data;
	surface_data.wnormal = UnpackNormal(gbuf0);
	surface_data.albedo = float4(gbuf1.rgb,1.0f);
	surface_data.roughness = gbuf1.w;
	surface_data.metallic = gbuf2.w;
	surface_data.specular = 1.0.xxx;
	surface_data.anisotropy = gbuf2.z;
	surface_data.emssive = emssion.rgb;
    // _GI_Texture[pixel] = surface_data.albedo;
    // return;

	OrthonormalBasis(surface_data.wnormal,surface_data.tangent,surface_data.bitangent);

    Surface surface;
    surface._position = world_pos;
    surface._normal = surface_data.wnormal;
    surface._albedo = surface_data.albedo.rgb;
    surface._roughness = surface_data.roughness;
    surface._metallic = surface_data.metallic;
    surface._geo_normal = surface_data.wnormal;
    surface._padding = surface_data.anisotropy;
    surface._linear_depth = Linear01Depth(depth,_ProjectionParams.y,_ProjectionParams.z);
    if (!IsValidSurface(surface))
    {
        Surface invalid_surface = (Surface)0;
        invalid_surface._linear_depth = kZFar;
        StoreCurrentSurface(pixel, invalid_surface);
        g_curr_reservoir[PixelToLinearIndex(pixel)] = InitReservoir();
        _GI_Texture[pixel] = 0.0;
        return;
    }

    StoreCurrentSurface(pixel, surface);


    float3 wo = normalize(_CameraPos.xyz - world_pos);
    bool is_debug = (pixel.x == _PickPixel.x && pixel.y == _PickPixel.y);
    DebugData debug_data;
    float4 color = EvaluateSurfaceReSTIRDI(pixel, world_pos, surface_data,surface, wo, is_debug, ctx, debug_data);
    if (!_enable_ris || _light_count == 0u)
        g_curr_reservoir[PixelToLinearIndex(pixel)] = InitReservoir();

    float3 debug_output = color.rgb;
    HandleDebugOutput(debug_data, surface_data.wnormal * 0.5 + 0.5, color.rgb, debug_output);
    _GI_Texture[pixel] = float4(debug_output,color.a);
}

[shader("compute")]
[numthreads(16,16,1)]
void PrimaryRay(CSInput input)
{
    uint2 pixel = input.DispatchThreadID.xy + uint2(_GI_TileOffset);
    if (pixel.x >= (_GI_TileOffset.x + _GI_TileSize.x) || pixel.y >= (_GI_TileOffset.y + _GI_TileSize.y))
        return;
    RandomCtx ctx;
    InitSeed(ctx, pixel, 0);
    float2 uv = (pixel + 0.5) * _ScreenParams.xy;
    float2 jitter = rand2(ctx); 
    jitter = (jitter * 2.0 - 1.0) * 0.5;
    uv += jitter * _ScreenParams.xy;
    float3 ray_origin = _CameraPos.xyz;
    uint depth = 1;
    float3 ray_dir = normalize(Unproject(uv,1.0f) - ray_origin);
    bool is_debug = (pixel.x == _PickPixel.x && pixel.y == _PickPixel.y);
    float4 debug_color = float4(1, 1, 1, 0);

    DebugData debug_data;
    float3 color = Trace(pixel, ray_dir, ray_origin, depth, is_debug, ctx, debug_color, debug_data);

    float3 debug_output = color;
    HandleDebugOutput(debug_data, debug_color.rgb, color, debug_output);
    _GI_Texture[pixel] = float4(debug_output, 1.0);
}

TEXTURE2D(_HistoryTarget)
RWTEXTURE2D(_CurrentTarget,float4)

[shader("compute")]
[numthreads(16,16,1)]
void Denoise(CSInput input)
{
    uint2 pixel = input.DispatchThreadID.xy;
    float3 current = _CurrentTarget[pixel].rgb;
    float2 motion = _MotionVectorTexture[pixel].rg;
    float2 uv = (float2(pixel) + 0.5.xx) * _ScreenParams.xy;
    float2 history_uv = uv - motion;

    if (any(history_uv != saturate(history_uv)))
    {
        _CurrentTarget[pixel] = float4(current, 1.0);
        return;
    }

    int2 history_pixel = int2(history_uv * _ScreenParams.zw);
    int2 history_max = int2(_ScreenParams.zw) - int2(1, 1);
    history_pixel = clamp(history_pixel, int2(0, 0), history_max);

    float3 history = SAMPLE_TEXTURE2D_LOD(_HistoryTarget,g_LinearClampSampler,history_uv,0).rgb;//  _HistoryTarget[history_pixel].rgb;
    float a = 1.0 / clamp((float)_frame_index + 1.0, 1.0, MAX_ACCUMULATED_FRAMES);
    _CurrentTarget[pixel].rgb = lerp(history, current, a);
    return;

    int2 max_pixel = int2(_ScreenParams.zw) - int2(1, 1);
    float3 neighborhood_min = 1e20.xxx;
    float3 neighborhood_max = -1e20.xxx;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 sample_pixel = clamp(int2(pixel) + int2(x, y), int2(0, 0), max_pixel);
            float3 sample_color = _CurrentTarget[uint2(sample_pixel)].rgb;
            neighborhood_min = min(neighborhood_min, sample_color);
            neighborhood_max = max(neighborhood_max, sample_color);
        }
    }

    float3 clamped_history = clamp(history, neighborhood_min, neighborhood_max);
    float motion_pixels = length(motion * _ScreenParams.zw);
    float history_luma = Luminance(clamped_history);
    float current_luma = Luminance(current);
    float luma_delta = abs(history_luma - current_luma) / max(max(history_luma, current_luma), 1e-3);
    //float a = 1.0 / clamp((float)_frame_index + 1.0, 1.0, MAX_ACCUMULATED_FRAMES);
    float current_weight = max(a, saturate(0.08 + motion_pixels * 0.12 + luma_delta * 0.4));

    float3 blended = lerp(clamped_history, current, current_weight);
    _CurrentTarget[pixel] = float4(blended, 1.0);
    //_CurrentTarget[pixel] = float4(motion.xy * 4,0.0, 1.0);
}
