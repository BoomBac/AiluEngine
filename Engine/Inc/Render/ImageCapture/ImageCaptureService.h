#pragma once

#include "Render/ImageCapture/ImageCaptureTypes.h"

namespace Ailu::Render
{
class CommandBuffer;
class AILU_API ImageCaptureService final
{
public:
    static ImageCaptureSessionId BeginSequence(const ImageCaptureSequenceDesc &desc);
    static ImageCaptureSessionId CaptureOnce(CommandBuffer &cmd, Texture *texture,
                                             const ImageCaptureTextureDesc &texture_desc,
                                             const ImageCaptureSequenceDesc &sequence_desc);
    static bool TryCapture(ImageCaptureSessionId session_id, CommandBuffer &cmd, Texture *texture,
                           const ImageCaptureTextureDesc &texture_desc);
    static void Cancel(ImageCaptureSessionId session_id);
    static ImageCaptureStatus GetStatus(ImageCaptureSessionId session_id);
    static void Tick();
    static void Shutdown();

    // Backend entry points. They carry no graphics API types and are used by the DX12 command processor.
    static bool PrepareReadback(ImageCaptureSessionId session_id, ImageCaptureReadbackId readback_id, u64 byte_size);
    static void CompleteReadback(ImageCaptureReadbackData &&data);
    static void FailReadback(ImageCaptureSessionId session_id, ImageCaptureReadbackId readback_id, String error);
    static void ReportError(ImageCaptureSessionId session_id, String error);
};
} // namespace Ailu::Render
