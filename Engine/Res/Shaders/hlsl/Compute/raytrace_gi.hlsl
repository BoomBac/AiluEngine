#pragma kernel RayGen
#include "cs_common.hlsli"

RWTEXTURE2D(_GI_Texture,float4)

static const float3 _SphereCenter = float3(0.0, 2, 0.0);
static const float _SphereRadius = 2;

// --- 新增：简单定向光参数（可改为常量或从 CBUFFER 传入） ---
static const float3 _SunDirection = normalize(float3(-0.5, -1.0, -0.3)); // 方向：从表面指向光源
static const float3 _SunColor = float3(1.00, 0.95, 0.9);
static const float  _SunIntensity = 4.0;

CBUFFER_START(ComputeCB)
    int2 _PickPixel;
CBUFFER_END

struct DebugRay
{
    float3 pos;
    uint data;
};
static const uint kMaxDepth = 9;
AppendStructuredBuffer<DebugRay>   _debug_rays;
AppendStructuredBuffer<uint>   _debug_ray_indices;

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


#define NUM_SPHERES 4

static const Sphere spheres[NUM_SPHERES] = {
    {float3(0.0, 2, 0.0), 2,0},
    {float3(0, -100, 0.0), 100,0},
    {float3(5, 2, 0.0), 2,1},
    {float3(-5, 2, 0.0), 2,2},
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


bool HitWorld(float3 ray_dir,float3 ray_origin, out HitRecord rec)
{
    bool hit_anything = false;
    float t_min = 1e20;
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
    return hit_anything;
}

// 新增：检测从 origin 沿 dir 是否被场景遮挡
// max_t: 对于点光传入到光源的距离；对于定向光传入一个很大的数
bool IsOccluded(float3 origin, float3 dir, float max_t)
{
    // HitWorld 参数顺序是 (ray_dir, ray_origin, out rec)
    HitRecord tmp;
    bool hit = HitWorld(dir, origin, tmp);
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
        bool hit_anything = HitWorld(ray_dir, ray_origin,temp_rec);
        if (is_debug)
        {
            uint d = hit_anything ? depth : kMaxDepth-1;
            DebugRay dr;
            dr.pos = ray_origin;
            dr.data = d;
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
    uint depth = 5;
    float3 ray_dir = normalize(Unproject(uv,1.0f) - ray_origin);
    bool is_debug = (input.DispatchThreadID.x == _PickPixel.x && input.DispatchThreadID.y == _PickPixel.y);
    //bool is_debug = (input.DispatchThreadID.x == 200 && input.DispatchThreadID.y == 200);
    float4 debug_color = float4(1, 1, 1, 0);
    float3 color = Trace(ray_dir, ray_origin,depth,is_debug, debug_color);
    _GI_Texture[input.DispatchThreadID.xy] = float4(pow(color * debug_color.rgb,1.0), 1.0);
}