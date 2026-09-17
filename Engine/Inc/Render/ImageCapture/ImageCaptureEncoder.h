#pragma once

#include "Render/ImageCapture/ImageCaptureTypes.h"

namespace Ailu::Render
{
class ImageCaptureEncoder
{
public:
    virtual ~ImageCaptureEncoder() = default;

    virtual bool Encode(const ImageCaptureCpuImage &image, const WString &file_path, String &error) = 0;
};
} // namespace Ailu::Render
