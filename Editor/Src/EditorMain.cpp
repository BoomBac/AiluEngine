#include "Ailu.h"
#include "EditorApp.h"
#include "Framework/Common/CrashHandler.h"
#include "Framework/Common/Log.h"
#include "Framework/Plugin/PluginManager.h"

#include <shellapi.h>
#include <filesystem>
#include <stdexcept>

#pragma comment(lib, "Shell32.lib")

// #define _DEBUG_MEM_LEAK 1

#ifdef _DEBUG_MEM_LEAK
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include <stdlib.h>
#endif

using namespace Ailu;

namespace
{
    void LogEditorStartupError(const std::wstring &message,
                               const std::source_location &location = std::source_location::current())
    {
        FileAppender file_appender;
        file_appender.Print(LogMessage(ELogLevel::kError, message, location.file_name(), location.line(),
                                       location.function_name()));
    }
}

int WINAPI WinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE previous_instance,
    _In_ PSTR command_line,
    _In_ int command_show)
{
#ifdef _DEBUG_MEM_LEAK
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // _CrtSetBreakAlloc(4627010);
#endif

    pow2::Assert::SetLoggingHandler();
    InstallCrashHandler();
    int argument_count = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr)
    {
        LogEditorStartupError(L"Failed to parse command line.");
        MessageBoxW(nullptr, L"Failed to parse command line.", L"Ailu Editor", MB_OK | MB_ICONERROR);
        return 1;
    }

    ApplicationInitContext context;
    for (int i = 0; i < argument_count; ++i)
        context._arguments.emplace_back(arguments[i]);

    LocalFree(arguments);
    arguments = nullptr;

    if (context._arguments.size() <= 1)
    {
        LogEditorStartupError(L"No project file specified.");
        MessageBoxW(nullptr, L"No project file specified.", L"Ailu Editor", MB_OK | MB_ICONERROR);
        return 1;
    }

    context._project_file_path = context._arguments[1];
    context._require_project = true;

    // Explicit, bounded lifecycle verification; normal discovery/autoload stays in EditorApp.
    WString smoke_plugin_path;
    bool expect_smoke_autoload = false;
    u32 exit_after_frames = 0;
    try
    {
        for (size_t argument_index = 2; argument_index < context._arguments.size(); ++argument_index)
        {
            const auto& argument = context._arguments[argument_index];
            if (argument == L"--plugin-smoke" || argument == L"--plugin-smoke-autoload"
                || argument == L"--exit-after-frames")
            {
                if (++argument_index == context._arguments.size())
                    throw std::invalid_argument("Missing lifecycle test option value");
                const auto& value = context._arguments[argument_index];
                if (argument == L"--plugin-smoke" || argument == L"--plugin-smoke-autoload")
                {
                    smoke_plugin_path = value;
                    expect_smoke_autoload = argument == L"--plugin-smoke-autoload";
                }
                else
                {
                    size_t consumed = 0;
                    const auto count = std::stoul(value, &consumed);
                    if (count == 0 || consumed != value.size() || count > 100000)
                        throw std::invalid_argument("Invalid lifecycle test frame count");
                    exit_after_frames = static_cast<u32>(count);
                }
            }
        }
        if (!smoke_plugin_path.empty())
        {
            smoke_plugin_path = std::filesystem::absolute(smoke_plugin_path).lexically_normal().wstring();
            const auto attributes = GetFileAttributesW(smoke_plugin_path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                throw std::invalid_argument("Smoke DLL path does not exist");
        }
    }
    catch (const std::exception&)
    {
        LogEditorStartupError(L"Invalid plugin-smoke/exit-after-frames options.");
        return 1;
    }

    Editor::EditorApp app;
    if (app.Initialize(context) != 0)
        return 1;

    if (!smoke_plugin_path.empty() && (expect_smoke_autoload
        ? !app.GetPluginManager()->IsLoaded("com.ailu.test.smoke")
        : !app.GetPluginManager()->LoadPlugin(smoke_plugin_path)))
    {
        app.Finalize();
        return 1;
    }
    PluginRegistrationHandle smoke_registration;
    if (!smoke_plugin_path.empty())
    {
        const auto owner = app.GetPluginManager()->GetPluginToken("com.ailu.test.smoke");
        smoke_registration = app.GetPluginManager()->RegisterHostRecord(owner, "Editor lifecycle acceptance");
        LOG_INFO("[Plugin] Editor smoke registration: owner={} handle={}", owner._value, smoke_registration._value);
    }
    Delegate<>::Handle exit_handle = Delegate<>::kInvalidHandle;
    u32 observed_frames = 0;
    if (exit_after_frames != 0)
    {
        exit_handle = app._before_update.Subscribe([&]
        {
            if (++observed_frames == exit_after_frames)
                PostMessageW(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), WM_CLOSE, 0, 0);
        });
    }

    app.Tick(16.6f);
    app._before_update.Unsubscribe(exit_handle);
    bool smoke_passed = true;
    if (!smoke_plugin_path.empty())
    {
        const auto module = GetModuleHandleW(smoke_plugin_path.c_str());
        using CounterFn = u64 (AILU_PLUGIN_CALL*)() noexcept;
        const auto ticks = module ? reinterpret_cast<CounterFn>(GetProcAddress(module,
            "AiluPluginTestGetTickCount")) : nullptr;
        const auto workers = module ? reinterpret_cast<CounterFn>(GetProcAddress(module,
            "AiluPluginTestGetWorkerCount")) : nullptr;
        smoke_passed = ticks && workers && ticks() > 0 && workers() > 0
            && app.GetPluginManager()->HasRegistration(smoke_registration);
        LOG_INFO("[Plugin] Editor smoke activity: ticks={} workers={}", ticks ? ticks() : 0, workers ? workers() : 0);
    }
    app.Finalize();
    if (!smoke_plugin_path.empty())
        smoke_passed = smoke_passed && GetModuleHandleW(smoke_plugin_path.c_str()) == nullptr;
    UninstallCrashHandler();

#ifdef _DEBUG_MEM_LEAK
    _CrtDumpMemoryLeaks();
#endif

    if (!smoke_passed)
    {
        LogEditorStartupError(L"[Plugin] Editor smoke failed: no activity or module still resident after Finalize.");
        return 1;
    }
    if (exit_after_frames != 0)
    {
        FileAppender file_appender;
        file_appender.Print(LogMessage(ELogLevel::kInfo,
            L"[Plugin] Editor lifecycle validation PASS: normal exit; module absent after Finalize.",
            __FILE__, __LINE__, __FUNCTION__));
    }
    return 0;
}
