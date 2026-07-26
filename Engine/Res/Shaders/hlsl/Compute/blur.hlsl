#include "cs_common.hlsli"
#pragma kernel blur_x
#pragma kernel blur_y

Texture2D _SourceTex;
RWTexture2D<float4> _OutTex;

static const int kRadius = 8;
static const float kWeights[9] = {
    0.1336684537f,
    0.1264449394f,
    0.1070333304f,
    0.0810740171f,
    0.0549527441f,
    0.0332356932f,
    0.0179631428f,
    0.0086967040f,
    0.0037652022f
};

float4 GaussianBlur(int2 pixel, int2 direction, int2 source_size)
{
    float4 c = _SourceTex.Load(uint3(clamp(pixel, int2(0, 0), source_size - 1), 0)) * kWeights[0];
    for (int i = 1; i <= kRadius; ++i)
    {
        int2 offset = direction * i;
        c += _SourceTex.Load(uint3(clamp(pixel + offset, int2(0, 0), source_size - 1), 0)) * kWeights[i];
        c += _SourceTex.Load(uint3(clamp(pixel - offset, int2(0, 0), source_size - 1), 0)) * kWeights[i];
    }
    c.a = 1.0f;
    return c;
}

[numthreads(16,16,1)]
void blur_x(CSInput input)
{
    uint width;
    uint height;
    _OutTex.GetDimensions(width, height);
    if (input.DispatchThreadID.x >= width || input.DispatchThreadID.y >= height)
        return;

    uint source_width;
    uint source_height;
    _SourceTex.GetDimensions(source_width, source_height);
    _OutTex[input.DispatchThreadID.xy] = GaussianBlur(input.DispatchThreadID.xy, int2(1, 0), int2(source_width, source_height));
}

[numthreads(16,16,1)]
void blur_y(CSInput input)
{
    uint width;
    uint height;
    _OutTex.GetDimensions(width, height);
    if (input.DispatchThreadID.x >= width || input.DispatchThreadID.y >= height)
        return;

    uint source_width;
    uint source_height;
    _SourceTex.GetDimensions(source_width, source_height);
    _OutTex[input.DispatchThreadID.xy] = GaussianBlur(input.DispatchThreadID.xy, int2(0, 1), int2(source_width, source_height));
}
