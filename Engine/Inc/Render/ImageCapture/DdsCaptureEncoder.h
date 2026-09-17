#pragma once

#include "Render/ImageCapture/ImageCaptureEncoder.h"

namespace Ailu::Render
{
class DdsCaptureEncoder final : public ImageCaptureEncoder
{
public:
    bool Encode(const ImageCaptureCpuImage &image, const WString &file_path, String &error) override;
};
} // namespace Ailu::Render
