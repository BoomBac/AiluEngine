#pragma once
#ifndef __UTILS_H__
#define __UTILS_H__
#include "pch.h"

namespace Ailu
{
    /// <summary>
    /// 返回的指针需要在使用后销毁
    /// </summary>
    /// <param name="multiByteStr"></param>
    /// <returns></returns>
    static wchar_t* ToWChar(const char* multiByteStr)
    {
        int size = MultiByteToWideChar(CP_UTF8, 0, multiByteStr, -1, nullptr, 0);
        if (size == 0) {
            // Error handling
            return nullptr;
        }
        wchar_t* wideStr = new wchar_t[size];
        MultiByteToWideChar(CP_UTF8, 0, multiByteStr, -1, wideStr, size);
        return wideStr;
    }

    static char* ToChar(const wchar_t* wideStr)
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
        if (size == 0) {
            // Error handling
            return nullptr;
        }
        char* multiByteStr = new char[size];
        WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, multiByteStr, size, nullptr, nullptr);
        return multiByteStr;
    }
}


#endif // !UTILS_H__

