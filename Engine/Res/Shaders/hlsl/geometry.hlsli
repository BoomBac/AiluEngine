#ifndef __GEOMETRY_H__
#define __GEOMETRY_H__
struct Ray
{
    float3 o;
    float3 d;
};
struct Plane
{
    float3 p;
    float3 n;
};
struct Sphere
{
    float3 c;
    float r;
};

bool RayPlaneIntersection(Ray ray, Plane plane, out float3 intersection)
{
    float denominator = dot(plane.n, ray.d);
    if (abs(denominator) < 1e-6)
    {
        return false;
    }
    float t = dot(plane.n, (plane.p - ray.o)) / denominator;
    if (t < 0)
    {
        return false;
    }
    intersection = ray.o + t * ray.d;
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

#endif