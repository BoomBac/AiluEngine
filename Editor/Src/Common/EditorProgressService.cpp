#include "Common/EditorProgressService.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>

namespace Ailu::Editor
{
    using ProgressClock = std::chrono::steady_clock;
    struct ProgressTask
    {
        u64 _id = 0;
        String _title;
        String _message;
        bool _can_cancel = false;
        std::atomic<u64> _completed{0}, _total{0};
        std::atomic<EProgressState> _state{EProgressState::kRunning};
        std::atomic<bool> _cancel_requested{false};
        mutable std::mutex _mutex;
        ProgressClock::time_point _finished_at{};
    };
    namespace
    {
        bool IsActive(EProgressState state) { return state == EProgressState::kRunning || state == EProgressState::kCancelling; }
        void RequestCancellation(ProgressTask &task)
        {
            std::lock_guard lock(task._mutex);
            if (IsActive(task._state.load()))
            {
                task._cancel_requested.store(true);
                task._state.store(EProgressState::kCancelling);
            }
        }
    }
    void EditorProgressHandle::SetTotal(u64 total)
    {
        if (!_task) return;
        std::lock_guard lock(_task->_mutex);
        if (IsActive(_task->_state.load())) _task->_total.store(total);
    }
    void EditorProgressHandle::SetCompleted(u64 completed)
    {
        if (!_task) return;
        std::lock_guard lock(_task->_mutex);
        if (IsActive(_task->_state.load())) _task->_completed.store(completed);
    }
    void EditorProgressHandle::Advance(u64 count)
    {
        if (!_task) return;
        std::lock_guard lock(_task->_mutex);
        if (!IsActive(_task->_state.load())) return;
        const u64 completed = _task->_completed.load();
        _task->_completed.store(completed + std::min(count, std::numeric_limits<u64>::max() - completed));
    }
    void EditorProgressHandle::SetMessage(StringView message)
    {
        if (!_task) return;
        std::lock_guard lock(_task->_mutex);
        if (IsActive(_task->_state.load())) _task->_message.assign(message);
    }
    bool EditorProgressHandle::IsCancellationRequested() const { return _task && _task->_cancel_requested.load(); }
    u64 EditorProgressHandle::Id() const { return _task ? _task->_id : 0; }
    void EditorProgressHandle::Finish(EProgressState state, StringView reason)
    {
        if (!_task) return;
        std::lock_guard lock(_task->_mutex);
        if (!IsActive(_task->_state.load())) return;
        if (state == EProgressState::kFailed) _task->_message.assign(reason);
        _task->_finished_at = ProgressClock::now();
        _task->_state.store(state);
    }
    void EditorProgressHandle::Complete() { Finish(EProgressState::kCompleted); }
    void EditorProgressHandle::Fail(StringView reason) { Finish(EProgressState::kFailed, reason); }
    void EditorProgressHandle::Cancel() { Finish(EProgressState::kCancelled); }
    EditorProgressService::~EditorProgressService() { Shutdown(); }
    EditorProgressHandle EditorProgressService::BeginTask(StringView title, u64 total, bool can_cancel)
    {
        std::lock_guard lock(_mutex);
        if (!_accepting) return {};
        auto task = std::make_shared<ProgressTask>();
        task->_id = _next_id++;
        task->_title.assign(title);
        task->_total.store(total);
        task->_can_cancel = can_cancel;
        _tasks.push_back(task);
        return EditorProgressHandle(std::move(task));
    }
    void EditorProgressService::RequestCancel(u64 task_id)
    {
        std::shared_ptr<ProgressTask> task;
        {
            std::lock_guard lock(_mutex);
            for (const auto &candidate : _tasks) if (candidate->_id == task_id) { task = candidate; break; }
        }
        if (task && task->_can_cancel) RequestCancellation(*task);
    }
    void EditorProgressService::DismissTask(u64 task_id)
    {
        std::lock_guard lock(_mutex);
        std::erase_if(_tasks, [task_id](const auto &task) { return task->_id == task_id && !IsActive(task->_state.load()); });
    }
    void EditorProgressService::GetSnapshots(Vector<ProgressSnapshot> &out_snapshots) const
    {
        Vector<std::shared_ptr<ProgressTask>> tasks;
        {
            std::lock_guard lock(_mutex);
            if (_tasks.empty()) { out_snapshots.clear(); return; }
            tasks = _tasks;
        }
        out_snapshots.resize(tasks.size());
        for (size_t index = 0; index < tasks.size(); ++index)
        {
            const auto &task = tasks[index];
            std::lock_guard lock(task->_mutex);
            out_snapshots[index] = {task->_id, task->_title, task->_message, task->_completed.load(), task->_total.load(),
                task->_state.load(), task->_can_cancel};
        }
    }
    void EditorProgressService::Tick(f32 delta_time)
    {
        (void)delta_time;
        const auto now = ProgressClock::now();
        std::lock_guard lock(_mutex);
        std::erase_if(_tasks, [now](const auto &task)
        {
            std::lock_guard task_lock(task->_mutex);
            const auto state = task->_state.load();
            return (state == EProgressState::kCompleted || state == EProgressState::kCancelled) &&
                now - task->_finished_at >= std::chrono::seconds(2);
        });
        constexpr size_t kMaxFailures = 16;
        size_t failure_count = 0;
        for (const auto &task : _tasks) if (task->_state.load() == EProgressState::kFailed) ++failure_count;
        std::erase_if(_tasks, [&failure_count](const auto &task)
        {
            if (failure_count <= kMaxFailures || task->_state.load() != EProgressState::kFailed) return false;
            --failure_count;
            return true;
        });
    }
    void EditorProgressService::Shutdown()
    {
        Vector<std::shared_ptr<ProgressTask>> tasks;
        {
            std::lock_guard lock(_mutex);
            _accepting = false;
            tasks.swap(_tasks);
        }
        for (const auto &task : tasks) RequestCancellation(*task);
    }
}
