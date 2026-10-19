#pragma once
#ifndef __AUTOMATION_BATCH_RUNNER_H__
#define __AUTOMATION_BATCH_RUNNER_H__

#include "Automation/AutomationService.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Render/ImageCapture/ImageCaptureTypes.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <optional>

namespace Ailu
{
    namespace Editor
    {
        class EditorApp;

        struct AutomationBatchOptions
        {
            bool _list_tests = false;
            String _test_filter;
            String _graphics_mode = "hidden-compat";
            WString _report_path;
            WString _run_id;
            u32 _timeout_seconds = 60u;
        };

        class AutomationBatchRunner final
        {
        public:
            AutomationBatchRunner(EditorApp &app, AutomationBatchOptions options);

            void Tick();
            bool IsFinished() const { return _is_finished; }
            int ExitCode() const { return _exit_code; }

        public:
            enum class ETestKind
            {
                kPing,
                kListMethods,
                kProjectOpen,
                kCpuCapabilities,
                kCpuAssetDatabase,
                kCpuSceneDocuments,
                kCpuGpuAccessRejected,
                kOffscreenFrameSingle,
                kOffscreenFrameBoundary,
                kOffscreenFrameStress,
                kOffscreenCapture,
                kOffscreenCapturePending,
                kOffscreenSceneCapture,
                kOffscreenResize,
                kOffscreenGraphCapture,
                kOffscreenGraphFinalCapture,
                kDeliberateFailure,
                kDeliberateTimeout,
            };

            struct TestDesc
            {
                String _name;
                String _category;
                String _required_graphics;
                ETestKind _kind;
                u32 _timeout_seconds = 5u;
            };

            struct TestResult
            {
                String _name;
                String _status;
                u64 _duration_ms = 0u;
                String _error_code;
                String _error_message;
                String _expected;
                String _actual;
            };

        private:

            void Prepare();
            void StartCurrentTest();
            void FinishCurrentTest(TestResult result);
            void Complete(String status, String error_code = {}, String error_message = {});
            bool WriteReport(const String &status, const String &error_code, const String &error_message);
            bool WriteAuxiliaryFiles(const std::filesystem::path &run_directory);
            AutomationRequest MakeRequest(const char *method) const;
            static bool ValidateSceneCapturePng(const std::filesystem::path &path, String &error);
            static bool Matches(StringView value, StringView pattern);
            static Vector<TestDesc> BuildTests();

        private:
            EditorApp &_app;
            AutomationBatchOptions _options;
            Vector<TestDesc> _selected_tests;
            Vector<TestResult> _results;
            std::optional<std::future<AutomationResult>> _pending_request;
            std::chrono::steady_clock::time_point _run_started;
            std::chrono::steady_clock::time_point _test_started;
            size_t _current_index = 0u;
            u64 _request_id = 1u;
        Render::ImageCaptureSessionId _capture_session = 0u;
            std::filesystem::path _capture_output_path;
            u64 _graph_capture_session = 0u;
            WString _graph_capture_output_directory;
            size_t _offscreen_resize_stage = 0u;
            u64 _offscreen_resize_wait_frame = 0u;
            bool _capture_saw_pending = false;
            bool _has_unsupported_capability = false;
            bool _is_prepared = false;
            bool _current_started = false;
            bool _is_finished = false;
            int _exit_code = 3;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_BATCH_RUNNER_H__
