#pragma once

#include "Render/ImageCapture/ImageCaptureEncoder.h"

namespace Ailu::Render
{
// 把抓帧结果写成 8bit PNG，便于直接看图和做像素统计。
// 浮点/高动态范围格式会先做 Reinhard 色调映射再转 sRGB，避免高光整片爆白。
class PngCaptureEncoder final : public ImageCaptureEncoder
{
public:
    bool Encode(const ImageCaptureCpuImage &image, const WString &file_path, String &error) override;
};
} // namespace Ailu::Render
