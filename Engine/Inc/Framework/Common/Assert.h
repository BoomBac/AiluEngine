#pragma once
#ifndef __ASSERT_H__
#define __ASSERT_H__

//https://github.com/TheRealMJP/DeferredTexturing/blob/849304047f1cca0f23fe9d0fd201758d77ed3c41/SampleFramework12/v1.01/Assert.h#L110
#define POW2_ASSERTS_ENABLED

#ifdef AILU_BUILD_DLL
#define AILU_API __declspec(dllexport)
#else
#define AILU_API __declspec(dllimport)
#endif

namespace Ailu
{
    namespace pow2
    {
        namespace Assert
        {
            enum FailBehavior
            {
                Halt,
                Continue,
            };
            typedef FailBehavior (*Handler)(const char *condition, const char *msg, const char *file, int line);
            Handler GetHandler();
            void SetHandler(Handler newHandler);
            AILU_API FailBehavior ReportFailure(const char *condition, const char *file, int line, const char *msg, ...);
        }// namespace Assert
    }// namespace pow2

#define POW2_HALT() __debugbreak()
#define POW2_UNUSED(x)    \
    do {                  \
        (void) sizeof(x); \
    } while (0)

#ifdef POW2_ASSERTS_ENABLED
#define POW2_ASSERT(cond)                                                          \
    do                                                                             \
    {                                                                              \
        if (!(cond))                                                                  \
        {                                                                          \
            if (pow2::Assert::ReportFailure(#cond, __FILE__, __LINE__, 0) == \
                pow2::Assert::Halt)                                          \
                POW2_HALT();                                                       \
        }                                                                          \
    } while (0)

#define POW2_ASSERT_MSG(cond, msg, ...)                                                             \
    do                                                                                              \
    {                                                                                               \
        if (!(cond))                                                                                   \
        {                                                                                           \
            if (pow2::Assert::ReportFailure(#cond, __FILE__, __LINE__, (msg), __VA_ARGS__) == \
                pow2::Assert::Halt)                                                           \
                POW2_HALT();                                                                        \
        }                                                                                           \
    } while (0)

#define POW2_ASSERT_FAIL(msg, ...)                                                          \
    do                                                                                      \
    {                                                                                       \
        if (pow2::Assert::ReportFailure(0, __FILE__, __LINE__, (msg), __VA_ARGS__) == \
            pow2::Assert::Halt)                                                       \
            POW2_HALT();                                                                    \
    } while (0)

#define POW2_VERIFY(cond) POW2_ASSERT(cond)
#define POW2_VERIFY_MSG(cond, msg, ...) POW2_ASSERT_MSG(cond, msg, ##__VA_ARGS__)
#else
#define POW2_ASSERT(condition)  \
    do {                        \
        POW2_UNUSED(condition); \
    } while (0)
#define POW2_ASSERT_MSG(condition, msg, ...) \
    do {                                     \
        POW2_UNUSED(condition);              \
        POW2_UNUSED(msg);                    \
    } while (0)
#define POW2_ASSERT_FAIL(msg, ...) \
    do {                           \
        POW2_UNUSED(msg);          \
    } while (0)
#define POW2_VERIFY(cond) (void) (cond)
#define POW2_VERIFY_MSG(cond, msg, ...) \
    do {                                \
        (void) (cond);                  \
        POW2_UNUSED(msg);               \
    } while (0)
#endif

    // SampleFramework12 Asserts
#ifdef _DEBUG
#define UseAsserts_ 1
#else
#define UseAsserts_ 0
#endif

#if UseAsserts_
#define AL_ASSERT(x) POW2_ASSERT(x)
#define AL_ASSERT_MSG(x, msg, ...) POW2_ASSERT_MSG(x, msg, __VA_ARGS__)
#define AL_ASSERT_FAIL(msg, ...) POW2_ASSERT_FAIL(msg, __VA_ARGS__)
#else
#define AL_ASSERT(x)
#define AL_ASSERT_MSG(x, msg, ...)
#define AL_ASSERT_FAIL(msg, ...)
#endif

#define UseStaticAsserts_ 1

#if UseStaticAsserts_
#define AL_STATIC_ASSERT(x) static_assert(x, #x);
#define AL_STATIC_ASSERT_MSG(x, msg) static_assert(x, #x ", " msg);
#else
#define StaticAssert_(x)
#define StaticAssertMsg_(x, msg)
#endif
}// namespace Ailu


#endif// !ASSERT_H__
