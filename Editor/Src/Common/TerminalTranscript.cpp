#include "Common/TerminalTranscript.h"

#include <algorithm>

namespace Ailu::Editor
{
    u64 TerminalTranscript::Append(ETerminalLineChannel channel, String text)
    {
        if (text.size() > kMaxBytes)
        {
            _dropped_bytes += static_cast<u64>(text.size() - kMaxBytes);
            text.resize(static_cast<size_t>(kMaxBytes));
        }
        const u64 text_bytes = static_cast<u64>(text.size());
        while (!_lines.empty() && (_lines.size() >= kMaxLines || _bytes > kMaxBytes - text_bytes))
        {
            _dropped_bytes += static_cast<u64>(_lines.front()._text.size());
            ++_dropped_lines;
            _bytes -= static_cast<u64>(_lines.front()._text.size());
            _lines.erase(_lines.begin());
        }
        if (_lines.empty() && text_bytes > kMaxBytes)
            return 0u;
        const u64 id = _next_id++;
        _lines.push_back({id, channel, std::move(text)});
        _bytes += text_bytes;
        ++_revision;
        return id;
    }

    void TerminalTranscript::Clear()
    {
        _lines.clear();
        _bytes = 0u;
        _dropped_bytes = 0u;
        _dropped_lines = 0u;
        ++_revision;
    }
}
