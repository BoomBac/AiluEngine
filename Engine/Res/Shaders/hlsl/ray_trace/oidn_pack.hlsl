// Private native compute pipeline; no engine shader asset/reflection registration is required.
Texture2D<float4> source_hdr : register(t0);
ByteAddressBuffer filtered_hdr : register(t1);
Texture2D<float4> source_albedo : register(t2);
Texture2D<float4> source_normal : register(t3);
RWByteAddressBuffer packed_hdr : register(u0);
RWTexture2D<float4> preview_hdr : register(u1);
cbuffer Parameters : register(b0) { uint width; uint height; uint use_guides; }

uint InvalidCounterOffset() { return width * height * 16u * (use_guides != 0u ? 3u : 1u); }

[numthreads(1, 1, 1)]
void ClearInvalidCounter(uint3 pixel : SV_DispatchThreadID)
{
    packed_hdr.Store(InvalidCounterOffset(), 0u);
}
[numthreads(8, 8, 1)]
void PackHDR(uint3 pixel : SV_DispatchThreadID)
{
    if (pixel.x >= width || pixel.y >= height) return;
    float3 color = source_hdr[pixel.xy].rgb;
    bool invalid = any(isnan(color)) || any(isinf(color)) || any(color < 0.0);
    // Invalid components become zero. Finite positive HDR values, including values > 1, remain unchanged.
    color = float3(isfinite(color.x) ? max(color.x, 0.0) : 0.0,
                   isfinite(color.y) ? max(color.y, 0.0) : 0.0,
                   isfinite(color.z) ? max(color.z, 0.0) : 0.0);
    packed_hdr.Store4((pixel.y * width + pixel.x) * 16u, asuint(float4(color, 1.0)));
    if (use_guides != 0u)
    {
        float3 albedo = source_albedo[pixel.xy].rgb;
        float3 normal = source_normal[pixel.xy].rgb;
        albedo = float3(isfinite(albedo.x) ? saturate(albedo.x) : 0.0,
                        isfinite(albedo.y) ? saturate(albedo.y) : 0.0,
                        isfinite(albedo.z) ? saturate(albedo.z) : 0.0);
        normal = float3(isfinite(normal.x) ? clamp(normal.x, -1.0, 1.0) : 0.0,
                        isfinite(normal.y) ? clamp(normal.y, -1.0, 1.0) : 0.0,
                        isfinite(normal.z) ? clamp(normal.z, -1.0, 1.0) : 0.0);
        const uint offset = (pixel.y * width + pixel.x) * 16u;
        const uint plane_bytes = width * height * 16u;
        packed_hdr.Store4(plane_bytes + offset, asuint(float4(albedo, 1.0)));
        packed_hdr.Store4(plane_bytes * 2u + offset, asuint(float4(normal, 1.0)));
    }
    if (invalid)
    {
        uint previous;
        packed_hdr.InterlockedAdd(InvalidCounterOffset(), 1u, previous);
    }
}
[numthreads(8, 8, 1)]
void UnpackHDR(uint3 pixel : SV_DispatchThreadID)
{
    if (pixel.x >= width || pixel.y >= height) return;
    preview_hdr[pixel.xy] = float4(asfloat(filtered_hdr.Load3((pixel.y * width + pixel.x) * 16u)), 1.0);
}
