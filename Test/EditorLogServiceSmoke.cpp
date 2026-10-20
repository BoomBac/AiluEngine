#include "Common/EditorLogService.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/TimeMgr.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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
}

int main()
{
    using namespace Ailu;
    using namespace Ailu::Editor;

    LogMessage stable_message(ELogLevel::kInfo, L"stable", "EditorLogServiceSmoke.cpp", 42, "main",
                              ELogModule::kEditor);
    const std::string creation_time = stable_message._time_str;
    const auto wait_start = std::chrono::steady_clock::now();
    while (TimeMgr::CurrentTime() == creation_time &&
           std::chrono::steady_clock::now() - wait_start < std::chrono::seconds(3))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Expect(stable_message.ToDetailString().find(ToWChar(creation_time)) != std::wstring::npos,
           "detail string retains event timestamp");
    Expect(stable_message._thread_id == std::this_thread::get_id(), "message captures creator thread");
    Expect(stable_message._module == ELogModule::kEditor, "message captures module");

    LogMgr::Init();
    LogMgr::Get().SetOutputLevel(ELogLevel::kInfo);
    LogMgr::Get().SetModuleDiagnostics(ELogModule::kShader, true);
    EditorLogService service;
    Expect(service.Initialize(), "service initializes after LogMgr");

    LOG_INFO("ordinary service record");
    LOG_MODULE_DEBUG(ELogModule::kShader, "module service record");
    service.DrainPending(64u, std::chrono::seconds(1));
    Vector<EditorLogRecord> snapshot;
    service.GetSnapshot(snapshot);
    bool found_general = false;
    bool found_shader = false;
    for (const auto &record : snapshot)
    {
        if (record._message == L"ordinary service record")
            found_general = record._module == ELogModule::kGeneral;
        if (record._message == L"module service record")
            found_shader = record._module == ELogModule::kShader;
    }
    Expect(found_general, "ordinary log uses general module");
    Expect(found_shader, "module log preserves module");

    std::atomic<bool> producers_done = false;
    const auto stress_start = std::chrono::steady_clock::now();
    std::thread consumer([&]
    {
        while (!producers_done.load(std::memory_order_acquire))
        {
            if (service.DrainPending(128u, std::chrono::milliseconds(2)) == 0u)
                std::this_thread::yield();
        }
        while (service.DrainPending(1024u, {}) != 0u) {}
    });

    constexpr int kThreadCount = 8;
    constexpr int kMessagesPerThread = 10000;
    std::vector<std::thread> producers;
    producers.reserve(kThreadCount);
    for (int thread_index = 0; thread_index < kThreadCount; ++thread_index)
    {
        producers.emplace_back([thread_index]
        {
            for (int message_index = 0; message_index < kMessagesPerThread; ++message_index)
                LogMgr::Get().LogMsg(ELogLevel::kInfo, std::source_location::current(), "EditorLogServiceSmoke",
                                     "thread={} message={}", thread_index, message_index);
        });
    }
    for (auto &producer : producers)
        producer.join();
    producers_done.store(true, std::memory_order_release);
    consumer.join();
    const auto stress_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - stress_start).count();

    for (u64 index = 0u; index < EditorLogService::kMaxPendingRecords + 100u; ++index)
        LOG_INFO("pending burst {}", index);
    while (service.DrainPending(1024u, {}) != 0u) {}
    const std::string long_message(5u * 1024u * 1024u, 'x');
    LogMgr::Get().LogMsg(ELogLevel::kInfo, std::source_location::current(), "EditorLogServiceSmoke", "{}", long_message);
    while (service.DrainPending(1024u, {}) != 0u) {}
    const EditorLogStats stats = service.GetStats();
    service.GetSnapshot(snapshot);
    std::cout << "stress: elapsed_ms=" << stress_elapsed_ms << " history=" << stats._history_count
              << " pending=" << stats._pending_count << " dropped=" << stats._dropped_pending_count
              << " evicted=" << stats._evicted_history_count << " truncated=" << stats._truncated_record_count << '\n';
    Expect(stats._pending_count <= EditorLogService::kMaxPendingRecords, "pending count is bounded");
    Expect(stats._pending_bytes <= EditorLogService::kMaxPendingBytes, "pending bytes are bounded");
    Expect(stats._history_count <= EditorLogService::kMaxHistoryRecords, "history count is bounded");
    Expect(stats._history_bytes <= EditorLogService::kMaxHistoryBytes, "history bytes are bounded");
    Expect(stats._dropped_pending_count > 0u || stats._evicted_history_count > 0u,
           "overload telemetry records a drop or eviction");
    Expect(stats._truncated_record_count > 0u, "oversized record telemetry records truncation");
    for (size_t index = 1u; index < snapshot.size(); ++index)
        Expect(snapshot[index - 1u]._sequence < snapshot[index]._sequence, "retained sequence is increasing");

    service.ClearHistory();
    service.GetSnapshot(snapshot);
    Expect(snapshot.empty(), "clear history removes retained and pending records");
    LOG_INFO("record after clear history");
    while (service.DrainPending(1024u, {}) != 0u) {}
    service.GetSnapshot(snapshot);
    Expect(!snapshot.empty() && snapshot.back()._message == L"record after clear history",
           "clear history keeps future logging active");

    service.BeginShutdown();
    const size_t snapshot_size_before_late_log = snapshot.size();
    LOG_INFO("late producer after service shutdown");
    service.DrainPending(1024u, {});
    service.GetSnapshot(snapshot);
    Expect(snapshot.size() == snapshot_size_before_late_log, "late producer is rejected after shutdown");
    service.Finalize();
    LogMgr::Shutdown();

    if (s_failures == 0)
    {
        std::cout << "EditorLogServiceSmoke: PASS\n";
        return 0;
    }
    std::cerr << "EditorLogServiceSmoke: " << s_failures << " failure(s)\n";
    return 1;
}
