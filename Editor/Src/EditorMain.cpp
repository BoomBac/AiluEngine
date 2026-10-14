#include "Ailu.h"
#include "EditorApp.h"
#include "Framework/Common/CrashHandler.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Plugin/PluginManager.h"
#include "UI/UIRenderer.h"

#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
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
    struct ProfileCaptureOptions
    {
        u32 _warmup_frames = 0;
        u32 _sample_frames = 0;
        WString _output_path;
    };

    void LogEditorStartupError(const std::wstring &message,
                               const std::source_location &location = std::source_location::current())
    {
        FileAppender file_appender;
        file_appender.Print(LogMessage(ELogLevel::kError, message, location.file_name(), location.line(),
                                       location.function_name()));
    }

    std::string QuoteCsv(const String &value)
    {
        std::string quoted = "\"";
        for (const char character : value)
        {
            if (character == '"')
                quoted += '"';
            quoted += character;
        }
        quoted += '"';
        return quoted;
    }

    bool WriteProfileCaptureCsv(const WString &output_path,
                                const List<ProfileFrameData> &frames,
                                u32 minimum_frames)
    {
        if (frames.size() < minimum_frames)
            return false;

        const std::filesystem::path path(output_path);
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
            return false;

        output << "frame_index,frame_start_ms,frame_end_ms,thread_id,block_name,start_ms,duration_ms,call_depth\r\n";
        size_t event_count = 0;
        for (const auto &frame : frames)
        {
            for (const auto &[thread_id, blocks] : frame._data_threads)
            {
                const size_t thread_hash = std::hash<std::thread::id>{}(thread_id);
                for (const auto &block : blocks)
                {
                    output << frame._frame_index << ',' << std::fixed << std::setprecision(4)
                           << frame._start_time << ',' << frame._end_time << ',' << thread_hash << ','
                           << QuoteCsv(block._name) << ',' << block._start_time << ',' << block._duration << ','
                           << block._call_depth << "\r\n";
                    ++event_count;
                }
            }
        }
        output.flush();
        return output.good() && event_count > 0;
    }


    bool WriteUIWidgetDrawStatsCsv(const WString &output_path,
                                  const Vector<Vector<UI::UIWidgetDrawStats>> &stats)
    {
        if (stats.empty())
            return false;

        std::filesystem::path path(output_path);
        path.replace_extension(L".ui-widgets.csv");
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
            return false;

        output << "sample_index,widget_name,draw_node_count\r\n";
        for (size_t sample_index = 0u; sample_index < stats.size(); ++sample_index)
        {
            for (const auto &widget_stats : stats[sample_index])
                output << sample_index << ',' << QuoteCsv(widget_stats._widget_name) << ','
                       << widget_stats._draw_node_count << "\r\n";
        }
        output.flush();
        return output.good();
    }

    bool WriteUIRenderStatsCsv(const WString &output_path, const Vector<UI::UIRenderStats> &stats)
    {
        if (stats.empty())
            return false;

        std::filesystem::path path(output_path);
        path.replace_extension(L".ui-stats.csv");
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
            return false;

        output << "sample_index,ui_widget_update_executed_count,ui_widget_update_skipped_count,"
                  "ui_texture_cache_hit_count,ui_texture_cache_miss_count,ui_texture_cache_dirty_count,"
                  "ui_texture_cache_composite_draw_count,ui_texture_cache_resident_bytes,ui_texture_cache_repaint_time_ms,"
                  "ui_cache_hit_count,ui_cache_miss_count,ui_draw_call_count,ui_uploaded_bytes\r\n";
        for (size_t sample_index = 0u; sample_index < stats.size(); ++sample_index)
        {
            const auto &frame_stats = stats[sample_index];
            output << sample_index << ',' << frame_stats._ui_widget_update_executed_count << ','
                   << frame_stats._ui_widget_update_skipped_count << ',' << frame_stats._ui_texture_cache_hit_count << ','
                   << frame_stats._ui_texture_cache_miss_count << ',' << frame_stats._ui_texture_cache_dirty_count << ','
                   << frame_stats._ui_texture_cache_composite_draw_count << ',' << frame_stats._ui_texture_cache_resident_bytes << ','
                   << frame_stats._ui_texture_cache_repaint_time << ',' << frame_stats._ui_cache_hit_count << ','
                   << frame_stats._ui_cache_miss_count << ',' << frame_stats._ui_draw_call_count << ','
                   << frame_stats._ui_uploaded_bytes << "\r\n";
        }
        output.flush();
        return output.good();
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
    std::optional<ProfileCaptureOptions> profile_capture;
    try
    {
        for (size_t argument_index = 2; argument_index < context._arguments.size(); ++argument_index)
        {
            const auto& argument = context._arguments[argument_index];
            if (argument == L"--profile-capture")
            {
                if (argument_index + 3 >= context._arguments.size())
                    throw std::invalid_argument("Missing profile-capture option values");
                auto parse_frame_count = [](const WString &value, u32 minimum, u32 maximum)
                {
                    size_t consumed = 0;
                    const auto count = std::stoul(value, &consumed);
                    if (consumed != value.size() || count < minimum || count > maximum)
                        throw std::invalid_argument("Invalid profile-capture frame count");
                    return static_cast<u32>(count);
                };
                ProfileCaptureOptions options;
                options._warmup_frames = parse_frame_count(context._arguments[argument_index + 1], 0, 100000);
                options._sample_frames = parse_frame_count(context._arguments[argument_index + 2], 1,
                                                           Profiler::kMaxCacheDataNum);
                options._output_path = std::filesystem::absolute(context._arguments[argument_index + 3])
                                           .lexically_normal().wstring();
                argument_index += 3;
                profile_capture = std::move(options);
                continue;
            }
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
        if (profile_capture && exit_after_frames != 0)
            throw std::invalid_argument("profile-capture cannot be combined with exit-after-frames");
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
        LogEditorStartupError(L"Invalid Editor profiling/lifecycle options.");
        return 1;
    }

    if (profile_capture && !Profiler::IsEngineProfilerEnabled())
    {
        LogEditorStartupError(L"Profile capture requires AILU_ENABLE_ENGINE_PROFILER=ON.");
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
    Delegate<>::Handle profile_capture_handle = Delegate<>::kInvalidHandle;
    u32 observed_frames = 0;
    bool profile_capture_passed = !profile_capture.has_value();
    if (exit_after_frames != 0)
    {
        exit_handle = app._before_update.Subscribe([&]
        {
            if (++observed_frames == exit_after_frames)
                PostMessageW(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), WM_CLOSE, 0, 0);
        });
    }

    u32 warmup_frames = 0;
    u32 recorded_frames = 0;
    bool recording_started = false;
    if (profile_capture && profile_capture->_warmup_frames == 0u)
    {
        Profiler::Get().StartRecord();
        recording_started = true;
    }
    bool drain_frame_pending = false;
    Vector<UI::UIRenderStats> ui_stats;
    Vector<Vector<UI::UIWidgetDrawStats>> widget_draw_stats;
    if (profile_capture)
    {
        ui_stats.reserve(profile_capture->_sample_frames);
        widget_draw_stats.reserve(profile_capture->_sample_frames);
        profile_capture_handle = app._after_update.Subscribe([&]
        {
            if (!recording_started)
            {
                if (++warmup_frames >= profile_capture->_warmup_frames)
                {
                    Profiler::Get().StartRecord();
                    recording_started = true;
                }
                return;
            }
            if (recorded_frames < profile_capture->_sample_frames)
            {
                if (++recorded_frames == profile_capture->_sample_frames)
                    drain_frame_pending = true;
                if (auto *ui_renderer = UI::UIRenderer::Get(); ui_renderer != nullptr)
                {
                    ui_stats.push_back(ui_renderer->GetFrameStats());
                    widget_draw_stats.push_back(ui_renderer->GetWidgetDrawStats());
                }
                return;
            }
            if (!drain_frame_pending)
                return;

            drain_frame_pending = false;
            Profiler::Get().StopRecord();
            const auto &frames = Profiler::Get().GetFrameData();
            profile_capture_passed = WriteProfileCaptureCsv(profile_capture->_output_path, frames,
                                                            profile_capture->_sample_frames) &&
                                     ui_stats.size() == profile_capture->_sample_frames &&
                                     WriteUIRenderStatsCsv(profile_capture->_output_path, ui_stats) &&
                                     widget_draw_stats.size() == profile_capture->_sample_frames &&
                                     WriteUIWidgetDrawStatsCsv(profile_capture->_output_path, widget_draw_stats);
            LOG_INFO("[Profiler] Capture completed: requested_frames={} stored_frames={} result={}",
                     profile_capture->_sample_frames, frames.size(), profile_capture_passed ? "PASS" : "FAIL");
            if (!profile_capture_passed)
                LogEditorStartupError(L"Profile capture produced incomplete CPU blocks or UI render stats.");
            PostMessageW(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), WM_CLOSE, 0, 0);
        });
    }

    app.Tick(16.6f);
    app._before_update.Unsubscribe(exit_handle);
    app._after_update.Unsubscribe(profile_capture_handle);
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
    if (profile_capture && !profile_capture_passed)
        return 1;
    if (exit_after_frames != 0)
    {
        FileAppender file_appender;
        file_appender.Print(LogMessage(ELogLevel::kInfo,
            L"[Plugin] Editor lifecycle validation PASS: normal exit; module absent after Finalize.",
            __FILE__, __LINE__, __FUNCTION__));
    }
    return 0;
}
