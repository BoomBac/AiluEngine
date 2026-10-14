#ifndef AILU_MATERIAL_SAMPLING_HLSLI
#define AILU_MATERIAL_SAMPLING_HLSLI
SamplerState g_material_samplers[] : register(s0, space11);
#if defined(DXR)
#define MATERIAL_MANUAL_MIP_BIAS g_perCamData._TextureManualMipBias
#else
#define MATERIAL_MANUAL_MIP_BIAS _TextureManualMipBias
#endif
float ResolveMaterialMipBias(uint view_mask, uint slot, float manual_bias, float effective_bias)
{
    return (view_mask & (1u << slot)) != 0u ? effective_bias : manual_bias;
}
// The hardware applies native sampler MipLODBias; bias here is an additional sampling/view offset.
float4 SampleMaterialTextureLevel(Texture2D texture_value, uint sampler_index, float2 uv, float lod, float bias)
{
    return texture_value.SampleLevel(g_material_samplers[NonUniformResourceIndex(sampler_index)], uv, lod + bias);
}
#endif
