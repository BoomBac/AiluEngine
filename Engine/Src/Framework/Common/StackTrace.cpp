#include "pch.h"
#include "Framework/Common/StackTrace.h"

#if defined(PLATFORM_WINDOWS)
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment(lib, "Dbghelp.lib")
#endif
#include <sstream>
#include <iomanip>
#include <mutex>

namespace Ailu
{
    String StackTrace::Capture(u16 max_frames)
    {
    #if defined(PLATFORM_WINDOWS)
        static std::once_flag sym_init_flag;
        std::call_once(sym_init_flag, []() {
            SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        });
    
        void* stack[62];
        HANDLE process = GetCurrentProcess();
        WORD frames = CaptureStackBackTrace(0, max_frames, stack, nullptr);
    
        std::ostringstream oss;
    
        SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256);
        if (!symbol) return "<failed to allocate symbol info>\n";
    
        symbol->MaxNameLen = 255;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    
        IMAGEHLP_LINE64 line;
        DWORD displacement = 0;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    
        for (unsigned int i = 0; i < frames; ++i)
        {
            DWORD64 address = (DWORD64)(stack[i]);
            if (SymFromAddr(process, address, 0, symbol))
            {
                oss << "#" << std::setw(2) << std::setfill('0') << i << " "
                    << symbol->Name << " - 0x" << std::hex << address << std::dec;
    
                if (SymGetLineFromAddr64(process, address, &displacement, &line))
                {
                    oss << " (" << line.FileName << ":" << line.LineNumber << ")";
                }
    
                oss << "\n";
            }
            else
            {
                oss << "#" << std::setw(2) << std::setfill('0') << i
                    << " <unknown> - 0x" << std::hex << address << std::dec << "\n";
            }
        }
    
        free(symbol);
        return oss.str();
    #else
        return "<stack trace not supported on this platform>\n";
    #endif
    }
}
