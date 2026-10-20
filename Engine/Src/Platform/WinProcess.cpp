#include "Platform/WinProcess.h"

#include "Framework/Common/Log.h"
#include "pch.h"

#if AL_PLATFORM_WINDOWS

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>

    #include <algorithm>
    #include <array>
    #include <atomic>
    #include <chrono>
    #include <condition_variable>
    #include <deque>
    #include <memory>
    #include <mutex>
    #include <system_error>
    #include <thread>
    #include <vector>

namespace Ailu
{
    namespace
    {
        constexpr u64 kMaxQueuedOutputBytes = 4u * 1024u * 1024u;
        constexpr u64 kMaxQueuedOutputChunks = 4096u;
        constexpr u32 kReaderCancelWaitMs = 250u;

        HANDLE ToHandle(void *handle)
        {
            return reinterpret_cast<HANDLE>(handle);
        }

        void *ToVoid(HANDLE handle)
        {
            return reinterpret_cast<void *>(handle);
        }

        bool IsValidHandle(HANDLE handle)
        {
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }

        void CloseIfValid(void *&handle)
        {
            if (handle != nullptr)
            {
                ::CloseHandle(ToHandle(handle));
                handle = nullptr;
            }
        }

        class ScopedHandle
        {
        public:
            ScopedHandle() = default;
            explicit ScopedHandle(HANDLE handle) : _handle(handle) {}
            ~ScopedHandle() { Reset(); }

            ScopedHandle(const ScopedHandle &) = delete;
            ScopedHandle &operator=(const ScopedHandle &) = delete;

            ScopedHandle(ScopedHandle &&other) noexcept : _handle(other.Release()) {}
            ScopedHandle &operator=(ScopedHandle &&other) noexcept
            {
                if (this != &other)
                    Reset(other.Release());
                return *this;
            }

            HANDLE Get() const { return _handle; }
            HANDLE Release()
            {
                HANDLE handle = _handle;
                _handle = nullptr;
                return handle;
            }
            void Reset(HANDLE handle = nullptr)
            {
                if (IsValidHandle(_handle))
                    ::CloseHandle(_handle);
                _handle = handle;
            }

        private:
            HANDLE _handle = nullptr;
        };

        bool DuplicateInheritableHandle(HANDLE source, ScopedHandle &out_handle)
        {
            if (!IsValidHandle(source))
                return false;
            HANDLE duplicate = nullptr;
            if (!::DuplicateHandle(::GetCurrentProcess(), source, ::GetCurrentProcess(), &duplicate, 0, TRUE,
                                   DUPLICATE_SAME_ACCESS))
                return false;
            out_handle.Reset(duplicate);
            return true;
        }

        bool CreateNulHandle(DWORD access, ScopedHandle &out_handle)
        {
            SECURITY_ATTRIBUTES security_attributes{};
            security_attributes.nLength = sizeof(security_attributes);
            security_attributes.bInheritHandle = TRUE;
            HANDLE handle = ::CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE, &security_attributes,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (!IsValidHandle(handle))
                return false;
            out_handle.Reset(handle);
            return true;
        }

        bool PrepareInheritedStandardHandle(DWORD standard_handle, DWORD access, ScopedHandle &out_handle)
        {
            if (DuplicateInheritableHandle(::GetStdHandle(standard_handle), out_handle))
                return true;
            return CreateNulHandle(access, out_handle);
        }

        class ScopedProcThreadAttributeList
        {
        public:
            ScopedProcThreadAttributeList() = default;
            ~ScopedProcThreadAttributeList() { Reset(); }

            ScopedProcThreadAttributeList(const ScopedProcThreadAttributeList &) = delete;
            ScopedProcThreadAttributeList &operator=(const ScopedProcThreadAttributeList &) = delete;

            bool Initialize(const std::vector<HANDLE> &handles)
            {
                if (handles.empty())
                    return false;

                SIZE_T attribute_list_size = 0u;
                ::InitializeProcThreadAttributeList(nullptr, 1u, 0u, &attribute_list_size);
                if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || attribute_list_size == 0u)
                    return false;

                _attribute_list = ::HeapAlloc(::GetProcessHeap(), 0u, attribute_list_size);
                if (_attribute_list == nullptr)
                    return false;
                if (!::InitializeProcThreadAttributeList(
                        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(_attribute_list), 1u, 0u,
                        &attribute_list_size))
                {
                    Reset();
                    return false;
                }
                _initialized = true;

                if (!::UpdateProcThreadAttribute(
                        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(_attribute_list), 0u,
                        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, const_cast<HANDLE *>(handles.data()),
                        handles.size() * sizeof(HANDLE), nullptr, nullptr))
                {
                    Reset();
                    return false;
                }
                return true;
            }

            LPPROC_THREAD_ATTRIBUTE_LIST Get() const
            {
                return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(_attribute_list);
            }

        private:
            void Reset()
            {
                if (_initialized)
                    ::DeleteProcThreadAttributeList(Get());
                _initialized = false;
                if (_attribute_list != nullptr)
                    ::HeapFree(::GetProcessHeap(), 0u, _attribute_list);
                _attribute_list = nullptr;
            }

            void *_attribute_list = nullptr;
            bool _initialized = false;
        };
    }

    struct ProcessSessionState
    {
        mutable std::mutex _mutex;
        std::condition_variable _condition;
        std::deque<ProcessOutputChunk> _chunks;
        std::vector<std::thread> _readers;
        std::atomic<bool> _stop_readers = false;
        std::atomic<u32> _active_readers = 0u;
        bool _stdout_enabled = false;
        bool _stderr_enabled = false;
        bool _stdout_eof = true;
        bool _stderr_eof = true;
        bool _process_exited = false;
        bool _output_truncated = false;
        u64 _queued_bytes = 0u;
        u64 _next_sequence = 1u;
        u64 _dropped_chunk_count = 0u;
        u64 _dropped_bytes = 0u;

        ProcessSessionState(bool stdout_enabled, bool stderr_enabled)
            : _stdout_enabled(stdout_enabled), _stderr_enabled(stderr_enabled), _stdout_eof(!stdout_enabled),
              _stderr_eof(!stderr_enabled)
        {
        }

        void SetProcessExited()
        {
            std::lock_guard lock(_mutex);
            _process_exited = true;
            _condition.notify_all();
        }

        void MarkStreamComplete(EProcessOutputStream stream)
        {
            std::lock_guard lock(_mutex);
            if (stream == EProcessOutputStream::kStdout)
                _stdout_eof = true;
            else
                _stderr_eof = true;
            if (_active_readers > 0u)
                --_active_readers;
            _condition.notify_all();
        }

        void Enqueue(EProcessOutputStream stream, const char *bytes, u32 byte_count)
        {
            if (byte_count == 0u)
                return;
            std::lock_guard lock(_mutex);
            ProcessOutputChunk chunk;
            chunk._sequence = _next_sequence++;
            chunk._stream = stream;
            chunk._bytes.assign(bytes, byte_count);
            while (!_chunks.empty() &&
                   (_chunks.size() >= kMaxQueuedOutputChunks ||
                    _queued_bytes + byte_count > kMaxQueuedOutputBytes))
            {
                _dropped_bytes += _chunks.front()._bytes.size();
                ++_dropped_chunk_count;
                _output_truncated = true;
                _queued_bytes -= _chunks.front()._bytes.size();
                _chunks.pop_front();
            }
            if (byte_count > kMaxQueuedOutputBytes || _queued_bytes + byte_count > kMaxQueuedOutputBytes)
            {
                _dropped_bytes += byte_count;
                ++_dropped_chunk_count;
                _output_truncated = true;
                return;
            }
            _queued_bytes += byte_count;
            _chunks.emplace_back(std::move(chunk));
        }

        void MarkTruncated()
        {
            std::lock_guard lock(_mutex);
            _output_truncated = true;
            _condition.notify_all();
        }

        bool IsDrained() const
        {
            std::lock_guard lock(_mutex);
            return _stdout_eof && _stderr_eof;
        }

        bool WaitForReaders(u32 timeout_ms)
        {
            std::unique_lock lock(_mutex);
            const auto predicate = [this]() { return _active_readers == 0u; };
            if (timeout_ms == 0xFFFFFFFFu)
                _condition.wait(lock, predicate);
            else if (!predicate() && !_condition.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate))
                return false;
            return true;
        }

        void RequestStop()
        {
            _stop_readers.store(true, std::memory_order_release);
            for (auto &reader : _readers)
            {
                if (reader.joinable())
                    ::CancelSynchronousIo(reader.native_handle());
            }
        }

        void JoinReaders()
        {
            for (auto &reader : _readers)
            {
                if (reader.joinable())
                    reader.join();
            }
            _readers.clear();
        }

        void DetachReaders()
        {
            for (auto &reader : _readers)
            {
                if (reader.joinable())
                    reader.detach();
            }
            _readers.clear();
        }

        ProcessOutputStats GetStats() const
        {
            std::lock_guard lock(_mutex);
            ProcessOutputStats stats;
            stats._supports_capture = true;
            stats._process_exited = _process_exited;
            stats._stdout_enabled = _stdout_enabled;
            stats._stderr_enabled = _stderr_enabled;
            stats._stdout_eof = _stdout_eof;
            stats._stderr_eof = _stderr_eof;
            stats._streams_drained = _stdout_eof && _stderr_eof;
            stats._output_truncated = _output_truncated;
            stats._queued_chunk_count = _chunks.size();
            stats._queued_bytes = _queued_bytes;
            stats._dropped_chunk_count = _dropped_chunk_count;
            stats._dropped_bytes = _dropped_bytes;
            return stats;
        }

        void Drain(Vector<ProcessOutputChunk> &out_chunks, u32 max_chunks)
        {
            std::lock_guard lock(_mutex);
            out_chunks.clear();
            const u64 chunk_count = std::min<u64>(_chunks.size(), max_chunks);
            out_chunks.reserve(static_cast<size_t>(chunk_count));
            for (u64 index = 0u; index < chunk_count; ++index)
            {
                _queued_bytes -= _chunks.front()._bytes.size();
                out_chunks.emplace_back(std::move(_chunks.front()));
                _chunks.pop_front();
            }
            _condition.notify_all();
        }
    };

    namespace
    {
        void ReadOutputPipe(const std::shared_ptr<ProcessSessionState> &session, EProcessOutputStream stream,
                            HANDLE read_handle)
        {
            std::array<char, 16u * 1024u> buffer{};
            while (!session->_stop_readers.load(std::memory_order_acquire))
            {
                DWORD bytes_read = 0u;
                const BOOL ok = ::ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                                           nullptr);
                if (!ok || bytes_read == 0u)
                {
                    if (!ok && ::GetLastError() != ERROR_BROKEN_PIPE &&
                        !session->_stop_readers.load(std::memory_order_acquire))
                        session->MarkTruncated();
                    break;
                }
                session->Enqueue(stream, buffer.data(), bytes_read);
            }
            ::CloseHandle(read_handle);
            session->MarkStreamComplete(stream);
        }

        void StartReader(const std::shared_ptr<ProcessSessionState> &session, EProcessOutputStream stream,
                         ScopedHandle &read_handle)
        {
            HANDLE handle = read_handle.Release();
            ++session->_active_readers;
            try
            {
                session->_readers.emplace_back([session, stream, handle]() { ReadOutputPipe(session, stream, handle); });
            }
            catch (const std::system_error &)
            {
                ::CloseHandle(handle);
                session->MarkTruncated();
                session->MarkStreamComplete(stream);
            }
        }

        void UpdateProcessExitState(const std::shared_ptr<ProcessSessionState> &session, HANDLE process)
        {
            DWORD exit_code = STILL_ACTIVE;
            if (IsValidHandle(process) && ::GetExitCodeProcess(process, &exit_code) && exit_code != STILL_ACTIVE)
                session->SetProcessExited();
        }
    }

    WinProcess::~WinProcess()
    {
        Close();
    }

    WinProcess::WinProcess(WinProcess &&other) noexcept
        : Process(std::move(other)), _session(std::move(other._session))
    {
        _process = other._process;
        _thread = other._thread;
        _job = other._job;
        other._process = nullptr;
        other._thread = nullptr;
        other._job = nullptr;
        other.ResetPid();
    }

    WinProcess &WinProcess::operator=(WinProcess &&other) noexcept
    {
        if (this == &other)
            return *this;
        Close();
        Process::operator=(std::move(other));
        _process = other._process;
        _thread = other._thread;
        _job = other._job;
        _session = std::move(other._session);
        other._process = nullptr;
        other._thread = nullptr;
        other._job = nullptr;
        other.ResetPid();
        return *this;
    }

    bool WinProcess::Start(const ProcessStartInfo &info)
    {
        Close();

        if (info.CommandLine.empty())
        {
            LOG_ERROR("WinProcess::Start failed: empty CommandLine");
            return false;
        }
        const bool redirect_stdout = info.RedirectStdout;
        const bool redirect_stderr = info.RedirectStderr;
        const bool redirect_output = redirect_stdout || redirect_stderr;
        if (redirect_output && info.NewConsole)
        {
            LOG_ERROR("WinProcess::Start rejected NewConsole with redirected output");
            return false;
        }

        ScopedHandle job_handle;
        if (info.UseJobObject)
        {
            job_handle.Reset(::CreateJobObjectW(nullptr, nullptr));
            if (!IsValidHandle(job_handle.Get()))
                LOG_WARNING("WinProcess: CreateJobObjectW failed (err={})", (u32) ::GetLastError());
        }

        SECURITY_ATTRIBUTES pipe_attributes{};
        pipe_attributes.nLength = sizeof(pipe_attributes);
        pipe_attributes.bInheritHandle = TRUE;
        ScopedHandle stdout_read;
        ScopedHandle stdout_write;
        ScopedHandle stderr_read;
        ScopedHandle stderr_write;
        HANDLE read_handle = nullptr;
        HANDLE write_handle = nullptr;
        if (redirect_stdout)
        {
            if (!::CreatePipe(&read_handle, &write_handle, &pipe_attributes, 0) ||
                !::SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0))
            {
                if (IsValidHandle(read_handle)) ::CloseHandle(read_handle);
                if (IsValidHandle(write_handle)) ::CloseHandle(write_handle);
                return false;
            }
            stdout_read.Reset(read_handle);
            stdout_write.Reset(write_handle);
        }
        if (redirect_stderr)
        {
            read_handle = nullptr;
            write_handle = nullptr;
            if (!::CreatePipe(&read_handle, &write_handle, &pipe_attributes, 0) ||
                !::SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0))
            {
                if (IsValidHandle(read_handle)) ::CloseHandle(read_handle);
                if (IsValidHandle(write_handle)) ::CloseHandle(write_handle);
                return false;
            }
            stderr_read.Reset(read_handle);
            stderr_write.Reset(write_handle);
        }

        ScopedHandle child_stdin;
        ScopedHandle child_stdout;
        ScopedHandle child_stderr;
        HANDLE child_stdin_handle = nullptr;
        HANDLE child_stdout_handle = nullptr;
        HANDLE child_stderr_handle = nullptr;
        STARTUPINFOEXW startup_info{};
        startup_info.StartupInfo.cb = sizeof(startup_info.StartupInfo);
        std::vector<HANDLE> inherited_handles;
        ScopedProcThreadAttributeList attribute_list;
        if (redirect_output)
        {
            if (!PrepareInheritedStandardHandle(STD_INPUT_HANDLE, GENERIC_READ, child_stdin))
                return false;
            child_stdin_handle = child_stdin.Get();
            if (redirect_stdout)
                child_stdout_handle = stdout_write.Get();
            else
            {
                if (!PrepareInheritedStandardHandle(STD_OUTPUT_HANDLE, GENERIC_WRITE, child_stdout))
                    return false;
                child_stdout_handle = child_stdout.Get();
            }
            if (redirect_stderr)
                child_stderr_handle = stderr_write.Get();
            else
            {
                if (!PrepareInheritedStandardHandle(STD_ERROR_HANDLE, GENERIC_WRITE, child_stderr))
                    return false;
                child_stderr_handle = child_stderr.Get();
            }

            inherited_handles.reserve(3u);
            inherited_handles.emplace_back(child_stdin_handle);
            inherited_handles.emplace_back(child_stdout_handle);
            inherited_handles.emplace_back(child_stderr_handle);
        }

        std::vector<wchar_t> command_line(info.CommandLine.begin(), info.CommandLine.end());
        command_line.push_back(L'\0');
        const wchar_t *working_directory = info.WorkingDirectory.empty() ? nullptr : info.WorkingDirectory.c_str();
        DWORD creation_flags = 0u;
        if (info.NewConsole)
            creation_flags |= CREATE_NEW_CONSOLE;
        else if (info.CreateNoWindow)
            creation_flags |= CREATE_NO_WINDOW;
        if (redirect_output)
        {
            startup_info.StartupInfo.cb = sizeof(startup_info);
            startup_info.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
            startup_info.StartupInfo.hStdInput = child_stdin_handle;
            startup_info.StartupInfo.hStdOutput = child_stdout_handle;
            startup_info.StartupInfo.hStdError = child_stderr_handle;
            if (!attribute_list.Initialize(inherited_handles))
            {
                LOG_ERROR("WinProcess::Start failed to initialize inherited-handle allowlist (err={})",
                          (u32) ::GetLastError());
                return false;
            }
            startup_info.lpAttributeList = attribute_list.Get();
            creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
        }

        PROCESS_INFORMATION process_info{};
        const BOOL inherit_handles = redirect_output || info.InheritHandles;
        const BOOL created = ::CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, inherit_handles,
                                              creation_flags, nullptr, working_directory,
                                              reinterpret_cast<LPSTARTUPINFOW>(&startup_info),
                                              &process_info);
        if (!created)
        {
            LOG_ERROR(L"WinProcess::Start CreateProcessW failed (err={}) cmd='{}'", (u32) ::GetLastError(),
                      info.CommandLine);
            return false;
        }

        stdout_write.Reset();
        stderr_write.Reset();
        child_stdin.Reset();
        child_stdout.Reset();
        child_stderr.Reset();

        _process = ToVoid(process_info.hProcess);
        _thread = ToVoid(process_info.hThread);
        SetPid((u32) process_info.dwProcessId);
        if (job_handle.Get() != nullptr)
        {
            if (!::AssignProcessToJobObject(job_handle.Get(), process_info.hProcess))
            {
                LOG_WARNING("WinProcess: AssignProcessToJobObject failed (err={})", (u32) ::GetLastError());
                job_handle.Reset();
            }
            else
                _job = ToVoid(job_handle.Release());
        }

        if (redirect_output)
        {
            _session = std::make_shared<ProcessSessionState>(redirect_stdout, redirect_stderr);
            if (redirect_stdout)
                StartReader(_session, EProcessOutputStream::kStdout, stdout_read);
            if (redirect_stderr)
                StartReader(_session, EProcessOutputStream::kStderr, stderr_read);
        }
        return true;
    }

    bool WinProcess::IsValid() const
    {
        return _process != nullptr;
    }

    bool WinProcess::IsRunning() const
    {
        if (_process == nullptr)
            return false;
        DWORD exit_code = 0u;
        if (!::GetExitCodeProcess(ToHandle(_process), &exit_code))
            return false;
        if (exit_code != STILL_ACTIVE && _session != nullptr)
            _session->SetProcessExited();
        return exit_code == STILL_ACTIVE;
    }

    bool WinProcess::Wait(u32 timeout_ms) const
    {
        if (_process == nullptr)
            return false;
        const DWORD timeout = timeout_ms == 0xFFFFFFFFu ? INFINITE : static_cast<DWORD>(timeout_ms);
        const DWORD result = ::WaitForSingleObject(ToHandle(_process), timeout);
        if (result == WAIT_OBJECT_0 && _session != nullptr)
            _session->SetProcessExited();
        return result == WAIT_OBJECT_0;
    }

    bool WinProcess::TryGetExitCode(u32 &out_exit_code) const
    {
        out_exit_code = 0u;
        if (_process == nullptr)
            return false;
        DWORD exit_code = 0u;
        if (!::GetExitCodeProcess(ToHandle(_process), &exit_code) || exit_code == STILL_ACTIVE)
            return false;
        out_exit_code = static_cast<u32>(exit_code);
        if (_session != nullptr)
            _session->SetProcessExited();
        return true;
    }

    bool WinProcess::Terminate(u32 exit_code)
    {
        if (_process == nullptr)
            return false;
        BOOL terminated = FALSE;
        if (_job != nullptr)
            terminated = ::TerminateJobObject(ToHandle(_job), static_cast<UINT>(exit_code));
        if (!terminated)
            terminated = ::TerminateProcess(ToHandle(_process), static_cast<UINT>(exit_code));
        return terminated != FALSE;
    }

    bool WinProcess::DrainOutput(Vector<ProcessOutputChunk> &out_chunks, u32 max_chunks)
    {
        if (_session == nullptr)
        {
            out_chunks.clear();
            return false;
        }
        _session->Drain(out_chunks, max_chunks);
        return true;
    }

    ProcessOutputStats WinProcess::GetOutputStats() const
    {
        if (_session == nullptr)
            return {};
        UpdateProcessExitState(_session, ToHandle(_process));
        return _session->GetStats();
    }

    bool WinProcess::StreamsDrained() const
    {
        return _session == nullptr || _session->IsDrained();
    }

    bool WinProcess::WaitForOutputDrain(u32 timeout_ms) const
    {
        if (_session == nullptr)
            return true;
        if (!_session->WaitForReaders(timeout_ms))
            return false;
        _session->JoinReaders();
        return _session->IsDrained();
    }

    void WinProcess::Close()
    {
        if (_session != nullptr)
        {
            if (!_session->WaitForReaders(0u))
            {
                _session->RequestStop();
                if (_session->WaitForReaders(kReaderCancelWaitMs))
                    _session->JoinReaders();
                else
                {
                    _session->MarkTruncated();
                    _session->DetachReaders();
                }
            }
            else
                _session->JoinReaders();
            _session.reset();
        }
        CloseIfValid(_thread);
        CloseIfValid(_process);
        CloseIfValid(_job);
        ResetPid();
    }
}

#endif // AL_PLATFORM_WINDOWS
