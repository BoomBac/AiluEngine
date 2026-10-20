#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"

namespace Ailu
{
    enum class EProcessOutputStream : u8
    {
        kStdout,
        kStderr
    };

    struct ProcessOutputChunk
    {
        u64 _sequence = 0u;
        EProcessOutputStream _stream = EProcessOutputStream::kStdout;
        String _bytes;
    };

    struct ProcessOutputStats
    {
        bool _supports_capture = false;
        bool _process_exited = false;
        bool _stdout_enabled = false;
        bool _stderr_enabled = false;
        bool _stdout_eof = true;
        bool _stderr_eof = true;
        bool _streams_drained = true;
        bool _output_truncated = false;
        u64 _queued_chunk_count = 0u;
        u64 _queued_bytes = 0u;
        u64 _dropped_chunk_count = 0u;
        u64 _dropped_bytes = 0u;
    };
}
