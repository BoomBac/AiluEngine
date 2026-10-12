#pragma once
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include <memory>
#include <mutex>

namespace Ailu::Editor
{
    enum class EProgressState : u8 { kRunning, kCancelling, kCompleted, kFailed, kCancelled };
    struct ProgressSnapshot
    {
        u64 _id = 0;
        String _title;
        String _message;
        u64 _completed = 0;
        u64 _total = 0;
        EProgressState _state = EProgressState::kRunning;
        bool _can_cancel = false;
    };
    struct ProgressTask;
    class EditorProgressHandle
    {
    public:
        EditorProgressHandle() = default;
        void SetTotal(u64 total);
        void SetCompleted(u64 completed);
        void Advance(u64 count = 1);
        void SetMessage(StringView message);
        bool IsCancellationRequested() const;
        void Complete();
        void Fail(StringView reason);
        void Cancel();
        u64 Id() const;
    private:
        friend class EditorProgressService;
        explicit EditorProgressHandle(std::shared_ptr<ProgressTask> task) : _task(std::move(task)) {}
        void Finish(EProgressState state, StringView reason = {});
        std::shared_ptr<ProgressTask> _task;
    };
    // State only: producers own execution and must explicitly acknowledge cancellation.
    class EditorProgressService
    {
    public:
        ~EditorProgressService();
        EditorProgressHandle BeginTask(StringView title, u64 total = 0, bool can_cancel = false);
        void RequestCancel(u64 task_id);
        void DismissTask(u64 task_id);
        void GetSnapshots(Vector<ProgressSnapshot> &out_snapshots) const;
        void Tick(f32 delta_time);
        void Shutdown();
    private:
        mutable std::mutex _mutex;
        Vector<std::shared_ptr<ProgressTask>> _tasks;
        u64 _next_id = 1;
        bool _accepting = true;
    };
}
