#include "Platform/WinProcess.h"

#include "Framework/Common/Log.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

using namespace Ailu;

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

    std::wstring Quote(const std::wstring &value)
    {
        return L"\"" + value + L"\"";
    }

    struct ChildResult
    {
        bool _started = false;
        bool _finished = false;
        u32 _exit_code = 0u;
        Vector<ProcessOutputChunk> _chunks;
        ProcessOutputStats _stats;
    };

    ChildResult RunChild(const std::wstring &child_path, const std::wstring &arguments, bool redirect_stdout,
                         bool redirect_stderr, bool drain_while_running = true, u32 timeout_ms = 30000u)
    {
        ChildResult result;
        auto process = ProcessFactory::Create();
        ProcessStartInfo start_info(Quote(child_path) + L" " + arguments);
        start_info.RedirectStdout = redirect_stdout;
        start_info.RedirectStderr = redirect_stderr;
        result._started = process != nullptr && process->Start(start_info);
        if (!result._started)
            return result;

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        const auto append_output = [&](u32 max_chunks)
        {
            Vector<ProcessOutputChunk> batch;
            process->DrainOutput(batch, max_chunks);
            result._chunks.insert(result._chunks.end(), std::make_move_iterator(batch.begin()),
                                  std::make_move_iterator(batch.end()));
        };
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (drain_while_running)
                append_output(256u);
            if (!process->IsRunning() && process->StreamsDrained())
            {
                append_output(0xFFFFFFFFu);
                result._finished = process->WaitForOutputDrain(1000u);
                append_output(0xFFFFFFFFu);
                process->TryGetExitCode(result._exit_code);
                result._stats = process->GetOutputStats();
                process->Close();
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        process->Terminate(99u);
        process->Wait(5000u);
        process->WaitForOutputDrain(5000u);
        process->DrainOutput(result._chunks, 0xFFFFFFFFu);
        result._stats = process->GetOutputStats();
        process->Close();
        return result;
    }

    std::string BytesFor(const ChildResult &result, EProcessOutputStream stream)
    {
        std::string bytes;
        for (const auto &chunk : result._chunks)
        {
            if (chunk._stream == stream)
                bytes += chunk._bytes;
        }
        return bytes;
    }

    bool Contains(const ChildResult &result, EProcessOutputStream stream, const std::string &value)
    {
        return BytesFor(result, stream).find(value) != std::string::npos;
    }
}

int main()
{
    const std::filesystem::path smoke_path(CurrentExecutablePath());
    const std::wstring child_path = (smoke_path.parent_path() / L"ProcessPipeChild.exe").wstring();
    Ailu::LogMgr::Init();

    const ChildResult stdout_only = RunChild(child_path, L"--stdout-lines 4", true, false);
    Expect(stdout_only._started && stdout_only._finished && stdout_only._exit_code == 0u, "stdout-only child completes");
    Expect(Contains(stdout_only, EProcessOutputStream::kStdout, "stdout-line-3"), "stdout channel is captured");
    Expect(BytesFor(stdout_only, EProcessOutputStream::kStderr).empty(), "stderr remains empty when not redirected");

    const ChildResult stderr_only = RunChild(child_path, L"--stderr-lines 4", false, true);
    Expect(stderr_only._started && stderr_only._finished, "stderr-only child completes");
    Expect(Contains(stderr_only, EProcessOutputStream::kStderr, "stderr-line-3"), "stderr channel is captured");
    Expect(BytesFor(stderr_only, EProcessOutputStream::kStdout).empty(), "stdout remains empty when not redirected");

    const ChildResult both = RunChild(child_path, L"--long-line 8388608", true, true, false, 30000u);
    Expect(both._started && both._finished, "large dual-stream child completes without deadlock");
    Expect(both._stats._queued_bytes <= 4u * 1024u * 1024u, "queued output stays bounded");
    Expect(both._stats._dropped_chunk_count > 0u && both._stats._output_truncated,
           "slow consumer reports bounded-output loss");

    const ChildResult exit_seven = RunChild(child_path, L"--stdout-lines 1 --exit-code 7", true, true);
    Expect(exit_seven._started && exit_seven._finished && exit_seven._exit_code == 7u, "exit code is preserved");
    Expect(Contains(exit_seven, EProcessOutputStream::kStdout, "stdout-line-0"), "final stdout output is preserved");

    SECURITY_ATTRIBUTES sentinel_attributes{};
    sentinel_attributes.nLength = sizeof(sentinel_attributes);
    sentinel_attributes.bInheritHandle = TRUE;
    HANDLE sentinel_handle = ::CreateEventW(&sentinel_attributes, FALSE, FALSE, nullptr);
    Expect(sentinel_handle != nullptr, "unrelated inheritable sentinel is created");
    if (sentinel_handle != nullptr)
    {
        const std::wstring sentinel_argument =
            L"--probe-inherited-handle " + std::to_wstring(reinterpret_cast<uintptr_t>(sentinel_handle));
        const ChildResult sentinel = RunChild(child_path, sentinel_argument, true, false);
        Expect(sentinel._started && sentinel._finished, "sentinel probe child completes");
        Expect(Contains(sentinel, EProcessOutputStream::kStdout, "probe-handle-not-inherited"),
               "unrelated inheritable handle is excluded");
        ::CloseHandle(sentinel_handle);
    }

    const ChildResult standard_output_check = RunChild(child_path, L"--check-standard-output", true, false);
    Expect(standard_output_check._started && standard_output_check._finished && standard_output_check._exit_code == 0u,
           ("child receives a writable stdout pipe (exit=" + std::to_string(standard_output_check._exit_code) + ")").c_str());

    auto invalid_process = ProcessFactory::Create();
    ProcessStartInfo invalid_info(L"Z:\\missing\\process-pipe-child.exe");
    invalid_info.RedirectStdout = true;
    Expect(invalid_process != nullptr && !invalid_process->Start(invalid_info), "missing executable fails cleanly");
    ProcessStartInfo invalid_directory(Quote(child_path) + L" --exit-code 0");
    invalid_directory.WorkingDirectory = L"Z:\\missing\\working-directory";
    Expect(!invalid_process->Start(invalid_directory), "missing working directory fails cleanly");
    ProcessStartInfo incompatible(Quote(child_path) + L" --stdout-lines 1");
    incompatible.NewConsole = true;
    incompatible.RedirectStdout = true;
    Expect(!invalid_process->Start(incompatible), "NewConsole with redirected output is rejected");

    auto cancel_process = ProcessFactory::Create();
    ProcessStartInfo cancel_info(Quote(child_path) + L" --spawn-child --sleep-ms 30000");
    cancel_info.RedirectStdout = true;
    cancel_info.RedirectStderr = true;
    Expect(cancel_process != nullptr && cancel_process->Start(cancel_info), "long-running child starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Expect(cancel_process->Terminate(23u), "long-running child terminates");
    Expect(cancel_process->Wait(5000u) && cancel_process->WaitForOutputDrain(5000u),
           "termination and output drain are bounded");
    cancel_process->Close();

    WinProcess first;
    WinProcess second(std::move(first));
    ProcessStartInfo move_before_start(Quote(child_path) + L" --stdout-lines 1");
    move_before_start.RedirectStdout = true;
    Expect(second.Start(move_before_start), "move-before-start session starts");
    WinProcess third;
    third = std::move(second);
    Expect(third.Wait(5000u) && third.WaitForOutputDrain(5000u), "move-after-start session drains");
    third.Close();

    const ChildResult unicode_long = RunChild(child_path, L"--long-line 131072 --unicode", true, true);
    Expect(unicode_long._started && unicode_long._finished, "unicode long-line child completes");
    Expect(Contains(unicode_long, EProcessOutputStream::kStdout, "\xE4\xB8\xAD\xE6\x96\x87"),
           "raw stdout bytes preserve UTF-8 payload");

    for (u32 index = 0u; index < 100u; ++index)
    {
        const ChildResult repeated = RunChild(child_path, L"--stdout-lines 1", true, true, true, 10000u);
        Expect(repeated._started && repeated._finished && repeated._stats._streams_drained,
               "repeated launch drains without accumulating state");
    }

    auto legacy_process = ProcessFactory::Create();
    ProcessStartInfo legacy_info(Quote(child_path) + L" --sleep-ms 1");
    Expect(legacy_process != nullptr && legacy_process->Start(legacy_info), "legacy non-redirected process starts");
    Expect(legacy_process->Wait(5000u), "legacy non-redirected wait remains functional");
    Expect(!legacy_process->GetOutputStats()._supports_capture, "legacy path does not opt into capture");
    legacy_process->Close();

    LogMgr::Shutdown();
    if (s_failures == 0)
    {
        std::cout << "ProcessPipeSmoke: PASS\n";
        return 0;
    }
    std::cerr << "ProcessPipeSmoke: " << s_failures << " failure(s)\n";
    return 1;
}
