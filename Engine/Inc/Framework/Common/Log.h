#pragma once
#ifndef __LOG_H__
#define __LOG_H__
#pragma warning(disable:4251)

//#include <Windows.h>
#include <chrono>
#include <atomic>
#include <format>
#include <mutex>
#include <string>

#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "Utils.h"
#include <source_location>

namespace Ailu
{
    template<typename... Args>
    inline static constexpr bool ContainsString()
    {
        constexpr bool a = (... || std::is_same_v<std::string, std::remove_cvref_t<Args>>);
        constexpr bool b = (... || std::is_same_v<char *, std::remove_cvref_t<Args>>);
        constexpr bool c = (... || std::is_same_v<const char *, std::decay_t<Args>>);//移除数组属性
        return a || b || c;
    }

    template<typename... Args>
    inline static constexpr bool ContainsWString()
    {
        constexpr bool a = (... || std::is_same_v<std::wstring, std::remove_cvref_t<Args>>);
        constexpr bool b = (... || std::is_same_v<wchar_t *, std::remove_cvref_t<Args>>);
        constexpr bool c = (... || std::is_same_v<const wchar_t *, std::decay_t<Args>>);//移除数组属性
        return a || b || c;
        //return false;
    }

    template<typename... Targs>
    static std::wstring format_str(std::wstring_view str, Targs... args)
    {
        return std::vformat(str, std::make_wformat_args(args...));
    }
    template<typename... Targs>
    static std::string format_str(std::string_view str, Targs... args)
    {
        return std::vformat(str, std::make_format_args(args...));
    }

    using TraceLevle = uint8_t;
    constexpr TraceLevle TRACE_ALL = 0;
    constexpr TraceLevle TRACE_1 = 1 << 0;
    constexpr TraceLevle TRACE_2 = 1 << 1;
    constexpr TraceLevle TRACE_3 = 1 << 2;
    enum ELogLevel
    {
        kTrace = -2,
        kDebug = -1,
        kInfo = 0,
        kWarning = 1,
        kError = 2,
        kFatal = 3
    };
    static constexpr ELogLevel kLogLevel = ELogLevel::kInfo;
    static constexpr TraceLevle kTraceLevel = TRACE_ALL;

    enum class ELogModule : u8
    {
        kGeneral,
        kAsset,
        kShader,
        kRHI,
        kEditor
    };

    class AILU_API LogMessage
    {
    public:
        static std::wstring LogLevelToString(ELogLevel level)
        {
            switch (level)
            {
                case ELogLevel::kInfo:
                    return L"INFO";
                case ELogLevel::kWarning:
                    return L"WARNING";
                case ELogLevel::kError:
                    return L"ERROR";
                case ELogLevel::kFatal:
                    return L"FATAL";
                default:
                    return L"UNKNOWN";
            }
        }
        LogMessage(ELogLevel level, const std::string &message,const std::string &file, int line,const std::string &function);
        LogMessage(ELogLevel level, const std::wstring &message,const std::string &file, int line,const std::string &function);
        /// @brief 只包含线程名称，日志级别和内容
        /// @return 
        std::wstring ToString() const;
        /// @brief 包含时间，线程名称，日志级别，文件名，行号，函数名和内容
        std::wstring ToDetailString() const;
    public:
        ELogLevel _level;
        std::wstring _message;
        std::string _file;
        int _line;
        std::string _function;
        std::thread::id _thread_id;
        std::string _time_str;
        std::string _thread_name;
    private:
        std::chrono::system_clock::time_point _timestamp;
    };

    class AILU_API IAppender
    {
    public:
        virtual ~IAppender() = default;
        virtual void Print(const LogMessage &log_msg) = 0;
    };

    AILU_API std::wstring ANSIColorful(const LogMessage &log_msg);
    class ConsoleAppender : public IAppender
    {
    public:
        void Print(const LogMessage &log_msg) override
        {
            std::wcout << ANSIColorful(log_msg) << std::endl;
        }
    };
    class AILU_API FileAppender : public IAppender
    {
    public:
        inline static const String &s_out_path = "FileAppender_log.txt";
        FileAppender();
        ~FileAppender() override;
        void Print(const LogMessage &log_msg) override;
    private:
        static std::mutex s_output_mutex;
        std::wofstream _output;
    };
    class OutputAppender : public IAppender
    {
    public:
        OutputAppender() = default;
        void Print(const LogMessage &log_msg) override
        {
            OutputDebugString((ANSIColorful(log_msg) + L"\r\n").c_str());
        }
    };


    class AILU_API LogMgr
    {
    public:
        static void Init();
        static void Shutdown();
        static LogMgr& Get();
        LogMgr();
        LogMgr(std::string name, ELogLevel output_level = kLogLevel, TraceLevle output_mark = kTraceLevel);
        int Initialize();
        void Finalize();
        void AddAppender(IAppender *appender);
        void SetOutputLevel(ELogLevel level);
        void SetTraceLevel(TraceLevle trace);
        bool IsEnabled(ELogLevel level) const;
        bool IsEnabled(ELogLevel level, ELogModule module) const;
        void SetModuleDiagnostics(ELogModule module, bool enabled);
        const std::string &GetOutputPath() const;
        ELogLevel GetOutputLevel() const;
        TraceLevle GetTraceLevel() const;
        const std::vector<IAppender *> &GetAppenders() const { return _appenders; };
        template<typename... Args, std::enable_if_t<!ContainsWString<Args...>(), bool> = true>
        void LogMsg(ELogLevel level,const std::source_location &loc,const std::string& func_name,const std::string& msg, Args &&...args)
        {
            if (!IsEnabled(level)) { return; }
            try
            {
                LogMsgImpl(LogMessage(level, format_str(msg, args...), loc.file_name(), loc.line(), func_name));
            }
            catch (const std::format_error &error)
            {
                LogMsgImpl(LogMessage(level, std::string("[format error: ") + error.what() + "] " + msg,
                                      loc.file_name(), loc.line(), func_name));
            }
        }
        template<typename... Args, std::enable_if_t<!ContainsString<Args...>(), bool> = true>
        void LogMsg(ELogLevel level,const std::source_location &loc,const std::string& func_name,const std::wstring& msg, Args &&...args)
        {
            if (!IsEnabled(level)) { return; }
            try
            {
                LogMsgImpl(LogMessage(level, format_str(msg, args...), loc.file_name(), loc.line(), func_name));
            }
            catch (const std::format_error &error)
            {
                LogMsgImpl(LogMessage(level, L"[format error: " + ToWChar(error.what()) + L"] " + msg,
                                      loc.file_name(), loc.line(), func_name));
            }
        }
        template<typename... Args, std::enable_if_t<!ContainsWString<Args...>(), bool> = true>
        void LogMsgModule(ELogModule module, ELogLevel level, const std::source_location &loc,
                          const std::string &func_name, const std::string &msg, Args &&...args)
        {
            if (!IsEnabled(level, module)) { return; }
            try
            {
                LogMsgImpl(LogMessage(level, format_str(msg, args...), loc.file_name(), loc.line(), func_name));
            }
            catch (const std::format_error &error)
            {
                LogMsgImpl(LogMessage(level, std::string("[format error: ") + error.what() + "] " + msg,
                                      loc.file_name(), loc.line(), func_name));
            }
        }
        template<typename... Args, std::enable_if_t<!ContainsString<Args...>(), bool> = true>
        void LogMsgModule(ELogModule module, ELogLevel level, const std::source_location &loc,
                          const std::string &func_name, const std::wstring &msg, Args &&...args)
        {
            if (!IsEnabled(level, module)) { return; }
            try
            {
                LogMsgImpl(LogMessage(level, format_str(msg, args...), loc.file_name(), loc.line(), func_name));
            }
            catch (const std::format_error &error)
            {
                LogMsgImpl(LogMessage(level, L"[format error: " + ToWChar(error.what()) + L"] " + msg,
                                      loc.file_name(), loc.line(), func_name));
            }
        }
    private:
        void LogMsgImpl(const LogMessage& log_msg);
    private:
        std::vector<IAppender *> _appenders;
        std::atomic<ELogLevel> _output_level;
        std::atomic<TraceLevle> _output_mark;
        std::atomic<u32> _module_diagnostics_mask{0u};
        std::string _output_path;
        std::string _name;
        mutable std::mutex _appender_mutex;
        mutable std::mutex _output_mutex;
    };
#define AE_LOG(maker, Level, msg, ...) do { auto &log_mgr = LogMgr::Get(); if (log_mgr.IsEnabled(Level)) { log_mgr.LogMsg(Level, std::source_location::current(), __FUNCTION__, msg, ##__VA_ARGS__); } } while (false);
#define LOG_TRACE(msg, ...)   do { auto &log_mgr = LogMgr::Get(); if (log_mgr.IsEnabled(ELogLevel::kTrace)) { log_mgr.LogMsg(ELogLevel::kTrace, std::source_location::current(), __FUNCTION__, msg, ##__VA_ARGS__); } } while (false);
#define LOG_DEBUG(msg, ...)   do { auto &log_mgr = LogMgr::Get(); if (log_mgr.IsEnabled(ELogLevel::kDebug)) { log_mgr.LogMsg(ELogLevel::kDebug, std::source_location::current(), __FUNCTION__, msg, ##__VA_ARGS__); } } while (false);
#define LOG_INFO(msg, ...)    do { auto &log_mgr = LogMgr::Get(); if (log_mgr.IsEnabled(ELogLevel::kInfo)) { log_mgr.LogMsg(ELogLevel::kInfo, std::source_location::current(), __FUNCTION__, msg, ##__VA_ARGS__); } } while (false);
#define LOG_WARNING(msg, ...) do { auto &log_mgr = LogMgr::Get(); if (log_mgr.IsEnabled(ELogLevel::kWarning)) { log_mgr.LogMsg(ELogLevel::kWarning, std::source_location::current(), __FUNCTION__, msg, ##__VA_ARGS__); } } while (false);
#define LOG_ERROR(msg, ...)   do { auto &log_mgr = LogMgr::Get(); if (log_mgr.IsEnabled(ELogLevel::kError)) { log_mgr.LogMsg(ELogLevel::kError, std::source_location::current(), __FUNCTION__, msg, ##__VA_ARGS__); } } while (false);
#define LOG_FATAL(msg, ...)   do { auto &log_mgr = LogMgr::Get(); if (log_mgr.IsEnabled(ELogLevel::kFatal)) { log_mgr.LogMsg(ELogLevel::kFatal, std::source_location::current(), __FUNCTION__, msg, ##__VA_ARGS__); } } while (false);
#define LOG_MODULE_TRACE(Module, msg, ...) do { auto &log_mgr = LogMgr::Get(); if (log_mgr.IsEnabled(ELogLevel::kTrace, Module)) { log_mgr.LogMsgModule(Module, ELogLevel::kTrace, std::source_location::current(), __FUNCTION__, msg, ##__VA_ARGS__); } } while (false);
#define LOG_MODULE_DEBUG(Module, msg, ...) do { auto &log_mgr = LogMgr::Get(); if (log_mgr.IsEnabled(ELogLevel::kDebug, Module)) { log_mgr.LogMsgModule(Module, ELogLevel::kDebug, std::source_location::current(), __FUNCTION__, msg, ##__VA_ARGS__); } } while (false);
// Keep the public logging macros stable for existing call sites.
}// namespace Ailu

#endif// !LOG_H__
