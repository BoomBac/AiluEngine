#pragma once
#ifndef __LOG_H__
#define __LOG_H__

//#include <Windows.h>
#include <format>
#include <sstream>
#include <string>

#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

#include "Framework/Interface/IRuntimeModule.h"
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

    template<typename T>
    static void format_helper(std::wostringstream &oss, std::wstring_view &str, const T &value)
    {
        std::size_t openBracket = str.find('{');
        if (openBracket == std::wstring::npos) { return; }
        std::size_t closeBracket = str.find('}', openBracket + 1);
        if (closeBracket == std::wstring::npos) { return; }
        oss << str.substr(0, openBracket) << value;
        str = str.substr(closeBracket + 1);
    }
    template<typename... Targs>
    static std::wstring format_str(std::wstring_view str, Targs... args)
    {
        std::wostringstream oss;
        (format_helper(oss, str, args), ...);
        oss << str;
        return oss.str();
    }
    template<typename T>
    static void format_helper(std::ostringstream &oss, std::string_view &str, const T &value)
    {
        std::size_t openBracket = str.find('{');
        if (openBracket == std::string::npos) { return; }
        std::size_t closeBracket = str.find('}', openBracket + 1);
        if (closeBracket == std::string::npos) { return; }
        oss << str.substr(0, openBracket) << value;
        str = str.substr(closeBracket + 1);
    }
    template<typename... Targs>
    static std::string format_str(std::string_view str, Targs... args)
    {
        std::ostringstream oss;
        (format_helper(oss, str, args), ...);
        oss << str;
        return oss.str();
    }

    using TraceLevle = uint8_t;
    constexpr TraceLevle TRACE_ALL = 0;
    constexpr TraceLevle TRACE_1 = 1 << 0;
    constexpr TraceLevle TRACE_2 = 1 << 1;
    constexpr TraceLevle TRACE_3 = 1 << 2;
    enum ELogLevel
    {
        kInfo,
        kWarning,
        kError,
        kFatal
    };
    static constexpr ELogLevel kLogLevel = ELogLevel::kInfo;
    static constexpr TraceLevle kTraceLevel = TRACE_ALL;

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
        virtual void Print(const LogMessage &log_msg) = 0;
    };

    static std::wstring ANSIColorful(const LogMessage &log_msg)
    {
        //https://talyian.github.io/ansicolors/
        // ANSI 控制码颜色：Info, Warning, Error, Fatal
        static const std::wstring kFormatControlBlock[] = {
            L"\u001b[1;37m",     // kInfo: 高亮白
            L"\x1b[38;5;220m",   // kWarning: 黄色
            L"\u001b[1;31m",     // kError: 高亮红
            L"\u001b[1;41m"      // kFatal: 红底白字
        };
        std::wstring msg = kFormatControlBlock[log_msg._level];
        msg.append(log_msg.ToString());
        return msg;
    }
    class ConsoleAppender : public IAppender
    {
    public:
        void Print(const LogMessage &log_msg) override
        {
            std::wcout << ANSIColorful(log_msg) << std::endl;
        }
    };
    class FileAppender : public IAppender
    {
    public:
        //static const const String&& GetLogPath() { return s_out_path; }
        inline static const String &s_out_path = GET_ENGINE_FULL_PATH(log.txt);
        void Print(const LogMessage &log_msg) override
        {
            std::wofstream out(s_out_path, std::ios_base::app);
            if (out.is_open())
            {
                out << log_msg.ToDetailString() << std::endl;
                out.close();
            }
        }
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


    class AILU_API LogMgr : public IRuntimeModule
    {
    public:
        LogMgr();
        LogMgr(std::string name, ELogLevel output_level = kLogLevel, TraceLevle output_mark = kTraceLevel);
        int Initialize() override;
        void Finalize() override;
        void Tick(f32 delta_time) override;
        void AddAppender(IAppender *appender);
        void SetOutputLevel(ELogLevel level);
        void SetTraceLevel(TraceLevle trace);
        const std::string &GetOutputPath() const;
        const ELogLevel &GetOutputLevel() const;
        const TraceLevle &GetTraceLevel() const;
        const std::vector<IAppender *> &GetAppenders() const { return _appenders; };
        template<typename... Args, std::enable_if_t<!ContainsWString<Args...>(), bool> = true>
        void LogMsg(ELogLevel level,const std::source_location &loc,const std::string& func_name,const std::string& msg, Args &&...args)
        {
            LogMsgImpl(LogMessage(level,format_str(msg,args...),loc.file_name(),loc.line(),func_name));
        }
        template<typename... Args, std::enable_if_t<!ContainsString<Args...>(), bool> = true>
        void LogMsg(ELogLevel level,const std::source_location &loc,const std::string& func_name,const std::wstring& msg, Args &&...args)
        {
            LogMsgImpl(LogMessage(level,format_str(msg,args...),loc.file_name(),loc.line(),func_name));
        }
    private:
        void LogMsgImpl(const LogMessage& log_msg);
    private:
        std::vector<IAppender *> _appenders;
        ELogLevel _output_level;
        TraceLevle _output_mark;
        std::string _output_path;
        std::string _name;
    };
    extern AILU_API LogMgr *g_pLogMgr;
#define AE_LOG(maker, Level, msg, ...) g_pLogMgr->LogMsg(Level,std::source_location::current(),__FUNCTION__,msg, ##__VA_ARGS__);
#define LOG_INFO(msg, ...)    g_pLogMgr->LogMsg(ELogLevel::kInfo,std::source_location::current(),__FUNCTION__,msg, ##__VA_ARGS__);
#define LOG_WARNING(msg, ...) g_pLogMgr->LogMsg(ELogLevel::kWarning,std::source_location::current(),__FUNCTION__,msg, ##__VA_ARGS__);
#define LOG_ERROR(msg, ...)   g_pLogMgr->LogMsg(ELogLevel::kError,std::source_location::current(),__FUNCTION__,msg, ##__VA_ARGS__);
#define LOG_FATAL(msg,...)    g_pLogMgr->LogMsg(ELogLevel::kFatal,std::source_location::current(),__FUNCTION__,msg, ##__VA_ARGS__);
}// namespace Ailu

#endif// !LOG_H__