#pragma once

#include "FrameCapture.h"

namespace Ailu::Render::FrameDebugger
{
class FrameCaptureSession;

class AILU_API FrameCaptureFinalizer
{
public:
    static void Finalize(const FrameCaptureSession &session, FrameCapture &out_capture);
};

} // namespace Ailu::Render::FrameDebugger
