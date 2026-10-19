#ifndef _PUNCTUAL_LIGHTING_H_
#define _PUNCTUAL_LIGHTING_H_

float GetRangeAtt(float light_distance, float attenuation_radius)
{
    if (attenuation_radius <= 0.0f)
        return 1.0f;
    const float normalized_distance = light_distance / attenuation_radius;
    const float window = Pow2(saturate(1.0f - Pow4(normalized_distance)));
    return normalized_distance >= 1.0f ? 0.0f : window;
}

// Shared by surface and froxel lighting. Range window and cone match the Renderer GPU data.
float GetDistanceAtt(float light_distance, float attenuation_radius, float source_radius = 0.01f)
{
    const float distance_squared = max(Pow2(light_distance), Pow2(max(source_radius, 1e-4f)));
    return rcp(distance_squared) * GetRangeAtt(light_distance, attenuation_radius);
}

float GetAngleAtt(float3 light_travel_dir, float3 light_dir, float angle_scale, float angle_offset)
{
    const float attenuation = saturate(dot(light_dir, light_travel_dir) * angle_scale + angle_offset);
    return attenuation * attenuation;
}
#endif
