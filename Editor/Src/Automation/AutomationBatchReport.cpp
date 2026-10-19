#include "Automation/AutomationBatchReport.h"

#include "Automation/AutomationJson.h"
#include "Framework/Common/Utils.h"

#include <filesystem>
#include <fstream>

namespace Ailu
{
    namespace Editor
    {
        bool AutomationBatchReport::WriteStartupFailure(const WString &report_path,
                                                        const WString &run_id,
                                                        const String &requested_tests,
                                                        const String &graphics_mode,
                                                        const String &error_code,
                                                        const String &error_message)
        {
            if (report_path.empty())
                return false;

            const std::filesystem::path path(report_path);
            std::error_code filesystem_error;
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
                return false;

            AutomationObject summary;
            summary.emplace("passed", AutomationValue(0u));
            summary.emplace("failed", AutomationValue(0u));
            summary.emplace("skipped", AutomationValue(0u));
            summary.emplace("error", AutomationValue(1u));
            summary.emplace("timeout", AutomationValue(0u));

            AutomationObject error;
            error.emplace("code", AutomationValue(error_code));
            error.emplace("message", AutomationValue(error_message));

            AutomationArray artifacts;
            artifacts.emplace_back(AutomationValue("events.jsonl"));
            artifacts.emplace_back(AutomationValue("engine.log"));

            AutomationObject report;
            report.emplace("schema_version", AutomationValue(1));
            report.emplace("run_id", AutomationValue(ToChar(run_id)));
            report.emplace("requested_tests", AutomationValue(requested_tests));
            report.emplace("graphics_mode", AutomationValue(graphics_mode));
            report.emplace("status", AutomationValue("error"));
            report.emplace("duration_ms", AutomationValue(0u));
            report.emplace("summary", AutomationValue(std::move(summary)));
            report.emplace("tests", AutomationValue(AutomationArray{}));
            report.emplace("error", AutomationValue(std::move(error)));
            report.emplace("artifacts", AutomationValue(std::move(artifacts)));

            const std::filesystem::path temporary_path = path.wstring() + L".tmp";
            std::ofstream output(temporary_path, std::ios::out | std::ios::trunc);
            if (!output)
                return false;
            output << AutomationJson::Write(AutomationValue(std::move(report)));
            output.flush();
            output.close();
            if (!output.good())
                return false;

            std::filesystem::remove(path, filesystem_error);
            filesystem_error.clear();
            std::filesystem::rename(temporary_path, path, filesystem_error);
            if (filesystem_error)
                return false;

            std::ofstream events(path.parent_path() / "events.jsonl", std::ios::out | std::ios::trunc);
            std::ofstream log(path.parent_path() / "engine.log", std::ios::out | std::ios::trunc);
            if (!events || !log)
                return false;
            AutomationObject event;
            event.emplace("run_id", AutomationValue(ToChar(run_id)));
            event.emplace("event", AutomationValue("startup_failed"));
            event.emplace("error", AutomationValue(error_code));
            events << AutomationJson::Write(AutomationValue(std::move(event))) << '\n';
            log << "batch startup failed: " << error_code << ": " << error_message << '\n';
            events.flush();
            log.flush();
            return events.good() && log.good();
        }
    }// namespace Editor
}// namespace Ailu
