#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Log.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>

using namespace Ailu;

static LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS *exception)
{
    const auto file = CreateFileW(L"JobGroupLifetimeSmoke.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
    if (file != INVALID_HANDLE_VALUE)
    {
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(file);
    }
    std::cerr << "CRASH code=0x" << std::hex << exception->ExceptionRecord->ExceptionCode
              << " address=" << exception->ExceptionRecord->ExceptionAddress << std::endl;
    return EXCEPTION_EXECUTE_HANDLER;
}

// The gate ensures a worker owns the job before Wait can execute it on the caller.
static bool RunIteration(JobSystem &system, bool guarded)
{
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    std::atomic<u32> completed{0u};
    auto run = [&](JobGroup &group)
    {
        system.Dispatch(group, [&]()
        {
            started.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire))
                YieldProcessor();
            completed.fetch_add(1u, std::memory_order_relaxed);
        });
        auto next_wake = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
        while (!started.load(std::memory_order_acquire))
        {
            // Dispatch can lose a worker wake-up independently of group lifetime.
            // An ungrouped no-op wakes it again without completing the gated job on this thread.
            if (std::chrono::steady_clock::now() >= next_wake)
            {
                system.Dispatch([]() {});
                next_wake = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
            }
            std::this_thread::yield();
        }
        release.store(true, std::memory_order_release);
        system.Wait(group);
    };
    if (guarded)
    {
        // Unmap immediately after destruction to expose late accesses to the group.
        // No scheduler hooks or changes to Engine are used.
        void *storage = VirtualAlloc(nullptr, sizeof(JobGroup), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (storage == nullptr)
            return false;
        auto *group = new (storage) JobGroup;
        run(*group);
        group->~JobGroup();
        if (!VirtualFree(storage, 0, MEM_RELEASE))
            return false;
    }
    else
    {
        JobGroup group;
        run(group);
    }
    // Keep captured gate/counter state alive until job cleanup has completed.
    system.Wait();
    return completed.load(std::memory_order_relaxed) == 1u;
}

int main(int argc, char **argv)
{
    const bool guarded = argc > 1 && std::string_view(argv[1]) == "guarded";
    const u32 iterations = argc > 2 ? static_cast<u32>(std::strtoul(argv[2], nullptr, 10)) : 200000u;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(WriteCrashDump);
    LogMgr::Init();
    std::cout << "mode=" << (guarded ? "guarded" : "stack") << " iterations=" << iterations << std::endl;
    // One worker isolates group completion from unrelated work-stealing concurrency.
    JobSystem::Init(1u);
    bool passed = true;
    for (u32 iteration = 0u; iteration < iterations; ++iteration)
    {
        if (!RunIteration(JobSystem::Get(), guarded))
        {
            passed = false;
            break;
        }
        if ((iteration + 1u) % 10000u == 0u)
            std::cout << "completed=" << iteration + 1u << std::endl;
    }
    JobSystem::Shutdown();
    LogMgr::Shutdown();
    std::cout << (passed ? "PASS (no crash observed; does not disprove the race)" : "FAIL") << std::endl;
    return passed ? 0 : 1;
}
