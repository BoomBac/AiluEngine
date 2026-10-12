#include "Framework/Plugin/PluginManager.h"
#include "Framework/Common/Log.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace Ailu;

static void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class LifecycleAppender final : public IAppender
{
public:
    void Print(const LogMessage& message) override
    {
        _messages.push_back(message._message);
        if (_manager && message._message == L"[Plugin] com.ailu.test.lifecycle:tick")
        {
            _unload_rejected = !_manager->UnloadPlugin("com.ailu.test.lifecycle");
            _load_rejected = !_manager->LoadPlugin(_path);
            _manager->Shutdown();
        }
    }
    std::vector<WString> _messages;
    PluginManager* _manager = nullptr;
    WString _path;
    bool _unload_rejected = false;
    bool _load_rejected = false;
};

int main()
{
    LogMgr::Init();
    auto* appender = new LifecycleAppender;
    LogMgr::Get().AddAppender(appender);
    int result = 0;
    try
    {
        wchar_t executable_path[32768]{};
        Require(GetModuleFileNameW(nullptr, executable_path, 32768) != 0, "Executable path");
        const auto root = std::filesystem::path(executable_path).parent_path();
        const auto path = root / L"Plugins/LifecycleProbe/AiluPluginLifecycle.dll";
        const auto second_path = root / L"Plugins/LifecycleProbe/AiluPluginSecond.dll";
        PluginManager manager;
        Require(!manager.LoadPlugin(path.wstring()), "Load before initialization rejected");
        Require(manager.Initialize(), "Initialize");
        std::vector<EPluginProgressState> progress_states;
        manager.SetProgressCallback([&](const char* title, const char* message, u64 completed, EPluginProgressState state)
        {
            Require(title && message, "Progress strings available during callback");
            Require(completed == (state == EPluginProgressState::kBegin ? 0u : 1u), "Progress counter transported");
            progress_states.push_back(state);
            if (state == EPluginProgressState::kRunning) throw std::runtime_error("Callback failure is contained by host");
        });
        Require(manager.LoadPlugin(path.wstring()), "Load");
        Require(manager.LoadedPluginCount() == 1 && manager.IsLoaded("com.ailu.test.lifecycle"), "Lookup/count");
        Require(!manager.LoadPlugin(path.wstring()), "Duplicate same DLL rejected");
        const auto duplicate_path = root / L"Plugins/LifecycleProbe/AiluPluginDuplicate.dll";
        std::filesystem::copy_file(path, duplicate_path, std::filesystem::copy_options::overwrite_existing);
        Require(!manager.LoadPlugin(duplicate_path.wstring()), "Duplicate ID from different DLL rejected");
        Require(GetModuleHandleW(duplicate_path.c_str()) == nullptr, "Duplicate candidate absent");
        std::filesystem::remove(duplicate_path);
        appender->_manager = &manager;
        appender->_path = path.wstring();
        manager.Tick(0.01f);
        appender->_manager = nullptr;
        Require(appender->_unload_rejected && appender->_load_rejected && manager.LoadedPluginCount() == 1,
            "Lifecycle mutation during Tick rejected");
        bool wrong_load = true, wrong_unload = true, wrong_initialize = true;
        std::thread wrong_thread([&]
        {
            wrong_initialize = manager.Initialize();
            wrong_load = manager.LoadPlugin(path.wstring());
            wrong_unload = manager.UnloadPlugin("com.ailu.test.lifecycle");
            manager.Tick(0.01f);
            manager.Shutdown();
        });
        wrong_thread.join();
        Require(!wrong_initialize && !wrong_load && !wrong_unload && manager.LoadedPluginCount() == 1,
            "Wrong-thread calls rejected");
        Require(manager.UnloadPlugin("com.ailu.test.lifecycle"), "Unload");
        Require(GetModuleHandleW(path.c_str()) == nullptr, "No extra reference after same-DLL duplicate");
        Require(progress_states == std::vector<EPluginProgressState>{EPluginProgressState::kBegin,
            EPluginProgressState::kRunning, EPluginProgressState::kCancelled}, "Optional progress ABI and unload termination");
        manager.SetProgressCallback({});
        for (const auto* file : {L"AiluPluginInvalid.dll", L"AiluPluginNullCreate.dll", L"AiluPluginLoadFail.dll",
            L"AiluPluginMissingDescriptor.dll", L"AiluPluginMissingCreate.dll", L"AiluPluginMissingDestroy.dll"})
        {
            const auto failure_path = root / L"Plugins/LifecycleProbe" / file;
            appender->_messages.clear();
            Require(!manager.LoadPlugin(failure_path.wstring()), "Load failure rejected");
            Require(manager.LoadedPluginCount() == 0 && GetModuleHandleW(failure_path.c_str()) == nullptr,
                "Load failure fully rolls back");
            if (std::wstring_view(file) == L"AiluPluginLoadFail.dll")
            {
                std::vector<WString> lifecycle;
                for (const auto& message : appender->_messages)
                    if (message.starts_with(L"[Plugin] com.ailu.test.loadfail:"))
                        lifecycle.push_back(message);
                Require(lifecycle == std::vector<WString>{L"[Plugin] com.ailu.test.loadfail:load",
                    L"[Plugin] com.ailu.test.loadfail:unload", L"[Plugin] com.ailu.test.loadfail:destroy"},
                    "Failed OnLoad pairs OnUnload before Destroy");
            }
        }
        Require(!manager.LoadPlugin((root / L"AiluDynamicLibraryProbe.dll").wstring()), "Missing version rejected");
        Require(GetModuleHandleW(L"AiluDynamicLibraryProbe.dll") == nullptr, "Missing version rollback");
        Require(!manager.LoadPlugin((root / L"missing.dll").wstring()), "Missing DLL rejected");
        appender->_messages.clear();
        Require(manager.LoadPlugin(path.wstring()) && manager.LoadPlugin(second_path.wstring()), "Load two modules");
        manager.Shutdown();
        Require(manager.LoadedPluginCount() == 0 && GetModuleHandleW(path.c_str()) == nullptr
            && GetModuleHandleW(second_path.c_str()) == nullptr, "Shutdown unloads all");
        std::vector<WString> destruction;
        for (const auto& message : appender->_messages)
            if (message.ends_with(L":unload") || message.ends_with(L":destroy"))
                destruction.push_back(message);
        Require(destruction == std::vector<WString>{L"[Plugin] com.ailu.test.second:unload",
            L"[Plugin] com.ailu.test.second:destroy", L"[Plugin] com.ailu.test.lifecycle:unload",
            L"[Plugin] com.ailu.test.lifecycle:destroy"}, "Reverse shutdown with Destroy after OnUnload");
        manager.Shutdown();
        Require(manager.Initialize(), "Reinitialize");
        std::cout << "M2 PluginManager: PASS (rollback, duplicates, affinity, Tick mutation, reverse shutdown)\n";
    }
    catch (const std::exception& error)
    {
        appender->_manager = nullptr;
        std::cerr << "M2 PluginManager: FAIL: " << error.what() << '\n';
        result = 1;
    }
    LogMgr::Shutdown();
    return result;
}
