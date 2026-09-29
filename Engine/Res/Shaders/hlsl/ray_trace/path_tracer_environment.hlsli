#ifndef PATH_TRACER_ENVIRONMENT_HLSLI
#define PATH_TRACER_ENVIRONMENT_HLSLI

#include "rt_common.hlsli"

TextureCube _EnvironmentSky : register(t3, space0);
#if defined(DXR)
Texture2D<float4> _EnvironmentDistribution : register(t4, space0);
#else
Texture2D<float4> _EnvironmentDistribution;
#endif
RWTexture2D<float4> _EnvironmentDistributionOutput;

float3 EvaluateEnvironmentRadiance(float3 direction);
float3 RotatePathEnvironmentDirection(float3 direction);

struct EnvironmentSample
{
    float3 _direction;
    float3 _radiance;
    float _pdf;
};

float3 CubeFaceDirection(uint face, float2 uv)
{
    float3 direction;
    if (face == 0u) direction = float3(1.0f, -uv.y, -uv.x);
    else if (face == 1u) direction = float3(-1.0f, -uv.y, uv.x);
    else if (face == 2u) direction = float3(uv.x, 1.0f, uv.y);
    else if (face == 3u) direction = float3(uv.x, -1.0f, -uv.y);
    else if (face == 4u) direction = float3(uv.x, -uv.y, 1.0f);
    else direction = float3(-uv.x, -uv.y, -1.0f);
    return normalize(direction);
}

void DirectionToCubeFace(float3 direction, out uint face, out float2 uv)
{
    float3 absolute_direction = abs(direction);
    if (absolute_direction.x >= absolute_direction.y && absolute_direction.x >= absolute_direction.z)
    {
        if (direction.x >= 0.0f) { face = 0u; uv = float2(-direction.z, -direction.y) / absolute_direction.x; }
        else { face = 1u; uv = float2(direction.z, -direction.y) / absolute_direction.x; }
    }
    else if (absolute_direction.y >= absolute_direction.z)
    {
        if (direction.y >= 0.0f) { face = 2u; uv = float2(direction.x, direction.z) / absolute_direction.y; }
        else { face = 3u; uv = float2(direction.x, -direction.z) / absolute_direction.y; }
    }
    else
    {
        if (direction.z >= 0.0f) { face = 4u; uv = float2(direction.x, -direction.y) / absolute_direction.z; }
        else { face = 5u; uv = float2(-direction.x, -direction.y) / absolute_direction.z; }
    }
}

float CubeAreaElement(float x, float y)
{
    return atan2(x * y, sqrt(x * x + y * y + 1.0f));
}

float CubeTexelSolidAngle(uint2 pixel, uint resolution)
{
    float2 lower = (float2(pixel) / resolution) * 2.0f - 1.0f;
    float2 upper = (float2(pixel + 1u) / resolution) * 2.0f - 1.0f;
    return CubeAreaElement(upper.x, upper.y) - CubeAreaElement(lower.x, upper.y) -
           CubeAreaElement(upper.x, lower.y) + CubeAreaElement(lower.x, lower.y);
}

float EnvironmentTexelJacobian(float2 uv)
{
    // uv is in [-1, 1], matching uv_cell_area = 4 / resolution^2.
    float inverse_length = rsqrt(1.0f + dot(uv, uv));
    return inverse_length * inverse_length * inverse_length;
}

float EnvironmentCellPdf(float cell_probability, uint resolution, float2 uv)
{
    float uv_cell_area = 4.0f / ((float)resolution * resolution);
    return cell_probability / max(uv_cell_area * EnvironmentTexelJacobian(uv), 1e-12f);
}

float3 InverseRotatePathEnvironmentDirection(float3 direction)
{
#if defined(DXR)
    const float rotation = g_perSceneData.g_EnvironmentRotation;
#else
    const float rotation = g_EnvironmentRotation;
#endif
    float sine = sin(rotation);
    float cosine = cos(rotation);
    return float3(cosine * direction.x + sine * direction.z,
                  direction.y,
                  -sine * direction.x + cosine * direction.z);
}

uint2 EnvironmentDistributionTexel(uint index, uint resolution, uint width)
{
    return uint2(index % width, index / width);
}

float PdfEnvironment(float3 world_direction)
{
    uint width, height;
    _EnvironmentDistribution.GetDimensions(width, height);
    uint resolution = height;
    if (resolution == 0u || width != resolution * 6u)
        return 1.0f / (4.0f * PI);

    uint face;
    float2 uv;
    DirectionToCubeFace(RotatePathEnvironmentDirection(normalize(world_direction)), face, uv);
    uint2 pixel = min((uint2)((uv * 0.5f + 0.5f) * resolution), resolution - 1u);
    uint index = face * resolution * resolution + pixel.y * resolution + pixel.x;
    uint2 cdf_texel = EnvironmentDistributionTexel(index, resolution, width);
    float4 distribution = _EnvironmentDistribution.Load(int3(cdf_texel, 0));
    float previous_cdf = 0.0f;
    if (index > 0u)
    {
        uint2 previous_texel = EnvironmentDistributionTexel(index - 1u, resolution, width);
        previous_cdf = _EnvironmentDistribution.Load(int3(previous_texel, 0)).x;
    }
    float cell_probability = max(distribution.x - previous_cdf, 0.0f);
    return EnvironmentCellPdf(cell_probability, resolution, uv);
}

EnvironmentSample SampleEnvironment(inout RandomCtx random_ctx)
{
    EnvironmentSample sample = (EnvironmentSample)0;
    uint width, height;
    _EnvironmentDistribution.GetDimensions(width, height);
    uint resolution = height;
    if (resolution == 0u || width != resolution * 6u)
    {
        float z = 1.0f - 2.0f * rand(random_ctx);
        float phi = 2.0f * PI * rand(random_ctx);
        float radius = sqrt(max(0.0f, 1.0f - z * z));
        sample._direction = float3(radius * cos(phi), z, radius * sin(phi));
        sample._pdf = 1.0f / (4.0f * PI);
        sample._radiance = EvaluateEnvironmentRadiance(sample._direction);
        return sample;
    }

    uint count = 6u * resolution * resolution;
    float target = rand(random_ctx);
    uint lower = 0u;
    uint upper = count - 1u;
    // BuildEnvironmentCDF sets the final entry to one. Keep CDF(lower-1) and CDF(upper).
    float lower_cdf = 0.0f;
    float upper_cdf = 1.0f;
    [loop]
    while (lower < upper)
    {
        uint middle = lower + (upper - lower) / 2u;
        float cdf = _EnvironmentDistribution.Load(int3(EnvironmentDistributionTexel(middle, resolution, width), 0)).x;
        if (cdf < target)
        {
            lower = middle + 1u;
            lower_cdf = cdf;
        }
        else
        {
            upper = middle;
            upper_cdf = cdf;
        }
    }

    uint index = lower;
    uint2 pixel = uint2(index % resolution, (index / resolution) % resolution);
    uint face = index / (resolution * resolution);
    float2 face_uv = ((float2(pixel) + rand2(random_ctx)) / resolution) * 2.0f - 1.0f;
    sample._direction = InverseRotatePathEnvironmentDirection(CubeFaceDirection(face, face_uv));
    sample._pdf = EnvironmentCellPdf(max(upper_cdf - lower_cdf, 0.0f), resolution, face_uv);
    sample._radiance = EvaluateEnvironmentRadiance(sample._direction);
    return sample;
}

float3 DecodeDistributionRadiance(float3 radiance)
{
    return max(radiance, 0.0.xxx);
}

float EnvironmentLuminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float EnvironmentDistributionWeight(uint index, uint resolution)
{
    uint face = index / (resolution * resolution);
    uint local_index = index % (resolution * resolution);
    uint2 pixel = uint2(local_index % resolution, local_index / resolution);
    float2 uv = ((float2(pixel) + 0.5f) / resolution) * 2.0f - 1.0f;
    float3 radiance = DecodeDistributionRadiance(
        _EnvironmentSky.SampleLevel(g_LinearClampSampler, CubeFaceDirection(face, uv), 0.0f).rgb);
    return EnvironmentLuminance(radiance) * CubeTexelSolidAngle(pixel, resolution);
}

[numthreads(8, 8, 1)]
void BuildEnvironmentWeights(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint width, height;
    _EnvironmentDistributionOutput.GetDimensions(width, height);
    uint2 output_pixel = dispatch_thread_id.xy;
    if (output_pixel.x >= width || output_pixel.y >= height)
        return;
    uint resolution = height;
    uint index = output_pixel.y * width + output_pixel.x;
    uint face = index / (resolution * resolution);
    uint local_index = index % (resolution * resolution);
    uint2 pixel = uint2(local_index % resolution, local_index / resolution);
    float solid_angle = CubeTexelSolidAngle(pixel, resolution);
    float weight = EnvironmentDistributionWeight(index, resolution);
    _EnvironmentDistributionOutput[output_pixel] = float4(0.0f, weight, solid_angle, (float)face);
}

[numthreads(1, 1, 1)]
void BuildEnvironmentCDF(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (any(dispatch_thread_id != 0u))
        return;
    uint width, height;
    _EnvironmentDistributionOutput.GetDimensions(width, height);
    uint resolution = height;
    uint count = 6u * resolution * resolution;
    float total_weight = 0.0f;
    [loop]
    for (uint weight_index = 0u; weight_index < count; ++weight_index)
    {
        uint2 texel = EnvironmentDistributionTexel(weight_index, resolution, width);
        total_weight += _EnvironmentDistributionOutput[texel].y;
    }
    const bool use_solid_angle_fallback = total_weight <= 1e-12f;
    if (use_solid_angle_fallback)
        total_weight = 4.0f * PI;
    float cumulative = 0.0f;
    [loop]
    for (uint output_index = 0u; output_index < count; ++output_index)
    {
        uint2 output_pixel = EnvironmentDistributionTexel(output_index, resolution, width);
        float4 distribution = _EnvironmentDistributionOutput[output_pixel];
        float weight = use_solid_angle_fallback ? distribution.z : distribution.y;
        // Normalize the accumulated weights once. Summing normalized increments can overshoot 1
        // under high dynamic range and make the forced final CDF entry decrease.
        cumulative += weight;
        distribution.x = min(cumulative / total_weight, 1.0f);
        _EnvironmentDistributionOutput[output_pixel] = distribution;
    }
    _EnvironmentDistributionOutput[uint2(width - 1u, height - 1u)].x = 1.0f;
}

float3 RotatePathEnvironmentDirection(float3 direction)
{
#if defined(DXR)
    const float rotation = g_perSceneData.g_EnvironmentRotation;
#else
    const float rotation = g_EnvironmentRotation;
#endif
    float sine = sin(rotation);
    float cosine = cos(rotation);
    return float3(cosine * direction.x - sine * direction.z,
                  direction.y,
                  sine * direction.x + cosine * direction.z);
}

float GetPathEnvironmentIntensity()
{
#if defined(DXR)
    return g_perSceneData.g_EnvironmentIntensity;
#else
    return g_EnvironmentIntensity;
#endif
}

float3 EvaluateEnvironmentRadiance(float3 direction)
{
    direction = RotatePathEnvironmentDirection(normalize(direction));
    return _EnvironmentSky.SampleLevel(g_LinearClampSampler, direction, 0.0f).rgb *
           GetPathEnvironmentIntensity();
}

#endif // PATH_TRACER_ENVIRONMENT_HLSLI
