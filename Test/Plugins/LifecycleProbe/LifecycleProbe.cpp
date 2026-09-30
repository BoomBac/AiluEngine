#include "Framework/Plugin/PluginApi.h"
#include <new>

#ifndef TEST_PLUGIN_MODE
    #define TEST_PLUGIN_MODE 0
#endif
#ifndef TEST_PLUGIN_ID
    #define TEST_PLUGIN_ID "com.ailu.test.lifecycle"
#endif

namespace Ailu
{
    static const PluginDescriptor s_descriptor{TEST_PLUGIN_ID, "Lifecycle probe", "0.1.0", EPluginType::kRuntime};
    class LifecycleProbe final : public IPlugin
    {
    public:
        ~LifecycleProbe() override { if (_host) _host->LogInfo(TEST_PLUGIN_ID ":destroy"); }
        bool OnLoad(const PluginContext& context) noexcept override
        {
            _host = context._host;
            _host->LogInfo(TEST_PLUGIN_ID ":load");
            return TEST_PLUGIN_MODE != 3;
        }
        void OnUnload() noexcept override { _host->LogInfo(TEST_PLUGIN_ID ":unload"); }
        void Tick(f32) noexcept override { _host->LogInfo(TEST_PLUGIN_ID ":tick"); }
    private:
        IPluginHost* _host = nullptr;
    };
}

AILU_PLUGIN_EXPORT Ailu::u32 AILU_PLUGIN_CALL AiluPluginGetApiVersion() noexcept { return Ailu::kPluginApiVersion; }
#if TEST_PLUGIN_MODE != 4
AILU_PLUGIN_EXPORT const Ailu::PluginDescriptor* AILU_PLUGIN_CALL AiluPluginGetDescriptor() noexcept
{
    return TEST_PLUGIN_MODE == 1 ? nullptr : &Ailu::s_descriptor;
}
#endif
#if TEST_PLUGIN_MODE != 5
AILU_PLUGIN_EXPORT Ailu::IPlugin* AILU_PLUGIN_CALL AiluPluginCreate() noexcept
{
    return TEST_PLUGIN_MODE == 2 ? nullptr : new (std::nothrow) Ailu::LifecycleProbe;
}
#endif
#if TEST_PLUGIN_MODE != 6
AILU_PLUGIN_EXPORT void AILU_PLUGIN_CALL AiluPluginDestroy(Ailu::IPlugin* plugin) noexcept { delete plugin; }
#endif
