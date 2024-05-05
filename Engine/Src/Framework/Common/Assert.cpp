#include "pch.h"
#include "GlobalMarco.h"
#include "Framework/Common/Assert.h"
namespace Ailu
{
	namespace pow2
	{

		namespace
		{
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

				return Assert::Halt;
			}

			Assert::Handler& GetAssertHandlerInstance()
			{
				static Assert::Handler s_handler = &DefaultHandler;
				return s_handler;
			}

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
