#pragma once

#include "FrameCaptureTypes.h"
#include "FrameCapture.h"
#include "Framework/Core/SmartPtr.h"
#include <atomic>

namespace Ailu::Render::FrameDebugger
{
class FrameCaptureSession;

class AILU_API FrameCaptureService final
{
public:
    static void RequestCapture(const FrameCaptureOptions &options);
    static void CancelCapture();
    static void BeginFrame(u64 frame_index);
    static void FinalizeFrame();
    static FrameCaptureSession *ActiveSession();
    static Ref<const FrameCapture> LatestCapture();
    static EFrameCaptureState State();
    static u32 LatestCaptureRevision() { return s_capture_revision; }

private:
    inline static std::atomic<FrameCaptureSession *> s_active_session{nullptr};
    inline static std::atomic<EFrameCaptureState> s_state{EFrameCaptureState::kIdle};
    inline static std::atomic<bool> s_capture_requested{false};
    inline static Ref<FrameCapture> s_latest_capture;
    inline static FrameCaptureOptions s_options;
    inline static std::mutex s_mutex;
    inline static u64 s_current_frame_index = 0u;
    inline static u32 s_capture_revision = 0u;
};

} // namespace Ailu::Render::FrameDebugger
