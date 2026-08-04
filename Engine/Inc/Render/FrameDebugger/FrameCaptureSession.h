#pragma once

#include "FrameCaptureTypes.h"
#include "FrameCapture.h"
#include "FrameCaptureReason.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/Containers/Vector.h"
#include <atomic>
#include <mutex>

namespace Ailu::Render::FrameDebugger
{
class FrameCaptureWriter;

class FrameCaptureSession final
{
    friend class FrameCaptureService;
    friend class FrameCaptureFinalizer;

public:
    explicit FrameCaptureSession(const FrameCaptureOptions &options);
    ~FrameCaptureSession();

    FrameCaptureWriter *CreateWriter(u32 submission_index, StringView command_buffer_name);
    bool TryReserveCaptureMemory(u64 size);

    u64 FrameIndex() const { return _frame_index; }
    bool IsTruncated() const { return _is_truncated.load(std::memory_order_acquire); }
    u64 ArenaUsedSize() const { return _reserved_size.load(std::memory_order_relaxed); }

private:
    FrameCaptureOptions _options;
    u64 _frame_index = 0u;
    FrameCaptureArena _arena;
    CaptureStringTable _global_strings;
    CaptureObjectTable _global_objects;
    Vector<Scope<FrameCaptureWriter>> _writers;
    std::atomic<u32> _next_chunk_index{0u};
    std::atomic<u64> _reserved_size{0u};
    std::atomic<bool> _is_truncated{false};
    std::mutex _chunk_mutex;

    void SetFrameIndex(u64 index) { _frame_index = index; }
};

} // namespace Ailu::Render::FrameDebugger
