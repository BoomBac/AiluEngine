#ifndef __HIT_HLSLI__
#define __HIT_HLSLI__
#include "rt_common.hlsli"
#include "../constants.hlsli"
#include "../geometry.hlsli"
#include "gpu_scene.hlsli"
#define RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS 1
#ifndef RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS
    #define RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS 0
#endif

#if RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS
    #include "../bindless.hlsli"
#endif

#define MAX_STACK 64
#define CULL_NONE 0u
#define CULL_BACK_FACE 1u
#define CULL_FRONT_FACE 2u

struct TraversalRayCtx
{
    float3 origin;
    float3 dir;
    float3 inv_dir;
    float3 parallel;
    int3 sign;
};

struct InstanceRayCtx
{
    float3 origin;
    float3 dir;
    float3 inv_dir;
    float3 parallel;
    int3 sign;
    float ray_tmax_local;
};

struct TriangleHitCandidate
{
    int tri_idx;
    float2 bary;
    float world_t;
    float3 world_pos;
};

struct HitRecord
{
    float3 p;
    float3 normal;
    float3 emission;
    float2 uv;
    float t;
    bool front_face;//true表示光线从正面打到表面，也就是进入了物体内部，false表示从背面打到表面，进入了物体外部
    uint material_type; // 0: diffuse, 1: metal,2: dielectric
    uint material_idx;
    bool is_light;
    uint light_idx;
    float pdf;
};


#if RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS
float3 HitLoadBindlessFloat3(ByteAddressBuffer buffer, uint byte_offset)
{
    return asfloat(buffer.Load3(byte_offset));
}

bool LoadTraversalTriangleFromOriginalMesh(PrimitiveData inst, uint scene_triangle_index, out TriangleData tri)
{
    tri = (TriangleData)0;

    if (!valid_bindless_buffer(inst._position_bindless_idx) || !valid_bindless_buffer(inst._index_bindless_idx))
        return false;

    uint submesh_local_triangle_index = scene_triangle_index;
    if (submesh_local_triangle_index >= inst._submesh_triangle_count)
        return false;

    ByteAddressBuffer index_buffer = g_bindless_index_buffer[inst._index_bindless_idx];
    uint3 tri_indices = index_buffer.Load3(submesh_local_triangle_index * 12u);

    ByteAddressBuffer position_buffer = g_bindless_vertex_buffer[inst._position_bindless_idx];
    tri.v0 = HitLoadBindlessFloat3(position_buffer, tri_indices.x * 12u);
    tri.v1 = HitLoadBindlessFloat3(position_buffer, tri_indices.y * 12u);
    tri.v2 = HitLoadBindlessFloat3(position_buffer, tri_indices.z * 12u);
    return true;
}
#endif

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
bool AABBHitFast(float3 bmin, float3 bmax,float3 origin, float3 inv_dir, float3 parallel,int3 sign,inout float tmin, inout float tmax)
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

void InitTraversalRayCtx(float3 ray_origin, float3 ray_dir, out TraversalRayCtx ray_ctx)
{
    ray_ctx.origin = ray_origin;
    ray_ctx.dir = ray_dir;
    ray_ctx.inv_dir = 1.0 / ray_dir;
    ray_ctx.sign = int3(ray_dir < 0);
    ray_ctx.parallel = step(abs(ray_dir), float3(1e-8, 1e-8, 1e-8));
}

void InitInstanceRayCtx(TraversalRayCtx world_ray, PrimitiveData inst, float ray_tmax_world, out InstanceRayCtx inst_ray)
{
    inst_ray.origin = mul(inst._world_to_local, float4(world_ray.origin, 1)).xyz;
    inst_ray.dir = mul(inst._world_to_local, float4(world_ray.dir, 0)).xyz;
    inst_ray.inv_dir = 1.0 / inst_ray.dir;
    inst_ray.sign = int3(inst_ray.dir < 0);
    inst_ray.parallel = step(abs(inst_ray.dir), float3(1e-8, 1e-8, 1e-8));
    inst_ray.ray_tmax_local = ray_tmax_world * inst._max_inv_scale;
}

bool IntersectWorldNode(LBVHNode node, TraversalRayCtx ray_ctx, out float tmin, out float tmax)
{
    float ltmin, lmax;
    bool hit = AABBHitFast(node._min, node._max,
                       ray_ctx.origin, ray_ctx.inv_dir, ray_ctx.parallel, ray_ctx.sign,
                       ltmin, lmax);
    tmin = ltmin;
    tmax = lmax;
    return hit;
}

bool IntersectInstanceNode(LBVHNode node, InstanceRayCtx ray_ctx, out float tmin, out float tmax)
{
    float ltmin, lmax;
    bool hit = AABBHitFast(node._min, node._max,
                       ray_ctx.origin, ray_ctx.inv_dir, ray_ctx.parallel, ray_ctx.sign,
                       ltmin, lmax);
    tmin = ltmin;
    tmax = lmax;
    return hit;
}

void PushNearFirst(uint left, uint right,
                   bool hit_left, bool hit_right,
                   float left_tmin, float right_tmin,
                   inout uint stack[MAX_STACK], inout int sp)
{
    if (hit_left && hit_right)
    {
        if (left_tmin < right_tmin)
        {
            stack[sp++] = right;
            stack[sp++] = left;
        }
        else
        {
            stack[sp++] = left;
            stack[sp++] = right;
        }
    }
    else if (hit_left)
    {
        stack[sp++] = left;
    }
    else if (hit_right)
    {
        stack[sp++] = right;
    }
}

void PushTLASChildrenNearFirst(LBVHNode node, TraversalRayCtx ray_ctx, inout uint stack[MAX_STACK], inout int sp)
{
    uint left = node._left_or_tri_offset_or_inst_idx;
    uint right = uint(-node._neg_right_or_tri_count);

    float lmin, lmax, rmin, rmax;
    bool hl = IntersectWorldNode(g_tlas_buffer[left], ray_ctx, lmin, lmax);
    bool hr = IntersectWorldNode(g_tlas_buffer[right], ray_ctx, rmin, rmax);
    PushNearFirst(left, right, hl, hr, lmin, rmin, stack, sp);
}

void PushBLASChildrenNearFirst(LBVHNode node, PrimitiveData inst, InstanceRayCtx ray_ctx, inout uint stack[MAX_STACK], inout int sp)
{
    uint left = node._left_or_tri_offset_or_inst_idx + inst._blas_node_start;
    uint right = uint(-node._neg_right_or_tri_count) + inst._blas_node_start;

    float lmin, lmax, rmin, rmax;
    bool hl = IntersectInstanceNode(g_blas_buffer[left], ray_ctx, lmin, lmax);
    bool hr = IntersectInstanceNode(g_blas_buffer[right], ray_ctx, rmin, rmax);
    PushNearFirst(left, right, hl, hr, lmin, rmin, stack, sp);
}

bool TraverseBLASClosest(PrimitiveData inst, TraversalRayCtx world_ray, inout HitRecord rec, out TriangleHitCandidate best_hit)
{
    InstanceRayCtx inst_ray;
    InitInstanceRayCtx(world_ray, inst, rec.t, inst_ray);

    uint blas_stack[MAX_STACK];
    int blas_sp = 0;
    blas_stack[blas_sp++] = inst._blas_node_start;

    best_hit.tri_idx = -1;
    best_hit.bary = 0.0.xx;
    best_hit.world_t = rec.t;
    best_hit.world_pos = 0.0.xxx;

    while (blas_sp > 0)
    {
        uint blas_idx = blas_stack[--blas_sp];
        if (blas_idx >= inst._blas_node_start + inst._blas_node_count)
            continue;

        LBVHNode blas_node = g_blas_buffer[blas_idx];

        float btmin, btmax;
        if (!IntersectInstanceNode(blas_node, inst_ray, btmin, btmax))
            continue;
        if (btmin >= inst_ray.ray_tmax_local)
            continue;

        if (blas_node._neg_right_or_tri_count > 0)
        {
            int tri_start = blas_node._left_or_tri_offset_or_inst_idx;
            int tri_count = blas_node._neg_right_or_tri_count;
            for (int tri_idx = tri_start; tri_idx < tri_start + tri_count; ++tri_idx)
            {
                TriangleData tri;
#if RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS
                if (!LoadTraversalTriangleFromOriginalMesh(inst, tri_idx, tri))
                    continue;
#else
                tri = g_scene[tri_idx];
#endif
                float local_t, u, v;
                if (!TriangleHitFast(inst_ray.origin, inst_ray.dir, tri, local_t, u, v, CULL_NONE))
                    continue;
                if (local_t >= inst_ray.ray_tmax_local)
                    continue;

                inst_ray.ray_tmax_local = local_t;

                float3 local_hit = inst_ray.origin + local_t * inst_ray.dir;
                float3 world_hit = mul(inst._local_to_world, float4(local_hit, 1)).xyz;
                float world_t = length(world_hit - world_ray.origin);
                if (world_t >= rec.t)
                    continue;

                rec.t = world_t;
                rec.p = world_hit;
                best_hit.tri_idx = tri_idx;
                best_hit.bary = float2(u, v);
                best_hit.world_t = world_t;
                best_hit.world_pos = world_hit;
            }
        }
        else
        {
            PushBLASChildrenNearFirst(blas_node, inst, inst_ray, blas_stack, blas_sp);
        }
    }

    return best_hit.tri_idx != -1;
}

bool TraverseBLASAny(PrimitiveData inst, TraversalRayCtx world_ray, float ray_tmax_world)
{
    InstanceRayCtx inst_ray;
    InitInstanceRayCtx(world_ray, inst, ray_tmax_world, inst_ray);

    uint blas_stack[MAX_STACK];
    int blas_sp = 0;
    blas_stack[blas_sp++] = inst._blas_node_start;

    while (blas_sp > 0)
    {
        uint blas_idx = blas_stack[--blas_sp];
        if (blas_idx >= inst._blas_node_start + inst._blas_node_count)
            continue;

        LBVHNode blas_node = g_blas_buffer[blas_idx];

        float btmin, btmax;
        if (!IntersectInstanceNode(blas_node, inst_ray, btmin, btmax))
            continue;
        if (btmin >= inst_ray.ray_tmax_local)
            continue;

        if (blas_node._neg_right_or_tri_count > 0)
        {
            int tri_start = blas_node._left_or_tri_offset_or_inst_idx;
            int tri_count = blas_node._neg_right_or_tri_count;
            for (int tri_idx = tri_start; tri_idx < tri_start + tri_count; ++tri_idx)
            {
                TriangleData tri;
#if RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS
                if (!LoadTraversalTriangleFromOriginalMesh(inst, tri_idx, tri))
                    continue;
#else
                tri = g_scene[tri_idx];
#endif
                float local_t, u, v;
                if (!TriangleHitFast(inst_ray.origin, inst_ray.dir, tri, local_t, u, v, CULL_NONE))
                    continue;
                if (local_t >= inst_ray.ray_tmax_local)
                    continue;

                inst_ray.ray_tmax_local = min(inst_ray.ray_tmax_local, local_t);

                float3 local_hit = inst_ray.origin + local_t * inst_ray.dir;
                float3 world_hit = mul(inst._local_to_world, float4(local_hit, 1)).xyz;
                float world_t = length(world_hit - world_ray.origin);
                if (world_t > 0.0 && world_t < ray_tmax_world)
                    return true;
            }
        }
        else
        {
            PushBLASChildrenNearFirst(blas_node, inst, inst_ray, blas_stack, blas_sp);
        }
    }

    return false;
}

bool TraverseSceneClosest(float3 ray_origin, float3 ray_dir, inout HitRecord rec, out TriangleHitCandidate best_hit, out PrimitiveData best_inst)
{
    TraversalRayCtx world_ray;
    InitTraversalRayCtx(ray_origin, ray_dir, world_ray);

    uint tlas_stack[MAX_STACK];
    int tlas_sp = 0;
    tlas_stack[tlas_sp++] = 0;

    best_hit.tri_idx = -1;
    best_hit.bary = 0.0.xx;
    best_hit.world_t = rec.t;
    best_hit.world_pos = 0.0.xxx;
    best_inst = (PrimitiveData)0;

    while (tlas_sp > 0)
    {
        uint tlas_idx = tlas_stack[--tlas_sp];
        LBVHNode tlas_node = g_tlas_buffer[tlas_idx];

        float tmin, tmax;
        if (!IntersectWorldNode(tlas_node, world_ray, tmin, tmax))
            continue;
        if (tmin >= rec.t)
            continue;

        if (tlas_node._neg_right_or_tri_count > 0)
        {
            int inst_id = tlas_node._left_or_tri_offset_or_inst_idx;
            PrimitiveData inst = g_primitive_data[inst_id];

            TriangleHitCandidate inst_hit;
            if (TraverseBLASClosest(inst, world_ray, rec, inst_hit))
            {
                best_hit = inst_hit;
                best_inst = inst;
            }
        }
        else
        {
            PushTLASChildrenNearFirst(tlas_node, world_ray, tlas_stack, tlas_sp);
        }
    }

    return best_hit.tri_idx != -1;
}

bool TraverseSceneAny(float3 ray_origin, float3 ray_dir, float ray_tmax_world)
{
    TraversalRayCtx world_ray;
    InitTraversalRayCtx(ray_origin, ray_dir, world_ray);

    uint tlas_stack[MAX_STACK];
    int tlas_sp = 0;
    tlas_stack[tlas_sp++] = 0;

    while (tlas_sp > 0)
    {
        uint tlas_idx = tlas_stack[--tlas_sp];
        LBVHNode tlas_node = g_tlas_buffer[tlas_idx];

        float tmin, tmax;
        if (!IntersectWorldNode(tlas_node, world_ray, tmin, tmax))
            continue;
        if (tmin >= ray_tmax_world)
            continue;

        if (tlas_node._neg_right_or_tri_count > 0)
        {
            int inst_id = tlas_node._left_or_tri_offset_or_inst_idx;
            if (TraverseBLASAny(g_primitive_data[inst_id], world_ray, ray_tmax_world))
                return true;
        }
        else
        {
            PushTLASChildrenNearFirst(tlas_node, world_ray, tlas_stack, tlas_sp);
        }
    }

    return false;
}

#endif// __HIT_HLSLI__
