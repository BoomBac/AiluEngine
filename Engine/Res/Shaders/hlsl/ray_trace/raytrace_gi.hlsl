#include "rt_common.hlsli"
#include "../Compute/cs_common.hlsli"
#include "../bindless.hlsli"
#include "../brdf.hlsli"
#pragma kernel RayGen
#pragma kernel Denoise

RWTEXTURE2D(_GI_Texture,float4)

CBUFFER_START(ComputeCB)
    int2 _PickPixel;
    uint _inst_count;
    uint _tlas_count;
    uint _blas_count;
    uint _tri_count;
    uint _debug_hit_box_idx;
CBUFFER_END

AppendStructuredBuffer<DebugRay>   _debug_rays;
AppendStructuredBuffer<uint>   _debug_ray_indices;


StructuredBuffer<TriangleData> g_scene;
StructuredBuffer<ObjectInstanceData> g_instance_data;
StructuredBuffer<LBVHNode> g_tlas_buffer;
StructuredBuffer<LBVHNode> g_blas_buffer;
StructuredBuffer<MaterialData> g_material_buffer;

// 固定的每层颜色
static const float3 kDepthColors[kMaxDepth] =
{
    float3(1.0, 1.0, 1.0),   // depth = 0  (红)
    float3(0.0, 1.0, 0.0),   // depth = 1  (绿)
    float3(0.0, 0.0, 1.0),   // depth = 2  (蓝)
    float3(1.0, 1.0, 0.0),   // depth = 3  (黄)
    float3(1.0, 0.0, 1.0),   // depth = 4  (品红)
    float3(0.0, 1.0, 1.0),   // depth = 5  (青)
    float3(1.0, 0.5, 0.0),   // depth = 6  (橙)
    float3(0.5, 0.0, 1.0),    // depth = 7  (紫)
    float3(1.0, 0.0, 0.0)    // miss
};

struct Ray
{
    float3 o;
    float3 d;
};

struct HitRecord
{
    float3 p;
    float3 normal;
    float2 uv;
    float t;
    bool front_face;
    uint material_type; // 0: diffuse, 1: metal,2: dielectric
    uint material_idx;
};

struct Sphere
{
    float3 center;
    float radius;
    uint material_type; // 0: diffuse, 1: metal,2: dielectric
};

bool Sphere_Hit(Sphere s, Ray r, float t_min, float t_max, out HitRecord rec)
{
    float3 oc = r.o - s.center;
    float a = dot(r.d, r.d);
    if (abs(a) < 1e-8) return false;

    float half_b = dot(oc, r.d);
    float c = dot(oc, oc) - s.radius * s.radius;
    float discriminant = half_b * half_b - a * c;

    if (discriminant < 0.0)
        return false;

    float sqrt_d = sqrt(discriminant);

    // 先取较近根
    float root = (-half_b - sqrt_d) / a;
    if (root < t_max && root > t_min)
    {
        rec.t = root;
        rec.p = r.o + rec.t * r.d;
        float3 outward_normal = (rec.p - s.center) / s.radius;
        rec.front_face = dot(r.d, outward_normal) < 0;
        rec.normal = rec.front_face ? outward_normal : -outward_normal;
        return true;
    }

    // 否则尝试远根
    root = (-half_b + sqrt_d) / a;
    if (root < t_max && root > t_min)
    {
        rec.t = root;
        rec.p = r.o + rec.t * r.d;
        float3 outward_normal = (rec.p - s.center) / s.radius;
        rec.front_face = dot(r.d, outward_normal) < 0;
        rec.normal = rec.front_face ? outward_normal : -outward_normal;
        return true;
    }

    return false;
}


#define NUM_SPHERES 3
#define MAX_STACK 64

static const Sphere spheres[NUM_SPHERES] = {
    {float3(0.0, 2, 0.0), 2,0},
    //{float3(0, -100, 0.0), 100,0},
    {float3(5, 2, 0.0), 2,1},
    {float3(-5, 2, 0.0), 2,2},
};

static const TriangleData tris_centered[2] =
{
    // tri 0: A, B, C
    {
        float3(-25.0f, 0.0f, -25.0f), // v0 = A
        float3( 25.0f, 0.0f, -25.0f), // v1 = B
        float3( 25.0f, 0.0f,  25.0f), // v2 = C
        // normals (per-vertex, 都指向 +Y)
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        // uvs
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f),
    },

    // tri 1: A, C, D
    {
        float3(-25.0f, 0.0f, -25.0f), // v0 = A
        float3( 25.0f, 0.0f,  25.0f), // v1 = C
        float3(-25.0f, 0.0f,  25.0f), // v2 = D
        // normals
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        // uvs
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(0.0f, 1.0f),
    }
};


// Wang hash (32-bit) -> 0..1
uint wang_hash(uint x)
{
    x = (x ^ 61u) ^ (x >> 16);
    x = x + (x << 3);
    x = x ^ (x >> 4);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15);
    return x;
}

float rand_from_uint(uint seed)
{
    return (float)wang_hash(seed) * (1.0f / 4294967296.0f); // / 2^32
}

// 生成两个独立随机数（生成两个不同的 hash）
float2 rand2_from_uint(uint seed)
{
    uint a = wang_hash(seed);
    uint b = wang_hash(seed + 0x9e3779b9u); // 加一个常数扰动
    return float2(a, b) * (1.0f / 4294967296.0f);
}
// n：单位法线（世界空间）
// 返回矩阵列为 (tangent, bitangent, normal)
void build_onb(float3 n, out float3 t, out float3 b)
{
    // 稳定的方式：选择与 n 不共线的参考向量
    float3 up = abs(n.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// seed 是 uint，用于产生随机数
float3 sample_cosine_hemisphere(float3 n, uint seed)
{
    float2 r = rand2_from_uint(seed); // r.x, r.y
    float u1 = r.x;
    float u2 = r.y;

    float r_sqrt = sqrt(u1);
    float phi = 2.0 * 3.14159265359 * u2;
    float x = r_sqrt * cos(phi);
    float y = r_sqrt * sin(phi);
    float z = sqrt(max(0.0, 1.0 - u1)); // z 朝上（局部）

    // 局部->世界
    float3 t, b;
    build_onb(n, t, b);
    float3 sampled_dir = normalize(x * t + y * b + z * n);
    return sampled_dir;
}

static float reflectance(float cosine, float refraction_index) 
{
    // Use Schlick's approximation for reflectance.
    float r0 = (1 - refraction_index) / (1 + refraction_index);
    r0 = r0*r0;
    float a = (1 - cosine);
    return r0 + (1-r0)* Pow4(a) * a;
}

#define CULL_NONE 0u
#define CULL_BACK_FACE 1u
#define CULL_FRONT_FACE 2u


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

// cull_mode:
// 0 = no culling (double sided)
// 1 = cull back faces  (keep only front-facing triangles, det > 0)
// 2 = cull front faces (keep only back-facing triangles, det < 0)
bool TriangleHitFast(float3 origin, float3 dir, TriangleData tri,inout float t, inout float u, inout float v,uint cull_mode)
{
    float3 e1 = tri.v1 - tri.v0;
    float3 e2 = tri.v2 - tri.v0;
    float3 p  = cross(dir, e2);
    float  det = dot(p, e1);
    //e1 · (dir x e2) = -dir · (e1 x e2)
    // ---- CULLING ----
    if (cull_mode == 1) // back-face cull
    {
        if (det < FLOAT_EPSILON) 
            return false;
    }
    else if (cull_mode == 2) // front-face cull
    {
        if (det > -FLOAT_EPSILON) 
            return false;
    }
    else // double-sided
    {
        if (abs(det) < FLOAT_EPSILON) return false;
    }

    float inv_det = 1.0 / det;

    float3 s = origin - tri.v0;
    u = dot(p, s) * inv_det;
    if (u < -FLOAT_EPSILON || u > 1.0 + FLOAT_EPSILON) 
        return false;

    float3 q = cross(s, e1);
    v = dot(q, dir) * inv_det;
    if (v < -FLOAT_EPSILON || (u + v) > 1.0 + FLOAT_EPSILON) 
        return false;

    t = dot(q, e2) * inv_det;
    return t > FLOAT_EPSILON;
}


// 高性能 AABB hit 判定（branchless）
//   bmin, bmax : 包围盒
//   origin     : 光线起点
//   inv_dir    : 光线方向倒数 (1.0 / ray.dir;)，事先算好
//   parallel   : 光线方向的每个分量是否为0（ray.dir == 0），用于特殊处理平行于某轴的情况
//   sign       : 每维 dir 是否 < 0(int3(ray.inv_dir.x < 0, ray.inv_dir.y < 0, ray.inv_dir.z < 0);)，用于选择 t0/t1
// 返回：是否相交（不返回 t 值，用于快速 culling）
bool AABBHitFast(float3 bmin, float3 bmax,float3 origin, float3 inv_dir, float3 parallel,int3 sign,out float tmin, out float tmax)
{
    bool3 outside = (origin < bmin) | (origin > bmax);

    if (any((bool3)parallel & outside))
        return false;

    float tx1 = ((sign.x ? bmax.x : bmin.x) - origin.x) * inv_dir.x;
    float tx2 = ((sign.x ? bmin.x : bmax.x) - origin.x) * inv_dir.x;

    tmin = min(tx1, tx2);
    tmax = max(tx1, tx2);

    float ty1 = ((sign.y ? bmax.y : bmin.y) - origin.y) * inv_dir.y;
    float ty2 = ((sign.y ? bmin.y : bmax.y) - origin.y) * inv_dir.y;

    tmin = max(tmin, min(ty1, ty2));
    tmax = min(tmax, max(ty1, ty2));

    float tz1 = ((sign.z ? bmax.z : bmin.z) - origin.z) * inv_dir.z;
    float tz2 = ((sign.z ? bmin.z : bmax.z) - origin.z) * inv_dir.z;

    tmin = max(tmin, min(tz1, tz2));
    tmax = min(tmax, max(tz1, tz2));

    return tmax >= max(tmin, 0.0);
}

float3 random(uint seed)
{
    // LCG 线性同余生成器
    seed = (seed ^ 61u) ^ (seed >> 16);
    seed *= 9u;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15);

    // 变成 float 0~1
    float x = (seed & 0x007FFFFF) / 8388608.0; seed *= 747796405u;
    float y = (seed & 0x007FFFFF) / 8388608.0; seed *= 747796405u;
    float z = (seed & 0x007FFFFF) / 8388608.0;

    return float3(x, y, z);
}

bool HitWorld(float3 ray_origin,float3 ray_dir,bool is_debug,out HitRecord rec,out float3 debug_color)
{
    rec.t = 1e20;
    debug_color = 0;
    bool hit_anything = false;

    // ===== world ray =====
    float3 inv_dir = 1.0 / ray_dir;
    int3 sign = int3(ray_dir < 0);
    float3 parallel = step(abs(ray_dir), float3(1e-8, 1e-8, 1e-8)); // ray_dir 分量接近0则认为平行

    uint tlas_stack[MAX_STACK];
    int  tlas_sp = 0;
    tlas_stack[tlas_sp++] = 0;

    // ================= TLAS traversal =================
    while (tlas_sp > 0)
    {
        uint tlas_idx = tlas_stack[--tlas_sp];
        LBVHNode tlas_node = g_tlas_buffer[tlas_idx];

        float tmin, tmax;
        if (!AABBHitFast(tlas_node._min, tlas_node._max,
                         ray_origin, inv_dir,parallel, sign,
                         tmin, tmax))
            continue;

        // world-space剪枝
        if (tmin >= rec.t)
            continue;

        bool is_leaf = tlas_node._neg_right_or_tri_count > 0;

        if (is_leaf)
        {
            int inst_id = tlas_node._left_or_tri_offset_or_inst_idx;
            ObjectInstanceData inst = g_instance_data[inst_id];

            // ===== transform ray to object space =====
            float3 obj_ro = mul(inst._world_to_local, float4(ray_origin,1)).xyz;
            float3 obj_rd = mul(inst._world_to_local, float4(ray_dir,0)).xyz;

            float3 obj_inv_dir = 1.0 / obj_rd;
            int3   obj_sign = int3(obj_rd < 0);
            float3 obj_parallel = step(abs(obj_rd), float3(1e-8, 1e-8, 1e-8));

            // ===== DXR-style conservative local ray_tmax =====
            float ray_tmax_world = rec.t;
            float ray_tmax_local = ray_tmax_world * inst._max_inv_scale;

            // ================= BLAS traversal =================
            uint blas_stack[MAX_STACK];
            int  blas_sp = 0;
            blas_stack[blas_sp++] = inst._blas_node_start;
            int best_tri_idx = -1;
            float2 best_uv = float2(0,0);
            while (blas_sp > 0)
            {
                uint blas_idx = blas_stack[--blas_sp];
                if (blas_idx >= inst._blas_node_start + inst._blas_node_count)
                    continue;

                LBVHNode blas_node = g_blas_buffer[blas_idx];

                float btmin, btmax;
                if (!AABBHitFast(blas_node._min, blas_node._max,
                                 obj_ro, obj_inv_dir, obj_parallel, obj_sign,
                                 btmin, btmax))
                    continue;

                // local-space剪枝
                if (btmin >= ray_tmax_local)
                    continue;

                bool blas_leaf = blas_node._neg_right_or_tri_count > 0;

                if (blas_leaf)
                {
                    int tri_start =blas_node._left_or_tri_offset_or_inst_idx +inst._global_triangle_offset;
                    int tri_count = blas_node._neg_right_or_tri_count;
                    for (int tri_idx = tri_start;tri_idx < tri_start + tri_count;++tri_idx)
                    {
                        TriangleData tri = g_scene[tri_idx];
                        float local_t, u, v;
                        if (!TriangleHitFast(obj_ro, obj_rd,tri, local_t, u, v,CULL_NONE))
                            continue;
                        if (local_t >= ray_tmax_local)
                            continue;
                        // ---- update local ray_tmax ----
                        ray_tmax_local = local_t;
                        // ---- compute world hit ----
                        float3 local_hit = obj_ro + local_t * obj_rd;
                        float3 world_hit =mul(inst._local_to_world,float4(local_hit,1)).xyz;
                        float world_t = length(world_hit - ray_origin);
                        // ---- update global closest hit ----
                        if (world_t < rec.t)
                        {
                            best_tri_idx = tri_idx;
                            best_uv = float2(u, v);
                            hit_anything = true;
                            rec.t = world_t;
                            rec.p = world_hit;
                        }
                    }
                }
                else
                {
                    uint left  =
                        blas_node._left_or_tri_offset_or_inst_idx +
                        inst._blas_node_start;

                    uint right =
                        uint(-blas_node._neg_right_or_tri_count) +
                        inst._blas_node_start;

                    // ---- BLAS near-first ----
                    float lmin, lmax, rmin, rmax;
                    bool hl = AABBHitFast(
                        g_blas_buffer[left]._min,
                        g_blas_buffer[left]._max,
                        obj_ro, obj_inv_dir, obj_parallel, obj_sign,
                        lmin, lmax);

                    bool hr = AABBHitFast(
                        g_blas_buffer[right]._min,
                        g_blas_buffer[right]._max,
                        obj_ro, obj_inv_dir, obj_parallel, obj_sign,
                        rmin, rmax);

                    if (hl && hr)
                    {
                        if (lmin < rmin)
                        {
                            blas_stack[blas_sp++] = right;
                            blas_stack[blas_sp++] = left;
                        }
                        else
                        {
                            blas_stack[blas_sp++] = left;
                            blas_stack[blas_sp++] = right;
                        }
                    }
                    else if (hl)
                        blas_stack[blas_sp++] = left;
                    else if (hr)
                        blas_stack[blas_sp++] = right;
                }
            }
            
            if (best_tri_idx != -1)
            {
                TriangleData tri = g_scene[best_tri_idx];
                float3 n_local = normalize(
                tri.n0 * (1 - best_uv.x - best_uv.y) +
                tri.n1 * best_uv.x +
                tri.n2 * best_uv.y);
                float3 n_world = normalize(mul(inst._local_to_world,float4(n_local,0)).xyz);
                rec.front_face = dot(ray_dir, n_world) < 0;
                rec.normal = rec.front_face ? n_world : -n_world;
                rec.material_idx = inst._material_id;
                rec.material_type = 0; // 默认漫反射
                rec.uv = tri.uv0 * (1 - best_uv.x - best_uv.y) +
                          tri.uv1 * best_uv.x +
                          tri.uv2 * best_uv.y;
                debug_color = float3(rec.uv,0.0);// random(best_tri_idx);
            }
        }
        else
        {
            uint left  = tlas_node._left_or_tri_offset_or_inst_idx;
            uint right = uint(-tlas_node._neg_right_or_tri_count);

            // ---- TLAS near-first ----
            float lmin, lmax, rmin, rmax;
            bool hl = AABBHitFast(
                g_tlas_buffer[left]._min,
                g_tlas_buffer[left]._max,
                ray_origin, inv_dir, parallel, sign,
                lmin, lmax);

            bool hr = AABBHitFast(
                g_tlas_buffer[right]._min,
                g_tlas_buffer[right]._max,
                ray_origin, inv_dir, parallel, sign,
                rmin, rmax);

            if (hl && hr)
            {
                if (lmin < rmin)
                {
                    tlas_stack[tlas_sp++] = right;
                    tlas_stack[tlas_sp++] = left;
                }
                else
                {
                    tlas_stack[tlas_sp++] = left;
                    tlas_stack[tlas_sp++] = right;
                }
            }
            else if (hl)
                tlas_stack[tlas_sp++] = left;
            else if (hr)
                tlas_stack[tlas_sp++] = right;
        }
    }

    return hit_anything;
}




// 新增：检测从 origin 沿 dir 是否被场景遮挡
// max_t: 对于点光传入到光源的距离；对于定向光传入一个很大的数
bool IsOccluded(float3 origin, float3 dir, float max_t)
{
    // HitWorld 参数顺序是 (ray_dir, ray_origin, out rec)
    HitRecord tmp;
    float3 c;
    bool hit = HitWorld(origin,dir,false, tmp,c);
    if (!hit) return false;
    // 如果返回了命中且距离小于 max_t 则认为被遮挡
    return (tmp.t > 0.0 && tmp.t < max_t);
}
//wi: 入射光方向（指向表面）
//wo: 出射光方向（从表面指出）
float3 EvaluateBRDF(HitRecord rec,float3 wi,float3 wo)
{
    MaterialData mat = g_material_buffer[rec.material_idx];
    //return mat._base_color.rgb / PI;
    SurfaceData surface = SurfaceData_Constructor(rec.normal,mat._roughness,float4(mat._base_color,1.0),mat._emission,mat._metallic,mat._specular,0.0,
    float3(0,0,0),float3(0,0,0));
    wi = -wi;
    float nl = saturate(dot(rec.normal, wi));
    float nv = saturate(dot(rec.normal, wo));
    float3 h = normalize(wi + wo);
    float nh = saturate(dot(rec.normal, h));
    float vh = saturate(dot(wo, h));
    float lh = saturate(dot(wi, h));
    float th = 0;
    float bh = 0;
    ShadingData sd = ShadingData_Constructor(wo,nl,nv,vh,lh,nh,th,bh);
    float3 f = CookTorranceBRDF(surface,sd);
    return f * _DirectionalLights[0]._LightColor * nl;
}

float3 ProceduralSky(float3 d)
{
    float h = saturate(d.y);

    float3 zenith = float3(0.22, 0.35, 0.9);
    float3 horizon = float3(0.8, 0.85, 0.9);
    float3 ground = float3(0.1, 0.1, 0.1);

    float3 sky = lerp(horizon, zenith, pow(h, 0.5));
    float3 env = (d.y > 0) ? sky : ground;

    return env;
}


float3 Trace(float3 ray_dir, float3 ray_origin, uint max_depth,bool is_debug,inout float4 debug_color)
{
    float3 radiance = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    HitRecord temp_rec;
    for (uint depth = 0; depth < max_depth; ++depth)
    {
        bool hit_anything = HitWorld(ray_origin, ray_dir,is_debug, temp_rec,debug_color.rgb);
        if (is_debug)
        {
            uint d = hit_anything ? depth : kMaxDepth-1;
            DebugRay dr = (DebugRay)0;
            dr.pos = ray_origin;
            dr.color = PackFloat4(float4(kDepthColors[d],1.0));
            dr.aabb_idx = -1;
            _debug_rays.Append(dr);
            dr.pos = hit_anything? temp_rec.p : ray_origin + 10 * ray_dir;
            _debug_rays.Append(dr);
        }
        if (!hit_anything)
        {
            float a = 0.5 * (ray_dir.y + 1.0);
            float3 env_Li = ProceduralSky(ray_dir);
            radiance += throughput * env_Li;
            break;
        }
        float3 p = temp_rec.p;
        float3 n = temp_rec.normal;
        float3 wo = -ray_dir; // 出射方向（指向相机）
        // 直接光照
        float3 light_dir = -_MainlightWorldPosition; // 入射方向（指向光源）
        float cos_theta = saturate(dot(n, light_dir));
        if (cos_theta > 0.0)
        {
            bool occluded = IsOccluded(p + 0.001 * n, light_dir, 1e20);
            if (!occluded)
            {
                float3 f = EvaluateBRDF(temp_rec, _MainlightWorldPosition, wo);
                float3 Li = _DirectionalLights[0]._LightColor;
                radiance += throughput * f * Li * cos_theta;
            }
        }
        radiance += g_material_buffer[temp_rec.material_idx]._emission;
        //间接光
        uint seed = asuint(temp_rec.p.x + temp_rec.p.y * 57.0 + temp_rec.p.z * 113.0 + _FrameIndex);
        float3 wi = sample_cosine_hemisphere(n, seed);
        float cos_theta_i = saturate(dot(n, wi));
        if (cos_theta_i < 0.0)
            break;
        float3 f = EvaluateBRDF(temp_rec, -wi, wo);
        float pdf = cos_theta_i / 3.14159265359; // cosine hemisphere pdf
        throughput *= f * cos_theta_i / pdf;
        ray_origin = p + 0.001 * n;
        ray_dir = wi;

        // // 现有材质处理流程（漫反射、金属、介质）
        // if (temp_rec.material_type == 0)
        // {
        //     // 采样反射方向
        //     uint seed = asuint(temp_rec.p.x + temp_rec.p.y * 57.0 + temp_rec.p.z * 113.0);
        //     float3 reflected_dir = sample_cosine_hemisphere(temp_rec.normal, seed);

        //     // 模拟衰减（这里乘0.5相当于每次反射亮度衰减）
        //     //throughput *= 0.5;

        //     // 更新 ray
        //     ray_origin = temp_rec.p + 0.001 * temp_rec.normal;
        //     ray_dir = reflected_dir;
        //     if (IsOccluded(ray_origin, -_MainlightWorldPosition, 1e20))
        //     {
        //         // 在点上采样到的光线被遮挡，什么都不做
        //     }
        //     else
        //     {
        //         float3 wi = -_MainlightWorldPosition;
        //         float cos_theta = saturate(dot(temp_rec.normal, wi));
        //         float3 Li = _DirectionalLights[0]._LightColor;
        //         float3 f  = EvaluateBRDF(temp_rec, wi, wo);
        //         radiance += throughput * f * Li * cos_theta;

        //         float pdf = cos_theta / 3.14159265359; // cosine hemisphere pdf
        //         throughput *= f * cos_theta / pdf;
        //     }
        // }
        // else if (temp_rec.material_type == 1)
        // {
        //     // 模拟衰减（这里乘0.5相当于每次反射亮度衰减）
        //     throughput *= 0.5;
        //     // 更新 ray
        //     ray_origin = temp_rec.p + 0.001 * temp_rec.normal;
        //     ray_dir = reflect(ray_dir, temp_rec.normal) + 0.1 * sample_cosine_hemisphere(temp_rec.normal, asuint(temp_rec.p.x + temp_rec.p.y * 57.0 + temp_rec.p.z * 113.0)) * 0.1;
        // }
        // else if (temp_rec.material_type == 2)
        // {
        //     float ior = 0.8;
        //     bool entering = dot(ray_dir, temp_rec.normal) < 0.0;
        //     float eta = entering ? (1.0 / ior) : ior;// eta_first / eta_second
        //     float3 n = entering ? temp_rec.normal : -temp_rec.normal;
        //     float cos_theta = dot(n, -ray_dir);
        //     float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
        //     bool cannot_refract = eta * sin_theta > 1.0;
        //     float3 dir;
        //     //if (cannot_refract || reflectance(cos_theta, ior))
        //     if (cannot_refract)
        //     {
        //         dir = reflect(ray_dir, n);
        //         debug_color = float4(1, 0, 0, 1); // 红色表示全反射
        //         ray_origin = temp_rec.p + 0.001 * n;
        //     }
        //     else
        //     {
        //         dir = refract(ray_dir, n, eta);//ray必须指向表面,normal必须为与射线方向相反的法线
        //         debug_color = float4(0, 1, 0, 1); // 红色表示全反射
        //         ray_origin = temp_rec.p -0.001 * n;
        //     }
        //     ray_dir = normalize(dir);

        //     // 简单能量衰减
        //     throughput *= 0.9; 
        // }
    }

    return saturate(radiance);
}


[numthreads(16,16,1)]
void RayGen(CSInput input)
{
    float2 uv = (input.DispatchThreadID.xy + 0.5) * _ScreenParams.xy;
    //uv.y = 1.0 - uv.y; // 翻转Y轴以匹配屏幕空间
    // --- 根据 uv 计算射线方向 ---
    // float3 top = lerp(_LT, _RT, uv.x);
    // float3 bottom = lerp(_LB, _RB, uv.x);
    // float3 far_point = lerp(bottom, top, uv.y);

    float3 ray_origin = _CameraPos.xyz;
    uint depth = 3;
    float3 ray_dir = normalize(Unproject(uv,1.0f) - ray_origin);
    bool is_debug = (input.DispatchThreadID.x == _PickPixel.x && input.DispatchThreadID.y == _PickPixel.y);
    //bool is_debug = (input.DispatchThreadID.x == 200 && input.DispatchThreadID.y == 200);
    float4 debug_color = float4(1, 1, 1, 0);
    float3 color = Trace(ray_dir, ray_origin,depth,is_debug, debug_color);
    _GI_Texture[input.DispatchThreadID.xy] = float4(color.rgb, 0.2);
    //_GI_Texture[input.DispatchThreadID.xy] = 0.0.xxxx;
}

TEXTURE2D(_HistoryTarget)
RWTEXTURE2D(_CurrentTarget,float4)

[numthreads(16,16,1)]
void Denoise(CSInput input)
{
    float alpha = 0.05;
    float4 blended = lerp(_HistoryTarget[input.DispatchThreadID.xy], _CurrentTarget[input.DispatchThreadID.xy], alpha);
    _CurrentTarget[input.DispatchThreadID.xy] = blended;
}