#include "pch.h"
#include "Framework/Common/CrashHandler.h"

#include <Windows.h>
#include <DbgHelp.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

#pragma comment(lib, "Dbghelp.lib")

namespace Ailu
{
    namespace
    {
        LPTOP_LEVEL_EXCEPTION_FILTER s_previous_filter = nullptr;
        HANDLE s_process = nullptr;
#if defined(_DEBUG)
        _CRT_REPORT_HOOK s_previous_report_hook = nullptr;
#endif
        // 崩溃时可能已经无法安全使用堆，这里用固定缓冲避免再次分配。
        char s_report_buffer[2048];

        void WriteText(HANDLE file, const char *text)
        {
            if (file == INVALID_HANDLE_VALUE || text == nullptr)
                return;

            DWORD written = 0u;
            WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
        }

        const char *ExceptionCodeToString(DWORD code)
        {
            switch (code)
            {
                case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
                case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
                case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
                case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
                case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
                case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
                case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
                case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
                case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
                case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
                default: return "UNKNOWN_EXCEPTION";
            }
        }

        void WriteStackTrace(HANDLE file, CONTEXT *context)
        {
            HANDLE thread = GetCurrentThread();

            STACKFRAME64 stack_frame = {};
            DWORD machine_type = 0u;

#if defined(_M_X64)
            machine_type = IMAGE_FILE_MACHINE_AMD64;
            stack_frame.AddrPC.Offset = context->Rip;
            stack_frame.AddrPC.Mode = AddrModeFlat;
            stack_frame.AddrStack.Offset = context->Rsp;
            stack_frame.AddrStack.Mode = AddrModeFlat;
            stack_frame.AddrFrame.Offset = context->Rbp;
            stack_frame.AddrFrame.Mode = AddrModeFlat;
#elif defined(_M_IX86)
            machine_type = IMAGE_FILE_MACHINE_I386;
            stack_frame.AddrPC.Offset = context->Eip;
            stack_frame.AddrPC.Mode = AddrModeFlat;
            stack_frame.AddrStack.Offset = context->Esp;
            stack_frame.AddrStack.Mode = AddrModeFlat;
            stack_frame.AddrFrame.Offset = context->Ebp;
            stack_frame.AddrFrame.Mode = AddrModeFlat;
#else
    #error Unsupported architecture
#endif

            char symbol_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
            SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(symbol_buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;

            // 进程内没有其它线程同时崩溃时，StackWalk64 复用传入的 context 是可行的；
            // 崩溃线程上下文不会被本函数修改。
            CONTEXT walk_context = *context;

            for (uint32_t frame_index = 0u; frame_index < 128u; ++frame_index)
            {
                const BOOL result = StackWalk64(machine_type, s_process, thread, &stack_frame, &walk_context, nullptr,
                                                SymFunctionTableAccess64, SymGetModuleBase64, nullptr);
                if (!result || stack_frame.AddrPC.Offset == 0u)
                    break;

                const DWORD64 address = stack_frame.AddrPC.Offset;
                DWORD64 displacement = 0u;
                DWORD line_displacement = 0u;

                IMAGEHLP_LINE64 line = {};
                line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

                if (SymFromAddr(s_process, address, &displacement, symbol))
                {
                    if (SymGetLineFromAddr64(s_process, address, &line_displacement, &line))
                    {
                        std::snprintf(s_report_buffer, sizeof(s_report_buffer),
                                      "#%-3u 0x%016llX  %s + 0x%llX  [%s:%lu]\r\n", frame_index,
                                      static_cast<unsigned long long>(address), symbol->Name,
                                      static_cast<unsigned long long>(displacement), line.FileName, line.LineNumber);
                    }
                    else
                    {
                        std::snprintf(s_report_buffer, sizeof(s_report_buffer), "#%-3u 0x%016llX  %s + 0x%llX\r\n",
                                      frame_index, static_cast<unsigned long long>(address), symbol->Name,
                                      static_cast<unsigned long long>(displacement));
                    }
                }
                else
                {
                    std::snprintf(s_report_buffer, sizeof(s_report_buffer), "#%-3u 0x%016llX\r\n", frame_index,
                                  static_cast<unsigned long long>(address));
                }

                WriteText(file, s_report_buffer);
            }
        }

        LONG WINAPI CrashExceptionHandler(EXCEPTION_POINTERS *exception_info)
        {
            // 报告写在工作目录（编辑器运行目录），与 FileAppender_log.txt 同级，便于一起收集。
            HANDLE file = CreateFileA("crash.log", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (file != INVALID_HANDLE_VALUE)
            {
                LARGE_INTEGER end{};
                SetFilePointerEx(file, end, nullptr, FILE_END);
                EXCEPTION_RECORD *exception_record = exception_info->ExceptionRecord;

                std::snprintf(s_report_buffer, sizeof(s_report_buffer),
                              "========================================\r\n"
                              "AiluEngine Crash\r\n"
                              "========================================\r\n"
                              "Exception: %s\r\n"
                              "Code:      0x%08lX\r\n"
                              "Address:   0x%p\r\n"
                              "Thread:    %lu\r\n"
                              "\r\n"
                              "Stack Trace:\r\n",
                              ExceptionCodeToString(exception_record->ExceptionCode), exception_record->ExceptionCode,
                              exception_record->ExceptionAddress, GetCurrentThreadId());

                WriteText(file, s_report_buffer);

                SymRefreshModuleList(s_process);
                CONTEXT context = *exception_info->ContextRecord;
                WriteStackTrace(file, &context);

                FlushFileBuffers(file);
                CloseHandle(file);
            }

            return EXCEPTION_EXECUTE_HANDLER;
        }

#if defined(_DEBUG)
        int __cdecl CrashCrtReportHook(int report_type, char *message, int *return_value)
        {
            if (report_type == _CRT_ASSERT)
            {
                HANDLE file = CreateFileA("crash.log", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file != INVALID_HANDLE_VALUE)
                {
                    LARGE_INTEGER end{};
                    end.QuadPart = 0;
                    SetFilePointerEx(file, end, nullptr, FILE_END);
                    std::snprintf(s_report_buffer, sizeof(s_report_buffer),
                                  "========================================\r\n"
                                  "AiluEngine Debug CRT assertion\r\n"
                                  "Thread: %lu\r\n"
                                  "========================================\r\n"
                                  "%s\r\n\r\nStack Trace:\r\n",
                                  GetCurrentThreadId(), message ? message : "(no assertion text)");
                    WriteText(file, s_report_buffer);
                    SymRefreshModuleList(s_process);
                    CONTEXT context{};
                    RtlCaptureContext(&context);
                    WriteStackTrace(file, &context);
                    WriteText(file, "\r\n");
                    FlushFileBuffers(file);
                    CloseHandle(file);
                }
            }
            return s_previous_report_hook ? s_previous_report_hook(report_type, message, return_value) : FALSE;
        }
#endif
    }// namespace

    void InstallCrashHandler()
    {
        if (s_process != nullptr)
            return;
        s_process = GetCurrentProcess();

        DWORD symbol_options = SymGetOptions();
        symbol_options |= SYMOPT_UNDNAME;
        symbol_options |= SYMOPT_DEFERRED_LOADS;
        symbol_options |= SYMOPT_LOAD_LINES;
        symbol_options |= SYMOPT_FAIL_CRITICAL_ERRORS;
        SymSetOptions(symbol_options);
        SymInitialize(s_process, nullptr, TRUE);

        s_previous_filter = SetUnhandledExceptionFilter(CrashExceptionHandler);
#if defined(_DEBUG)
        s_previous_report_hook = _CrtSetReportHook(CrashCrtReportHook);
#endif
    }

    void UninstallCrashHandler()
    {
        SetUnhandledExceptionFilter(s_previous_filter);
        s_previous_filter = nullptr;
#if defined(_DEBUG)
        _CrtSetReportHook(s_previous_report_hook);
        s_previous_report_hook = nullptr;
#endif

        if (s_process != nullptr)
        {
            SymCleanup(s_process);
            s_process = nullptr;
        }
    }
}// namespace Ailu
