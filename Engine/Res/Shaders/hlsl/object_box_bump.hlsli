#ifndef AILU_OBJECT_BOX_BUMP_HLSLI
#define AILU_OBJECT_BOX_BUMP_HLSLI

// BOX orientation/blending follows Blender's kernel/svm/image.h projection contract.
// https://github.com/blender/blender/blob/main/intern/cycles/kernel/svm/image.h
float3 ObjectBoxWeights(float3 normal, float blend)
{
    float3 n = abs(normal) / max(dot(abs(normal), 1.0.xxx), 1e-8);
    float limit = 0.5 * (1.0 + blend);
    if (n.x > limit * (n.x + n.y) && n.x > limit * (n.x + n.z)) return float3(1, 0, 0);
    if (n.y > limit * (n.x + n.y) && n.y > limit * (n.y + n.z)) return float3(0, 1, 0);
    if (n.z > limit * (n.x + n.z) && n.z > limit * (n.y + n.z)) return float3(0, 0, 1);
    if (blend <= 0.0) return float3(1, 0, 0);
    float lower = 0.5 * (1.0 - blend);
    if (n.z < (1.0 - limit) * (n.x + n.y))
    {
        float weight = saturate((n.x / max(n.x + n.y, 1e-8) - lower) / blend);
        return float3(weight, 1.0 - weight, 0);
    }
    if (n.x < (1.0 - limit) * (n.y + n.z))
    {
        float weight = saturate((n.y / max(n.y + n.z, 1e-8) - lower) / blend);
        return float3(0, weight, 1.0 - weight);
    }
    if (n.y < (1.0 - limit) * (n.x + n.z))
    {
        float weight = saturate((n.x / max(n.x + n.z, 1e-8) - lower) / blend);
        return float3(weight, 0, 1.0 - weight);
    }
    return ((2.0 - limit) * n + limit - 1.0) / max(blend, 1e-8);
}

float3 ObjectBoxColor(Texture2D image, SamplerState state, float3 position, float3 normal, float blend,
                      float3 position_dx, float3 position_dy)
{
    float3 weights = ObjectBoxWeights(normal, blend);
    float3 color = 0.0.xxx;
    if (weights.x > 0.0)
    {
        float sign_u = normal.x < 0.0 ? -1.0 : 1.0;
        float2 uv = float2(sign_u * position.y, 1.0 - position.z);
        color += weights.x * image.SampleGrad(state, uv, float2(sign_u * position_dx.y, -position_dx.z),
                                              float2(sign_u * position_dy.y, -position_dy.z)).rgb;
    }
    if (weights.y > 0.0)
    {
        float sign_u = normal.y > 0.0 ? -1.0 : 1.0;
        float2 uv = float2(sign_u * position.x, 1.0 - position.z);
        color += weights.y * image.SampleGrad(state, uv, float2(sign_u * position_dx.x, -position_dx.z),
                                              float2(sign_u * position_dy.x, -position_dy.z)).rgb;
    }
    if (weights.z > 0.0)
    {
        float sign_u = normal.z > 0.0 ? -1.0 : 1.0;
        float2 uv = float2(sign_u * position.y, 1.0 - position.x);
        color += weights.z * image.SampleGrad(state, uv, float2(sign_u * position_dx.y, -position_dx.x),
                                              float2(sign_u * position_dy.y, -position_dy.x)).rgb;
    }
    return color;
}

float ObjectBoxHeight(Texture2D image, SamplerState state, float3 position, float3 normal, float blend,
                      float3 position_dx, float3 position_dy)
{
    return dot(ObjectBoxColor(image, state, position, normal, blend, position_dx, position_dy),
               float3(0.2126, 0.7152, 0.0722));
}

float3 EvaluateObjectBoxColor(Texture2D image, SamplerState state, StandardPSInput input, float blend,
                             float mip_bias)
{
    float3 position = float3(input.object_pos.x, -input.object_pos.z, input.object_pos.y);
    float3 normal = normalize(float3(input.object_normal.x, -input.object_normal.z, input.object_normal.y));
    return ObjectBoxColor(image, state, position, normal, blend,
                          ddx(position) * exp2(mip_bias), ddy(position) * exp2(mip_bias));
}

float3 EvaluateObjectBoxBump(Texture2D image, SamplerState state, StandardPSInput input, float4 parameters,
                            float mip_bias)
{
    // Incoming positions/normals use Ailu's basis; source Object coordinates use Blender's basis.
    float3 position = float3(input.object_pos.x, -input.object_pos.z, input.object_pos.y);
    float3 normal = normalize(float3(input.object_normal.x, -input.object_normal.z, input.object_normal.y));
    float3 position_dx = ddx(position);
    float3 position_dy = ddy(position);
    float3 sample_dx = position_dx * exp2(mip_bias);
    float3 sample_dy = position_dy * exp2(mip_bias);
    uint width, height;
    image.GetDimensions(width, height);
    float step = 0.5 / max(max(width, height), 1u);
    float3 gradient;
    [unroll] for (uint axis = 0u; axis < 3u; ++axis)
    {
        float3 offset = 0.0.xxx;
        offset[axis] = step;
        float high = ObjectBoxHeight(image, state, position + offset, normal, parameters.z, sample_dx, sample_dy);
        float low = ObjectBoxHeight(image, state, position - offset, normal, parameters.z, sample_dx, sample_dy);
        gradient[axis] = (high - low) / (2.0 * step);
    }
    float3 world_normal = normalize(input.normal);
    float3 world_dx = ddx(input.world_pos);
    float3 world_dy = ddy(input.world_pos);
    float3 reciprocal_x = cross(world_dy, world_normal);
    float3 reciprocal_y = cross(world_normal, world_dx);
    float determinant = dot(world_dx, reciprocal_x);
    if (abs(determinant) <= 1e-12) return world_normal;
    float3 surface_gradient = (dot(gradient, position_dx) * reciprocal_x
                            + dot(gradient, position_dy) * reciprocal_y) / determinant;
    float distance = parameters.x < 0.0 ? -parameters.y : parameters.y;
    float3 perturbed = normalize(world_normal - distance * surface_gradient);
    return normalize(lerp(world_normal, perturbed, saturate(abs(parameters.x))));
}

#endif
