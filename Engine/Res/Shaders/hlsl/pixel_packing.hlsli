#ifndef __PIXEL_PACKING__
#define __PIXEL_PACKING__

uint EncodeRGBE(float3 color, uint mask)
{
    float maxRGB = max(max(color.r, color.g), color.b);
    if (maxRGB < 1e-5)
    {
        return 0;
    }
    float exponent = ceil(log2(maxRGB));
    float scale = exp2(-exponent);
    uint encodedR = (uint)(color.r * scale * 255.0);
    uint encodedG = (uint)(color.g * scale * 255.0);
    uint encodedB = (uint)(color.b * scale * 255.0);
    uint encodedE = (uint)(exponent + 64); // 偏移到 [0, 127]
    encodedE = (encodedE & 0x7F) | ((mask & 0x1) << 7);
    return (encodedE << 24) | (encodedB << 16) | (encodedG << 8) | encodedR;
}
float3 DecodeRGBE(uint encoded, out float mask)
{
    // 提取每个分量
    uint encodedR = encoded & 0xFF;            // 低 8 位为 R
    uint encodedG = (encoded >> 8) & 0xFF;    // 次低 8 位为 G
    uint encodedB = (encoded >> 16) & 0xFF;   // 中间 8 位为 B
    uint encodedE = (encoded >> 24) & 0xFF;   // 高 8 位为 E
    // 提取附加位（最高位）
    mask = float((encodedE >> 7) & 0x1);         // 提取最高位
    encodedE = encodedE & 0x7F;               // 提取指数部分（低 7 位）
    // 解码颜色值
    float scale = exp2((int)encodedE - 64);
    float3 decodedColor = float3(encodedR, encodedG, encodedB) / 255.0 * scale;
    return decodedColor;
}

#endif