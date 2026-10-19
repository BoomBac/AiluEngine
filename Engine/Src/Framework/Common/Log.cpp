#include "Framework/Common/Log.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Utils.h"
#include "pch.h"

namespace Ailu
{
    std::wstring ANSIColorful(const LogMessage &log_msg)
    {
        //https://talyian.github.io/ansicolors/
        const wchar_t *color = L"\u001b[1;37m";
        switch (log_msg._level)
        {
            case ELogLevel::kWarning: color = L"\x1b[38;5;220m"; break;
            case ELogLevel::kError: color = L"\u001b[1;31m"; break;
            case ELogLevel::kFatal: color = L"\u001b[1;41m"; break;
            case ELogLevel::kTrace:
            case ELogLevel::kDebug:
            case ELogLevel::kInfo:
            default: break;
        }
        std::wstring msg = color;
        msg.append(log_msg.ToString());
        return msg;
    }

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
    static LogMgr* g_LogMgr = nullptr;
    std::mutex FileAppender::s_output_mutex;
    void LogMgr::Init()
    {
        AL_ASSERT(g_LogMgr == nullptr);
        g_LogMgr = new LogMgr;
        g_LogMgr->Initialize();
    }
    void LogMgr::Shutdown()
    {
        AL_ASSERT(g_LogMgr != nullptr);
        g_LogMgr->Finalize();
        delete g_LogMgr;
        g_LogMgr = nullptr;
    }
    LogMgr &LogMgr::Get()
    {
        return *g_LogMgr;
    }

    int LogMgr::Initialize()
    {
        int ret = 0;
        _appenders.push_back(new OutputAppender);
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
    LogMgr::LogMgr(std::string name, ELogLevel output_level, TraceLevle output_mark)
        : _output_level(output_level), _output_mark(output_mark), _name(std::move(name))
    {
    }
    void LogMgr::Finalize()
    {
        std::lock_guard output_lock(_output_mutex);
        std::lock_guard appender_lock(_appender_mutex);
        for (auto *logger: _appenders)
        {
            delete logger;
        }
        _appenders.clear();
    }

    LogMgr::LogMgr()
    {
        _name = "DefaultLogMgr";
        _output_level = kLogLevel;
        _output_mark = kTraceLevel;
    }
    void LogMgr::AddAppender(IAppender *appender)
    {
        std::lock_guard lock(_appender_mutex);
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
    bool LogMgr::IsEnabled(ELogLevel level) const
    {
        return level >= _output_level.load(std::memory_order_relaxed);
    }
    bool LogMgr::IsEnabled(ELogLevel level, ELogModule module) const
    {
        if (IsEnabled(level))
            return true;
        if (level > ELogLevel::kDebug)
            return false;
        const u32 module_bit = 1u << static_cast<u32>(module);
        return (_module_diagnostics_mask.load(std::memory_order_relaxed) & module_bit) != 0u;
    }
    void LogMgr::SetModuleDiagnostics(ELogModule module, bool enabled)
    {
        const u32 module_bit = 1u << static_cast<u32>(module);
        if (enabled)
            _module_diagnostics_mask.fetch_or(module_bit, std::memory_order_relaxed);
        else
            _module_diagnostics_mask.fetch_and(~module_bit, std::memory_order_relaxed);
    }
    ELogLevel LogMgr::GetOutputLevel() const
    {
        return _output_level.load(std::memory_order_relaxed);
    }
    TraceLevle LogMgr::GetTraceLevel() const
    {
        return _output_mark.load(std::memory_order_relaxed);
    }
    void LogMgr::LogMsgImpl(const LogMessage &msg)
    {
        std::lock_guard output_lock(_output_mutex);
        std::vector<IAppender *> appenders;
        {
            std::lock_guard appender_lock(_appender_mutex);
            appenders = _appenders;
        }
        for (auto *appender: appenders)
        {
            appender->Print(msg);
        }
        // Core::ThreadPool::Get().Enqueue([=](){
        //     for (auto &appender: _appenders)
        //     {
        //         if (msg._level >= _output_level)
        //         {
        //             appender->Print(msg);
        //         }
        //     }
        // });
    }

    FileAppender::FileAppender() : _output(s_out_path, std::ios_base::out | std::ios_base::app)
    {
    }
    FileAppender::~FileAppender()
    {
        std::lock_guard lock(s_output_mutex);
        if (_output.is_open())
        {
            _output.flush();
            _output.close();
        }
    }
    void FileAppender::Print(const LogMessage &log_msg)
    {
        std::lock_guard lock(s_output_mutex);
        if (!_output.is_open()) { return; }
        _output << log_msg.ToDetailString() << L'\n';
        if (log_msg._level >= ELogLevel::kError) { _output.flush(); }
    }
#pragma endregion
}// namespace Ailu
