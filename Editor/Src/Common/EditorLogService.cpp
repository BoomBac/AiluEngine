#include "Common/EditorLogService.h"

#include <atomic>
#include <deque>
#include <mutex>

namespace Ailu::Editor
{
    namespace
    {
        constexpr wchar_t kTruncationMarker[] = L"... [truncated]";

        u64 EstimateRecordBytes(const EditorLogRecord &record)
        {
            return static_cast<u64>(sizeof(EditorLogRecord)) + record._message.size() * sizeof(wchar_t) +
                   record._file.size() + record._function.size() + record._thread_name.size();
        }

        bool TruncateRecord(EditorLogRecord &record, u64 max_bytes)
        {
            const bool needs_truncation = EstimateRecordBytes(record) > max_bytes;
            if (!needs_truncation)
                return false;

            const u64 fixed_bytes = sizeof(EditorLogRecord) + record._file.size() + record._function.size() +
                                    record._thread_name.size();
            if (fixed_bytes < max_bytes)
            {
                const u64 message_bytes = max_bytes - fixed_bytes;
                const size_t max_message_chars = static_cast<size_t>(message_bytes / sizeof(wchar_t));
                if (record._message.size() > max_message_chars)
                {
                    const size_t marker_size = sizeof(kTruncationMarker) / sizeof(wchar_t) - 1u;
                    if (max_message_chars > marker_size)
                    {
                        record._message.resize(max_message_chars - marker_size);
                        record._message.append(kTruncationMarker);
                    }
                    else
                        record._message.resize(max_message_chars);
                }
            }

            auto trim_string = [](auto &value, u64 &remaining_bytes)
            {
                const u64 value_bytes = static_cast<u64>(value.size() * sizeof(typename std::decay_t<decltype(value)>::value_type));
                if (value_bytes <= remaining_bytes)
                {
                    remaining_bytes -= value_bytes;
                    return false;
                }
                const size_t max_chars = static_cast<size_t>(remaining_bytes / sizeof(typename std::decay_t<decltype(value)>::value_type));
                value.resize(max_chars);
                remaining_bytes = 0;
                return true;
            };

            u64 remaining_bytes = max_bytes > sizeof(EditorLogRecord) ? max_bytes - sizeof(EditorLogRecord) : 0;
            bool truncated = needs_truncation;
            truncated = trim_string(record._message, remaining_bytes) || truncated;
            truncated = trim_string(record._file, remaining_bytes) || truncated;
            truncated = trim_string(record._function, remaining_bytes) || truncated;
            truncated = trim_string(record._thread_name, remaining_bytes) || truncated;
            return truncated;
        }

        void IncrementDropped(EditorLogServiceState &state);
    }

    struct EditorLogServiceState
    {
        mutable std::mutex _mutex;
        std::deque<EditorLogRecord> _pending;
        std::deque<EditorLogRecord> _history;
        u64 _pending_bytes = 0;
        u64 _history_bytes = 0;
        u64 _next_sequence = 1;
        u64 _revision = 0;
        u64 _dropped_pending_count = 0;
        u64 _evicted_history_count = 0;
        u64 _truncated_record_count = 0;
        std::atomic<bool> _active = true;
    };

    namespace
    {
        void IncrementDropped(EditorLogServiceState &state)
        {
            std::lock_guard lock(state._mutex);
            ++state._dropped_pending_count;
        }

        class EditorLogAppender final : public IAppender
        {
        public:
            explicit EditorLogAppender(std::shared_ptr<EditorLogServiceState> state) : _state(std::move(state)) {}

            void Print(const LogMessage &log_msg) override
            {
                if (!_state || !_state->_active.load(std::memory_order_acquire))
                    return;
                try
                {
                    EditorLogRecord record;
                    record._level = log_msg._level;
                    record._module = log_msg._module;
                    record._message = log_msg._message;
                    record._file = log_msg._file;
                    record._line = log_msg._line;
                    record._function = log_msg._function;
                    record._thread_id = log_msg._thread_id;
                    record._thread_name = log_msg._thread_name;
                    record._timestamp = log_msg.Timestamp();
                    const bool truncated = TruncateRecord(record, EditorLogService::kMaxPendingBytes);
                    const u64 record_bytes = EstimateRecordBytes(record);

                    std::lock_guard lock(_state->_mutex);
                    if (!_state->_active.load(std::memory_order_relaxed) ||
                        _state->_pending.size() >= EditorLogService::kMaxPendingRecords ||
                        record_bytes > EditorLogService::kMaxPendingBytes ||
                        _state->_pending_bytes > EditorLogService::kMaxPendingBytes - record_bytes)
                    {
                        ++_state->_dropped_pending_count;
                        return;
                    }
                    record._sequence = _state->_next_sequence++;
                    _state->_pending.emplace_back(std::move(record));
                    _state->_pending_bytes += record_bytes;
                    if (truncated)
                        ++_state->_truncated_record_count;
                }
                catch (...)
                {
                    IncrementDropped(*_state);
                }
            }

        private:
            std::shared_ptr<EditorLogServiceState> _state;
        };
    }

    bool EditorLogService::Initialize()
    {
        if (_state)
            return false;
        auto state = std::make_shared<EditorLogServiceState>();
        EditorLogAppender *appender = nullptr;
        try
        {
            appender = new EditorLogAppender(state);
            LogMgr::Get().AddAppender(appender);
        }
        catch (...)
        {
            delete appender;
            return false;
        }
        _state = std::move(state);
        return true;
    }

    void EditorLogService::BeginShutdown()
    {
        if (_state)
            _state->_active.store(false, std::memory_order_release);
    }

    void EditorLogService::Finalize()
    {
        BeginShutdown();
        _state.reset();
    }

    u32 EditorLogService::DrainPending(u32 max_records, std::chrono::microseconds time_budget)
    {
        if (!_state || max_records == 0u)
            return 0u;

        const auto start_time = std::chrono::steady_clock::now();
        u32 drained_count = 0u;
        while (drained_count < max_records)
        {
            {
                std::lock_guard lock(_state->_mutex);
                if (_state->_pending.empty())
                    break;
                EditorLogRecord record = std::move(_state->_pending.front());
                _state->_pending.pop_front();
                _state->_pending_bytes -= EstimateRecordBytes(record);
                const u64 record_bytes = EstimateRecordBytes(record);
                _state->_history.emplace_back(std::move(record));
                _state->_history_bytes += record_bytes;
                ++_state->_revision;
                while (_state->_history.size() > EditorLogService::kMaxHistoryRecords ||
                       _state->_history_bytes > EditorLogService::kMaxHistoryBytes)
                {
                    _state->_history_bytes -= EstimateRecordBytes(_state->_history.front());
                    _state->_history.pop_front();
                    ++_state->_evicted_history_count;
                }
            }
            ++drained_count;
            if (time_budget.count() > 0 &&
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time) >= time_budget)
                break;
        }
        return drained_count;
    }

    u64 EditorLogService::Revision() const
    {
        return _state ? GetStats()._revision : 0;
    }

    void EditorLogService::GetSnapshot(Vector<EditorLogRecord> &out_records) const
    {
        out_records.clear();
        if (!_state)
            return;
        std::lock_guard lock(_state->_mutex);
        out_records.assign(_state->_history.begin(), _state->_history.end());
    }

    void EditorLogService::ClearHistory()
    {
        if (!_state)
            return;
        std::lock_guard lock(_state->_mutex);
        _state->_pending.clear();
        _state->_history.clear();
        _state->_pending_bytes = 0;
        _state->_history_bytes = 0;
        ++_state->_revision;
    }

    EditorLogStats EditorLogService::GetStats() const
    {
        if (!_state)
            return {};
        std::lock_guard lock(_state->_mutex);
        return {
            static_cast<u64>(_state->_pending.size()),
            _state->_pending_bytes,
            static_cast<u64>(_state->_history.size()),
            _state->_history_bytes,
            _state->_dropped_pending_count,
            _state->_evicted_history_count,
            _state->_truncated_record_count,
            _state->_revision};
    }
}
