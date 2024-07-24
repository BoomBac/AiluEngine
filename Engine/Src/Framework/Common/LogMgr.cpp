#include "Framework/Common/LogMgr.h"
#include "pch.h"

namespace Ailu
{
    int LogMgr::Initialize()
    {
        int ret = 0;
        _appenders.push_back(new OutputAppender());
        // Open the file in truncate mode (clears the content).
        std::ofstream file(FileAppender::s_out_path, std::ofstream::out | std::ofstream::trunc);

        if (file.is_open())
        {
            file.close();
            std::cout << "File content cleared and closed." << std::endl;
        }
        else
            std::cerr << "Failed to open the file for clearing." << std::endl;
        return ret;
    }
    LogMgr::LogMgr(std::string name, ELogLevel output_level, TraceLevle output_mark) : _name(name), _output_level(output_level), _output_mark(output_mark)
    {
    }
    void LogMgr::Finalize()
    {
        for (auto &logger: _appenders)
        {
            DESTORY_PTR(logger);
        }
    }
    void LogMgr::Tick(const float &delta_time)
    {
    }
    LogMgr::LogMgr()
    {
        _name = "DefaultLogMgr";
        _output_level = kLogLevel;
        _output_mark = _output_mark;
    }

    void LogMgr::AddAppender(IAppender *appender)
    {
        _appenders.push_back(appender);
    }
    const std::string &LogMgr::GetOutputPath() const
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
    const ELogLevel &LogMgr::GetOutputLevel() const
    {
        return _output_level;
    }
    void LogMgr::Log(std::string msg)
    {
        std::string s = BuildLogMsg(ELogLevel::kNormal, msg);
        for (auto &appender: _appenders)
        {
            appender->Print(s);
        }
    }
    void LogMgr::Log(std::wstring msg)
    {
        std::wstring s = BuildLogMsg(ELogLevel::kNormal, msg);
        for (auto &appender: _appenders)
        {
            appender->Print(s);
        }
    }
    void LogMgr::LogWarning(std::string msg)
    {
        std::string s = BuildLogMsg(ELogLevel::kWarning, msg);
        for (auto &appender: _appenders)
        {
            appender->Print(s);
        }
    }
    void LogMgr::LogWarning(std::wstring msg)
    {
        std::wstring s = BuildLogMsg(ELogLevel::kWarning, msg);
        for (auto &appender: _appenders)
        {
            appender->Print(s);
        }
    }
    void LogMgr::LogError(std::string msg)
    {
        std::string s = BuildLogMsg(ELogLevel::kError, msg);
        for (auto &appender: _appenders)
        {
            appender->Print(s);
        }
    }
    void LogMgr::LogError(std::wstring msg)
    {
        std::wstring s = BuildLogMsg(ELogLevel::kNormal, msg);
        for (auto &appender: _appenders)
        {
            appender->Print(s);
        }
    }
    const TraceLevle &LogMgr::GetTraceLevel() const
    {
        return _output_mark;
    }
}// namespace Ailu