#pragma once

#include "Framework/Platform/Platform.h"

#if AL_PLATFORM_WINDOWS
    #if defined(AILU_BUILD_DLL)
        #define AILU_API __declspec(dllexport)
    #else
        #define AILU_API __declspec(dllimport)
    #endif
#else
    #define AILU_API
#endif