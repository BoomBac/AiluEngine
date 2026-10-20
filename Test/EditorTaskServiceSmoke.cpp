#include "Common/EditorTaskService.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using namespace Ailu;
using namespace Ailu::Editor;

namespace
{
    int s_failures = 0;

    void Expect(bool condition, const char *message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++s_failures;
        }
    }

    std::wstring CurrentExecutablePath()
    {
        wchar_t path_buffer[MAX_PATH]{};
        const DWORD length = ::GetModuleFileNameW(nullptr, path_buffer, MAX_PATH);
        return length == 0u ? std::wstring{} : std::wstring(path_buffer, length);
    }

    bool IsTerminal(EEditorTaskState state)
    {
        return state == EEditorTaskState::kSucceeded || state == EEditorTaskState::kFailed ||
               state == EEditorTaskState::kCanceled || state == EEditorTaskState::kLaunchFailed;
    }

    bool ContainsText(const TerminalTranscript *transcript, const String &needle, ETerminalLineChannel channel)
    {
        if (transcript == nullptr)
            return false;
        for (const auto &line : transcript->Lines())
            if (line._channel == channel && line._text.find(needle) != String::npos)
                return true;
        return false;
    }

    EditorTaskStatus WaitForTask(EditorTaskService &service, u64 task_id, u32 timeout_ms = 30000u)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline)
        {
            service.Tick();
            const EditorTaskStatus status = service.GetStatus(task_id);
            if (IsTerminal(status._state))
                return status;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return service.GetStatus(task_id);
    }
}

int main()
{
    const std::filesystem::path smoke_path(CurrentExecutablePath());
    const std::wstring child_path = (smoke_path.parent_path() / L"ProcessPipeChild.exe").wstring();
    EditorTaskService service;

    String parse_error;
    const Vector<WString> parsed = EditorTaskService::ParseWindowsArguments(
        L"\"space path\" \"quote\\\"value\" trailing\\", parse_error);
    Expect(parse_error.empty() && parsed.size() == 3u && parsed[0] == L"space path" &&
               parsed[1] == L"quote\"value" && parsed[2] == L"trailing\\",
           "Windows argument parser handles spaces, quotes and trailing backslashes");
    Expect(EditorTaskService::QuoteWindowsArgument(L"space path") == L"\"space path\"",
           "Windows argument quoting handles spaces");

    EditorTaskRequest direct_request;
    direct_request._label = "Direct child";
    direct_request._executable = child_path;
    direct_request._arguments = {L"--stdout-lines", L"3", L"--stderr-lines", L"2", L"--unicode"};
    direct_request._working_directory = smoke_path.parent_path().wstring();
    String error;
    const u64 direct_id = service.StartTask(direct_request, error);
    const EditorTaskStatus direct_status = WaitForTask(service, direct_id);
    Expect(direct_id != 0u && direct_status._state == EEditorTaskState::kSucceeded &&
               direct_status._has_exit_code && direct_status._exit_code == 0u,
           "direct task succeeds with exit code zero");
    Expect(ContainsText(service.GetTranscript(direct_id), "stdout-line-2", ETerminalLineChannel::kOutput),
           "direct stdout is decoded");
    Expect(ContainsText(service.GetTranscript(direct_id), "stderr-line-1", ETerminalLineChannel::kError),
           "direct stderr is decoded");
    Expect(ContainsText(service.GetTranscript(direct_id), "\xE4\xB8\xAD\xE6\x96\x87", ETerminalLineChannel::kOutput),
           "UTF-8 output is preserved");

    EditorTaskRequest cmd_request;
    cmd_request._mode = EEditorTaskLaunchMode::kCmd;
    cmd_request._label = "cmd one-shot";
    cmd_request._command = L"echo cmd-task-ok";
    cmd_request._working_directory = smoke_path.parent_path().wstring();
    const u64 cmd_id = service.StartTask(cmd_request, error);
    const EditorTaskStatus cmd_status = WaitForTask(service, cmd_id);
    Expect(cmd_id != 0u && cmd_status._state == EEditorTaskState::kSucceeded, "cmd one-shot succeeds");
    Expect(ContainsText(service.GetTranscript(cmd_id), "cmd-task-ok", ETerminalLineChannel::kOutput),
           "cmd output is visible");

    const WString trusted_command = L"cmd.exe /c \"echo trusted-build-parity\"";
    const WString trusted_directory = smoke_path.parent_path().wstring();
    const u64 trusted_id = service.StartTrustedCommandLine(trusted_command, trusted_directory,
                                                            "Build/Package Player", error);
    const EditorTaskStatus trusted_status = WaitForTask(service, trusted_id);
    Expect(trusted_status._state == EEditorTaskState::kSucceeded &&
               trusted_status._command_line == trusted_command && trusted_status._working_directory == trusted_directory,
           "trusted build command preserves command line and working directory");
    Expect(ContainsText(service.GetTranscript(trusted_id), "trusted-build-parity", ETerminalLineChannel::kOutput),
           "trusted build output is visible");

    EditorTaskRequest powershell_request;
    powershell_request._mode = EEditorTaskLaunchMode::kPowerShell;
    powershell_request._label = "PowerShell one-shot";
    powershell_request._command = L"Write-Output ps-task-ok";
    powershell_request._working_directory = smoke_path.parent_path().wstring();
    const u64 powershell_id = service.StartTask(powershell_request, error);
    const EditorTaskStatus powershell_status = WaitForTask(service, powershell_id);
    Expect(powershell_id != 0u && powershell_status._state == EEditorTaskState::kSucceeded,
           "PowerShell one-shot succeeds");
    Expect(ContainsText(service.GetTranscript(powershell_id), "ps-task-ok", ETerminalLineChannel::kOutput),
           "PowerShell output is visible");

    EditorTaskRequest failed_request = direct_request;
    failed_request._label = "nonzero task";
    failed_request._arguments = {L"--exit-code", L"7"};
    const u64 failed_id = service.StartTask(failed_request, error);
    const EditorTaskStatus failed_status = WaitForTask(service, failed_id);
    Expect(failed_status._state == EEditorTaskState::kFailed && failed_status._exit_code == 7u,
           "nonzero exit is reported as failed");

    EditorTaskRequest invalid_directory = direct_request;
    invalid_directory._working_directory = L"Z:\\missing\\editor-task-directory";
    const u64 invalid_id = service.StartTask(invalid_directory, error);
    Expect(service.GetStatus(invalid_id)._state == EEditorTaskState::kLaunchFailed,
           "invalid working directory is rejected");

    EditorTaskRequest large_request = direct_request;
    large_request._label = "bounded output";
    large_request._arguments = {L"--long-line", L"20971520"};
    const u64 large_id = service.StartTask(large_request, error);
    const EditorTaskStatus large_status = WaitForTask(service, large_id, 60000u);
    Expect(large_status._state == EEditorTaskState::kSucceeded, "large output task completes");
    Expect(large_status._dropped_output_bytes > 0u ||
               service.GetTranscript(large_id)->Lines().size() <= TerminalTranscript::kMaxLines,
           "large output remains bounded and reports loss when the process queue drops");
    for (u32 index = 0u; index < 3u; ++index)
    {
        EditorTaskRequest cancel_request = direct_request;
        cancel_request._label = "cancel task";
        cancel_request._arguments = {L"--sleep-ms", L"30000"};
        const u64 cancel_id = service.StartTask(cancel_request, error);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Expect(service.CancelTask(cancel_id), "cancel request is accepted");
        const EditorTaskStatus cancel_status = WaitForTask(service, cancel_id);
        Expect(cancel_status._state == EEditorTaskState::kCanceled, "cancel reaches canceled state");
    }

    for (u32 index = 0u; index < 100u; ++index)
    {
        EditorTaskRequest repeated_request = direct_request;
        repeated_request._label = "repeated task";
        repeated_request._arguments = {L"--stdout-lines", L"1"};
        const u64 repeated_id = service.StartTask(repeated_request, error);
        const EditorTaskStatus repeated_status = WaitForTask(service, repeated_id, 10000u);
        Expect(repeated_status._state == EEditorTaskState::kSucceeded, "repeated task launch succeeds");
    }
    Expect(service.GetTaskIds().size() <= 64u, "task history remains bounded");

    service.BeginShutdown();
    service.Finalize();
    Expect(!service.HasRunningTasks(), "service shutdown leaves no running tasks");

    if (s_failures == 0)
    {
        std::cout << "EditorTaskServiceSmoke: PASS\n";
        return 0;
    }
    std::cerr << "EditorTaskServiceSmoke: " << s_failures << " failure(s)\n";
    return 1;
}
