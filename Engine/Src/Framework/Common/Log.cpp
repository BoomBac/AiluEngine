#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Utils.h"
#include "pch.h"
#include <sstream>

namespace Ailu
{
#pragma region LogMessage
    LogMessage::LogMessage(ELogLevel level, const std::wstring &message, const std::string &file, int line, const std::string &function)
        : _level(level), _message(message), _file(file), _line(line), _function(function)
    {
        _timestamp = std::chrono::system_clock::now();
        _time_str = TimeMgr::CurrentTime();
        _thread_name = GetThreadName();
    }
    LogMessage::LogMessage(ELogLevel level, const std::string &message, const std::string &file, int line, const std::string &function)
        : _level(level), _message(ToWChar(message)), _file(file), _line(line), _function(function)
    {
        _timestamp = std::chrono::system_clock::now();
        _time_str = TimeMgr::CurrentTime();
        _thread_name = GetThreadName();
    }
    std::wstring LogMessage::ToString() const
    {
        return std::format(L"[{}] [{}] {}", ToWChar(_thread_name), LogLevelToString(_level), _message);
    }
    std::wstring LogMessage::ToDetailString() const
    {
        return std::format(L"[{}] [{}] [{}] [{}:{} ({})] {}", ToWChar(TimeMgr::CurrentTime()),
                           ToWChar(_thread_name), LogLevelToString(_level), ToWChar(_file), _line, ToWChar(_function), _message);
    }
#pragma endregion

#pragma region LogMgr
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
    void LogMgr::Tick(f32 delta_time)
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
    const TraceLevle &LogMgr::GetTraceLevel() const
    {
        return _output_mark;
    }
    void LogMgr::LogMsgImpl(const LogMessage &msg)
    {
        for (auto &appender: _appenders)
        {
            if (msg._level >= _output_level)
            {
                appender->Print(msg);
            }
        }
        // g_pThreadTool->Enqueue([=](){
        //     for (auto &appender: _appenders)
        //     {
        //         if (msg._level >= _output_level)
        //         {
        //             appender->Print(msg);
        //         }
        //     }
        // });
    }
#pragma endregion
}// namespace Ailu