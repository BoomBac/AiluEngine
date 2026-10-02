#ifndef _PUNCTUAL_LIGHTING_H_
#define _PUNCTUAL_LIGHTING_H_

// Shared by surface and froxel lighting. Range window and cone match the Renderer GPU data.
float GetDistanceAtt(float light_distance, float attenuation_radius)
{
    const float radius = max(attenuation_radius, 1e-4f);
    const float inverse_square = Pow2(radius) / (Pow2(light_distance) + 1.0f);
    const float window = Pow2(saturate(1.0f - Pow4(light_distance / radius)));
    return inverse_square * window;
}

float GetAngleAtt(float3 light_travel_dir, float3 light_dir, float angle_scale, float angle_offset)
{
    const float attenuation = saturate(dot(light_dir, light_travel_dir) * angle_scale + angle_offset);
    return attenuation * attenuation;
}
#endif
