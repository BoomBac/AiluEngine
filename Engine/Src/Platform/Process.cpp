#include "Platform/Process.h"

#include "pch.h"

#if AL_PLATFORM_WINDOWS
    #include "Platform/WinProcess.h"
#endif

namespace Ailu
{
    Scope<Process> ProcessFactory::Create()
    {
#if AL_PLATFORM_WINDOWS
        return MakeScope<WinProcess>();
#else
        return nullptr;
#endif
    }
}
