#pragma once

#if defined(_MSC_VER)
    #define AL_COMPILER_MSVC 1
    #define AL_FORCE_INLINE __forceinline
#elif defined(__clang__)
    #define AL_COMPILER_CLANG 1
    #define AL_FORCE_INLINE inline __attribute__((always_inline))
#elif defined(__GNUC__)
    #define AL_COMPILER_GCC 1
    #define AL_FORCE_INLINE inline __attribute__((always_inline))
#else
    #define AL_FORCE_INLINE inline
#endif

#ifndef LIKELY
    #if defined(__GNUC__) && !defined(__KERNEL_GPU__)
        #define LIKELY(x) __builtin_expect(!!(x), 1)
        #define UNLIKELY(x) __builtin_expect(!!(x), 0)
    #else
        #define LIKELY(x) (x)
        #define UNLIKELY(x) (x)
    #endif
#endif
