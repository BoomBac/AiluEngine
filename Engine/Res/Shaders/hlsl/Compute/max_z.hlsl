#pragma kernel MaxZ
#include "cs_common.hlsli"

TEXTURE2D(_CameraDepthTexture)
RWTEXTURE2D(_MaxZ_Texture,float)

CBUFFER_START(MaxZParams)
    uint2 _radius;
CBUFFER_END

[numthreads(16,16,1)]
void MaxZ(CSInput input)
{
    uint2 pixel = input.DispatchThreadID.xy;

    uint w, h;
    _MaxZ_Texture.GetDimensions(w, h);
    if (any(pixel >= uint2(w, h)))
        return;

    uint depth_w, depth_h;
    _CameraDepthTexture.GetDimensions(depth_w, depth_h);
    // 每个 tile 的起点
    uint2 origin = pixel * _radius;
#if defined(_REVERSED_Z)
    float z_best = 1.0;
#else
    float z_best = 0.0;
#endif

    for (uint y = 0; y < _radius.y; ++y)
    {
        for (uint x = 0; x < _radius.x; ++x)
        {
            uint2 p = origin + uint2(x, y);
            if (p.x >= depth_w || p.y >= depth_h)
                continue;
            float d = LOAD_TEXTURE2D(_CameraDepthTexture, p).r;
#if defined(_REVERSED_Z)
            z_best = min(z_best, d);
#else
            z_best = max(z_best, d);
#endif
        }
    }
    _MaxZ_Texture[pixel] = z_best;
}
