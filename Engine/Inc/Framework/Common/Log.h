#pragma once
#ifndef __LOG_H__
#define __LOG_H__

#include <Windows.h>
#include <string>
#include <sstream>
#include <format>

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
	static const std::string format_control_block[] {"\u001b[1;37m","\u001b[1;33m","\u001b[1;31m"};
	static const std::wstring format_control_blockw[] {L"\u001b[1;37m",L"\u001b[1;33m",L"\u001b[1;31m"};
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
		std::wstring s = format_control_blockw[level];
		s.append(log_strw[level] + L": ");
		s.append(format(str, args...));
		return s;
	}
	template<typename... Targs>
	static std::string BuildLogMsg(ELogLevel level, std::string_view str, Targs... args)
	{
		std::string s = format_control_block[level];
		s.append(log_str[level] + ": ");
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
#define AE_LOG(maker,Level,msg,...) Log(maker,Level,msg,##__VA_ARGS__);
#define LOG_INFO(msg,...) Log(TRACE_ALL,ELogLevel::kNormal,msg,##__VA_ARGS__);
#define LOG_WARNING(msg,...) Log(TRACE_ALL,ELogLevel::kWarning,msg,##__VA_ARGS__);
#define LOG_ERROR(msg,...) Log(TRACE_ALL,ELogLevel::kError,msg,##__VA_ARGS__);
	//extern AILU_API class LogMgr* g_pLogMgr;
}

#endif // !LOG_H__

