#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"

namespace Ailu::Editor
{
    enum class ETerminalLineChannel : u8
    {
        kPrompt,
        kOutput,
        kError,
        kStatus
    };

    struct TerminalTranscriptLine
    {
        u64 _id = 0u;
        ETerminalLineChannel _channel = ETerminalLineChannel::kOutput;
        String _text;
    };

    class TerminalTranscript
    {
    public:
        static constexpr u32 kMaxLines = 5000u;
        static constexpr u64 kMaxBytes = 8u * 1024u * 1024u;

        u64 Append(ETerminalLineChannel channel, String text);
        void Clear();

        const Vector<TerminalTranscriptLine> &Lines() const { return _lines; }
        u64 Revision() const { return _revision; }
        u64 Bytes() const { return _bytes; }
        u64 DroppedBytes() const { return _dropped_bytes; }
        u64 DroppedLines() const { return _dropped_lines; }

    private:
        Vector<TerminalTranscriptLine> _lines;
        u64 _next_id = 1u;
        u64 _bytes = 0u;
        u64 _dropped_bytes = 0u;
        u64 _dropped_lines = 0u;
        u64 _revision = 0u;
    };
}
