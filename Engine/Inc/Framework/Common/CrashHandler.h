#pragma once
#ifndef __CRASH_HANDLER_H__
#define __CRASH_HANDLER_H__

#include "Framework/Platform/Api.h"

namespace Ailu
{
    /// @brief 安装顶层未处理异常过滤器，崩溃时把异常信息与调用栈写入 crash.log。
    /// 断言（AL_ASSERT）中断不经过该过滤器，只覆盖真正的访问违例/非法指令等异常。
    AILU_API void InstallCrashHandler();
    /// @brief 还原之前安装的异常过滤器并释放 DbgHelp 符号会话。
    AILU_API void UninstallCrashHandler();
}// namespace Ailu

#endif// !__CRASH_HANDLER_H__
