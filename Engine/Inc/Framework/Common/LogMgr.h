#pragma once
#ifndef __LOG_MGR_H__
#define __LOG_MGR_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/Log.h"

namespace Ailu
{
	class IAppender
	{
	public:
		virtual void Print(std::string str) = 0;
		virtual void Print(std::wstring str) = 0;
	};
	class ConsoleAppender : public IAppender
	{
	public:
		void Print(std::string str) override
		{
			std::cout << str.c_str() << std::endl;
		}
	};
	class FileAppender : public IAppender
	{
	public:
		//static const std::string& GetLogPath() { return s_out_path; }
		inline static std::string s_out_path = GET_ENGINE_FULL_PATH(log.txt);
		void Print(std::string str) override
		{
			std::ofstream out(s_out_path, std::ios_base::app);
			if (out.is_open())
			{
				out << str << std::endl;
				out.close();
			}
		}
		void Print(std::wstring str) override
		{

		}
	};
	class OutputAppender : public IAppender
	{
	public:
		OutputAppender() = default;
		void Print(std::string str) override
		{
			str.append("\r\n");
			OutputDebugStringA(str.c_str());
		}
		void Print(std::wstring str)
		{
			str.append(L"\r\n");
			OutputDebugString(str.c_str());
		}
	};

	class LogMgr : public IRuntimeModule
	{
	private:
		std::vector<IAppender*> _appenders;
		ELogLevel _output_level;
		TraceLevle _output_mark;
		std::string _output_path;
		std::string _name;
	public:
		LogMgr();
		LogMgr(std::string name, ELogLevel output_level = kLogLevel, TraceLevle output_mark = kTraceLevel);
		int Initialize() override;
		void Finalize() override;
		void Tick(const float& delta_time) override;
		void AddAppender(IAppender* appender);
		void SetOutputLevel(ELogLevel level);
		void SetTraceLevel(TraceLevle trace);
		const std::string& GetOutputPath() const;
		const ELogLevel& GetOutputLevel() const;
		const TraceLevle& GetTraceLevel() const;
		void Log(std::string msg);
		void Log(std::wstring msg);
		void LogWarning(std::string msg);
		void LogWarning(std::wstring msg);
		void LogError(std::string msg);
		void LogError(std::wstring msg);
		template <typename... Args>
		void LogFormat(std::string_view msg, Args&&... args)
		{
			for (auto& appender : _appenders)
			{
				appender->Print(BuildLogMsg(_output_level, msg, args...));
			}
		}
		template <typename... Args>
		void LogErrorFormat(std::string_view msg, Args&&... args)
		{
			for (auto& appender : _appenders)
			{
				appender->Print(BuildLogMsg(ELogLevel::kError, msg, args...));
			}
		}

		template <typename... Args>
		void LogWarningFormat(std::string_view msg, Args&&... args)
		{
			for (auto& appender : _appenders)
			{
				appender->Print(BuildLogMsg(ELogLevel::kWarning, msg, args...));
			}
		}

		template <typename... Args>
		void LogFormat(std::wstring_view msg, Args&&... args)
		{
			for (auto& appender : _appenders)
			{
				appender->Print(BuildLogMsg(_output_level, msg, args...));
			}
		}
		template <typename... Args>
		void LogFormat(ELogLevel level, std::string_view msg, Args&&... args)
		{
			if (level <= _output_level) return;
			for (auto& appender : _appenders)
				appender->Print(BuildLogMsg(level, msg, args...));
		}
		template <typename... Args>
		void LogFormat(ELogLevel level, std::wstring_view msg, Args&&... args)
		{
			if (level <= _output_level) return;
			for (auto& appender : _appenders)
				appender->Print(BuildLogMsg(level, msg, args...));
		}
	};
	extern LogMgr* g_pLogMgr;
}


#endif // !LOG_MGR_H__
