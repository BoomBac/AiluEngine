#pragma once

#include "Common/TerminalTranscript.h"
#include "Framework/Core/CoreMinimal.h"
#include "Platform/Process.h"

namespace Ailu::Editor
{
    enum class EEditorTaskLaunchMode : u8
    {
        kDirect,
        kCmd,
        kPowerShell
    };

    enum class EEditorTaskState : u8
    {
        kQueued,
        kStarting,
        kRunning,
        kExitedDraining,
        kCanceling,
        kSucceeded,
        kFailed,
        kCanceled,
        kLaunchFailed
    };

    struct EditorTaskRequest
    {
        EEditorTaskLaunchMode _mode = EEditorTaskLaunchMode::kDirect;
        WString _executable;
        Vector<WString> _arguments;
        WString _command;
        WString _working_directory;
        String _label;
        bool _capture_stdout = true;
        bool _capture_stderr = true;
    };

    struct EditorTaskStatus
    {
        u64 _task_id = 0u;
        EEditorTaskState _state = EEditorTaskState::kQueued;
        u32 _exit_code = 0u;
        bool _has_exit_code = false;
        bool _cancel_requested = false;
        u64 _revision = 0u;
        u64 _output_bytes = 0u;
        u64 _dropped_output_bytes = 0u;
        u64 _transcript_dropped_bytes = 0u;
        String _label;
        String _error;
        WString _command_line;
        WString _working_directory;
    };

    class EditorTaskService
    {
    public:
        EditorTaskService();
        ~EditorTaskService();

        EditorTaskService(const EditorTaskService &) = delete;
        EditorTaskService &operator=(const EditorTaskService &) = delete;

        u64 StartTask(const EditorTaskRequest &request, String &out_error);
        u64 StartTrustedCommandLine(const WString &command_line, const WString &working_directory,
                                    const String &label, String &out_error);
        bool CancelTask(u64 task_id);
        void Tick(u32 max_chunks_per_task = 512u);

        EditorTaskStatus GetStatus(u64 task_id) const;
        const TerminalTranscript *GetTranscript(u64 task_id) const;
        Vector<u64> GetTaskIds() const;
        u64 TaskListRevision() const { return _task_list_revision; }
        u64 LatestTaskId() const;
        bool HasRunningTasks() const;

        void ClearTranscript(u64 task_id);
        void BeginShutdown();
        void Finalize();

        static WString QuoteWindowsArgument(const WString &argument);
        static Vector<WString> ParseWindowsArguments(const WString &arguments, String &out_error);

    private:
        struct TaskRecord;

        u64 StartCommandLine(const EditorTaskRequest &request, const WString &command_line,
                             const WString &working_directory, String &out_error);
        TaskRecord *FindTask(u64 task_id);
        const TaskRecord *FindTask(u64 task_id) const;
        void DrainTask(TaskRecord &task, u32 max_chunks);
        void FinalizeTask(TaskRecord &task);
        void AppendDecodedLine(TaskRecord &task, ETerminalLineChannel channel, const String &line);
        void TrimHistory();

        Vector<Scope<TaskRecord>> _tasks;
        u64 _next_task_id = 1u;
        u64 _latest_task_id = 0u;
        u64 _task_list_revision = 0u;
        bool _accepting_tasks = true;
    };
}
