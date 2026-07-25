#pragma once

#if defined(_WIN32)
    #define AL_PLATFORM_WINDOWS 1
#else
    #define AL_PLATFORM_WINDOWS 0
#endif

#if defined(__APPLE__)
    #define AL_PLATFORM_APPLE 1
#else
    #define AL_PLATFORM_APPLE 0
#endif

#if defined(__linux__)
    #define AL_PLATFORM_LINUX 1
#else
    #define AL_PLATFORM_LINUX 0
#endif