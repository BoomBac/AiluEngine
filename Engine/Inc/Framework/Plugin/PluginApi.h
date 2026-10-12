#pragma once
#include "Framework/Core/Types.h"
#include "Framework/Platform/Platform.h"

#if AL_PLATFORM_WINDOWS
    #define AILU_PLUGIN_CALL __cdecl
    #define AILU_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
    #define AILU_PLUGIN_CALL
    #define AILU_PLUGIN_EXPORT extern "C"
#endif

// v1 requires matching C++ compiler ABI, architecture and runtime settings.
// No exceptions, STL values, reflected types or Editor implementation cross this boundary.
namespace Ailu
{
    inline constexpr u32 kPluginApiVersion = 1u;

    enum class EPluginType : u8
    {
        kRuntime,
        kEditor,
    };

    // Strings live in the DLL. The host must copy them before unloading it.
    struct PluginDescriptor
    {
        const char* _id = nullptr;
        const char* _name = nullptr;
        const char* _version = nullptr;
        EPluginType _type = EPluginType::kRuntime;
    };

    class IPluginHost
    {
    public:
        virtual ~IPluginHost() = default;
        virtual void LogInfo(const char* message) noexcept = 0;
        virtual void LogWarning(const char* message) noexcept = 0;
        virtual void LogError(const char* message) noexcept = 0;
        virtual bool IsMainThread() const noexcept = 0;
    };

    // Optional side interface: ABI v1 host/context layouts remain unchanged. Owner-thread only.
    enum class EPluginProgressState : u8 { kBegin, kRunning, kCompleted, kFailed, kCancelled };
    class IPluginProgressHost
    {
    public:
        virtual ~IPluginProgressHost() = default;
        virtual void ReportProgress(const char* title, const char* message, u64 completed, EPluginProgressState state) noexcept = 0;
    };

    struct PluginContext
    {
        u32 _api_version = kPluginApiVersion;
        IPluginHost* _host = nullptr;
    };

    // Instances must be destroyed through AiluPluginDestroy while the DLL is still loaded.
    class IPlugin
    {
    public:
        virtual ~IPlugin() = default;
        virtual bool OnLoad(const PluginContext& context) noexcept = 0;
        virtual void OnUnload() noexcept = 0;
        virtual void Tick(f32 delta_time) noexcept = 0;
    };

    using PluginGetApiVersionFn = u32 (AILU_PLUGIN_CALL*)() noexcept;
    using PluginGetDescriptorFn = const PluginDescriptor* (AILU_PLUGIN_CALL*)() noexcept;
    using PluginCreateFn = IPlugin* (AILU_PLUGIN_CALL*)() noexcept;
    using PluginDestroyFn = void (AILU_PLUGIN_CALL*)(IPlugin*) noexcept;
}

// Hosts discover symbols dynamically and must not acquire DLL export directives.
#if defined(AILU_BUILD_PLUGIN)
AILU_PLUGIN_EXPORT Ailu::u32 AILU_PLUGIN_CALL AiluPluginGetApiVersion() noexcept;
AILU_PLUGIN_EXPORT const Ailu::PluginDescriptor* AILU_PLUGIN_CALL AiluPluginGetDescriptor() noexcept;
AILU_PLUGIN_EXPORT Ailu::IPlugin* AILU_PLUGIN_CALL AiluPluginCreate() noexcept;
AILU_PLUGIN_EXPORT void AILU_PLUGIN_CALL AiluPluginDestroy(Ailu::IPlugin* plugin) noexcept;
#endif
