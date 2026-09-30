#include "Framework/Plugin/PluginApi.h"
#include "Framework/Platform/DynamicLibrary.h"
#include "Framework/Common/Log.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace Ailu;
void RunPluginSystemTests(const std::filesystem::path& root);

static void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class SmokeHost final : public IPluginHost
{
public:
    void LogInfo(const char* message) noexcept override
    {
        if (std::string_view(message) == "SmokePlugin.OnLoad")
            ++_load_count;
        if (std::string_view(message) == "SmokePlugin.OnUnload")
            ++_unload_count;
    }
    void LogWarning(const char*) noexcept override { ++_warning_count; }
    void LogError(const char*) noexcept override { ++_error_count; }
    bool IsMainThread() const noexcept override { return std::this_thread::get_id() == _owner_thread; }

    u32 _load_count = 0;
    u32 _unload_count = 0;
    u32 _warning_count = 0;
    u32 _error_count = 0;
private:
    std::thread::id _owner_thread = std::this_thread::get_id();
};

// Test-only ownership guard; not a PluginModule/PluginManager implementation.
struct AbiTestScope
{
    ~AbiTestScope()
    {
        if (_instance)
        {
            if (_loaded)
                _instance->OnUnload();
            _destroy(_instance);
        }
        _instance = nullptr;
        _descriptor = nullptr;
        _get_version = nullptr;
        _get_descriptor = nullptr;
        _create = nullptr;
        _destroy = nullptr;
        _library.Unload();
    }

    DynamicLibrary _library;
    PluginGetApiVersionFn _get_version = nullptr;
    PluginGetDescriptorFn _get_descriptor = nullptr;
    PluginCreateFn _create = nullptr;
    PluginDestroyFn _destroy = nullptr;
    const PluginDescriptor* _descriptor = nullptr;
    IPlugin* _instance = nullptr;
    bool _loaded = false;
};

int main()
{
    LogMgr::Init();
    int result = 0;
    try
    {
        wchar_t executable_path[32768]{};
        Require(GetModuleFileNameW(nullptr, executable_path, 32768) != 0, "Executable path");
        const auto path = std::filesystem::path(executable_path).parent_path()
            / L"Plugins/SmokePlugin/AiluPluginSmoke.dll";
        Require(GetModuleHandleW(path.c_str()) == nullptr, "Module initially absent");
        SmokeHost host;
        std::string copied_id, copied_name, copied_version;
        {
            AbiTestScope scope;
            Require(scope._library.Load(path.wstring()), "Load plugin DLL");
            scope._get_version = reinterpret_cast<PluginGetApiVersionFn>(
                scope._library.GetSymbol("AiluPluginGetApiVersion"));
            Require(scope._get_version != nullptr, "API version export");
            // No descriptor or instance exports are accessed until this gate succeeds.
            Require(scope._get_version() == kPluginApiVersion, "API version gate");
            scope._get_descriptor = reinterpret_cast<PluginGetDescriptorFn>(
                scope._library.GetSymbol("AiluPluginGetDescriptor"));
            scope._create = reinterpret_cast<PluginCreateFn>(scope._library.GetSymbol("AiluPluginCreate"));
            scope._destroy = reinterpret_cast<PluginDestroyFn>(scope._library.GetSymbol("AiluPluginDestroy"));
            Require(scope._get_descriptor && scope._create && scope._destroy, "Descriptor/Create/Destroy exports");
            scope._descriptor = scope._get_descriptor();
            Require(scope._descriptor && scope._descriptor->_id && scope._descriptor->_name
                && scope._descriptor->_version, "Descriptor strings present");
            copied_id = scope._descriptor->_id;
            copied_name = scope._descriptor->_name;
            copied_version = scope._descriptor->_version;
            Require(copied_id == "com.ailu.test.smoke" && copied_name == "Ailu Smoke Plugin"
                && copied_version == "0.1.0", "Descriptor values");
            Require(scope._descriptor->_type == EPluginType::kEditor, "Descriptor type");
            scope._instance = scope._create();
            Require(scope._instance != nullptr, "Create non-null");
            Require(!scope._instance->OnLoad({kPluginApiVersion + 1, &host}), "Reject incompatible context");
            Require(!scope._instance->OnLoad({kPluginApiVersion, nullptr}), "Reject missing host");
            scope._loaded = scope._instance->OnLoad({kPluginApiVersion, &host});
            Require(scope._loaded, "OnLoad");
            scope._instance->Tick(1.0f / 60.0f);
        }
        Require(host._load_count == 1 && host._unload_count == 1, "Lifecycle callbacks");
        Require(host._warning_count == 0 && host._error_count == 0, "No plugin errors");
        Require(GetModuleHandleW(path.c_str()) == nullptr, "Module absent after Destroy/Unload");
        Require(copied_id == "com.ailu.test.smoke" && !copied_name.empty() && copied_version == "0.1.0",
            "Host descriptor copies survive unload");
        std::cout << "M1 Plugin ABI: PASS\n";
        RunPluginSystemTests(std::filesystem::path(executable_path).parent_path());
    }
    catch (const std::exception& error)
    {
        std::cerr << "M1 Plugin ABI: FAIL: " << error.what() << '\n';
        result = 1;
    }
    LogMgr::Shutdown();
    return result;
}
