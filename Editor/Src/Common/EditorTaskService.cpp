#include "Common/EditorTaskService.h"

#include "Framework/Common/Log.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

namespace Ailu::Editor
{
    namespace
    {
        constexpr size_t kMaxLineBytes = 16u * 1024u;
        constexpr u32 kMaxTaskHistory = 64u;
        constexpr u32 kShutdownWaitMs = 2000u;
        const String kLineTruncatedMarker = "... [line truncated]";

        bool IsTerminalState(EEditorTaskState state)
        {
            return state == EEditorTaskState::kSucceeded || state == EEditorTaskState::kFailed ||
                   state == EEditorTaskState::kCanceled || state == EEditorTaskState::kLaunchFailed;
        }

        bool IsActiveState(EEditorTaskState state)
        {
            return !IsTerminalState(state);
        }

        String DecodeUtf8(const String &bytes)
        {
            String result;
            result.reserve(bytes.size());
            auto append_replacement = [&result]() { result += "\xEF\xBF\xBD"; };
            for (size_t index = 0u; index < bytes.size();)
            {
                const u8 first = static_cast<u8>(bytes[index]);
                if (first == 0u)
                {
                    append_replacement();
                    ++index;
                    continue;
                }
                u32 length = 0u;
                if (first < 0x80u)
                    length = 1u;
                else if ((first & 0xE0u) == 0xC0u)
                    length = 2u;
                else if ((first & 0xF0u) == 0xE0u)
                    length = 3u;
                else if ((first & 0xF8u) == 0xF0u)
                    length = 4u;
                if (length == 0u || index + length > bytes.size())
                {
                    append_replacement();
                    ++index;
                    continue;
                }
                bool valid = true;
                u32 codepoint = first & (length == 2u ? 0x1Fu : length == 3u ? 0x0Fu : length == 4u ? 0x07u : 0x7Fu);
                for (u32 offset = 1u; offset < length; ++offset)
                {
                    const u8 continuation = static_cast<u8>(bytes[index + offset]);
                    if ((continuation & 0xC0u) != 0x80u)
                    {
                        valid = false;
                        break;
                    }
                    codepoint = (codepoint << 6u) | (continuation & 0x3Fu);
                }
                if (!valid || (length == 2u && codepoint < 0x80u) || (length == 3u && codepoint < 0x800u) ||
                    (length == 4u && codepoint < 0x10000u) || codepoint > 0x10FFFFu ||
                    (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
                {
                    append_replacement();
                    ++index;
                    continue;
                }
                result.append(bytes, index, length);
                index += length;
            }
            return result;
        }

        String StripAnsiSgr(const String &text)
        {
            String result;
            result.reserve(text.size());
            for (size_t index = 0u; index < text.size();)
            {
                if (static_cast<u8>(text[index]) == 0x1Bu && index + 1u < text.size() && text[index + 1u] == '[')
                {
                    size_t end = index + 2u;
                    while (end < text.size() && ((text[end] >= '0' && text[end] <= '9') || text[end] == ';'))
                        ++end;
                    if (end < text.size() && text[end] == 'm')
                    {
                        index = end + 1u;
                        continue;
                    }
                }
                result.push_back(text[index++]);
            }
            return result;
        }

        class LineAssembler
        {
        public:
            template<typename Callback>
            void Consume(const String &bytes, Callback &&callback)
            {
                _bytes += bytes;
                while (true)
                {
                    const size_t delimiter = _bytes.find_first_of("\r\n");
                    if (delimiter == String::npos)
                    {
                        BoundPartialLine();
                        return;
                    }
                    String line = _bytes.substr(0u, delimiter);
                    if (line.size() > kMaxLineBytes)
                        line = line.substr(0u, kMaxLineBytes - kLineTruncatedMarker.size()) + kLineTruncatedMarker;
                    const char delimiter_char = _bytes[delimiter];
                    size_t consumed = delimiter + 1u;
                    if (delimiter_char == '\r' && consumed < _bytes.size() && _bytes[consumed] == '\n')
                        ++consumed;
                    _bytes.erase(0u, consumed);
                    _line_truncated = false;
                    callback(StripAnsiSgr(DecodeUtf8(line)));
                }
            }

            template<typename Callback>
            void Flush(Callback &&callback)
            {
                if (!_bytes.empty())
                {
                    String line = _bytes;
                    if (line.size() > kMaxLineBytes)
                        line = line.substr(0u, kMaxLineBytes - kLineTruncatedMarker.size()) + kLineTruncatedMarker;
                    callback(StripAnsiSgr(DecodeUtf8(line)));
                }
                _bytes.clear();
                _line_truncated = false;
            }

        private:
            void BoundPartialLine()
            {
                if (_bytes.size() <= kMaxLineBytes)
                    return;
                _bytes.resize(kMaxLineBytes - kLineTruncatedMarker.size());
                _bytes += kLineTruncatedMarker;
                _line_truncated = true;
            }

            String _bytes;
            bool _line_truncated = false;
        };

        WString BuildDirectCommandLine(const EditorTaskRequest &request)
        {
            WString command_line = EditorTaskService::QuoteWindowsArgument(request._executable);
            for (const WString &argument : request._arguments)
            {
                command_line += L" ";
                command_line += EditorTaskService::QuoteWindowsArgument(argument);
            }
            return command_line;
        }

        WString BuildShellCommandLine(EEditorTaskLaunchMode mode, const WString &command)
        {
            const WString executable = mode == EEditorTaskLaunchMode::kCmd ? L"cmd.exe" : L"powershell.exe";
            if (mode == EEditorTaskLaunchMode::kCmd)
                return EditorTaskService::QuoteWindowsArgument(executable) + L" /d /s /c " +
                       EditorTaskService::QuoteWindowsArgument(command);
            return EditorTaskService::QuoteWindowsArgument(executable) +
                   L" -NoProfile -NonInteractive -Command " + EditorTaskService::QuoteWindowsArgument(command);
        }

        WString DefaultWorkingDirectory()
        {
            std::error_code error;
            const auto current = std::filesystem::current_path(error);
            return error ? WString{} : current.wstring();
        }
    }

    struct EditorTaskService::TaskRecord
    {
        EditorTaskStatus _status;
        Scope<Process> _process;
        TerminalTranscript _transcript;
        LineAssembler _stdout_decoder;
        LineAssembler _stderr_decoder;
        bool _stdout_flushed = false;
        bool _stderr_flushed = false;
        bool _cancel_requested = false;
        std::chrono::steady_clock::time_point _started_at;
    };

    EditorTaskService::EditorTaskService() = default;

    EditorTaskService::~EditorTaskService()
    {
        BeginShutdown();
        Finalize();
    }

    WString EditorTaskService::QuoteWindowsArgument(const WString &argument)
    {
        WString result;
        result.push_back(L'"');
        size_t backslashes = 0u;
        for (const wchar_t character : argument)
        {
            if (character == L'\\')
            {
                ++backslashes;
                continue;
            }
            if (character == L'"')
            {
                result.append(backslashes * 2u + 1u, L'\\');
                result.push_back(L'"');
                backslashes = 0u;
                continue;
            }
            result.append(backslashes, L'\\');
            backslashes = 0u;
            result.push_back(character);
        }
        result.append(backslashes * 2u, L'\\');
        result.push_back(L'"');
        return result;
    }

    Vector<WString> EditorTaskService::ParseWindowsArguments(const WString &arguments, String &out_error)
    {
        Vector<WString> result;
        size_t index = 0u;
        while (index < arguments.size())
        {
            while (index < arguments.size() && (arguments[index] == L' ' || arguments[index] == L'\t'))
                ++index;
            if (index >= arguments.size())
                break;
            WString argument;
            bool quoted = false;
            while (index < arguments.size())
            {
                size_t slashes = 0u;
                while (index < arguments.size() && arguments[index] == L'\\')
                {
                    ++slashes;
                    ++index;
                }
                if (index < arguments.size() && arguments[index] == L'"')
                {
                    argument.append(slashes / 2u, L'\\');
                    if ((slashes & 1u) != 0u)
                        argument.push_back(L'"');
                    else
                        quoted = !quoted;
                    ++index;
                    continue;
                }
                argument.append(slashes, L'\\');
                if (index >= arguments.size() || (!quoted && (arguments[index] == L' ' || arguments[index] == L'\t')))
                    break;
                argument.push_back(arguments[index++]);
            }
            if (quoted)
            {
                out_error = "unterminated quoted argument";
                return {};
            }
            result.emplace_back(std::move(argument));
            while (index < arguments.size() && (arguments[index] == L' ' || arguments[index] == L'\t'))
                ++index;
        }
        return result;
    }

    u64 EditorTaskService::StartTask(const EditorTaskRequest &request, String &out_error)
    {
        out_error.clear();
        if (!_accepting_tasks)
        {
            out_error = "task service is shutting down";
            return 0u;
        }
        WString command_line;
        if (request._mode == EEditorTaskLaunchMode::kDirect)
        {
            if (request._executable.empty())
            {
                out_error = "executable is empty";
                return 0u;
            }
            command_line = BuildDirectCommandLine(request);
        }
        else
        {
            if (request._command.empty())
            {
                out_error = "command is empty";
                return 0u;
            }
            command_line = BuildShellCommandLine(request._mode, request._command);
        }
        return StartCommandLine(request, command_line,
                                request._working_directory.empty() ? DefaultWorkingDirectory() : request._working_directory,
                                out_error);
    }

    u64 EditorTaskService::StartTrustedCommandLine(const WString &command_line, const WString &working_directory,
                                                    const String &label, String &out_error)
    {
        EditorTaskRequest request;
        request._label = label;
        request._capture_stdout = true;
        request._capture_stderr = true;
        return StartCommandLine(request, command_line,
                                working_directory.empty() ? DefaultWorkingDirectory() : working_directory, out_error);
    }

    u64 EditorTaskService::StartCommandLine(const EditorTaskRequest &request, const WString &command_line,
                                            const WString &working_directory, String &out_error)
    {
        if (!_accepting_tasks)
        {
            out_error = "task service is shutting down";
            return 0u;
        }
        std::error_code filesystem_error;
        if (working_directory.empty() ||
            !std::filesystem::is_directory(std::filesystem::path(working_directory), filesystem_error))
        {
            out_error = "working directory does not exist";
        }
        auto task = MakeScope<TaskRecord>();
        task->_status._task_id = _next_task_id++;
        task->_status._state = EEditorTaskState::kStarting;
        task->_status._label = request._label.empty() ? "External task" : request._label;
        task->_status._command_line = command_line;
        task->_status._working_directory = working_directory;
        task->_started_at = std::chrono::steady_clock::now();
        const u64 task_id = task->_status._task_id;
        _latest_task_id = task_id;
        _tasks.emplace_back(std::move(task));
        ++_task_list_revision;
        TaskRecord &record = *_tasks.back();
        if (!out_error.empty())
        {
            record._status._state = EEditorTaskState::kLaunchFailed;
            record._status._error = out_error;
            AppendDecodedLine(record, ETerminalLineChannel::kError, out_error);
            return task_id;
        }
        if (HasRunningTasks() && std::count_if(_tasks.begin(), _tasks.end(), [](const Scope<TaskRecord> &entry)
                                               { return IsActiveState(entry->_status._state); }) > 2u)
        {
            out_error = "too many running tasks";
            record._status._state = EEditorTaskState::kLaunchFailed;
            record._status._error = out_error;
            AppendDecodedLine(record, ETerminalLineChannel::kError, out_error);
            return task_id;
        }
        ProcessStartInfo process_info(command_line);
        process_info.WorkingDirectory = working_directory;
        process_info.RedirectStdout = request._capture_stdout;
        process_info.RedirectStderr = request._capture_stderr;
        record._process = ProcessFactory::Create();
        if (record._process == nullptr || !record._process->Start(process_info))
        {
            out_error = "failed to start external task";
            record._status._state = EEditorTaskState::kLaunchFailed;
            record._status._error = out_error;
            AppendDecodedLine(record, ETerminalLineChannel::kError, out_error);
            return task_id;
        }
        record._status._state = EEditorTaskState::kRunning;
        ++record._status._revision;
        return task_id;
    }

    EditorTaskService::TaskRecord *EditorTaskService::FindTask(u64 task_id)
    {
        for (const auto &task : _tasks)
            if (task != nullptr && task->_status._task_id == task_id)
                return task.get();
        return nullptr;
    }

    const EditorTaskService::TaskRecord *EditorTaskService::FindTask(u64 task_id) const
    {
        for (const auto &task : _tasks)
            if (task != nullptr && task->_status._task_id == task_id)
                return task.get();
        return nullptr;
    }

    bool EditorTaskService::CancelTask(u64 task_id)
    {
        TaskRecord *task = FindTask(task_id);
        if (task == nullptr || task->_process == nullptr || IsTerminalState(task->_status._state))
            return false;
        task->_cancel_requested = true;
        task->_status._cancel_requested = true;
        task->_status._state = EEditorTaskState::kCanceling;
        ++task->_status._revision;
        return task->_process->Terminate(130u);
    }

    void EditorTaskService::AppendDecodedLine(TaskRecord &task, ETerminalLineChannel channel, const String &line)
    {
        task._transcript.Append(channel, line);
        task._status._transcript_dropped_bytes = task._transcript.DroppedBytes();
        ++task._status._revision;
    }

    void EditorTaskService::DrainTask(TaskRecord &task, u32 max_chunks)
    {
        if (task._process == nullptr)
            return;
        Vector<ProcessOutputChunk> chunks;
        task._process->DrainOutput(chunks, max_chunks);
        for (const ProcessOutputChunk &chunk : chunks)
        {
            task._status._output_bytes += chunk._bytes.size();
            auto callback = [this, &task, stream = chunk._stream](const String &line)
            {
                AppendDecodedLine(task, stream == EProcessOutputStream::kStdout ? ETerminalLineChannel::kOutput
                                                                                : ETerminalLineChannel::kError,
                                  line);
            };
            if (chunk._stream == EProcessOutputStream::kStdout)
                task._stdout_decoder.Consume(chunk._bytes, callback);
            else
                task._stderr_decoder.Consume(chunk._bytes, callback);
        }
        const ProcessOutputStats stats = task._process->GetOutputStats();
        task._status._dropped_output_bytes = stats._dropped_bytes;
    }

    void EditorTaskService::FinalizeTask(TaskRecord &task)
    {
        if (task._process == nullptr)
            return;
        DrainTask(task, 0xFFFFFFFFu);
        if (!task._stdout_flushed)
        {
            task._stdout_decoder.Flush([this, &task](const String &line)
            { AppendDecodedLine(task, ETerminalLineChannel::kOutput, line); });
            task._stdout_flushed = true;
        }
        if (!task._stderr_flushed)
        {
            task._stderr_decoder.Flush([this, &task](const String &line)
            { AppendDecodedLine(task, ETerminalLineChannel::kError, line); });
            task._stderr_flushed = true;
        }
        u32 exit_code = 0u;
        if (task._process->TryGetExitCode(exit_code))
        {
            task._status._exit_code = exit_code;
            task._status._has_exit_code = true;
        }
        if (task._cancel_requested)
            task._status._state = EEditorTaskState::kCanceled;
        else
            task._status._state = task._status._has_exit_code && task._status._exit_code == 0u
                                      ? EEditorTaskState::kSucceeded
                                      : EEditorTaskState::kFailed;
        ++task._status._revision;
        task._process->Close();
        task._process.reset();
        TrimHistory();
    }

    void EditorTaskService::Tick(u32 max_chunks_per_task)
    {
        for (const auto &task_scope : _tasks)
        {
            TaskRecord &task = *task_scope;
            if (task._process == nullptr || IsTerminalState(task._status._state))
                continue;
            DrainTask(task, max_chunks_per_task);
            if (!task._process->IsRunning())
            {
                if (task._status._state != EEditorTaskState::kCanceling)
                    task._status._state = EEditorTaskState::kExitedDraining;
                if (task._process->StreamsDrained() && task._process->WaitForOutputDrain(0u))
                    FinalizeTask(task);
            }
        }
        TrimHistory();
    }

    EditorTaskStatus EditorTaskService::GetStatus(u64 task_id) const
    {
        if (const TaskRecord *task = FindTask(task_id); task != nullptr)
            return task->_status;
        EditorTaskStatus missing;
        missing._task_id = task_id;
        missing._state = EEditorTaskState::kLaunchFailed;
        missing._error = "task not found";
        return missing;
    }

    const TerminalTranscript *EditorTaskService::GetTranscript(u64 task_id) const
    {
        const TaskRecord *task = FindTask(task_id);
        return task == nullptr ? nullptr : &task->_transcript;
    }

    Vector<u64> EditorTaskService::GetTaskIds() const
    {
        Vector<u64> result;
        result.reserve(_tasks.size());
        for (auto it = _tasks.rbegin(); it != _tasks.rend(); ++it)
            result.emplace_back((*it)->_status._task_id);
        return result;
    }

    u64 EditorTaskService::LatestTaskId() const
    {
        return _latest_task_id;
    }

    bool EditorTaskService::HasRunningTasks() const
    {
        return std::any_of(_tasks.begin(), _tasks.end(), [](const Scope<TaskRecord> &task)
                           { return task != nullptr && IsActiveState(task->_status._state); });
    }

    void EditorTaskService::ClearTranscript(u64 task_id)
    {
        if (TaskRecord *task = FindTask(task_id); task != nullptr)
        {
            task->_transcript.Clear();
            ++task->_status._revision;
        }
    }

    void EditorTaskService::TrimHistory()
    {
        while (_tasks.size() > kMaxTaskHistory)
        {
            auto removable = std::find_if(_tasks.begin(), _tasks.end(), [](const Scope<TaskRecord> &task)
                                          { return task != nullptr && IsTerminalState(task->_status._state); });
            if (removable == _tasks.end())
                break;
            _tasks.erase(removable);
            ++_task_list_revision;
        }
    }

    void EditorTaskService::BeginShutdown()
    {
        _accepting_tasks = false;
        for (const auto &task : _tasks)
            if (task != nullptr && IsActiveState(task->_status._state))
                CancelTask(task->_status._task_id);
    }

    void EditorTaskService::Finalize()
    {
        BeginShutdown();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kShutdownWaitMs);
        while (HasRunningTasks() && std::chrono::steady_clock::now() < deadline)
        {
            Tick(0xFFFFFFFFu);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (const auto &task_scope : _tasks)
        {
            TaskRecord &task = *task_scope;
            if (task._process == nullptr)
                continue;
            task._process->Terminate(130u);
            task._process->Wait(250u);
            task._process->WaitForOutputDrain(250u);
            DrainTask(task, 0xFFFFFFFFu);
            task._status._state = EEditorTaskState::kCanceled;
            task._process->Close();
            task._process.reset();
        }
    }
}
