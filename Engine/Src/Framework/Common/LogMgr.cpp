#include "pch.h"
#include "Framework/Common/LogMgr.h"

namespace Ailu
{
    namespace Engine
    {
        int LogMgr::Initialize()
        {
            int ret = 0;
            _appenders.push_back(new OutputAppender());
            return ret;
        }
        LogMgr::LogMgr(std::string name, ELogLevel output_level, TraceLevle output_mark) : _name(name),_output_level(output_level),_output_mark(output_mark)
        {

        }
        void LogMgr::Finalize()
        {
            for (auto& logger : _appenders)
                delete logger;
        }
        LogMgr::LogMgr()
        {
            _name = "DefaultLogMgr";
            _output_level = kLogLevel;
            _output_mark = _output_mark;
        }
        void LogMgr::Tick()
        {

        }
        void LogMgr::AddAppender(IAppender* appender)
        {
            _appenders.push_back(appender);
        }
        const std::string& LogMgr::GetOutputPath() const
        {
            return _output_path;
        }
        void LogMgr::SetOutputLevel(ELogLevel level)
        {
            _output_level = level;
        }
        void LogMgr::SetTraceLevel(TraceLevle level)
        {
            _output_mark = level;
        }
        const ELogLevel& LogMgr::GetOutputLevel() const
        {
            return _output_level;
        }
        void LogMgr::Log(std::string msg)
        {
            std::string s = "Normal: ";
            s.append(msg);
            for (auto& appender : _appenders)
            {
                appender->Print(s);
            }
        }
        void LogMgr::Log(std::wstring msg)
        {
            std::wstring s = L"Normal: ";
            s.append(msg);
            for (auto& appender : _appenders)
            {
                appender->Print(msg);
            }
        }
        void LogMgr::LogWarning(std::string msg)
        {
            std::string s = "Warning: ";
            s.append(msg);
            for (auto& appender : _appenders)
            {
                appender->Print(s);
            }
        }
        void LogMgr::LogWarning(std::wstring msg)
        {
            std::wstring s = L"Warning: ";
            s.append(msg);
            for (auto& appender : _appenders)
            {
                appender->Print(msg);
            }
        }
        void LogMgr::LogError(std::string msg)
        {
            std::string s = "Error: ";
            s.append(msg);
            for (auto& appender : _appenders)
            {
                appender->Print(s);
            }
        }
        void LogMgr::LogError(std::wstring msg)
        {
            std::wstring s = L"Error: ";
            s.append(msg);
            for (auto& appender : _appenders)
            {
                appender->Print(msg);
            }
        }
        const TraceLevle& LogMgr::GetTraceLevel() const
        {
            return _output_mark;
        }
    }
}