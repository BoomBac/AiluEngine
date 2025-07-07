#ifndef __GEOMETRY_H__
#define __GEOMETRY_H__
struct Ray
{
    float3 o;
    float3 d;
};
struct Plane
{
    float3 normal;
    float d;
};
struct Sphere
{
    float3 c;
    float r;
};

bool RayPlaneIntersection(Ray ray, Plane plane, out float3 intersection)
{
    // float denominator = dot(plane.n, ray.d);
    // if (abs(denominator) < 1e-6)
    // {
    //     return false;
    // }
    // float t = dot(plane.n, (plane.p - ray.o)) / denominator;
    // if (t < 0)
    // {
    //     return false;
    // }
    // intersection = ray.o + t * ray.d;
    return true;
}

bool RaySphereIntersection(Ray ray, Sphere sphere, out float3 intersection)
{
    float3 oc = ray.o - sphere.c;
    float a = dot(ray.d, ray.d);
    float b = 2.0 * dot(oc, ray.d);
    float c = dot(oc, oc) - sphere.r * sphere.r;
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0)
    {
        return false;
    }
    float t = (-b - sqrt(discriminant)) / (2.0 * a);
    if (t < 0)
    {
        t = (-b + sqrt(discriminant)) / (2.0 * a);
    }
    intersection = ray.o + t * ray.d;
    return true;
}

bool FrustumCullAABB(float3 center, float3 extent, Plane planes[6])
{
    float3 bl = center - extent;
    float3 tr = center + extent;
    // 粗略 edge check
    bool edgeInFrustum = true;
    for (int ii = 0; ii < 6; ++ii)
    {
        float d1 = dot(planes[ii].normal, bl) + planes[ii].d;
        float d2 = dot(planes[ii].normal, tr) + planes[ii].d;
        if (d1 > 0.0 && d2 > 0.0)
        {
            edgeInFrustum = false;
            break;
        }
    }
    if (edgeInFrustum)
        return false;
    float3 corners[8];
    corners[0] = center + float3( extent.x,  extent.y,  extent.z);
    corners[1] = center + float3( extent.x,  extent.y, -extent.z);
    corners[2] = center + float3( extent.x, -extent.y,  extent.z);
    corners[3] = center + float3( extent.x, -extent.y, -extent.z);
    corners[4] = center + float3(-extent.x,  extent.y,  extent.z);
    corners[5] = center + float3(-extent.x,  extent.y, -extent.z);
    corners[6] = center + float3(-extent.x, -extent.y,  extent.z);
    corners[7] = center + float3(-extent.x, -extent.y, -extent.z);
    // 如果某个平面让所有角点都在其外侧，则剔除
    for (int i = 0; i < 6; ++i)
    {
        int outsideCount = 0;
        for (int j = 0; j < 8; ++j)
        {
            float d = dot(planes[i].normal, corners[j]) + planes[i].d;
            if (d > 0.0)
                outsideCount++;
        }
        if (outsideCount == 8)
            return true; // 完全在该平面外侧
    }
    return false;
}


#endif