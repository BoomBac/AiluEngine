#include "Common/EditorProgressService.h"
#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace Ailu;
using namespace Ailu::Editor;
namespace
{
    void Check(bool result) { if (!result) throw std::runtime_error("Editor progress contract failed"); }
}
int main()
{
    try
    {
        EditorProgressService service;
        Vector<ProgressSnapshot> snapshots;
        auto handle = service.BeginTask("parallel", 100, true);
        Vector<std::thread> workers;
        for (u32 index = 0; index < 4; ++index) workers.emplace_back([handle]() mutable
        { for (u32 step = 0; step < 10000; ++step) handle.Advance(); });
        for (auto &worker : workers) worker.join();
        service.GetSnapshots(snapshots);
        Check(snapshots.size() == 1 && snapshots[0]._completed == 40000);
        service.RequestCancel(handle.Id());
        service.GetSnapshots(snapshots);
        Check(handle.IsCancellationRequested() && snapshots[0]._state == EProgressState::kCancelling);
        handle.Cancel();
        handle.Complete(); handle.Fail("late failure"); handle.Advance(); handle.SetTotal(10); handle.SetMessage("late message");
        service.GetSnapshots(snapshots);
        Check(snapshots[0]._state == EProgressState::kCancelled && snapshots[0]._completed == 40000 && snapshots[0]._total == 100);
        auto unknown = service.BeginTask("unknown");
        unknown.SetCompleted(std::numeric_limits<u64>::max() - 1);
        unknown.Advance(10);
        service.GetSnapshots(snapshots);
        Check(snapshots.back()._total == 0 && snapshots.back()._completed == std::numeric_limits<u64>::max());
        unknown.Complete();
        for (u32 index = 0; index < 30; ++index) service.BeginTask("failure").Fail("expected reason");
        service.Tick(0);
        service.GetSnapshots(snapshots);
        Check(snapshots.size() == 18 && snapshots.back()._message == "expected reason");
        service.DismissTask(snapshots.back()._id);
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        service.Tick(0);
        service.GetSnapshots(snapshots);
        Check(snapshots.size() == 15);
        for (u32 cycle = 0; cycle < 100; ++cycle)
        {
            EditorProgressService transient;
            auto producer = transient.BeginTask("shutdown race", 0, false);
            std::atomic<bool> started{false};
            std::thread worker([producer, &started]() mutable
            {
                started.store(true);
                for (u32 step = 0; step < 1000; ++step)
                {
                    if (producer.IsCancellationRequested()) { producer.Cancel(); return; }
                    producer.Advance(); producer.SetMessage("working");
                }
                producer.Complete();
            });
            while (!started.load()) std::this_thread::yield();
            transient.GetSnapshots(snapshots);
            transient.Shutdown();
            Check(transient.BeginTask("rejected").Id() == 0);
            worker.join();
        }
        EditorProgressHandle survivor;
        { EditorProgressService transient; survivor = transient.BeginTask("survives service"); }
        Check(survivor.IsCancellationRequested());
        survivor.Cancel(); survivor.Advance();
        EditorProgressHandle empty;
        empty.Complete(); empty.Fail("empty"); empty.Advance();
        std::cout << "EditorProgressSmoke passed: concurrency, terminal states, overflow, retention, shutdown and handle lifetime\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
