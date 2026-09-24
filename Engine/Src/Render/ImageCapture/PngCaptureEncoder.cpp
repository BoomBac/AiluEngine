#include "Render/ImageCapture/PngCaptureEncoder.h"

#include "Framework/Common/Utils.h"
#include "Render/AlgFormat.h"

#include "Ext/stb/stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace Ailu::Render
{
namespace
{
    // 返回该格式每像素的字节数，未知/压缩格式返回 0。
    u32 BytesPerPixel(EALGFormat format)
    {
        switch (format)
        {
        case EALGFormat::kALGFormatR32G32B32A32_FLOAT:
        case EALGFormat::kALGFormatR32G32B32A32_UINT:
        case EALGFormat::kALGFormatR32G32B32A32_SINT:
        case EALGFormat::kALGFormatR32G32B32A32_TYPELESS: return 16u;
        case EALGFormat::kALGFormatR16G16B16A16_FLOAT:
        case EALGFormat::kALGFormatR16G16B16A16_UNORM:
        case EALGFormat::kALGFormatR16G16B16A16_TYPELESS: return 8u;
        case EALGFormat::kALGFormatR8G8B8A8_UNORM:
        case EALGFormat::kALGFormatR8G8B8A8_UNORM_SRGB:
        case EALGFormat::kALGFormatR8G8B8A8_UINT:
        case EALGFormat::kALGFormatR8G8B8A8_SNORM:
        case EALGFormat::kALGFormatR8G8B8A8_SINT:
        case EALGFormat::kALGFormatR10G10B10A2_UNORM:
        case EALGFormat::kALGFormatR10G10B10A2_TYPELESS:
        case EALGFormat::kALGFormatR11G11B10_FLOAT:
        case EALGFormat::kALGFormatB8G8R8A8_UNORM:
        case EALGFormat::kALGFormatB8G8R8A8_UNORM_SRGB: return 4u;
        default: return 0u;
        }
    }

    bool IsFloatFormat(EALGFormat format)
    {
        switch (format)
        {
        case EALGFormat::kALGFormatR32G32B32A32_FLOAT:
        case EALGFormat::kALGFormatR11G11B10_FLOAT: return true;
        default: return false;
        }
    }

    f32 HalfToFloat(u16 half)
    {
        const u32 sign = static_cast<u32>(half & 0x8000u) << 16u;
        u32 exponent = (half >> 10u) & 0x1Fu;
        u32 mantissa = half & 0x3FFu;
        u32 bits = 0u;
        if (exponent == 0u)
        {
            if (mantissa == 0u)
            {
                bits = sign;
            }
            else
            {
                // 次正规数：规格化后指数取 1
                exponent = 1u;
                while ((mantissa & 0x400u) == 0u)
                {
                    mantissa <<= 1u;
                    --exponent;
                }
                mantissa &= 0x3FFu;
                bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
            }
        }
        else if (exponent == 0x1Fu)
        {
            bits = sign | 0x7F800000u | (mantissa << 13u);
        }
        else
        {
            bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
        }
        f32 value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void UnpackR11G11B10(u32 packed, f32 &r, f32 &g, f32 &b)
    {
        const u32 r11 = packed & 0x7FFu;
        const u32 g11 = (packed >> 11u) & 0x7FFu;
        const u32 b10 = (packed >> 22u) & 0x3FFu;
        const auto expand = [](u32 value, u32 mantissa_bits) -> f32
        {
            const u32 exponent = (value >> mantissa_bits) & 0x1Fu;
            const u32 mantissa = value & ((1u << mantissa_bits) - 1u);
            if (exponent == 0u)
                return std::ldexp(static_cast<f32>(mantissa), -14 - static_cast<i32>(mantissa_bits));
            if (exponent == 0x1Fu)
                return 0.0f;   // Inf/NaN 在可视化里按 0 处理
            return std::ldexp(1.0f + static_cast<f32>(mantissa) / static_cast<f32>(1u << mantissa_bits),
                              static_cast<i32>(exponent) - 15);
        };
        r = expand(r11, 6u);
        g = expand(g11, 6u);
        b = expand(b10, 5u);
    }

    // Reinhard 色调映射 + sRGB 编码；线性值 <= 1 时基本等价于原来的 gamma 处理，
    // 高光不爆白，暗部也不会被压没。
    u8 LinearToSrgbByte(f32 linear)
    {
        if (!(linear > 0.0f))
            return 0u;
        const f32 mapped = linear / (1.0f + linear);
        const f32 srgb = mapped <= 0.0031308f ? mapped * 12.92f
                                               : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
        return static_cast<u8>(std::lround(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
    }

    struct FloatRgba
    {
        f32 _r = 0.0f;
        f32 _g = 0.0f;
        f32 _b = 0.0f;
        f32 _a = 1.0f;
    };

    FloatRgba ReadPixel(const u8 *row, u32 x, EALGFormat format)
    {
        FloatRgba out;
        switch (format)
        {
        case EALGFormat::kALGFormatR32G32B32A32_FLOAT:
        {
            const f32 *pixel = reinterpret_cast<const f32 *>(row) + static_cast<size_t>(x) * 4u;
            out = {pixel[0], pixel[1], pixel[2], pixel[3]};
            break;
        }
        case EALGFormat::kALGFormatR16G16B16A16_FLOAT:
        {
            const u16 *pixel = reinterpret_cast<const u16 *>(row) + static_cast<size_t>(x) * 4u;
            out = {HalfToFloat(pixel[0]), HalfToFloat(pixel[1]), HalfToFloat(pixel[2]), HalfToFloat(pixel[3])};
            break;
        }
        case EALGFormat::kALGFormatR8G8B8A8_UNORM:
        case EALGFormat::kALGFormatR8G8B8A8_UNORM_SRGB:
        {
            const u8 *pixel = row + static_cast<size_t>(x) * 4u;
            out = {pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f, pixel[3] / 255.0f};
            break;
        }
        case EALGFormat::kALGFormatR11G11B10_FLOAT:
        {
            const u32 packed = reinterpret_cast<const u32 *>(row)[x];
            UnpackR11G11B10(packed, out._r, out._g, out._b);
            out._a = 1.0f;
            break;
        }
        case EALGFormat::kALGFormatR10G10B10A2_UNORM:
        {
            const u32 packed = reinterpret_cast<const u32 *>(row)[x];
            out = {static_cast<f32>(packed & 0x3FFu) / 1023.0f,
                   static_cast<f32>((packed >> 10u) & 0x3FFu) / 1023.0f,
                   static_cast<f32>((packed >> 20u) & 0x3FFu) / 1023.0f,
                   static_cast<f32>((packed >> 30u) & 0x3u) / 3.0f};
            break;
        }
        default:
            break;
        }
        return out;
    }
} // namespace

bool PngCaptureEncoder::Encode(const ImageCaptureCpuImage &image, const WString &file_path, String &error)
{
    if (image._format == EALGFormat::kALGFormatUNKOWN || image._sub_resources.empty())
    {
        error = "PNG encoder received an empty or unknown image";
        return false;
    }

    const ImageCaptureCpuSubresource &sub = image._sub_resources.front();
    if (sub._data == nullptr || sub._width == 0u || sub._height == 0u || sub._row_pitch == 0u)
    {
        error = "PNG encoder received an invalid subresource";
        return false;
    }

    const u32 bytes_per_pixel = BytesPerPixel(image._format);
    if (bytes_per_pixel == 0u)
    {
        error = "PNG encoder does not support the source format";
        return false;
    }
    if (IsFloatFormat(image._format) && sub._row_pitch < sub._width * bytes_per_pixel)
    {
        error = "PNG encoder received a row pitch smaller than the row size";
        return false;
    }

    Vector<u8> pixels(static_cast<size_t>(sub._width) * sub._height * 4u);
    for (u32 y = 0u; y < sub._height; ++y)
    {
        const u8 *row = sub._data + static_cast<size_t>(y) * sub._row_pitch;
        u8 *dst = pixels.data() + static_cast<size_t>(y) * sub._width * 4u;
        for (u32 x = 0u; x < sub._width; ++x)
        {
            const FloatRgba value = ReadPixel(row, x, image._format);
            dst[x * 4u + 0u] = LinearToSrgbByte(value._r);
            dst[x * 4u + 1u] = LinearToSrgbByte(value._g);
            dst[x * 4u + 2u] = LinearToSrgbByte(value._b);
            dst[x * 4u + 3u] = static_cast<u8>(std::lround(std::clamp(value._a, 0.0f, 1.0f) * 255.0f));
        }
    }

    // stb 的实现在 Texture.cpp 里，这里只声明使用。
    const std::string path = ToChar(file_path);
    if (stbi_write_png(path.c_str(), static_cast<int>(sub._width), static_cast<int>(sub._height), 4,
                       pixels.data(), static_cast<int>(sub._width * 4u)) == 0)
    {
        error = "stbi_write_png failed";
        return false;
    }
    return true;
}
} // namespace Ailu::Render
