#include "Render/FrameDebugger/FrameCaptureSession.h"
#include "Render/FrameDebugger/FrameCaptureWriter.h"

namespace Ailu::Render::FrameDebugger
{
FrameCaptureSession::FrameCaptureSession(const FrameCaptureOptions &options)
    : _options(options)
    , _arena(options._memory_budget)
{
    _writers.reserve(32);
}

FrameCaptureSession::~FrameCaptureSession()
{
    _writers.clear();
}

FrameCaptureWriter *FrameCaptureSession::CreateWriter(u32 submission_index, StringView command_buffer_name)
{
    if (!TryReserveCaptureMemory(sizeof(FrameCaptureWriter)))
        return nullptr;
    auto writer = MakeScope<FrameCaptureWriter>(*this, submission_index, command_buffer_name);
    writer->Chunk()._chunk_index = _next_chunk_index.fetch_add(1u, std::memory_order_relaxed);
    FrameCaptureWriter *raw = writer.get();
    {
        std::lock_guard lock(_chunk_mutex);
        _writers.push_back(std::move(writer));
    }
    return raw;
}

bool FrameCaptureSession::TryReserveCaptureMemory(u64 size)
{
    u64 used_size = _reserved_size.load(std::memory_order_relaxed);
    while (used_size + size <= _options._memory_budget)
    {
        if (_reserved_size.compare_exchange_weak(used_size, used_size + size, std::memory_order_acq_rel,
                                                  std::memory_order_relaxed))
            return true;
    }
    _is_truncated.store(true, std::memory_order_release);
    return false;
}

} // namespace Ailu::Render::FrameDebugger
