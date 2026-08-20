#include "Render/FrameDebugger/FrameCaptureService.h"
#include "Framework/Common/Allocator.hpp"
#include "Render/FrameDebugger/FrameCaptureSession.h"
#include "Render/FrameDebugger/FrameCaptureWriter.h"
#include "Render/FrameDebugger/FrameCaptureFinalizer.h"
#include "Framework/Common/Log.h"

namespace Ailu::Render::FrameDebugger
{
void FrameCaptureService::RequestCapture(const FrameCaptureOptions &options)
{
    std::lock_guard lock(s_mutex);
    const EFrameCaptureState state = s_state.load(std::memory_order_acquire);
    if (state != EFrameCaptureState::kIdle && state != EFrameCaptureState::kReady)
        return;
    s_latest_capture.reset();
    s_options = options;
    s_capture_requested.store(true);
    s_state.store(EFrameCaptureState::kArmed, std::memory_order_release);
    LOG_INFO("FrameDebugger: Capture requested (mode={})", (u8)options._output_mode);
}

void FrameCaptureService::CancelCapture()
{
    std::lock_guard lock(s_mutex);
    s_capture_requested.store(false);
    s_state.store(EFrameCaptureState::kIdle);
    s_active_session.store(nullptr);
}

void FrameCaptureService::BeginFrame(u64 frame_index)
{
    if (!s_capture_requested.load(std::memory_order_acquire))
        return;

    std::lock_guard lock(s_mutex);
    if (s_state.load() != EFrameCaptureState::kArmed && s_state.load() != EFrameCaptureState::kIdle)
        return;

    s_capture_requested.store(false);
    s_current_frame_index = frame_index;

    auto session = AL_NEW_TAG(EMemoryTag::kTemporary, FrameCaptureSession, s_options);
    session->SetFrameIndex(frame_index);
    s_active_session.store(session, std::memory_order_release);
    s_state.store(EFrameCaptureState::kCapturing, std::memory_order_release);
}

void FrameCaptureService::FinalizeFrame()
{
    EFrameCaptureState state = s_state.load(std::memory_order_acquire);
    if (state != EFrameCaptureState::kCapturing)
        return;

    s_state.store(EFrameCaptureState::kFinalizing, std::memory_order_release);

    FrameCaptureSession *session = s_active_session.exchange(nullptr, std::memory_order_acquire);
    if (!session)
    {
        s_state.store(EFrameCaptureState::kIdle, std::memory_order_release);
        return;
    }

    Ref<FrameCapture> capture = MakeRef<FrameCapture>();
    FrameCaptureFinalizer::Finalize(*session, *capture);
    capture->CalculateStatistics();
    capture->Statistics()._is_truncated = session->IsTruncated();
    capture->Statistics()._capture_cpu_memory = session->ArenaUsedSize();
    capture->Statistics()._capture_revision = ++s_capture_revision;

    std::lock_guard lock(s_mutex);
    s_latest_capture = capture;
    s_state.store(EFrameCaptureState::kReady, std::memory_order_release);

    AL_DELETE(session);
}

FrameCaptureSession *FrameCaptureService::ActiveSession()
{
    EFrameCaptureState state = s_state.load(std::memory_order_acquire);
    if (state != EFrameCaptureState::kCapturing)
        return nullptr;
    return s_active_session.load(std::memory_order_acquire);
}

Ref<const FrameCapture> FrameCaptureService::LatestCapture()
{
    std::lock_guard lock(s_mutex);
    return s_latest_capture;
}

EFrameCaptureState FrameCaptureService::State()
{
    return s_state.load(std::memory_order_acquire);
}

} // namespace Ailu::Render::FrameDebugger
