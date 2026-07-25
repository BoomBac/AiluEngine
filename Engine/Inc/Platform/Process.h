#pragma once
#ifndef __PROCESS_H__
#define __PROCESS_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"

namespace Ailu
{
    struct ProcessStartInfo
    {
        // Full command line passed to platform API. Example (Windows): L"cmd.exe /c dir".
        WString CommandLine;
        WString WorkingDirectory;

        // When true, uses CREATE_NO_WINDOW on Windows (ignored if NewConsole is true).
        bool CreateNoWindow = true;
        bool NewConsole = false;
        bool InheritHandles = false;

        // When true, attempts to attach the process to a Job Object on Windows so Terminate() can kill the whole tree.
        bool UseJobObject = true;

        ProcessStartInfo() = default;
        explicit ProcessStartInfo(const WString &command_line) : CommandLine(command_line) {}
        explicit ProcessStartInfo(const wchar_t *command_line) : CommandLine(command_line ? command_line : L"") {}
    };

    class AILU_API Process
    {
    public:
        Process() = default;
        virtual ~Process() = default;

        Process(Process &&other) noexcept
        {
            _pid = other._pid;
            other._pid = 0;
        }

        Process &operator=(Process &&other) noexcept
        {
            if (this == &other)
                return *this;
            _pid = other._pid;
            other._pid = 0;
            return *this;
        }

        Process(const Process &) = delete;
        Process &operator=(const Process &) = delete;

        virtual bool Start(const ProcessStartInfo &info) = 0;
        virtual bool IsValid() const = 0;
        virtual bool IsRunning() const = 0;

        u32 ProcessId() const { return _pid; }

        // Returns true if the process is signaled within timeout.
        // Use timeout_ms = 0xFFFFFFFFu for infinite wait.
        virtual bool Wait(u32 timeout_ms = 0xFFFFFFFFu) const = 0;

        // Returns true only when the process already exited.
        virtual bool TryGetExitCode(u32 &out_exit_code) const = 0;

        virtual bool Terminate(u32 exit_code = 1) = 0;
        virtual void Close() = 0;

    protected:
        void SetPid(u32 pid) { _pid = pid; }
        void ResetPid() { _pid = 0; }

    private:
        u32 _pid = 0;
    };

    class AILU_API ProcessFactory
    {
    public:
        static Scope<Process> Create();
    };
}

#endif // __PROCESS_H__
