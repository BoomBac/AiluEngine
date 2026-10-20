#pragma once

#include "Framework/Common/Log.h"
#include "Framework/Core/Containers/Vector.h"

#include <chrono>
#include <memory>

namespace Ailu::Editor
{
    struct EditorLogRecord
    {
        u64 _sequence = 0;
        ELogLevel _level = ELogLevel::kInfo;
        ELogModule _module = ELogModule::kGeneral;
        std::wstring _message;
        std::string _file;
        i32 _line = 0;
        std::string _function;
        std::thread::id _thread_id;
        std::string _thread_name;
        std::chrono::system_clock::time_point _timestamp;
    };

    struct EditorLogStats
    {
        u64 _pending_count = 0;
        u64 _pending_bytes = 0;
        u64 _history_count = 0;
        u64 _history_bytes = 0;
        u64 _dropped_pending_count = 0;
        u64 _evicted_history_count = 0;
        u64 _truncated_record_count = 0;
        u64 _revision = 0;
    };

    struct EditorLogServiceState;

    class EditorLogService
    {
    public:
        static constexpr u64 kMaxPendingRecords = 2048;
        static constexpr u64 kMaxPendingBytes = 4u * 1024u * 1024u;
        static constexpr u64 kMaxHistoryRecords = 10000;
        static constexpr u64 kMaxHistoryBytes = 16u * 1024u * 1024u;

        bool Initialize();
        void BeginShutdown();
        void Finalize();
        u32 DrainPending(u32 max_records, std::chrono::microseconds time_budget);
        u64 Revision() const;
        void GetSnapshot(Vector<EditorLogRecord> &out_records) const;
        void ClearHistory();
        EditorLogStats GetStats() const;

    private:
        std::shared_ptr<EditorLogServiceState> _state;
    };
}
