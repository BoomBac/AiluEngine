#include "Platform/WinProcess.h"

#include "Framework/Common/Log.h"
#include "pch.h"

#if PLATFORM_WINDOWS

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
    #include <vector>

namespace Ailu
{
    static HANDLE ToHandle(void *h) { return reinterpret_cast<HANDLE>(h); }
    static void *ToVoid(HANDLE h) { return reinterpret_cast<void *>(h); }

    static void CloseIfValid(void *&h)
    {
        if (h != nullptr)
        {
            ::CloseHandle(ToHandle(h));
            h = nullptr;
        }
    }

    WinProcess::~WinProcess()
    {
        Close();
    }

    WinProcess::WinProcess(WinProcess &&other) noexcept
        : Process(std::move(other))
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

        HANDLE job = nullptr;
        if (info.UseJobObject)
        {
            job = ::CreateJobObjectW(nullptr, nullptr);
            if (job == nullptr)
            {
                LOG_WARNING("WinProcess: CreateJobObjectW failed (err={})", (u32) ::GetLastError());
            }
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi{};

        DWORD creation_flags = 0;
        if (info.NewConsole)
            creation_flags |= CREATE_NEW_CONSOLE;
        else if (info.CreateNoWindow)
            creation_flags |= CREATE_NO_WINDOW;

        std::vector<wchar_t> cmd(info.CommandLine.begin(), info.CommandLine.end());
        cmd.push_back(L'\0');

        const wchar_t *working_dir = info.WorkingDirectory.empty() ? nullptr : info.WorkingDirectory.c_str();

        const BOOL ok = ::CreateProcessW(
                nullptr,                // lpApplicationName
                cmd.data(),             // lpCommandLine (mutable)
                nullptr,                // lpProcessAttributes
                nullptr,                // lpThreadAttributes
                info.InheritHandles,    // bInheritHandles
                creation_flags,         // dwCreationFlags
                nullptr,                // lpEnvironment
                working_dir,            // lpCurrentDirectory
                &si,                    // lpStartupInfo
                &pi                     // lpProcessInformation
        );

        if (!ok)
        {
            const u32 err = (u32) ::GetLastError();
            LOG_ERROR(L"WinProcess::Start CreateProcessW failed (err={}) cmd='{}'", err, info.CommandLine);
            if (job)
                ::CloseHandle(job);
            return false;
        }

        _process = ToVoid(pi.hProcess);
        _thread = ToVoid(pi.hThread);
        SetPid((u32) pi.dwProcessId);

        if (job)
        {
            if (!::AssignProcessToJobObject(job, pi.hProcess))
            {
                const u32 err = (u32) ::GetLastError();
                // Common failure: ERROR_ACCESS_DENIED when the process is already in a Job.
                LOG_WARNING("WinProcess: AssignProcessToJobObject failed (err={})", err);
                ::CloseHandle(job);
                job = nullptr;
            }
        }

        _job = ToVoid(job);
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

        DWORD code = 0;
        if (!::GetExitCodeProcess(ToHandle(_process), &code))
            return false;

        return code == STILL_ACTIVE;
    }

    bool WinProcess::Wait(u32 timeout_ms) const
    {
        if (_process == nullptr)
            return false;

        const DWORD timeout = (timeout_ms == 0xFFFFFFFFu) ? INFINITE : (DWORD) timeout_ms;
        const DWORD r = ::WaitForSingleObject(ToHandle(_process), timeout);
        return r == WAIT_OBJECT_0;
    }

    bool WinProcess::TryGetExitCode(u32 &out_exit_code) const
    {
        out_exit_code = 0;
        if (_process == nullptr)
            return false;

        DWORD code = 0;
        if (!::GetExitCodeProcess(ToHandle(_process), &code))
            return false;

        if (code == STILL_ACTIVE)
            return false;

        out_exit_code = (u32) code;
        return true;
    }

    bool WinProcess::Terminate(u32 exit_code)
    {
        if (_process == nullptr)
            return false;

        if (_job != nullptr)
        {
            const BOOL ok = ::TerminateJobObject(ToHandle(_job), (UINT) exit_code);
            return ok != FALSE;
        }

        const BOOL ok = ::TerminateProcess(ToHandle(_process), (UINT) exit_code);
        return ok != FALSE;
    }

    void WinProcess::Close()
    {
        CloseIfValid(_thread);
        CloseIfValid(_process);
        CloseIfValid(_job);
        ResetPid();
    }
}

#endif // PLATFORM_WINDOWS
