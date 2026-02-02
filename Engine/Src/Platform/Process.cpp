#include "Platform/Process.h"

#include "pch.h"

#if PLATFORM_WINDOWS
    #include "Platform/WinProcess.h"
#endif

namespace Ailu
{
    Scope<Process> ProcessFactory::Create()
    {
#if PLATFORM_WINDOWS
        return MakeScope<WinProcess>();
#else
        return nullptr;
#endif
    }
}
