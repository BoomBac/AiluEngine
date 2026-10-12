#include "Widgets/AssetImportController.h"
#include "Framework/Common/Log.h"
#include <iostream>
#include <stdexcept>

using namespace Ailu;
using namespace Ailu::Editor;
namespace
{
    void Check(bool value) { if (!value) throw std::runtime_error("Import queue contract failed"); }
    ProgressSnapshot Snapshot(EditorProgressService &service)
    {
        Vector<ProgressSnapshot> snapshots;
        service.GetSnapshots(snapshots);
        Check(!snapshots.empty());
        return snapshots.back();
    }
}
int main()
{
    LogMgr::Init();
    try
    {
        {
            EditorProgressService service;
            Vector<WString> sources, destinations;
            u32 notifications = 0;
            AssetImportController queue(service, [&](const WString &source, const WString &destination, const ImportSetting &)
            { sources.push_back(source); destinations.push_back(destination); return true; });
            queue.SetOnImported([&]() { ++notifications; });
            queue.QueueFiles({L"first.wav", L"second.wav"}, L"original", {});
            const auto id = Snapshot(service)._id;
            Check(sources.empty());
            queue.Tick();
            Check(sources.empty() && Snapshot(service)._message.find("first.wav") != String::npos);
            queue.Tick();
            Check(sources.size() == 1 && notifications == 1 && Snapshot(service)._completed == 1);
            queue.QueueFiles({L"appended.wav"}, L"changed", {});
            Check(Snapshot(service)._id == id && Snapshot(service)._total == 3);
            queue.Tick(); Check(sources.size() == 1);
            queue.Tick(); Check(sources.size() == 2);
            queue.Tick(); queue.Tick();
            Check(sources.size() == 3 && notifications == 3 && destinations[1] == L"original" && destinations[2] == L"changed");
            Check(Snapshot(service)._state == EProgressState::kCompleted && Snapshot(service)._completed == 3);
        }
        {
            EditorProgressService service;
            u32 calls = 0, notifications = 0;
            AssetImportController queue(service, [&](const WString &source, const WString &, const ImportSetting &)
            {
                ++calls;
                if (source == L"throw.wav") throw std::runtime_error("expected import exception");
                return source == L"good.wav";
            });
            queue.SetOnImported([&]() { ++notifications; });
            queue.QueueFiles({L"null.wav", L"throw.wav", L"good.wav"}, L"target", {});
            for (u32 frame = 0; frame < 6; ++frame) queue.Tick();
            const auto result = Snapshot(service);
            Check(calls == 3 && notifications == 1 && result._state == EProgressState::kFailed && result._completed == 3);
            Check(result._message.find("Success 1 / Skipped 0 / Failed 2") != String::npos);
            queue.QueueFiles({L"good.wav"}, L"target", {});
            queue.Tick(); queue.Tick();
            Check(calls == 4 && notifications == 2 && Snapshot(service)._state == EProgressState::kCompleted);
        }
        {
            EditorProgressService service;
            u32 calls = 0;
            AssetImportController queue(service, [&](const WString &, const WString &, const ImportSetting &) { ++calls; return true; });
            queue.QueueFiles({L"one.wav", L"two.wav", L"three.wav"}, L"target", {});
            queue.Tick(); queue.Tick();
            service.RequestCancel(Snapshot(service)._id);
            queue.Tick(); queue.Tick();
            const auto result = Snapshot(service);
            Check(calls == 1 && result._state == EProgressState::kCancelled && result._completed == 3);
            Check(result._message == "Success 1 / Skipped 2 / Failed 0");
        }
        {
            EditorProgressService service;
            AssetImportController queue(service, [](const WString &, const WString &, const ImportSetting &) -> bool
            { throw std::runtime_error("must not start cancelled file"); });
            queue.QueueFiles({L"queued.wav"}, L"target", {});
            service.RequestCancel(Snapshot(service)._id);
            queue.Tick();
            Check(Snapshot(service)._state == EProgressState::kCancelled && Snapshot(service)._message == "Success 0 / Skipped 1 / Failed 0");
        }
        {
            EditorProgressService service;
            u32 calls = 0, notifications = 0;
            AssetImportController queue(service, [&](const WString &, const WString &, const ImportSetting &)
            {
                ++calls;
                // A request arriving during a synchronous call cannot abort that call.
                service.RequestCancel(Snapshot(service)._id);
                return true;
            });
            queue.SetOnImported([&]() { ++notifications; });
            queue.QueueFiles({L"in_progress.wav", L"pending.wav"}, L"target", {});
            queue.Tick(); queue.Tick();
            Check(calls == 1 && notifications == 1 && Snapshot(service)._completed == 1);
            queue.Tick();
            Check(calls == 1 && Snapshot(service)._state == EProgressState::kCancelled && Snapshot(service)._completed == 2);
        }
        {
            EditorProgressService service;
            { AssetImportController queue(service); queue.QueueFiles({L"pending.wav"}, L"target", {}); }
            Check(Snapshot(service)._state == EProgressState::kCancelled);
            service.Shutdown();
            AssetImportController queue(service);
            queue.QueueFiles({L"rejected.wav"}, L"target", {});
            queue.Tick();
        }
        std::cout << "AssetImportQueueSmoke passed: frame yielding, append, destinations, null/exception recovery, notifications, cancel and teardown\n";
        LogMgr::Shutdown();
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; LogMgr::Shutdown(); return 1; }
}
