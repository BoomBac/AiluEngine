#pragma once
#ifndef __WIN_PROCESS_H__
#define __WIN_PROCESS_H__

#include "Platform/Process.h"

namespace Ailu
{
    class AILU_API WinProcess : public Process
    {
    public:
        WinProcess() = default;
        ~WinProcess() override;

        WinProcess(WinProcess &&other) noexcept;
        WinProcess &operator=(WinProcess &&other) noexcept;

        WinProcess(const WinProcess &) = delete;
        WinProcess &operator=(const WinProcess &) = delete;

        bool Start(const ProcessStartInfo &info) override;

        bool IsValid() const override;
        bool IsRunning() const override;

        // Returns true if the process is signaled within timeout.
        // Use timeout_ms = 0xFFFFFFFFu for infinite wait.
        bool Wait(u32 timeout_ms = 0xFFFFFFFFu) const override;

        // Returns true only when the process already exited.
        bool TryGetExitCode(u32 &out_exit_code) const override;

        // Terminates the process. If UseJobObject was enabled and succeeded, this kills the whole process tree.
        bool Terminate(u32 exit_code = 1) override;

        // Closes owned handles (does not terminate the process).
        void Close() override;

    private:
        void *_process = nullptr;
        void *_thread = nullptr;
        void *_job = nullptr;
    };
}

#endif // __WIN_PROCESS_H__
