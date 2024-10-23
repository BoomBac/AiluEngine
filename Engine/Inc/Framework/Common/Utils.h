#pragma once
#ifndef __UTILS_H__
#define __UTILS_H__
//#include "pch.h"
#if !defined(_M_X64) && !defined(_M_IX86)
#error "No Target Architecture"
#endif

//#include <stringapiset.h>
#include <Windows.h>
#include "GlobalMarco.h"
#include <algorithm>
#include <numeric>
#include <fstream>
#include <set>

namespace Ailu
{
    static WString ToWStr(const char* multiByteStr)
    {
        int size = MultiByteToWideChar(CP_UTF8, 0, multiByteStr, -1, nullptr, 0);
        if (size == 0 || size > 256) {
            throw std::runtime_error("to wstring error!");
            return WString{};
        }
        wchar_t wideStr[256];
        MultiByteToWideChar(CP_UTF8, 0, multiByteStr, -1, wideStr, size);
        return WString{ wideStr };
    }
    static WString ToWStr(const String& multiByteStr)
    {
        return ToWStr(multiByteStr.c_str());
    }

    static String ToChar(const wchar_t* wideStr)
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
        if (size == 0 || size > 256) {
            throw std::runtime_error("to nstring error!");
            return nullptr;
        }
        char multiByteStr[256];
        WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, multiByteStr, size, nullptr, nullptr);
        return multiByteStr;
    }

    static String ToChar(const WString& wideStr)
    {
        return ToChar(wideStr.c_str());
    }
    static String GetIndentation(int level)
    {
        String ret{ "" };
        while (level--)
        {
            ret.append("  ");
        }
        return ret;
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

        /// <summary>
        /// 移除字符串中的所有空格
        /// </summary>
        /// <param name="str"></param>
        static void RemoveSpaces(String& str)
        {
            str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
        }
        /// <summary>
        /// 移除字符串中的所有空格
        /// </summary>
        /// <param name="str"></param>
        static void RemoveSpaces(WString& str)
        {
            str.erase(std::remove(str.begin(), str.end(), L' '), str.end());
        }

        /// <summary>
        /// 移除字符串前后的空格和换行符
        /// </summary>
        /// <param name="str"></param>
        /// <returns></returns>
        static String Trim(const String& str)
        {
            size_t first = str.find_first_not_of(" \t\n\r");
            if (first == String::npos)
            {
                return "";
            }
            size_t last = str.find_last_not_of(" \t\n\r");
            return str.substr(first, (last - first + 1));
        }
        /// <summary>
        /// 移除字符串前后的空格和换行符
        /// </summary>
        /// <param name="str"></param>
        /// <returns></returns>
        static WString Trim(const WString& str)
        {
            size_t first = str.find_first_not_of(L" \t\n\r");
            if (first == String::npos)
            {
                return L"";
            }
            size_t last = str.find_last_not_of(L" \t\n\r");
            return str.substr(first, (last - first + 1));
        }

        /// <summary>
        /// 按分隔符提取字符串
        /// </summary>
        /// <param name="input"></param>
        /// <param name="delimiter"></param>
        /// <returns></returns>
        static Vector<String> Split(const String& input, const String& delimiter) 
        {
            Vector<String> tokens;
            size_t start = 0, end = 0;
            while ((end = input.find(delimiter, start)) != String::npos) 
            {
                tokens.push_back(input.substr(start, end - start));
                start = end + 1;
            }
            tokens.push_back(input.substr(start));
            return tokens;
        }
        /// <summary>
        /// 按分隔符提取字符串
        /// </summary>
        /// <param name="input"></param>
        /// <param name="delimiter"></param>
        /// <returns></returns>
        static Vector<WString> Split(const WString& input, const WString& delimiter)
        {
            Vector<WString> tokens;
            size_t start = 0, end = 0;
            while ((end = input.find(delimiter, start)) != WString::npos)
            {
                tokens.push_back(input.substr(start, end - start));
                start = end + 1;
            }
            tokens.push_back(input.substr(start));
            return tokens;
        }


        static String Join(const Vector<String>& strings, const String& delimiter)
        {
            if (strings.empty())
                return "";
            return std::accumulate(strings.begin() + 1, strings.end(), strings[0],
                [&delimiter](const String& a, const String& b) {
                    return a + delimiter + b;
                });
        }

        static String Join(const std::set<String>& strings, const String& delimiter)
        {
            if (strings.empty()) 
                return "";
            auto iter = strings.begin();
            String result = *iter;
            ++iter;
            return std::accumulate(iter, strings.end(), result,
                [&delimiter](const String& a, const String& b) {
                    return a + delimiter + b;
                });
        }

        static bool BeginWith(const String& s, const String& prefix)
        {
            if (s.length() < prefix.length()) return false;
            return s.substr(0, prefix.length()) == prefix;
        }
        static bool BeginWith(const WString& s, const WString& prefix)
        {
            if (s.length() < prefix.length()) return false;
            return s.substr(0, prefix.length()) == prefix;
        }
        static bool EndWith(const String& s, const String& prefix)
        {
            if (s.size() < prefix.size())
                return false;
            return s.rfind(prefix) == s.size() - prefix.size();
        }
        static bool EndWith(const WString& s, const WString& prefix)
        {
            if (s.size() < prefix.size())
                return false;
            return s.rfind(prefix) == s.size() - prefix.size();
        }

        //[begin,end]
        static inline String SubStrRange(const String& s, const size_t& begin, const size_t& end)
        {
            if (end < begin) return s;
            return s.substr(begin, end - begin + 1);
        }
        //[begin,end]
        static inline WString SubStrRange(const WString& s, const size_t& begin, const size_t& end)
        {
            if (end < begin) return s;
            return s.substr(begin, end - begin + 1);
        }
    }
    namespace su = StringUtils;

    namespace Algorithm
    {
        namespace 
        {
            static void GeneratePermutationsHelper(const Vector<Vector<String>>& input, Vector<String>& current, Vector<Vector<String>>& result, size_t index)
            {
                if (index == input.size())
                {
                    result.push_back(current);
                    return;
                }
                // Iterate through the elements at the current index
                for (const String& element : input[index])
                {
                    // Include the current element in the permutation
                    current.push_back(element);
                    // Recursively generate permutations for the next index
                    GeneratePermutationsHelper(input, current, result, index + 1);
                    // Backtrack: remove the last element to explore other possibilities
                    current.pop_back();
                }
            }
        }

        static Vector<Vector<String>> Permutations(const Vector<Vector<String>>& input) 
        {
            Vector<Vector<String>> result;
            Vector<String> current;

            GeneratePermutationsHelper(input, current, result, 0);

            return result;
        }
    }
}


#endif // !UTILS_H__

