#include "Framework/Common/Log.h"
#include "Framework/Math/Vector.hpp"
#include "Render/GraphicsPipelineStateObject.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    int s_failures = 0;

    class CountingAppender final : public Ailu::IAppender
    {
    public:
        void Print(const Ailu::LogMessage &) override { ++_count; }
        int Count() const { return _count.load(); }

    private:
        std::atomic<int> _count = 0;
    };

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
    using namespace Ailu::Math;

    Expect(format_str("0x{:08X}", 0x80070005u) == "0x80070005", "hex format");
    Expect(format_str("{:.2f}", 1.234f) == "1.23", "float precision format");
    Expect(format_str("{:<10}", "ok") == "ok        ", "alignment format");
    Expect(format_str("{{{}}}", 7) == "{7}", "escaped braces format");
    Expect(format_str(L"0x{:016X}", 0x80070005ull) == L"0x0000000080070005", "wide hex format");
    Expect(format_str(L"{:.2f}", 1.234f) == L"1.23", "wide float precision format");
    Expect(format_str(L"{{{}}}", 7) == L"{7}", "wide escaped braces format");
    Expect(format_str("vector={}", Vector3f(1.0f, 2.0f, 3.0f)) == "vector=1.00,2.00,3.00", "vector formatter");
    bool invalid_format_threw = false;
    try
    {
        static_cast<void>(format_str("{:invalid}", 1));
    }
    catch (const std::format_error &)
    {
        invalid_format_threw = true;
    }
    Expect(invalid_format_threw, "invalid format reports format_error");

    LogMgr::Init();
    LogMgr::Get().SetOutputLevel(ELogLevel::kInfo);
    std::atomic<int> evaluated = 0;
    LOG_DEBUG("filtered {}", ++evaluated);
    Expect(evaluated.load() == 0, "filtered macro arguments are not evaluated");
    LogMgr::Get().SetOutputLevel(ELogLevel::kDebug);
    LOG_DEBUG("enabled {}", ++evaluated);
    Expect(evaluated.load() == 1, "enabled debug macro evaluates arguments");
    LogMgr::Get().SetOutputLevel(ELogLevel::kInfo);
    LogMgr::Get().SetModuleDiagnostics(ELogModule::kShader, true);
    LOG_MODULE_DEBUG(ELogModule::kShader, "shader diagnostics {}", ++evaluated);
    Expect(evaluated.load() == 2, "module diagnostics enable only selected debug output");
    LogMgr::Get().SetModuleDiagnostics(ELogModule::kShader, false);
    LOG_MODULE_DEBUG(ELogModule::kShader, "filtered shader diagnostics {}", ++evaluated);
    Expect(evaluated.load() == 2, "module diagnostics can be disabled");
    LOG_INFO("invalid format {:invalid}", 1);
    {
        Ailu::Render::CommandRecordingContext pso_context;
        Expect(pso_context.FindMatchPSO() == nullptr, "invalid PSO request is recoverable");
    }
    LogMgr::Shutdown();

    {
        std::ofstream clear_file(FileAppender::s_out_path, std::ios::out | std::ios::trunc);
        Expect(clear_file.is_open(), "open log file for concurrency test");
    }

    LogMgr logger("LogSmoke", ELogLevel::kTrace);
    logger.AddAppender(new FileAppender);
    constexpr int kThreadCount = 8;
    constexpr int kMessagesPerThread = 1000;
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);
    for (int thread_index = 0; thread_index < kThreadCount; ++thread_index)
    {
        workers.emplace_back([&logger, thread_index]()
        {
            for (int message_index = 0; message_index < kMessagesPerThread; ++message_index)
            {
                logger.LogMsg(ELogLevel::kInfo, std::source_location::current(), "LogSmoke",
                              "thread={} message={}", thread_index, message_index);
            }
        });
    }
    for (auto &worker : workers) { worker.join(); }
    logger.Finalize();

    std::ifstream log_file(FileAppender::s_out_path);
    Expect(log_file.is_open(), "log file is closed and readable after shutdown");
    std::string line;
    int line_count = 0;
    while (std::getline(log_file, line))
    {
        Expect(line.find("thread=") != std::string::npos && line.find("message=") != std::string::npos,
               "concurrent log line is complete");
        ++line_count;
    }
    Expect(line_count == kThreadCount * kMessagesPerThread, "concurrent log count");

    LogMgr module_logger("ModuleAudit", ELogLevel::kInfo);
    auto *counting_appender = new CountingAppender;
    module_logger.AddAppender(counting_appender);
    constexpr int kModuleMessages = 5000;
    const auto filtered_start = std::chrono::steady_clock::now();
    for (int index = 0; index < kModuleMessages; ++index)
        module_logger.LogMsgModule(ELogModule::kShader, ELogLevel::kDebug, std::source_location::current(),
                                   "ModuleAudit", "filtered={}", index);
    const auto filtered_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - filtered_start).count();
    Expect(counting_appender->Count() == 0, "filtered module messages do not reach appender");
    module_logger.SetModuleDiagnostics(ELogModule::kShader, true);
    const auto enabled_start = std::chrono::steady_clock::now();
    for (int index = 0; index < kModuleMessages; ++index)
        module_logger.LogMsgModule(ELogModule::kShader, ELogLevel::kDebug, std::source_location::current(),
                                   "ModuleAudit", "enabled={}", index);
    const auto enabled_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - enabled_start).count();
    Expect(counting_appender->Count() == kModuleMessages, "enabled module messages reach appender");
    module_logger.Finalize();
    std::cout << "ModuleAudit: filtered_us=" << filtered_ms << " enabled_us=" << enabled_ms
              << " messages=" << kModuleMessages << '\n';

    if (s_failures == 0)
    {
        std::cout << "LogSmoke: PASS\n";
        return 0;
    }
    std::cerr << "LogSmoke: " << s_failures << " failure(s)\n";
    return 1;
}
