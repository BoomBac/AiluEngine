#include "Framework/Plugin/PluginManager.h"
#include "Framework/Platform/DynamicLibrary.h"
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Ailu;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct WorkerDiagnostics
    {
        void Resolve(const std::filesystem::path& path)
        {
            // Borrow the existing handle; do not add a reference that hides unload leaks.
            const auto module = GetModuleHandleW(path.c_str());
            Require(module != nullptr, "Diagnostic module resident");
            _ticks = reinterpret_cast<CounterFn>(GetProcAddress(module, "AiluPluginTestGetTickCount"));
            _workers = reinterpret_cast<CounterFn>(GetProcAddress(module, "AiluPluginTestGetWorkerCount"));
            _running = reinterpret_cast<RunningFn>(GetProcAddress(module, "AiluPluginTestIsWorkerRunning"));
            Require(_ticks && _workers && _running, "Worker diagnostic exports");
        }
        void Verify()
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (_workers() == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            Require(_ticks() > 0 && _workers() > 0 && _running(), "Tick and worker activity");
        }
        void Clear() { _ticks = nullptr; _workers = nullptr; _running = nullptr; }
        using CounterFn = u64 (AILU_PLUGIN_CALL*)() noexcept;
        using RunningFn = bool (AILU_PLUGIN_CALL*)() noexcept;
        CounterFn _ticks = nullptr;
        CounterFn _workers = nullptr;
        RunningFn _running = nullptr;
    };
}

void RunPluginSystemTests(const std::filesystem::path& root)
{
    const auto smoke_path = root / L"Plugins/SmokePlugin/AiluPluginSmoke.dll";
    const auto bad_path = root / L"Plugins/BadVersionPlugin/AiluPluginBadVersion.dll";
    {
        DynamicLibrary library;
        Require(library.Load(smoke_path.wstring()) && library.IsLoaded(), "DynamicLibrary load");
        Require(library.GetSymbol("AiluPluginGetApiVersion") && !library.GetSymbol("MissingSymbol"), "Symbols");
        library.Unload();
        Require(!GetModuleHandleW(smoke_path.c_str()), "DynamicLibrary unload");
    }
    std::cout << "[PASS] DynamicLibrary\n";
    PluginManager manager;
    Require(manager.Initialize() && manager.LoadPlugin(smoke_path.wstring()), "Manager load");
    Require(manager.LoadedPluginCount() == 1 && manager.IsLoaded("com.ailu.test.smoke"), "Manager count/ID");
    std::cout << "[PASS] Plugin ABI\n[PASS] Plugin load\n";
    WorkerDiagnostics diagnostics;
    diagnostics.Resolve(smoke_path);
    for (u32 tick_index = 0; tick_index < 10; ++tick_index)
        manager.Tick(1.0f / 60.0f);
    diagnostics.Verify();
    std::cout << "[PASS] Plugin tick\n[PASS] Plugin threaded worker\n";
    Require(!manager.LoadPlugin(smoke_path.wstring()) && manager.LoadedPluginCount() == 1, "Duplicate rejection");
    diagnostics.Clear();
    Require(manager.UnloadPlugin("com.ailu.test.smoke"), "Unload");
    Require(!GetModuleHandleW(smoke_path.c_str()), "Module absent after unload and duplicate");
    std::cout << "[PASS] Plugin unload\n[PASS] Duplicate plugin rejection\n";
    constexpr u32 kRepeatCount = 100u;
    for (u32 iteration = 0; iteration < kRepeatCount; ++iteration)
    {
        Require(manager.LoadPlugin(smoke_path.wstring()), "Repeated load");
        diagnostics.Resolve(smoke_path);
        for (u32 tick_index = 0; tick_index < 3; ++tick_index)
            manager.Tick(1.0f / 60.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        diagnostics.Verify();
        diagnostics.Clear();
        Require(manager.UnloadPlugin("com.ailu.test.smoke"), "Repeated unload");
        Require(manager.LoadedPluginCount() == 0 && !GetModuleHandleW(smoke_path.c_str()), "No leaked module");
    }
    std::cout << "[PASS] Plugin repeated load/unload x100\n";
    Require(!manager.LoadPlugin(bad_path.wstring()) && manager.LoadedPluginCount() == 0, "Bad version rejected");
    Require(!GetModuleHandleW(bad_path.c_str()), "Bad version module absent");
    std::cout << "[PASS] Bad API version rejection\n";
    Require(manager.LoadPlugin(smoke_path.wstring()), "Load for shutdown");
    diagnostics.Resolve(smoke_path);
    manager.Tick(0.01f);
    diagnostics.Verify();
    diagnostics.Clear();
    manager.Shutdown();
    Require(manager.LoadedPluginCount() == 0 && !GetModuleHandleW(smoke_path.c_str()), "Shutdown unload");
    std::cout << "[PASS] PluginManager shutdown\n10 passed, 0 failed; 0 crashes, 0 hangs, 0 leaked modules\n";
}
