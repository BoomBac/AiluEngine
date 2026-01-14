#include "rt_common.hlsli"
#include "../Compute/cs_common.hlsli"
#pragma kernel RayGen

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
    float t;
    bool front_face;
    uint material_type; // 0: diffuse, 1: metal,2: dielectric
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
bool TriangleHitFast(float3 origin, float3 dir, TriangleData tri,
                     out float t, out float u, out float v,
                     uint cull_mode)
{
    const float kEps = 1e-6;

    float3 e1 = tri.v1 - tri.v0;
    float3 e2 = tri.v2 - tri.v0;

    float3 p  = cross(dir, e2);
    float  det = dot(p, e1);

    // ---- CULLING ----
    if (cull_mode == 1) // back-face cull
    {
        if (det < kEps) return false;
    }
    else if (cull_mode == 2) // front-face cull
    {
        if (det > -kEps) return false;
    }
    else // double-sided
    {
        if (abs(det) < kEps) return false;
    }

    float inv_det = 1.0 / det;

    float3 s = origin - tri.v0;
    u = dot(p, s) * inv_det;
    if (u < -kEps || u > 1.0 + kEps) return false;

    float3 q = cross(s, e1);
    v = dot(q, dir) * inv_det;
    if (v < -kEps || (u + v) > 1.0 + kEps) return false;

    t = dot(q, e2) * inv_det;
    return t > kEps;
}


// 高性能 AABB hit 判定（branchless）
//   bmin, bmax : 包围盒
//   origin     : 光线起点
//   inv_dir    : 光线方向倒数 (1.0 / ray.dir;)，事先算好
//   sign       : 每维 dir 是否 < 0(int3(ray.inv_dir.x < 0, ray.inv_dir.y < 0, ray.inv_dir.z < 0);)，用于选择 t0/t1
// 返回：是否相交（不返回 t 值，用于快速 culling）
bool AABBHitFast(float3 bmin, float3 bmax,float3 origin, float3 inv_dir, int3 sign,out float tmin, out float tmax)
{
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


bool TraverseTLAS(float3 ray_origin, float3 ray_dir,out int hit_instance)
{
    hit_instance = -1;
    uint stack[MAX_STACK];
    int stack_ptr = 0;
    float3 inv_dir = 1.0f / ray_dir;
    float hit_t = 1e20;
    int3 sign = int3(ray_dir.x < 0, ray_dir.y < 0, ray_dir.z < 0);
    // 从 root 节点开始
    stack[stack_ptr++] = 0; // root index = 0
    [loop]
    while(stack_ptr > 0)
    {
        // pop
        uint node_idx = stack[--stack_ptr];
        LBVHNode node = g_tlas_buffer[node_idx];
        float tmin, tmax;
        // AABB 检测
        if(!AABBHitFast(node._min, node._max, ray_origin, inv_dir, sign, tmin, tmax))
            continue;
        // if (tmin > hit_t)
        //     continue;
        hit_t = tmin;
        // 判断叶子
        bool is_leaf = node._neg_right_or_tri_count > 0;

        if(is_leaf)
        {
            hit_instance = int(node._left_or_tri_offset_or_inst_idx);
            return true; // 找到第一个命中实例，或者累积多个
        }
        else
        {
            // 内部节点，push children
            uint left_idx = uint(node._left_or_tri_offset_or_inst_idx);
            uint right_idx = uint(-node._neg_right_or_tri_count);

            // 注意顺序，可先 push 右再左，保证 stack 后序访问左子树 
            stack[stack_ptr++] = right_idx; 
            stack[stack_ptr++] = left_idx;
        }
    }
    return false; // 没有命中任何实例
}
//对象空间bvh查找
bool TraverseBLAS(float3 ray_origin,float3 ray_dir,int start,int count,bool is_debug,out int hit_node)
{
    int n = max(_blas_count,900);
    [loop]
    for(int i = 0; i < n; ++i)
    {
        LBVHNode leaf_node = g_blas_buffer[i];
        if (leaf_node._neg_right_or_tri_count <= 0)
            continue; //
        int tri_start = leaf_node._left_or_tri_offset_or_inst_idx;
        int tri_count = leaf_node._neg_right_or_tri_count;
        [loop]
        for(int tri_idx = tri_start; tri_idx < tri_start + tri_count; ++tri_idx)
        {
            TriangleData tri = g_scene[tri_idx];
            float local_t, u, v;
            if (TriangleHitFast(ray_origin, ray_dir, tri, local_t, u, v,CULL_NONE))
            {
                hit_node = i;
                return true;
            }
        }
    }
    return false;
    // hit_node = -1;
    // uint stack[MAX_STACK];
    // int stack_ptr = 0;
    // float3 inv_dir = 1.0f / ray_dir;
    // int3 sign = int3(ray_dir.x < 0, ray_dir.y < 0, ray_dir.z < 0);
    // float hit_t = 1e20;
    // // 从 start 节点开始
    // stack[stack_ptr++] = start;
    // int index = -1;
    // [loop]
    // while(stack_ptr > 0)
    // {
    //     // pop
    //     uint node_idx = stack[--stack_ptr];
    //     if (node_idx >= start + count)
    //         return false;
    //     LBVHNode node = g_blas_buffer[node_idx];
    //     float tmin, tmax;
    //     // AABB 检测
    //     if(!AABBHitFast(node._min, node._max, ray_origin, inv_dir, sign, tmin, tmax))
    //         continue;
    //     // if (tmin > hit_t)
    //     //     continue;
    //     ++index;
    //     hit_t = tmin;
    //     if (is_debug)
    //     {
    //         DebugDrawAABB(node._min,node._max,1.0f,index,1.0.xxxx);
    //     }
    //     // 判断叶子
    //     bool is_leaf = node._neg_right_or_tri_count > 0;

    //     if(is_leaf)
    //     {
    //         hit_node = node_idx;
    //         return true; // 找到第一个命中实例，或者累积多个
    //     }
    //     else
    //     {
    //         // 内部节点，push children
    //         uint left_idx = uint(node._left_or_tri_offset_or_inst_idx) + start;
    //         uint right_idx = uint(-node._neg_right_or_tri_count) + start;

    //         // 注意顺序，可先 push 右再左，保证 stack 后序访问左子树 
    //         stack[stack_ptr++] = right_idx; 
    //         stack[stack_ptr++] = left_idx;
    //     }
    // }
    // return false; // 没有命中任何实例
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


bool HitWorld(float3 ray_dir,float3 ray_origin,bool is_debug, out HitRecord rec,out float3 debug_color)
{
    bool hit_anything = false;
    float t_min = 1e20;
    int it_count = min(50,_tlas_count);
    float3 obj_ro = ray_origin;
    float3 obj_rd = ray_dir;
    int instance_hit = -1;
    debug_color = float3(0,0,0);
    if (TraverseTLAS(ray_origin, ray_dir, instance_hit))
    {
        // 找到实例，获取它的世界变换
        ObjectInstanceData inst_data = g_instance_data[instance_hit];
        obj_ro = mul(inst_data._world_to_local, float4(ray_origin, 1.0f)).xyz;
        obj_rd = mul(inst_data._world_to_local, float4(ray_dir, 0.0f)).xyz;
        int node_hit = -1;
        if (TraverseBLAS(obj_ro,obj_rd,inst_data._blas_node_start,inst_data._blas_node_count,is_debug,node_hit))
        {
            LBVHNode leaf_node = g_blas_buffer[node_hit];
            //int tri_start = 0;//leaf_node._left_or_tri_offset_or_inst_idx + inst_data._global_triangle_offset;
            //int tri_count = _tri_count;//leaf_node._neg_right_or_tri_count;
            int tri_start = leaf_node._left_or_tri_offset_or_inst_idx + inst_data._global_triangle_offset;
            int tri_count = leaf_node._neg_right_or_tri_count;
            //debug_color = random(node_hit);
            [loop]
            for(int tri_idx = tri_start; tri_idx < tri_start + tri_count; ++tri_idx)
            {
                TriangleData tri = g_scene[tri_idx];
                float local_t, u, v;
                //InterlockedAdd(_triangle_hit_count[0], 1);
                if (TriangleHitFast(obj_ro, obj_rd, tri, local_t, u, v,CULL_BACK_FACE))
                {
                    if (is_debug)
                    {
                        float3 v0 = mul(inst_data._local_to_world, float4(tri.v0, 1)).xyz;
                        float3 v1 = mul(inst_data._local_to_world, float4(tri.v1, 1)).xyz;
                        float3 v2 = mul(inst_data._local_to_world, float4(tri.v2, 1)).xyz;
                        DebugDrawTriangle(v0, v1, v2);
                    }
                    float3 local_hit = obj_ro + local_t * obj_rd;
                    float3 world_hit = mul(inst_data._local_to_world, float4(local_hit, 1)).xyz;
                    float t = length(ray_origin - world_hit);
                    if (t < t_min)
                    {
                        hit_anything = true;
                        t_min = t;
                        rec.p = world_hit;
                        // 插值法线
                        float3 n_model = normalize(
                            tri.n0 * (1 - u - v) + tri.n1 * u + tri.n2 * v
                        );
                        float3 n_world = local_hit;
                        if (dot(n_world, ray_dir) > 0) n_world = -n_world;
                        rec.normal = n_world;
                        rec.material_type = 0;
                        debug_color = n_world;
                        debug_color = n_model;//random(tri_idx);
                        rec.t = t;
                        rec.front_face = dot(ray_dir, rec.normal) < 0;
                    }
                }
            }
        }
    }
    else
    {
        // 没有命中任何实例，直接返回
        return false;
    }
    /*
    for (int i = 0; i < NUM_SPHERES; ++i)
    {
        Sphere sphere = spheres[i];
        float3 oc = ray_origin - sphere.center;

        float b = dot(oc, ray_dir);  // 半判别式
        float c = dot(oc, oc) - sphere.radius * sphere.radius;
        float discriminant = b*b - c;

        if (discriminant > 0.0)
        {
            float sqrt_disc = sqrt(discriminant);
            float t0 = -b - sqrt_disc;
            float t1 = -b + sqrt_disc;
            float t_hit = (t0 > 0.0) ? t0 : ((t1 > 0.0) ? t1 : 1e20);

            if (t_hit < t_min)
            {
                hit_anything = true;
                t_min = t_hit;
                rec.p = ray_origin + t_hit * ray_dir;
                rec.normal = normalize(rec.p - sphere.center);
                rec.material_type = sphere.material_type;
                rec.t = t_hit;
                rec.front_face = dot(ray_dir, rec.normal) < 0;
            }
        }
    }
    */
    return hit_anything;
}

// 新增：检测从 origin 沿 dir 是否被场景遮挡
// max_t: 对于点光传入到光源的距离；对于定向光传入一个很大的数
bool IsOccluded(float3 origin, float3 dir, float max_t)
{
    // HitWorld 参数顺序是 (ray_dir, ray_origin, out rec)
    HitRecord tmp;
    float3 c;
    bool hit = HitWorld(dir, origin,false, tmp,c);
    if (!hit) return false;
    // 如果返回了命中且距离小于 max_t 则认为被遮挡
    return (tmp.t > 0.0 && tmp.t < max_t);
}

float3 Trace(float3 ray_dir, float3 ray_origin, uint max_depth,bool is_debug,inout float4 debug_color)
{
    float3 radiance = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    HitRecord temp_rec;
    for (uint depth = 0; depth < max_depth; ++depth)
    {
        bool hit_anything = HitWorld(ray_dir, ray_origin,is_debug, temp_rec,debug_color.rgb);
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
        if (hit_anything)
        {
            // 现有材质处理流程（漫反射、金属、介质）
            if (temp_rec.material_type == 0)
            {
                // 采样反射方向
                uint seed = asuint(temp_rec.p.x + temp_rec.p.y * 57.0 + temp_rec.p.z * 113.0);
                float3 reflected_dir = sample_cosine_hemisphere(temp_rec.normal, seed);

                // 模拟衰减（这里乘0.5相当于每次反射亮度衰减）
                throughput *= 0.5;

                // 更新 ray
                ray_origin = temp_rec.p + 0.001 * temp_rec.normal;
                ray_dir = reflected_dir;
                if (IsOccluded(ray_origin, -_MainlightWorldPosition, 1e20))
                {
                    // 在点上采样到的光线被遮挡，什么都不做
                }
                else
                {
                    // 没有被遮挡，采样到直射光照
                    float NdotL = max(0.0, dot(temp_rec.normal, -_MainlightWorldPosition));
                    float3 direct_light = _MainlightColor * NdotL;
                    radiance += throughput * direct_light;
                }
            }
            else if (temp_rec.material_type == 1)
            {
                // 模拟衰减（这里乘0.5相当于每次反射亮度衰减）
                throughput *= 0.5;
                // 更新 ray
                ray_origin = temp_rec.p + 0.001 * temp_rec.normal;
                ray_dir = reflect(ray_dir, temp_rec.normal) + 0.1 * sample_cosine_hemisphere(temp_rec.normal, asuint(temp_rec.p.x + temp_rec.p.y * 57.0 + temp_rec.p.z * 113.0)) * 0.1;
            }
            else if (temp_rec.material_type == 2)
            {
                float ior = 0.8;
                bool entering = dot(ray_dir, temp_rec.normal) < 0.0;
                float eta = entering ? (1.0 / ior) : ior;// eta_first / eta_second
                float3 n = entering ? temp_rec.normal : -temp_rec.normal;
                float cos_theta = dot(n, -ray_dir);
                float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
                bool cannot_refract = eta * sin_theta > 1.0;
                float3 dir;
                //if (cannot_refract || reflectance(cos_theta, ior))
                if (cannot_refract)
                {
                    dir = reflect(ray_dir, n);
                    debug_color = float4(1, 0, 0, 1); // 红色表示全反射
                    ray_origin = temp_rec.p + 0.001 * n;
                }
                else
                {
                    dir = refract(ray_dir, n, eta);//ray必须指向表面,normal必须为与射线方向相反的法线
                    debug_color = float4(0, 1, 0, 1); // 红色表示全反射
                    ray_origin = temp_rec.p -0.001 * n;
                }
                ray_dir = normalize(dir);

                // 简单能量衰减
                throughput *= 0.9; 
            }

            // 下一轮循环继续 trace
        }
        else
        {
            // 没有命中物体，采样背景色
            float a = 0.5 * (ray_dir.y + 1.0);
            float3 bg = lerp(float3(1.0, 1.0, 1.0), float3(0.5, 0.7, 1.0), a);

            // 把当前路径的贡献加进最终颜色
            radiance += throughput * bg;
            break;
        }
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
    uint depth = 1;
    float3 ray_dir = normalize(Unproject(uv,1.0f) - ray_origin);
    bool is_debug = (input.DispatchThreadID.x == _PickPixel.x && input.DispatchThreadID.y == _PickPixel.y);
    //bool is_debug = (input.DispatchThreadID.x == 200 && input.DispatchThreadID.y == 200);
    float4 debug_color = float4(1, 1, 1, 0);
    float3 color = Trace(ray_dir, ray_origin,depth,is_debug, debug_color);
    _GI_Texture[input.DispatchThreadID.xy] = float4(pow(debug_color.rgb,1.0), 0.2);
    //_GI_Texture[input.DispatchThreadID.xy] = 0.0.xxxx;
}