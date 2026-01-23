#pragma kernel light_injection

#include "cs_common.hlsli"
#include "../shadow.hlsli"

struct FogVolumeVoxel
{
    float3 light;
    float  density;
};

CBUFFER_START(VolumetricLightParams)
    float4x4 _matrix_iv;
    float4x4 _matrix_p;
    float    _cam_near;
    float    _cam_far;
CBUFFER_END

RWTEXTURE3D(_FogTexture,float4)

[numthreads(16,16,4)]
void light_injection(CSInput input)
{
    uint w, h, d;
    _FogTexture.GetDimensions(w, h, d);

    if (any(input.DispatchThreadID >= uint3(w, h, d)))
        return;

    uint3 idx = input.DispatchThreadID;

    float3 uvw = (float3(idx) + 0.5) / float3(w, h, d);
    //_FogTexture[idx] = distance(uvw, float3(0.5, 0.5, 0.5)) < 0.4 ? float4(1, 1, 1, 1) : float4(0, 0, 0, 1);
    //return;

    // ---- 1. UV + Z Slice ----
    float2 uv = (float2(idx.xy) + 0.5) / float2(w, h);

    float t = (idx.z + 0.5) / d;
    //float viewZ = _cam_near * pow(_cam_far / _cam_near, t);
    float viewZ = _cam_near + t * (_cam_far - _cam_near);

    // ---- 2. View Space Position ----
    float3 viewPos;
    viewPos.x = (uv.x * 2 - 1) * viewZ / _matrix_p._11;
    viewPos.y = (uv.y * 2 - 1) * viewZ / _matrix_p._22;
    viewPos.z = viewZ;

    // ---- 3. World Space Position ----
    float3 worldPos = mul(_matrix_iv, float4(viewPos, 1)).xyz;

    // ---- 4. Lighting ----
    float3 lightDir = normalize(_MainlightWorldPosition.xyz);
    float3 lightColor = _DirectionalLights[0]._LightColor * 200.0;

    float shadow = ApplyCascadeShadow(1.0, worldPos, _DirectionalLights[0]._ShadowDistance);

    float3 injected = lightColor * (1-shadow);

    _FogTexture[idx] = float4(injected, 1.0);
}