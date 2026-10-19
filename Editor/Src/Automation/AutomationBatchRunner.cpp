#include "Automation/AutomationBatchRunner.h"

#include "Automation/AutomationJson.h"
#include "EditorApp.h"
#include "Assets/AssetDocument.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/ImageCapture/ImageCaptureService.h"
#include "Render/Texture.h"
#include "Project/ProjectManager.h"
#include "Scene/Scene.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "Ext/stb/stb_image.h"

#include <algorithm>
#include <fstream>
#include <format>
#include <unordered_set>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            using Clock = std::chrono::steady_clock;

            u64 ElapsedMilliseconds(Clock::time_point start)
            {
                return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
            }

            AutomationValue MakeErrorValue(const AutomationBatchRunner::TestResult &result)
            {
                AutomationObject error;
                error.emplace("code", AutomationValue(result._error_code));
                error.emplace("message", AutomationValue(result._error_message));
                if (!result._expected.empty())
                    error.emplace("expected", AutomationValue(result._expected));
                if (!result._actual.empty())
                    error.emplace("actual", AutomationValue(result._actual));
                return AutomationValue(std::move(error));
            }

            String EscapeLine(StringView value)
            {
                String escaped;
                for (const char character : value)
                {
                    if (character == '\\' || character == '"')
                        escaped += '\\';
                    escaped += character;
                }
                return escaped;
            }
        }// namespace

        AutomationBatchRunner::AutomationBatchRunner(EditorApp &app, AutomationBatchOptions options)
            : _app(app), _options(std::move(options))
        {
        }

        Vector<AutomationBatchRunner::TestDesc> AutomationBatchRunner::BuildTests()
        {
            return {
                {"Automation.Ping", "Automation", "hidden-compat", ETestKind::kPing, 5u},
                {"Automation.ListMethods", "Automation", "hidden-compat", ETestKind::kListMethods, 5u},
                {"Automation.ProjectOpen", "Automation", "hidden-compat", ETestKind::kProjectOpen, 5u},
                {"Cpu.AutomationCapabilities", "Automation", "none", ETestKind::kCpuCapabilities, 5u},
                {"Cpu.AssetDatabase", "Assets", "none", ETestKind::kCpuAssetDatabase, 10u},
                {"Cpu.SceneDocuments", "Serialization", "none", ETestKind::kCpuSceneDocuments, 30u},
                {"Cpu.GpuAccessRejected", "Automation", "none", ETestKind::kCpuGpuAccessRejected, 5u},
                {"Rendering.OffscreenFrameSingle", "Rendering", "offscreen", ETestKind::kOffscreenFrameSingle, 10u},
                {"Rendering.OffscreenFrameBoundary", "Rendering", "offscreen", ETestKind::kOffscreenFrameBoundary, 10u},
                {"Rendering.OffscreenFrameStress", "Rendering", "offscreen", ETestKind::kOffscreenFrameStress, 30u},
                {"Rendering.OffscreenCapture", "Rendering", "offscreen", ETestKind::kOffscreenCapture, 15u},
                {"Rendering.OffscreenCapturePending", "Rendering", "offscreen", ETestKind::kOffscreenCapturePending, 15u},
                {"Rendering.OffscreenSceneCapture", "Rendering", "offscreen", ETestKind::kOffscreenGraphFinalCapture, 45u},
                {"Rendering.OffscreenResize", "Rendering", "offscreen", ETestKind::kOffscreenResize, 45u},
                {"Rendering.OffscreenGraphCapture", "Rendering", "offscreen", ETestKind::kOffscreenGraphCapture, 45u},
                {"Runner.DeliberateFailure", "Runner", "hidden-compat", ETestKind::kDeliberateFailure, 5u},
                {"Runner.DeliberateTimeout", "Runner", "hidden-compat", ETestKind::kDeliberateTimeout, 1u},
            };
        }

        bool AutomationBatchRunner::Matches(StringView value, StringView pattern)
        {
            size_t value_index = 0u;
            size_t pattern_index = 0u;
            size_t star_index = StringView::npos;
            size_t match_index = 0u;
            while (value_index < value.size())
            {
                if (pattern_index < pattern.size() &&
                    (pattern[pattern_index] == '?' || pattern[pattern_index] == value[value_index]))
                {
                    ++value_index;
                    ++pattern_index;
                    continue;
                }
                if (pattern_index < pattern.size() && pattern[pattern_index] == '*')
                {
                    star_index = pattern_index++;
                    match_index = value_index;
                    continue;
                }
                if (star_index == StringView::npos)
                    return false;
                pattern_index = star_index + 1u;
                value_index = ++match_index;
            }
            while (pattern_index < pattern.size() && pattern[pattern_index] == '*')
                ++pattern_index;
            return pattern_index == pattern.size();
        }

        AutomationRequest AutomationBatchRunner::MakeRequest(const char *method) const
        {
            AutomationRequest request;
            request._request_id = _request_id;
            request._method = method;
            request._context._caller = "batch-runner";
            request._context._interactive = false;
            request._context._allow_write = false;
            request._context._allow_destructive = false;
            return request;
        }

        bool AutomationBatchRunner::ValidateSceneCapturePng(const std::filesystem::path &path, String &error)
        {
            const String path_string = ToChar(path.wstring());
            int width = 0;
            int height = 0;
            int channels = 0;
            u8 *pixels = stbi_load(path_string.data(), &width, &height, &channels, 4);
            if (pixels == nullptr)
            {
                error = std::format("failed to decode PNG {}: {}", path_string, stbi_failure_reason());
                return false;
            }

            const u64 pixel_count = static_cast<u64>(width) * static_cast<u64>(height);
            u64 non_black_pixels = 0u;
            u8 min_channel = 255u;
            u8 max_channel = 0u;
            for (u64 pixel_index = 0u; pixel_index < pixel_count; ++pixel_index)
            {
                const u8 *pixel = pixels + pixel_index * 4u;
                const u8 max_rgb = std::max({pixel[0], pixel[1], pixel[2]});
                const u8 min_rgb = std::min({pixel[0], pixel[1], pixel[2]});
                if (max_rgb > 2u && pixel[3] > 2u)
                    ++non_black_pixels;
                min_channel = std::min(min_channel, min_rgb);
                max_channel = std::max(max_channel, max_rgb);
            }
            stbi_image_free(pixels);

            const f64 non_black_ratio = pixel_count == 0u ? 0.0 :
                static_cast<f64>(non_black_pixels) / static_cast<f64>(pixel_count);
            if (width <= 0 || height <= 0 || non_black_ratio < 0.001 || max_channel - min_channel < 2u)
            {
                error = std::format("scene capture is empty or uniform: {}x{}, non_black_ratio={:.6f}, channel_range={}..{}",
                    width, height, non_black_ratio, min_channel, max_channel);
                return false;
            }
            return true;
        }

        void AutomationBatchRunner::Prepare()
        {
            _is_prepared = true;
            _run_started = Clock::now();
            for (const TestDesc &test : BuildTests())
            {
                if (_options._list_tests || Matches(test._name, _options._test_filter))
                {
                    if (!_options._list_tests && test._required_graphics != _options._graphics_mode)
                    {
                        TestResult result;
                        result._name = test._name;
                        result._status = "skipped";
                        result._error_code = "unsupported_capability";
                        result._error_message = std::format("test requires graphics mode '{}', active mode is '{}'",
                            test._required_graphics, _options._graphics_mode);
                        _results.push_back(std::move(result));
                        _has_unsupported_capability = true;
                    }
                    else
                        _selected_tests.push_back(test);
                }
            }

            if (!_options._list_tests && _selected_tests.empty())
            {
                Complete(_has_unsupported_capability ? "failed" : "error",
                    _has_unsupported_capability ? "unsupported_capability" : "no_tests_matched",
                    _has_unsupported_capability ? "all matched tests require unavailable capabilities" :
                                                   "the test filter matched no registered tests");
                return;
            }
            if (_options._list_tests)
            {
                Complete("passed");
                return;
            }
        }

        void AutomationBatchRunner::StartCurrentTest()
        {
            _current_started = true;
            _test_started = Clock::now();
            const TestDesc &test = _selected_tests[_current_index];
            switch (test._kind)
            {
                case ETestKind::kPing:
                    _pending_request = _app.GetAutomationService()->Submit(MakeRequest("automation.ping"));
                    ++_request_id;
                    break;
                case ETestKind::kListMethods:
                    _pending_request = _app.GetAutomationService()->Submit(MakeRequest("automation.list_methods"));
                    ++_request_id;
                    break;
                case ETestKind::kProjectOpen:
                {
                    TestResult result;
                    result._name = test._name;
                    result._status = ProjectManager::Get().HasOpenedProject() ? "passed" : "failed";
                    result._error_code = result._status == "passed" ? "" : "project_not_open";
                    result._error_message = result._status == "passed" ? "" : "the Editor did not open a project";
                    FinishCurrentTest(std::move(result));
                    break;
                }
                case ETestKind::kCpuCapabilities:
                {
                    TestResult result;
                    result._name = test._name;
                    const bool passed = _app.IsCpuOnly() && _app.GetWindowPtr() == nullptr &&
                        Render::g_pGfxContext == nullptr && ResourceMgr::Get().IsCpuOnly();
                    result._status = passed ? "passed" : "failed";
                    if (!passed)
                    {
                        result._error_code = "cpu_only_contract_failed";
                        result._error_message = "CPU-only mode created a window, graphics context, or GPU resource path";
                    }
                    FinishCurrentTest(std::move(result));
                    break;
                }
                case ETestKind::kCpuAssetDatabase:
                {
                    TestResult result;
                    result._name = test._name;
                    bool passed = ResourceMgr::Get().IsCpuOnly() && ResourceMgr::Get().AssetNum() > 0u;
                    u64 invalid_assets = 0u;
                    for (auto iter = ResourceMgr::Get().Begin(); iter != ResourceMgr::Get().End(); ++iter)
                    {
                        const Asset *asset = iter->second.get();
                        if (asset == nullptr || !asset->GetGuid().IsValid() || asset->_asset_path.empty() ||
                            asset->_asset_type == nullptr)
                            ++invalid_assets;
                    }
                    passed = passed && invalid_assets == 0u;
                    result._status = passed ? "passed" : "failed";
                    if (!passed)
                    {
                        result._error_code = "asset_database_invalid";
                        result._error_message = std::format("asset_count={}, invalid_assets={}",
                            ResourceMgr::Get().AssetNum(), invalid_assets);
                    }
                    FinishCurrentTest(std::move(result));
                    break;
                }
                case ETestKind::kCpuSceneDocuments:
                {
                    TestResult result;
                    result._name = test._name;
                    bool passed = ResourceMgr::Get().IsCpuOnly();
                    u64 scene_count = 0u;
                    String failure;
                    for (auto iter = ResourceMgr::Get().Begin(); passed && iter != ResourceMgr::Get().End(); ++iter)
                    {
                        const Asset *asset = iter->second.get();
                        if (asset == nullptr || asset->_asset_type != SceneManagement::Scene::StaticType())
                            continue;
                        ++scene_count;
                        JsonArchive archive;
                        archive.Load(ResourceMgr::Get().GetResSysPath(asset->_asset_path));
                        if (!archive.IsLoaded())
                        {
                            passed = false;
                            failure = std::format("failed to load scene document '{}'", ToChar(asset->_asset_path));
                            break;
                        }
                        SceneAssetDocument document;
                        document.Deserialize(archive);
                        if (Guid(document._header._guid) != asset->GetGuid())
                        {
                            passed = false;
                            failure = std::format("scene document GUID mismatch for '{}'", ToChar(asset->_asset_path));
                            break;
                        }
                        const bool requires_entity_guids = document._scene_format_version >=
                            SceneAssetDocument::kEntityGuidSceneFormatVersion;
                        std::unordered_set<Guid, GuidHasher> entity_guids;
                        for (const auto &entity : document._entities)
                        {
                            if ((!entity._entity_guid.IsEmpty() && !entity_guids.insert(entity._entity_guid).second) ||
                                (requires_entity_guids && entity._entity_guid.IsEmpty()))
                            {
                                passed = false;
                                failure = std::format("missing or duplicate entity GUID in '{}'",
                                    ToChar(asset->_asset_path));
                                break;
                            }
                        }
                        if (!passed)
                            break;
                        for (const auto &entity : document._entities)
                        {
                            const Guid parent_guid = entity._hierarchy_component._parent_guid;
                            if (entity._has_hierarchy_component && !parent_guid.IsEmpty() &&
                                !entity_guids.contains(parent_guid))
                            {
                                passed = false;
                                failure = std::format("broken parent GUID in '{}'", ToChar(asset->_asset_path));
                                break;
                            }
                        }
                    }
                    passed = passed && scene_count > 0u;
                    result._status = passed ? "passed" : "failed";
                    if (!passed)
                    {
                        result._error_code = "scene_document_invalid";
                        result._error_message = failure.empty() ? "no scene documents found" : failure;
                    }
                    FinishCurrentTest(std::move(result));
                    break;
                }
                case ETestKind::kCpuGpuAccessRejected:
                {
                    TestResult result;
                    result._name = test._name;
                    const auto shader = ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/default_ui.alasset");
                    const bool passed = ResourceMgr::Get().IsCpuOnly() && shader == nullptr &&
                        Render::g_pGfxContext == nullptr;
                    result._status = passed ? "passed" : "failed";
                    if (!passed)
                    {
                        result._error_code = "gpu_access_not_rejected";
                        result._error_message = "CPU-only ResourceMgr allowed a GPU-backed shader load";
                    }
                    FinishCurrentTest(std::move(result));
                    break;
                }
                case ETestKind::kDeliberateFailure:
                {
                    TestResult result;
                    result._name = test._name;
                    result._status = "failed";
                    result._error_code = "assertion_failed";
                    result._error_message = "deliberate runner failure";
                    result._expected = "pass";
                    result._actual = "fail";
                    FinishCurrentTest(std::move(result));
                    break;
                }
                case ETestKind::kDeliberateTimeout:
                    break;
                case ETestKind::kOffscreenGraphCapture:
                case ETestKind::kOffscreenGraphFinalCapture:
                    break;
            }
        }

        void AutomationBatchRunner::FinishCurrentTest(TestResult result)
        {
            result._duration_ms = ElapsedMilliseconds(_test_started);
            _results.push_back(std::move(result));
            _pending_request.reset();
            if (_current_index < _selected_tests.size())
            {
                const ETestKind kind = _selected_tests[_current_index]._kind;
                if (kind == ETestKind::kOffscreenCapture ||
                    kind == ETestKind::kOffscreenCapturePending ||
                    kind == ETestKind::kOffscreenSceneCapture)
                {
                    _capture_session = 0u;
                    _capture_output_path.clear();
                    _capture_saw_pending = false;
                }
                if (kind == ETestKind::kOffscreenGraphCapture || kind == ETestKind::kOffscreenGraphFinalCapture)
                {
                    _graph_capture_session = 0u;
                    _graph_capture_output_directory.clear();
                }
                if (kind == ETestKind::kOffscreenResize)
                {
                    _offscreen_resize_stage = 0u;
                    _offscreen_resize_wait_frame = 0u;
                }
            }
            _current_started = false;
            ++_current_index;
            if (_current_index >= _selected_tests.size())
            {
                String final_status = _has_unsupported_capability ? "failed" : "passed";
                String final_error_code = _has_unsupported_capability ? "unsupported_capability" : String{};
                String final_error_message = _has_unsupported_capability ?
                    "one or more matched tests require unavailable capabilities" : String{};
                for (const TestResult &item : _results)
                {
                    if (item._status == "timeout")
                    {
                        final_status = "timeout";
                        final_error_code.clear();
                        final_error_message.clear();
                        break;
                    }
                    if (item._status != "passed")
                        final_status = "failed";
                }
                Complete(final_status, final_error_code, final_error_message);
            }
        }

        void AutomationBatchRunner::Tick()
        {
            if (_is_finished)
                return;
            if (!_is_prepared)
            {
                Prepare();
                if (_is_finished)
                    return;
            }

            if (ElapsedMilliseconds(_run_started) >= static_cast<u64>(_options._timeout_seconds) * 1000u)
            {
                if (_current_started)
                {
                    TestResult result;
                    result._name = _selected_tests[_current_index]._name;
                    result._status = "timeout";
                    result._error_code = "overall_timeout";
                    result._error_message = "batch deadline exceeded";
                    FinishCurrentTest(std::move(result));
                }
                Complete("timeout", "overall_timeout", "batch deadline exceeded");
                return;
            }

            if (!_current_started)
            {
                if (_current_index >= _selected_tests.size())
                    return;
                StartCurrentTest();
                if (!_current_started)
                    return;
            }

            const TestDesc &test = _selected_tests[_current_index];
            if (ElapsedMilliseconds(_test_started) >= static_cast<u64>(test._timeout_seconds) * 1000u)
            {
                TestResult result;
                result._name = test._name;
                result._status = "timeout";
                result._error_code = "test_timeout";
                result._error_message = "test deadline exceeded";
                FinishCurrentTest(std::move(result));
                return;
            }

            if (test._kind == ETestKind::kOffscreenFrameSingle ||
                test._kind == ETestKind::kOffscreenFrameBoundary ||
                test._kind == ETestKind::kOffscreenFrameStress)
            {
                const u64 required_frames = test._kind == ETestKind::kOffscreenFrameStress ? 128u :
                    test._kind == ETestKind::kOffscreenFrameSingle ? 1u : 8u;
                if (_app.GetFrameCount() < required_frames)
                    return;
                TestResult result;
                result._name = test._name;
                const bool has_target = _app.GetOffscreenTarget() != nullptr;
                const bool no_window = _app.GetWindowPtr() == nullptr;
                const bool no_swapchain = Render::g_pGfxContext != nullptr &&
                    Render::g_pGfxContext->GetSwapchainCount() == 0u;
                const bool no_present = Render::g_pGfxContext != nullptr &&
                    Render::g_pGfxContext->GetPresentCount() == 0u;
                if (_app.IsOffscreen() && has_target && no_window && no_swapchain && no_present)
                    result._status = "passed";
                else
                {
                    result._status = "failed";
                    result._error_code = "offscreen_contract_failed";
                    result._error_message = std::format("window={}, swapchain={}, present={}, target={}",
                        no_window ? 0 : 1, no_swapchain ? 0 : 1, no_present ? 0 : 1, has_target ? 1 : 0);
                }
                FinishCurrentTest(std::move(result));
                return;
            }

            if (test._kind == ETestKind::kOffscreenGraphCapture ||
                test._kind == ETestKind::kOffscreenGraphFinalCapture)
            {
                if (_app.GetFrameCount() < 8u)
                    return;
                if (!_pending_request.has_value())
                {
                    AutomationRequest request = MakeRequest(_graph_capture_session == 0u
                        ? "render.capture_pass" : "render.capture_status");
                    if (_graph_capture_session == 0u)
                    {
                        request._context._allow_write = true;
                        request._context._allow_destructive = true;
                        const bool final_capture = test._kind == ETestKind::kOffscreenGraphFinalCapture;
                        request._arguments.emplace("pass_name", AutomationValue(final_capture ? "FinalBlit" : "ToneMapping"));
                        request._arguments.emplace("capture_output", AutomationValue(true));
                        request._arguments.emplace("access_index", AutomationValue(0));
                        request._arguments.emplace("frame_count", AutomationValue(1));
                        request._arguments.emplace("file_prefix", AutomationValue(final_capture
                            ? "offscreen_graph_final" : "offscreen_graph"));
                        const auto output_directory = std::filesystem::path(_options._report_path).parent_path() /
                            L"graph_capture";
                        _graph_capture_output_directory = output_directory.wstring();
                        request._arguments.emplace("output_directory", AutomationValue(output_directory.string()));
                        request._arguments.emplace("output_format", AutomationValue("png"));
                    }
                    _pending_request = _app.GetAutomationService()->Submit(std::move(request));
                    ++_request_id;
                    return;
                }
                if (_pending_request->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                    return;

                const AutomationResult automation_result = _pending_request->get();
                if (!automation_result._success)
                {
                    TestResult result;
                    result._name = test._name;
                    result._status = "failed";
                    result._error_code = automation_result._error._code;
                    result._error_message = automation_result._error._message;
                    FinishCurrentTest(std::move(result));
                    return;
                }
                const auto &data = automation_result._data.AsObject();
                const auto session_it = data.find("session_id");
                if (_graph_capture_session == 0u)
                {
                    _graph_capture_session = session_it != data.end() ? session_it->second.AsUInt() : 0u;
                    if (_graph_capture_session == 0u)
                    {
                        TestResult result;
                        result._name = test._name;
                        result._status = "failed";
                        result._error_code = "graph_capture_not_armed";
                        result._error_message = "render.capture_pass returned no session id";
                        FinishCurrentTest(std::move(result));
                        return;
                    }
                    _pending_request.reset();
                    return;
                }

                const auto state_it = data.find("state");
                const String state = state_it != data.end() ? state_it->second.AsString() : "unknown";
                if (state != "completed" && state != "failed" && state != "cancelled")
                {
                    _pending_request.reset();
                    return;
                }
                TestResult result;
                result._name = test._name;
                result._status = state == "completed" ? "passed" : "failed";
                if (result._status != "passed")
                {
                    result._error_code = "graph_capture_failed";
                    const auto error_it = data.find("error");
                    result._error_message = error_it != data.end() ? error_it->second.AsString() : state;
                }
                else
                {
                    const auto files_it = data.find("files");
                    result._actual = files_it != data.end() ? files_it->second.Dump() :
                        ToChar(_graph_capture_output_directory);
                }
                FinishCurrentTest(std::move(result));
                return;
            }

            if (test._kind == ETestKind::kOffscreenResize)
            {
                constexpr u32 kResizeSizes[][2] = {{640u, 360u}, {1280u, 720u}, {896u, 512u}, {1491u, 902u}};
                constexpr size_t kResizeCount = sizeof(kResizeSizes) / sizeof(kResizeSizes[0]);
                if (_app.GetFrameCount() < 8u)
                    return;
                if (_offscreen_resize_stage < kResizeCount)
                {
                    const u32 width = kResizeSizes[_offscreen_resize_stage][0];
                    const u32 height = kResizeSizes[_offscreen_resize_stage][1];
                    if (!_app.ResizeOffscreenTarget(width, height))
                    {
                        TestResult result;
                        result._name = test._name;
                        result._status = "failed";
                        result._error_code = "offscreen_resize_failed";
                        result._error_message = std::format("failed to recreate offscreen target {}x{}", width, height);
                        FinishCurrentTest(std::move(result));
                        return;
                    }
                    ++_offscreen_resize_stage;
                    _offscreen_resize_wait_frame = _app.GetFrameCount() + 2u;
                    return;
                }
                if (_app.GetFrameCount() < _offscreen_resize_wait_frame)
                    return;
                auto *target = _app.GetOffscreenTarget();
                TestResult result;
                result._name = test._name;
                const bool no_window = _app.GetWindowPtr() == nullptr;
                const bool no_swapchain = Render::g_pGfxContext != nullptr &&
                    Render::g_pGfxContext->GetSwapchainCount() == 0u;
                const bool no_present = Render::g_pGfxContext != nullptr &&
                    Render::g_pGfxContext->GetPresentCount() == 0u;
                const bool size_matches = target != nullptr && target->Width() == 1491u && target->Height() == 902u;
                if (_app.IsOffscreen() && no_window && no_swapchain && no_present && size_matches)
                {
                    result._status = "passed";
                    result._actual = "640x360,1280x720,896x512,1491x902";
                }
                else
                {
                    result._status = "failed";
                    result._error_code = "offscreen_resize_contract_failed";
                    result._error_message = std::format("window={}, swapchain={}, present={}, target={}x{}",
                        no_window ? 0 : 1, no_swapchain ? 0 : 1, no_present ? 0 : 1,
                        target != nullptr ? target->Width() : 0u, target != nullptr ? target->Height() : 0u);
                }
                FinishCurrentTest(std::move(result));
                return;
            }

            if (test._kind == ETestKind::kOffscreenCapture ||
                test._kind == ETestKind::kOffscreenCapturePending ||
                test._kind == ETestKind::kOffscreenSceneCapture)
            {
                if (_capture_session == 0u)
                {
                    const bool scene_capture = test._kind == ETestKind::kOffscreenSceneCapture;
                    // BatchRunner ticks before RenderFrame; defer scene capture until one rendered scene frame
                    // has completed so the direct target readback cannot observe the initial black texture.
                    const u64 required_frames = scene_capture ? 11u : 2u;
                    if (_app.GetFrameCount() < required_frames)
                        return;
                    if (!_app.IsOffscreen() || _app.GetOffscreenTarget() == nullptr || Render::g_pGfxContext == nullptr)
                    {
                        TestResult result;
                        result._name = test._name;
                        result._status = "failed";
                        result._error_code = "offscreen_contract_failed";
                        result._error_message = "offscreen target or graphics context is unavailable";
                        FinishCurrentTest(std::move(result));
                        return;
                    }
                    if (scene_capture)
                    {
                        auto *active_scene = SceneManagement::SceneMgr::Get().ActiveScene();
                        if (active_scene == nullptr || active_scene->EntityNum() == 0u)
                        {
                            TestResult result;
                            result._name = test._name;
                            result._status = "failed";
                            result._error_code = "scene_not_loaded";
                            result._error_message = "the requested scene has no active entities";
                            FinishCurrentTest(std::move(result));
                            return;
                        }
                        if (active_scene->FindMainCamera() == nullptr)
                        {
                            TestResult result;
                            result._name = test._name;
                            result._status = "failed";
                            result._error_code = "scene_camera_unavailable";
                            result._error_message = "the requested scene has no enabled MainCamera entity";
                            FinishCurrentTest(std::move(result));
                            return;
                        }
                        bool has_ready_mesh = false;
                        for (const auto &component : active_scene->GetAllStaticRenderable())
                        {
                            if (component._p_mesh != nullptr && component._p_mesh->GetVertexBuffer() != nullptr)
                            {
                                has_ready_mesh = true;
                                break;
                            }
                        }
                        if (!has_ready_mesh)
                        {
                            if (_app.GetFrameCount() < 60u)
                                return;
                            TestResult result;
                            result._name = test._name;
                            result._status = "failed";
                            result._error_code = "scene_resources_not_ready";
                            result._error_message = "no enabled StaticMesh has a ready GPU VertexBuffer after 60 frames";
                            FinishCurrentTest(std::move(result));
                            return;
                        }
                    }
                    Render::ImageCaptureSequenceDesc capture_desc;
                    capture_desc._output_format = Render::EImageCaptureOutputFormat::kPng;
                    capture_desc._output_directory = std::filesystem::path(_options._report_path).parent_path().wstring() + L"/captures";
                    capture_desc._file_prefix = scene_capture ? "offscreen_scene" : "offscreen";
                    _capture_output_path = std::filesystem::path(capture_desc._output_directory) / L"session_1" /
                        (scene_capture ? L"offscreen_scene_0000.png" : L"offscreen_0000.png");
                    auto cmd = Render::CommandBufferPool::Get("OffscreenCapture");
                    Render::ImageCaptureTextureDesc source;
                    source._debug_name = "OffscreenOutput";
                    cmd->SetRenderTarget(_app.GetOffscreenTarget());
                    if (!scene_capture)
                        cmd->ClearRenderTarget(Colors::kCyan);
                    _capture_session = Render::ImageCaptureService::CaptureOnce(*cmd, _app.GetOffscreenTarget(), source,
                                                                                 capture_desc);
                    Render::g_pGfxContext->ExecuteCommandBuffer(cmd);
                    Render::CommandBufferPool::Release(cmd);
                    return;
                }

                const auto status = Render::ImageCaptureService::GetStatus(_capture_session);
                if (status._state != Render::EImageCaptureState::kCompleted &&
                    status._state != Render::EImageCaptureState::kFailed)
                    _capture_saw_pending = true;
                if (status._state != Render::EImageCaptureState::kCompleted &&
                    status._state != Render::EImageCaptureState::kFailed)
                    return;
                TestResult result;
                result._name = test._name;
                result._status = status._state == Render::EImageCaptureState::kCompleted ? "passed" : "failed";
                if (test._kind == ETestKind::kOffscreenCapturePending && !_capture_saw_pending)
                {
                    result._status = "failed";
                    result._error_code = "capture_pending_not_observed";
                    result._error_message = "capture reached a terminal state before the runner observed pending";
                }
                if (test._kind == ETestKind::kOffscreenSceneCapture && result._status == "passed")
                {
                    String validation_error;
                    if (!ValidateSceneCapturePng(_capture_output_path, validation_error))
                    {
                        result._status = "failed";
                        result._error_code = "scene_capture_empty";
                        result._error_message = validation_error;
                    }
                }
                if (result._status != "passed")
                {
                    if (result._error_code.empty())
                    {
                        result._error_code = "capture_failed";
                        result._error_message = status._error;
                    }
                }
                FinishCurrentTest(std::move(result));
                return;
            }

            if (!_pending_request.has_value())
                return;
            if (_pending_request->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                return;

            const AutomationResult automation_result = _pending_request->get();
            TestResult result;
            result._name = test._name;
            if (!automation_result._success)
            {
                result._status = "error";
                result._error_code = automation_result._error._code;
                result._error_message = automation_result._error._message;
                FinishCurrentTest(std::move(result));
                return;
            }
            if (test._kind == ETestKind::kPing)
            {
                const auto &data = automation_result._data.AsObject();
                const auto pong = data.find("pong");
                if (pong == data.end() || !pong->second.AsBool())
                {
                    result._status = "failed";
                    result._error_code = "assertion_failed";
                    result._error_message = "automation.ping did not return pong=true";
                }
                else
                    result._status = "passed";
            }
            else
            {
                const auto &data = automation_result._data.AsObject();
                const auto methods = data.find("methods");
                if (methods == data.end() || !methods->second.IsArray() || methods->second.AsArray().empty())
                {
                    result._status = "failed";
                    result._error_code = "assertion_failed";
                    result._error_message = "automation.list_methods returned no methods";
                }
                else
                    result._status = "passed";
            }
            FinishCurrentTest(std::move(result));
        }

        bool AutomationBatchRunner::WriteAuxiliaryFiles(const std::filesystem::path &run_directory)
        {
            std::ofstream events(run_directory / "events.jsonl", std::ios::out | std::ios::trunc);
            std::ofstream log(run_directory / "engine.log", std::ios::out | std::ios::trunc);
            if (!events || !log)
                return false;
            events << "{\"run_id\":\"" << EscapeLine(ToChar(_options._run_id)) << "\",\"event\":\"completed\"}\n";
            for (const TestResult &result : _results)
            {
                events << "{\"run_id\":\"" << EscapeLine(ToChar(_options._run_id))
                       << "\",\"event\":\"test_completed\",\"test\":\""
                       << EscapeLine(result._name) << "\",\"status\":\""
                       << EscapeLine(result._status) << "\"}\n";
                log << result._name << ": " << result._status << "\n";
            }
            log << "batch run " << ToChar(_options._run_id) << " completed with " << _results.size() << " test result(s)\n";
            events.flush();
            log.flush();
            return events.good() && log.good();
        }

        bool AutomationBatchRunner::WriteReport(const String &status, const String &error_code, const String &error_message)
        {
            const std::filesystem::path report_path(_options._report_path);
            std::error_code filesystem_error;
            std::filesystem::create_directories(report_path.parent_path(), filesystem_error);
            if (filesystem_error)
                return false;

            u32 passed = 0u;
            u32 failed = 0u;
            u32 skipped = 0u;
            u32 errors = 0u;
            u32 timeouts = 0u;
            AutomationArray tests;
            for (const TestDesc &test : BuildTests())
            {
                if (!_options._list_tests && !Matches(test._name, _options._test_filter))
                    continue;
                AutomationObject entry;
                entry.emplace("name", AutomationValue(test._name));
                entry.emplace("category", AutomationValue(test._category));
                entry.emplace("required_graphics", AutomationValue(test._required_graphics));
                if (_options._list_tests)
                {
                    entry.emplace("status", AutomationValue("listed"));
                    tests.emplace_back(AutomationValue(std::move(entry)));
                    continue;
                }
                const auto result = std::find_if(_results.begin(), _results.end(), [&](const TestResult &item)
                {
                    return item._name == test._name;
                });
                if (result == _results.end())
                    continue;
                entry.emplace("status", AutomationValue(result->_status));
                entry.emplace("duration_ms", AutomationValue(result->_duration_ms));
                if (!result->_error_code.empty())
                    entry.emplace("error", MakeErrorValue(*result));
                tests.emplace_back(AutomationValue(std::move(entry)));
                if (result->_status == "passed") ++passed;
                else if (result->_status == "failed") ++failed;
                else if (result->_status == "skipped") ++skipped;
                else if (result->_status == "timeout") ++timeouts;
                else ++errors;
            }

            AutomationObject summary;
            summary.emplace("passed", AutomationValue(passed));
            summary.emplace("failed", AutomationValue(failed));
            summary.emplace("skipped", AutomationValue(skipped));
            summary.emplace("error", AutomationValue(errors));
            summary.emplace("timeout", AutomationValue(timeouts));
            AutomationObject report;
            report.emplace("schema_version", AutomationValue(1));
            report.emplace("run_id", AutomationValue(ToChar(_options._run_id)));
            report.emplace("requested_tests", AutomationValue(_options._list_tests ? "list" : _options._test_filter));
            report.emplace("graphics_mode", AutomationValue(_options._graphics_mode));
            AutomationObject capabilities;
            capabilities.emplace("window_bound", AutomationValue(_app.GetWindowPtr() != nullptr));
            capabilities.emplace("gpu_device_count", AutomationValue(Render::g_pGfxContext != nullptr ? 1 : 0));
            capabilities.emplace("swapchain_count", AutomationValue(Render::g_pGfxContext != nullptr ?
                Render::g_pGfxContext->GetSwapchainCount() : 0));
            capabilities.emplace("present_count", AutomationValue(Render::g_pGfxContext != nullptr ?
                Render::g_pGfxContext->GetPresentCount() : 0));
            capabilities.emplace("frame_count", AutomationValue(_app.GetFrameCount()));
            if (Render::g_pGfxContext != nullptr)
            {
                capabilities.emplace("gpu_frame_count", AutomationValue(Render::g_pGfxContext->GetFrameCount()));
            }
            else
                capabilities.emplace("gpu_frame_count", AutomationValue(0));
            report.emplace("capabilities", AutomationValue(std::move(capabilities)));
            report.emplace("status", AutomationValue(status));
            report.emplace("duration_ms", AutomationValue(ElapsedMilliseconds(_run_started)));
            report.emplace("summary", AutomationValue(std::move(summary)));
            report.emplace("tests", AutomationValue(std::move(tests)));
            if (!error_code.empty())
            {
                AutomationObject error;
                error.emplace("code", AutomationValue(error_code));
                error.emplace("message", AutomationValue(error_message));
                report.emplace("error", AutomationValue(std::move(error)));
            }
            AutomationArray artifacts;
            artifacts.emplace_back(AutomationValue("events.jsonl"));
            artifacts.emplace_back(AutomationValue("engine.log"));
            report.emplace("artifacts", AutomationValue(std::move(artifacts)));

            const std::filesystem::path temp_path = report_path.wstring() + L".tmp";
            std::ofstream output(temp_path, std::ios::out | std::ios::trunc);
            if (!output)
                return false;
            output << AutomationJson::Write(AutomationValue(std::move(report)));
            output.flush();
            output.close();
            if (!output.good())
                return false;
            std::filesystem::remove(report_path, filesystem_error);
            filesystem_error.clear();
            std::filesystem::rename(temp_path, report_path, filesystem_error);
            if (filesystem_error)
                return false;
            return WriteAuxiliaryFiles(report_path.parent_path());
        }

        void AutomationBatchRunner::Complete(String status, String error_code, String error_message)
        {
            if (_is_finished)
                return;
            if (status == "passed")
                _exit_code = 0;
            else if (status == "failed" || status == "timeout")
                _exit_code = 1;
            else
                _exit_code = 2;
            if (!WriteReport(status, error_code, error_message))
                _exit_code = 3;
            _is_finished = true;
            LOG_INFO("[Batch] completed status={} exit_code={} tests={}", status, _exit_code, _results.size());
            if (_app.GetWindowPtr() != nullptr)
                _app.GetWindow().RequestClose();
            else
                _app.RequestExit();
        }
    }// namespace Editor
}// namespace Ailu
