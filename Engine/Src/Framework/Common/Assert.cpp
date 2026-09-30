#include "pch.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Common/Assert.h"
#include "Framework/Common/Log.h"
namespace Ailu
{
	namespace pow2
	{

		namespace
		{
			// 断言后进程会 __debugbreak 直接消失，OutputDebugString 在无调试器时看不到内容，
			// 因此这里同时把断言写进 FileAppender_log.txt，保证崩溃点在日志里可查。
			void AppendToLogFile(const char* condition, const char* msg, const char* file, const int line)
			{
				const String detail = std::format("ASSERT FAILED: file={} line={} cond={} msg={}",
				                                  file != nullptr ? file : "<none>", line,
				                                  condition != nullptr ? condition : "<none>",
				                                  msg != nullptr ? msg : "<none>");
				FileAppender file_appender;
				file_appender.Print(LogMessage(ELogLevel::kError, detail, __FILE__, __LINE__, __FUNCTION__));
			}

			Assert::FailBehavior DefaultHandler(const char* condition,
				const char* msg,
				const char* file,
				const int line)
			{
				const u64 BufferSize = 2048;
				char buffer[BufferSize];
				sprintf_s(buffer, BufferSize, "%s(%d): Assert Failure: ", file, line);

				if (condition != NULL)
					sprintf_s(buffer, BufferSize, "%s'%s' ", buffer, condition);

				if (msg != NULL)
					sprintf_s(buffer, BufferSize, "%s%s", buffer, msg);

				sprintf_s(buffer, BufferSize, "%s\n", buffer);

				OutputDebugStringA(buffer);
				AppendToLogFile(condition, msg, file, line);

				return Assert::Halt;
			}

			Assert::Handler& GetAssertHandlerInstance()
			{
				static Assert::Handler s_handler = &DefaultHandler;
				return s_handler;
			}

		}

		// 显式把断言处理器切换为带日志落盘的版本（默认处理器已经会落盘，保留该接口便于调用方显式声明）。
		AILU_API void Assert::SetLoggingHandler()
		{
			GetAssertHandlerInstance() = &DefaultHandler;
		}

		Assert::Handler Assert::GetHandler()
		{
			return GetAssertHandlerInstance();
		}

		void Assert::SetHandler(Assert::Handler newHandler)
		{
			GetAssertHandlerInstance() = newHandler;
		}

		Assert::FailBehavior Assert::ReportFailure(const char* condition,
			const char* file,
			const int line,
			const char* msg, ...)
		{
			const char* message = NULL;
			if (msg != NULL)
			{
				char messageBuffer[1024];
				{
					va_list args;
					va_start(args, msg);
					vsnprintf_s(messageBuffer, 1024, 1024, msg, args);
					va_end(args);
				}

				message = messageBuffer;
			}

			return GetAssertHandlerInstance()(condition, message, file, line);
		}

	}
}
