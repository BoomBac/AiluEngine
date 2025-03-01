#pragma once
#ifndef __LOG_H__
#define __LOG_H__

//#include <Windows.h>
#include <string>
#include <sstream>
#include <format>

#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

#include "Utils.h"
#include <source_location>
#include "Framework/Interface/IRuntimeModule.h"

namespace Ailu
{
	template<typename... Args>
	inline static constexpr bool ContainsString()
	{
		constexpr bool a = (... || std::is_same_v<std::string, std::remove_cvref_t<Args>>);
		constexpr bool b = (... || std::is_same_v<char*, std::remove_cvref_t<Args>>);
		constexpr bool c = (... || std::is_same_v<const char*, std::decay_t<Args>>);//移除数组属性
		return a || b || c;
	}

	template<typename... Args>
	inline static constexpr bool ContainsWString()
	{
		constexpr bool a = (... || std::is_same_v<std::wstring, std::remove_cvref_t<Args>>);
		constexpr bool b = (... || std::is_same_v<wchar_t*, std::remove_cvref_t<Args>>);
		constexpr bool c = (... || std::is_same_v<const wchar_t*, std::decay_t<Args>>);//移除数组属性
		return a || b || c;
		//return false;
	}

	template<typename T>
	static void format_helper(std::wostringstream& oss, std::wstring_view& str, const T& value)
	{
		std::size_t openBracket = str.find('{');
		if (openBracket == std::wstring::npos) { return; }
		std::size_t closeBracket = str.find('}', openBracket + 1);
		if (closeBracket == std::wstring::npos) { return; }
		oss << str.substr(0, openBracket) << value;
		str = str.substr(closeBracket + 1);
	}
	template<typename... Targs>
	static std::wstring format(std::wstring_view str, Targs...args)
	{
		std::wostringstream oss;
		(format_helper(oss, str, args), ...);
		oss << str;
		return oss.str();
	}
	template<typename T>
	static void format_helper(std::ostringstream& oss, std::string_view& str, const T& value)
	{
		std::size_t openBracket = str.find('{');
		if (openBracket == std::string::npos) { return; }
		std::size_t closeBracket = str.find('}', openBracket + 1);
		if (closeBracket == std::string::npos) { return; }
		oss << str.substr(0, openBracket) << value;
		str = str.substr(closeBracket + 1);
	}
	template<typename... Targs>
	static std::string format(std::string_view str, Targs...args)
	{
		std::ostringstream oss;
		(format_helper(oss, str, args), ...);
		oss << str;
		return oss.str();
	}
	static const std::string log_str[]{ "Normal","Warning","Error" };
	static const std::wstring log_strw[]{ L"Normal",L"Warning",L"Error" };

	using TraceLevle = uint8_t;
	constexpr TraceLevle TRACE_ALL = 0;
	constexpr TraceLevle TRACE_1 = 1 << 0;
	constexpr TraceLevle TRACE_2 = 1 << 1;
	constexpr TraceLevle TRACE_3 = 1 << 2;
	enum ELogLevel
	{
		kNormal,
		kWarning,
		kError,
	};
	static constexpr ELogLevel kLogLevel = ELogLevel::kNormal;
	static constexpr TraceLevle kTraceLevel = TRACE_ALL;

	template<typename... Targs>
	static std::wstring BuildLogMsg(ELogLevel level, std::wstring_view str, Targs... args)
	{
        std::wstring s = log_strw[level] + L": ";
		s.append(format(str, args...));
		return s;
	}
	template<typename... Targs>
	static std::string BuildLogMsg(ELogLevel level, std::string_view str, Targs... args)
	{
        std::string s = log_str[level] + ": ";
		s.append(format(str, args...));
		return s;
	}
	template<typename... Targs, std::enable_if_t<!ContainsWString<Targs...>(), bool> = true>
	static void Log(uint8_t maker, ELogLevel level, std::string_view str, Targs... args)
	{
		if (level >= kLogLevel && (maker == 0 || (maker & kTraceLevel) != 0))
		{
			OutputDebugStringA(BuildLogMsg(level, str, args...).append("\r\n").c_str());
		}
	}
	template<typename... Targs, std::enable_if_t<!ContainsString<Targs...>(), bool> = true>
	static void Log(uint8_t maker, ELogLevel level, std::wstring_view str, Targs... args)
	{
		if (level >= kLogLevel && (maker == 0 || (maker & kTraceLevel) != 0))
		{
			OutputDebugStringW(BuildLogMsg(level, str, args...).append(L"\r\n").c_str());
		}
	}
    class AILU_API IAppender
    {
    public:
        virtual void Print(const String &str) = 0;
        virtual void Print(const WString &str) = 0;
    };

    //https://talyian.github.io/ansicolors/
    class ANSIColorful
    {
    public:
        static WString Colorful(const WString& msg)
        {
            u16 block_index = 0;
            if (msg.data()[0] == *L"W")
                block_index = 1;
            else if (msg.data()[0] == *L"E")
                block_index = 2;
            WString s = format_control_blockw[block_index];
            s.append(msg);
            return s;
        }
        static String Colorful(const String& msg)
        {
            u16 block_index = 0;
            if (msg.data()[0] == *"W")
                block_index = 1;
            else if (msg.data()[0] == *"E")
                block_index = 2;
            String s = format_control_block[block_index];
            s.append(msg);
            return s;
        }
    private:
        inline static const std::string format_control_block[]{"\u001b[1;37m", "\x1b[38;5;220m", "\u001b[1;31m"};
        inline static const std::wstring format_control_blockw[]{L"\u001b[1;37m", L"\x1b[38;5;220m", L"\u001b[1;31m"};
    };
    class ConsoleAppender : public IAppender
    {
    public:
        void Print(const String &str) override
        {
            std::cout << ANSIColorful::Colorful(str) << std::endl;
        }
        void Print(const WString &str) override
        {
            std::wcout << ANSIColorful::Colorful(str) << std::endl;
        }
    };
    class FileAppender : public IAppender
    {
    public:
        //static const const String&& GetLogPath() { return s_out_path; }
        inline static const String &s_out_path = GET_ENGINE_FULL_PATH(log.txt);
        void Print(const String &str) override
        {
            std::ofstream out(s_out_path, std::ios_base::app);
            if (out.is_open())
            {
                out << str.substr(7, str.size() - 7) << std::endl;
                out.close();
            }
        }
        void Print(const WString &str) override
        {
        }
    };
    class OutputAppender : public IAppender
    {
    public:
        OutputAppender() = default;
        void Print(const String &str) override
        {
            OutputDebugStringA((ANSIColorful::Colorful(str) + "\r\n").c_str());
        }
        void Print(const WString &str)
        {
            OutputDebugString((ANSIColorful::Colorful(str) + L"\r\n").c_str());
        }
    };


    class AILU_API LogMgr : public IRuntimeModule
    {
    private:
        std::vector<IAppender *> _appenders;
        ELogLevel _output_level;
        TraceLevle _output_mark;
        std::string _output_path;
        std::string _name;

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
        void Log(std::string msg);
        void Log(std::wstring msg);
        void LogWarning(std::string msg);
        void LogWarning(std::wstring msg);
        void LogError(std::string msg);
        void LogError(std::wstring msg);

        template<typename... Args, std::enable_if_t<!ContainsWString<Args...>(), bool> = true>
        void Log(std::string_view msg, Args &&...args)
        {
            for (auto &appender: _appenders)
            {
                appender->Print(BuildLogMsg(_output_level, msg, args...));
            }
        }
        template<typename... Args, std::enable_if_t<!ContainsWString<Args...>(), bool> = true>
        void LogFormat(std::string_view msg, Args &&...args)
        {
            for (auto &appender: _appenders)
            {
                appender->Print(BuildLogMsg(_output_level, msg, args...));
            }
        }
        template<typename... Args, std::enable_if_t<!ContainsWString<Args...>(), bool> = true>
        void LogErrorFormat(std::string_view msg, Args &&...args)
        {
            for (auto &appender: _appenders)
            {
                appender->Print(BuildLogMsg(ELogLevel::kError, msg, args...));
            }
        }

        template<typename... Args, std::enable_if_t<!ContainsWString<Args...>(), bool> = true>
        void LogErrorFormat(const std::source_location &loc, std::string_view msg, Args &&...args)
        {
            String new_msg = BuildLogMsg(ELogLevel::kError, msg, args...);
            new_msg.append(std::format("\n  File: {},Line: {};\n    Function: {}", loc.file_name(), loc.line(), loc.function_name()));
            LogErrorFormat("{}", new_msg);
        }

        template<typename... Args, std::enable_if_t<!ContainsString<Args...>(), bool> = true>
        void LogErrorFormat(std::wstring_view msg, Args &&...args)
        {
            for (auto &appender: _appenders)
            {
                appender->Print(BuildLogMsg(ELogLevel::kError, msg, args...));
            }
        }

        template<typename... Args, std::enable_if_t<!ContainsString<Args...>(), bool> = true>
        void LogErrorFormat(const std::source_location &loc, std::wstring_view msg, Args &&...args)
        {
            WString new_msg = BuildLogMsg(ELogLevel::kError, msg, args...);
            new_msg.append(std::format(L"\n  File: {},Line: {};\n    Function: {}", ToWStr(loc.file_name()), loc.line(), ToWStr(loc.function_name())));
            LogErrorFormat(L"{}", new_msg);
        }

        template<typename... Args, std::enable_if_t<!ContainsWString<Args...>(), bool> = true>
        void LogWarningFormat(std::string_view msg, Args &&...args)
        {
            for (auto &appender: _appenders)
            {
                appender->Print(BuildLogMsg(ELogLevel::kWarning, msg, args...));
            }
        }

        template<typename... Args, std::enable_if_t<!ContainsString<Args...>(), bool> = true>
        void LogWarningFormat(std::wstring_view msg, Args &&...args)
        {
            for (auto &appender: _appenders)
            {
                appender->Print(BuildLogMsg(ELogLevel::kWarning, msg, args...));
            }
        }

        template<typename... Args, std::enable_if_t<!ContainsString<Args...>(), bool> = true>
        void LogFormat(std::wstring_view msg, Args &&...args)
        {
            for (auto &appender: _appenders)
            {
                appender->Print(BuildLogMsg(_output_level, msg, args...));
            }
        }
        template<typename... Args, std::enable_if_t<!ContainsWString<Args...>(), bool> = true>
        void LogFormat(ELogLevel level, std::string_view msg, Args &&...args)
        {
            if (level <= _output_level) return;
            for (auto &appender: _appenders)
                appender->Print(BuildLogMsg(level, msg, args...));
        }
        template<typename... Args, std::enable_if_t<!ContainsString<Args...>(), bool> = true>
        void LogFormat(ELogLevel level, std::wstring_view msg, Args &&...args)
        {
            if (level <= _output_level) return;
            for (auto &appender: _appenders)
                appender->Print(BuildLogMsg(level, msg, args...));
        }
    };
	extern AILU_API LogMgr* g_pLogMgr;
#define AE_LOG(maker,Level,msg,...) Log(maker,Level,msg,##__VA_ARGS__);
//#define LOG_INFO(msg,...) Log(TRACE_ALL,ELogLevel::kNormal,msg,##__VA_ARGS__);
//#define LOG_WARNING(msg,...) Log(TRACE_ALL,ELogLevel::kWarning,msg,##__VA_ARGS__);
//#define LOG_ERROR(msg,...) Log(TRACE_ALL,ELogLevel::kError,msg,##__VA_ARGS__);
#define LOG_ERROR(msg,...) g_pLogMgr->LogErrorFormat(msg,##__VA_ARGS__);
#define LOG_WARNING(msg,...) g_pLogMgr->LogWarningFormat(msg,##__VA_ARGS__);
#define LOG_INFO(msg,...) g_pLogMgr->LogFormat(msg,##__VA_ARGS__);
}

#endif // !LOG_H__

