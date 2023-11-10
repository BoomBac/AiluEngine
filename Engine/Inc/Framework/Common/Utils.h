#pragma once
#ifndef __UTILS_H__
#define __UTILS_H__
//#include "pch.h"
#include <algorithm>
#include "GlobalMarco.h"

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
        if (size == 0 || size > 256) {
            throw std::runtime_error("to wstring error!");
            return nullptr;
        }
        static wchar_t wideStr[256];
        MultiByteToWideChar(CP_UTF8, 0, multiByteStr, -1, wideStr, size);
        return wideStr;
    }

    static char* ToChar(const wchar_t* wideStr)
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
        if (size == 0 || size > 256) {
            throw std::runtime_error("to nstring error!");
            return nullptr;
        }
        static char multiByteStr[256];
        WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, multiByteStr, size, nullptr, nullptr);
        return multiByteStr;
    }

    namespace StringUtils
    {
        inline static bool Equal(const String& a, const String& b, bool case_insensitive = true)
        {
            static auto CaseInsensitiveStringCompare = [](const char& a, const char& b) {
                return std::tolower(a) == std::tolower(b);
                };
            if (case_insensitive)
                return a == b;
            else
            {
                return (a.size() == b.size()) && std::equal(a.begin(), a.end(), b.begin(), CaseInsensitiveStringCompare);
            }
        }

        // 移除字符串中的所有空格
        static void RemoveSpaces(String& str)
        {
            str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
        }

        // 移除字符串前后的空格和换行符
        static String Trim(const String& str)
        {
            size_t first = str.find_first_not_of(" \t\n\r");
            if (first == std::string::npos)
            {
                return "";
            }
            size_t last = str.find_last_not_of(" \t\n\r");
            return str.substr(first, (last - first + 1));
        }
    }
    namespace su = StringUtils;


    static List<String> ReadFileToLines(const String& sys_path, u32& line_count,String begin = "", String end = "")
    {
        std::ifstream file(sys_path);
        List<String> lines{};
        if (!file.is_open())
        {
            LOG_ERROR("Load asset with path: {} failed!", sys_path);
            return lines;
        }
        std::string line;
        bool start = begin.empty(), stop = false;
        while (std::getline(file, line))
        {
            line = StringUtils::Trim(line);
            if (!start)
            {
                start = line == begin;
                continue;
            }
            if (!stop && !end.empty())
            {
                stop = end == line;
            }
            if (start && !stop)
            {
                lines.emplace_back(line);
            }
        }
        file.close();
        line_count = static_cast<u32>(lines.size());
        return lines;
    }
}


#endif // !UTILS_H__

