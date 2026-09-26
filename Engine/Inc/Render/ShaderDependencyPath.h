#pragma once

#include "Framework/Core/String.h"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <set>

namespace Ailu::Render
{
    inline WString NormalizeShaderDependencyPath(const WString &path)
    {
        if (path.empty())
            return {};
        WString normalized = std::filesystem::absolute(std::filesystem::path(path)).lexically_normal().generic_wstring();
#ifdef AL_PLATFORM_WINDOWS
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
#endif
        return normalized;
    }

    inline std::set<WString> NormalizeShaderDependencyPaths(const std::set<WString> &paths)
    {
        std::set<WString> normalized_paths;
        for (const auto &path : paths)
        {
            if (!path.empty())
                normalized_paths.insert(NormalizeShaderDependencyPath(path));
        }
        return normalized_paths;
    }
}
