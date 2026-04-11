#include "rt_common.hlsli"
#include "../Compute/cs_common.hlsli"
#include "../bindless.hlsli"
#include "../color_space_utils.hlsli"
#include "sampling.hlsli"
#include "hit.hlsli"

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
// DEBUG_MODE = 10 (DEBUG_MODE_ROUGHNESS)    - 可视化粗糙度
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
#pragma kernel Denoise

#define MAX_ACCUMULATED_FRAMES 4096

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

#ifndef DEBUG_MODE
#define DEBUG_MODE DEBUG_MODE_NONE
#endif

//#define DEBUG_MODE DEBUG_MODE_WI

AppendStructuredBuffer<DebugRay>   _debug_rays;
AppendStructuredBuffer<uint>   _debug_ray_indices;



StructuredBuffer<MaterialData> g_material_buffer;

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




struct RandomCtx
{
    uint4 seed;
    uint2 pixel;
};


// void InitSeed(inout RandomCtx ctx,uint2 pixel, uint frame, uint bounce = 0)
// {
//     ctx.seed = uint4(pixel, frame, pixel.x + pixel.y);
//     ctx.pixel = pixel;
// }

// void pcg4d(inout uint4 v)
// {
//     v = v * 1664525u + 1013904223u;
//     v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
//     v = v ^ (v >> 16u);
//     v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
// }

// float rand(inout RandomCtx ctx)
// {
//     pcg4d(ctx.seed);
//     return float(ctx.seed.x) / float(0xffffffffu);
// }

void InitSeed(inout RandomCtx ctx,uint2 pixel, uint frame, uint bounce = 0)
{
    ctx.seed.x = pixel.x * 1973 + pixel.y * 9277 + frame * 26699 + bounce * 104729;
    ctx.pixel = pixel;
}

void pcg_hash(inout uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    seed = (word >> 22u) ^ word;
}

float rand(inout RandomCtx ctx)
{
    pcg_hash(ctx.seed.x);
    return float(ctx.seed.x) / 4294967296.0;
}

float2 rand2(inout RandomCtx ctx)
{
    return float2(rand(ctx), rand(ctx));
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


bool HitAreaLight(ShaderArealLightData light,float3 ray_origin,float3 ray_dir,inout float t)
{
    float3 p0 = light._points[0].xyz;
    float3 e1 = light._points[1].xyz - p0;
    float3 e2 = light._points[3].xyz - p0;
    float3 n = -normalize(cross(e1, e2));
    float denom = dot(ray_dir, n);

    // Single-sided area light only emits on the normal side.
    if (light._is_twosided == 0 && denom >= -FLOAT_EPSILON)
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
float PowerHeuristic(float pdfA, float pdfB)
{
    float a2 = pdfA * pdfA;
    float b2 = pdfB * pdfB;
    return a2 / max(a2 + b2, 1e-6);
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
    wi /= r;
    float cos_theta_l = dot(light_normal, -wi);
    if (light._is_twosided != 0)
        cos_theta_l = abs(cos_theta_l);
    if (cos_theta_l <= 0)
    {
        pdf = 0;
        return 0;
    }
    float area = length(cross(light_u, light_v));

    pdf = r2 / (area * cos_theta_l);
    return wi;
}

float PdfAreaLight(ShaderArealLightData light,float3 x,float3 wi)
{
    float3 light_u = light._points[1].xyz - light._points[0].xyz;
    float3 light_v = light._points[3].xyz - light._points[0].xyz;
    float3 light_normal = -normalize(cross(light_u, light_v));

    float denom = dot(wi, light_normal);
    if (light._is_twosided == 0 && denom >= -FLOAT_EPSILON)
        return 0;
    if (abs(denom) < FLOAT_EPSILON)
        return 0;

    float t = dot(light._points[0].xyz - x, light_normal) / denom;
    if (t <= FLOAT_EPSILON)
        return 0;

    float3 y = x + wi * t;
    float3 rel = y - light._points[0].xyz;
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

    float r2 = t * t;
    float cos_theta_l = dot(light_normal, -wi);
    if (light._is_twosided != 0)
        cos_theta_l = abs(cos_theta_l);
    if (cos_theta_l <= 0)
        return 0;

    float area = length(cross(light_u, light_v));
    return r2 / (area * cos_theta_l);
}



bool HitWorld(float3 ray_origin,float3 ray_dir,bool is_debug,out HitRecord rec,out float3 debug_color)
{
    rec.t = 1e20;
    rec.is_light = false;
    rec.emission = 0;
    rec.pdf = 1.0;
    rec.light_idx = 0;
    debug_color = 0;
    bool hit_anything = false;

    uint area_light_count = min((uint)(_ActiveLightCount.w),MAX_AREA_LIGHT);
    for (uint z = 0; z < area_light_count; z++)
    {
        ShaderArealLightData light = _AreaLights[z];
        float light_t;
        if (HitAreaLight(light, ray_origin, ray_dir, light_t) && light_t < rec.t)
        {
             rec.t = light_t;
             rec.p = ray_origin + rec.t * ray_dir;
             rec.normal = 0;
             rec.front_face = false;
             rec.uv = 0;
             rec.material_type = 0;
             rec.material_idx = 0;
             rec.is_light = true;
             rec.light_idx = z;
             rec.emission = light._LightColor;
             hit_anything = true;
             debug_color = float3(1.0, 0.8, 0.2);
             rec.pdf = PdfAreaLight(light, ray_origin, ray_dir);
        }
    }

    TriangleHitCandidate best_hit;
    ObjectInstanceData best_inst;
    if (!TraverseSceneClosest(ray_origin, ray_dir, rec, best_hit, best_inst))
        return hit_anything;

    TriangleData tri = g_scene[best_hit.tri_idx];
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
        ObjectInstanceData best_inst;
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

float3 ProceduralSky(float3 d)
{
    float h = saturate(d.y);

    float3 zenith  = float3(0.22, 0.35, 0.95);
    float3 horizon = float3(0.8, 0.85, 0.9);
    float3 ground  = float3(0.1, 0.1, 0.1);

    // 天空渐变
    float3 sky = lerp(horizon, zenith, pow(h, 0.2));
    float horizon_boost = exp(-abs(d.y) * 20.0);
    sky += horizon * horizon_boost * 0.2;

    // ===== 关键修改 ===== 
    // 过渡范围控制地平线宽度
    float horizon_width = 0.05;  // 可调 0.02~0.1

    float t = smoothstep(-horizon_width, horizon_width, d.y);

    float3 env = lerp(ground, sky, t);

    return env;
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

float DirectLightCosTheta(Material mat, float3 n, float3 wi)
{
    float ndotl = dot(n, wi);
    return mat._is_glass ? abs(ndotl) : saturate(ndotl);
}
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
    float pdf_light = 1.0;
    float3 wi = SampleAreaLight(_AreaLights[0], p, n, ctx, pdf_light);
    float cos_theta = saturate(dot(n, wi));
    float3 Li = _AreaLights[0]._LightColor;
    bool is_occluded = IsOccluded(p + 0.002 * n, wi, 1e20);
    //if (cos_theta > 0.0 && !is_occluded)
    {
        float pdf = 1.0;
        float3 h = normalize(wi + ray_dir);
        float3 f = EvalBRDF(trace_ctx, n, h, ray_dir, wi, pdf);
        return Li * f;
    }
    return 0.0.xxx;
}


float3 Trace(uint2 pixel,float3 ray_dir, float3 ray_origin, uint max_depth,bool is_debug,inout RandomCtx ctx,inout float4 debug_color, inout DebugData debug_data)
{
    float3 radiance = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    HitRecord temp_rec;
    float3 c;
    uint area_light_count = min((uint)(_ActiveLightCount.w), MAX_AREA_LIGHT);
    float prev_bsdf_pdf = 0.0;
    bool has_prev_bsdf_sample = false;

    // 初始化 debug 数据
    debug_data = (DebugData)0;
    debug_data.throughput = throughput;
    bool hit_anything = false;
    TraceContext trace_ctx = (TraceContext)0;
    for (uint depth = 0; depth < max_depth; ++depth)
    {
        hit_anything = HitWorld(ray_origin, ray_dir,is_debug, temp_rec, c); 
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
            radiance += env_Li * throughput;
            debug_data.normal = ray_dir;
            break;
        }
        if (temp_rec.is_light)
        {
            float mis_weight = 1.0;
            if (has_prev_bsdf_sample) 
            {
                float select_pmf = (area_light_count > 0) ? (1.0 / (float)area_light_count) : 0.0;
                float pdf_light = PdfAreaLight(_AreaLights[temp_rec.light_idx], ray_origin, ray_dir) * select_pmf;
                mis_weight = PowerHeuristic(prev_bsdf_pdf, pdf_light);
            }
            radiance += temp_rec.emission * throughput * mis_weight; 
            break;
        }
        //radiance += DirectLight(ray_dir, temp_rec.p, temp_rec.normal, g_material_buffer[temp_rec.material_idx], ctx) * throughput;
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
        if(valid_bindless_srv(mat_data._base_color_tex))
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

        // 直接光照 - 方向光
        float3 wi_light = -_MainlightWorldPosition; // 入射方向（指向光源）
        float cos_theta_l = DirectLightCosTheta(mat, n, wi_light);
        debug_data.nl = cos_theta_l;

        if (cos_theta_l > 1e-6)
        {
            bool is_occluded = IsOccluded(p + 0.002 * wi_light, wi_light, 1e20);
            if (!is_occluded)
            {
                float pdf_brdf;
                float3 f = EvalBRDF(trace_ctx, n, normalize(wi_light + wo), wo, wi_light, pdf_brdf); 
                float3 Li = _DirectionalLights[0]._LightColor;
                // 方向光是 delta 分布，不使用 MIS，直接计算
                radiance += throughput * f * Li * cos_theta_l;

#if DEBUG_MODE == DEBUG_MODE_ALL
                // 在 debug 模式下记录直接光照的 BRDF 数据
                debug_data.brdf_value = f;
                debug_data.pdf = pdf_brdf;
            }
#endif
            }
        }


        // //间接光照 - 使用新的采样接口
        float3 random_values = float3(rand(ctx), rand(ctx), rand(ctx));

        // 直接光照 - 面光源
        if (area_light_count > 0)
        {
            float select_r = rand(ctx);
            uint sampled_light_idx = min((uint)(select_r * area_light_count), area_light_count - 1);
            ShaderArealLightData sampled_light = _AreaLights[sampled_light_idx];

            float pdf_light_dir = 0.0;
            float3 wi_light = SampleAreaLight(sampled_light, p, n, ctx, pdf_light_dir, is_debug);
            float select_pmf = 1.0 / (float)area_light_count;
            float pdf_light = pdf_light_dir * select_pmf;

            float cos_theta = DirectLightCosTheta(mat, n, wi_light);
            float3 Li = sampled_light._LightColor;
            bool is_occluded = IsOccluded(p + 0.002 * wi_light, wi_light, 1e20);
            if (cos_theta > 1e-6 && !is_occluded && pdf_light > 1e-6)
            {
                float3 h = normalize(wi_light + wo);
                float pdf_brdf_for_light = 0.0;
                float3 f = EvalBRDF(trace_ctx, n, h, wo, wi_light, pdf_brdf_for_light);
                float weight = PowerHeuristic(pdf_light, pdf_brdf_for_light);
                radiance += max(0.0,throughput * Li * f * cos_theta * weight / pdf_light);
            }
        }

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

[numthreads(16,16,1)]
void RayGen(CSInput input)
{
    if (_frame_index > MAX_ACCUMULATED_FRAMES)
    {
        // 超过累积帧数上限后不再更新，保持最后的结果
        return;
    }
    uint2 pixel = input.DispatchThreadID.xy + uint2(_GI_TileOffset);
    if (pixel.x >= (_GI_TileOffset.x + _GI_TileSize.x) || pixel.y >= (_GI_TileOffset.y + _GI_TileSize.y))
        return;
    RandomCtx ctx;
    InitSeed(ctx, pixel, _FrameIndex);
    float2 uv = (pixel + 0.5) * _ScreenParams.xy;
    float2 jitter = rand2(ctx); 
    jitter = (jitter * 2.0 - 1.0) * 0.5;
    uv += jitter * _ScreenParams.xy;
    float3 ray_origin = _CameraPos.xyz;
    uint depth = 2;
    float3 ray_dir = normalize(Unproject(uv,1.0f) - ray_origin);
    bool is_debug = (pixel.x == _PickPixel.x && pixel.y == _PickPixel.y);
    float4 debug_color = float4(1, 1, 1, 0);

    DebugData debug_data;
    float3 color = Trace(pixel, ray_dir, ray_origin, depth, is_debug, ctx, debug_color, debug_data);

    float3 debug_output = color;
    HandleDebugOutput(debug_data, debug_color.rgb, color, debug_output);

    float3 prev = _GI_Texture[pixel].rgb;
    debug_output = ApplySRGBCurve(ACESFilm(debug_output));
    float a = 1.0 / clamp((float)_frame_index, 1.0, MAX_ACCUMULATED_FRAMES);
    //a = 1;
    prev = lerp(prev, debug_output, a);
    _GI_Texture[pixel] = float4(prev, 1.0); 
}

TEXTURE2D(_HistoryTarget)
RWTEXTURE2D(_CurrentTarget,float4)

[numthreads(16,16,1)]
void Denoise(CSInput input)
{
    return; // 先禁用降噪，专注调试光照计算
    uint2 pixel = input.DispatchThreadID.xy;
    float3 current = _CurrentTarget[pixel].rgb;

    // Clamp frame count to avoid unstable weights and hard-freeze artifacts.
    float n = clamp((float)_frame_index, 1.0, 2560.0);
    float alpha = 0.01;//1.0 / n;

    float3 history = _HistoryTarget[pixel].rgb;
    float3 blended = lerp(history, current, alpha);
    _CurrentTarget[pixel] = float4(blended, 1.0);
}