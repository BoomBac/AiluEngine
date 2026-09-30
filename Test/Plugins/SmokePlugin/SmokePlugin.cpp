#include "Framework/Plugin/PluginApi.h"
#include <new>
#include <atomic>
#include <chrono>
#include <thread>

namespace Ailu
{
    static const PluginDescriptor s_descriptor{
        "com.ailu.test.smoke", "Ailu Smoke Plugin", "0.1.0", EPluginType::kEditor};
    static std::atomic<u64> s_tick_count{0};
    static std::atomic<u64> s_worker_count{0};
    static std::atomic<bool> s_worker_running{false};

    class SmokePlugin final : public IPlugin
    {
    public:
        ~SmokePlugin() override
        {
            if (_worker.joinable())
                std::terminate();
            if (_host)
                _host->LogInfo("SmokePlugin.Destroy (worker joined)");
        }
        bool OnLoad(const PluginContext& context) noexcept override
        {
            if (_host || context._api_version != kPluginApiVersion || !context._host
                || !context._host->IsMainThread())
                return false;
            _host = context._host;
            s_tick_count = 0;
            s_worker_count = 0;
            _stop = false;
            try
            {
                _worker = std::thread([this] { WorkerMain(); });
                _host->LogInfo("SmokePlugin.OnLoad");
                return true;
            }
            catch (...)
            {
                _host->LogError("SmokePlugin worker startup failed");
                return false;
            }
        }
        void OnUnload() noexcept override
        {
            _stop = true;
            if (_worker.joinable())
                _worker.join();
            if (_host)
            {
                _host->LogInfo("SmokePlugin.WorkerJoined");
                _host->LogInfo("SmokePlugin.OnUnload");
            }
        }
        void Tick(f32 delta_time) noexcept override
        {
            if (_host && delta_time >= 0.0f)
                ++s_tick_count;
        }
    private:
        void WorkerMain()
        {
            s_worker_running = true;
            while (!_stop.load())
            {
                ++s_worker_count;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            s_worker_running = false;
        }
        IPluginHost* _host = nullptr;
        std::atomic<bool> _stop{false};
        std::thread _worker;
    };
}

AILU_PLUGIN_EXPORT Ailu::u32 AILU_PLUGIN_CALL AiluPluginGetApiVersion() noexcept
{
    return Ailu::kPluginApiVersion;
}
AILU_PLUGIN_EXPORT const Ailu::PluginDescriptor* AILU_PLUGIN_CALL AiluPluginGetDescriptor() noexcept
{
    return &Ailu::s_descriptor;
}
AILU_PLUGIN_EXPORT Ailu::IPlugin* AILU_PLUGIN_CALL AiluPluginCreate() noexcept
{
    return new (std::nothrow) Ailu::SmokePlugin;
}
AILU_PLUGIN_EXPORT void AILU_PLUGIN_CALL AiluPluginDestroy(Ailu::IPlugin* plugin) noexcept
{
    delete plugin;
}

// Test diagnostics only, not part of Plugin ABI v1. Valid only while this DLL is loaded.
AILU_PLUGIN_EXPORT Ailu::u64 AILU_PLUGIN_CALL AiluPluginTestGetTickCount() noexcept
{
    return Ailu::s_tick_count.load();
}
AILU_PLUGIN_EXPORT Ailu::u64 AILU_PLUGIN_CALL AiluPluginTestGetWorkerCount() noexcept
{
    return Ailu::s_worker_count.load();
}
AILU_PLUGIN_EXPORT bool AILU_PLUGIN_CALL AiluPluginTestIsWorkerRunning() noexcept
{
    return Ailu::s_worker_running.load();
}
