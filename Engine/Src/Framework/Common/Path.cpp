#include "pch.h"
#include "Framework/Common/Path.h"

namespace Ailu
{
    namespace PathUtils
    {
        namespace
        {
            template<typename TString>
            void NormalizePathSeparators(TString &path)
            {
                using CharT = typename TString::value_type;
                constexpr CharT kBackwardSlash = static_cast<CharT>('\\');
                constexpr CharT kForwardSlash = static_cast<CharT>('/');

                for (auto &ch : path)
                {
                    if (ch == kBackwardSlash)
                    {
                        ch = kForwardSlash;
                    }
                }

                size_t pos = 0;
                TString duplicated_sep(2, kForwardSlash);
                TString normalized_sep(1, kForwardSlash);
                while ((pos = path.find(duplicated_sep, pos)) != TString::npos)
                {
                    path.replace(pos, 2, normalized_sep);
                }
            }
        }

        bool IsSystemPath(const String &path)
        {
            return path.find_first_of(":") == 1;
        }

        bool IsSystemPath(const WString &path)
        {
            return path.find_first_of(L":") == 1;
        }

        bool IsInAssetFolder(const WString &path)
        {
            return path.find(L"Res/") != path.npos || path.find(L"Res\\") != path.npos;
        }

        bool IsInAssetFolder(const String &path)
        {
            return path.find("Res/") != path.npos || path.find("Res\\") != path.npos;
        }

        WString Parent(const WString &path)
        {
            return std::filesystem::path(path).parent_path().wstring().append(L"/");
        }

        String Parent(const String &path)
        {
            return std::filesystem::path(path).parent_path().string().append("/");
        }

        String ExtractAssetPath(const String &path)
        {
            auto p1 = path.find("Res/");
            auto p2 = path.find("Res\\");
            if (p1 != path.npos)
            {
                return path.substr(p1 + 4);
            }
            if (p2 != path.npos)
            {
                return path.substr(p2 + 4);
            }
            return path;
        }

        WString ExtractAssetPath(const WString &path)
        {
            auto p1 = path.find(L"Res/");
            auto p2 = path.find(L"Res\\");
            if (p1 != path.npos)
            {
                return path.substr(p1 + 4);
            }
            if (p2 != path.npos)
            {
                return path.substr(p2 + 4);
            }
            return path;
        }

        String FormatFilePath(const String &file_path)
        {
            std::string formattedPath = file_path;
            NormalizePathSeparators(formattedPath);
            return formattedPath;
        }

        WString FormatFilePath(const WString &file_path)
        {
            std::wstring formattedPath = file_path;
            NormalizePathSeparators(formattedPath);
            return formattedPath;
        }

        void FormatFilePathInPlace(WString &file_path)
        {
            NormalizePathSeparators(file_path);
        }

        void FormatFilePathInPlace(String &file_path)
        {
            NormalizePathSeparators(file_path);
        }

        std::filesystem::path ResolveRelPath(const std::filesystem::path &relative_path, const std::filesystem::path &base_path)
        {
            std::filesystem::path absolute_base_path = std::filesystem::absolute(base_path);
            std::filesystem::path combined_path = absolute_base_path / relative_path;
            return std::filesystem::weakly_canonical(combined_path);
        }

        WString ToPlatformPath(const WString &sys_path)
        {
#ifdef AL_PLATFORM_WINDOWS
            auto convertedPath = sys_path;
            // 使用循环替换所有的斜杠
            for (size_t i = 0; i < convertedPath.length(); ++i)
            {
                if (convertedPath[i] == L'/')
                {
                    convertedPath[i] = L'\\';
                }
            }
            return convertedPath;
#else
            return sys_path;
#endif// AL_PLATFORM_WINDOWS
            return sys_path;
        }
        WString GetFileName(const std::wstring_view filePath, bool include_ext)
        {
            size_t found = filePath.find_last_of(L"/\\");
            size_t dot_pos = filePath.find_last_of(L".");

            if (found != std::string::npos)
            {
                if (include_ext)
                    return WString(filePath.substr(found + 1).data());
                else
                {
                    if (dot_pos != std::string::npos)
                    {
                        size_t name_length = dot_pos - (found + 1);
                        return WString(filePath.substr(found + 1, name_length).data(), name_length);
                    }
                    return WString(filePath.substr(found + 1).data());
                }
            }
            else
            {
                return WString(filePath.data(), filePath.length());
            }
        }

        String GetFileName(const std::string_view filePath, bool include_ext)
        {
            size_t found = filePath.find_last_of("/\\");
            size_t dot_pos = filePath.find_last_of(".");

            if (found != std::string::npos)
            {
                if (include_ext)
                    return String(filePath.substr(found + 1).data());
                else
                {
                    if (dot_pos != std::string::npos)
                    {
                        size_t name_length = dot_pos - (found + 1);
                        return String(filePath.substr(found + 1, name_length).data(), name_length);
                    }
                    return String(filePath.substr(found + 1).data());
                }
            }
            else
            {
                return String(filePath.data(), filePath.length());
            }
        }

        WString RenameFile(const WString &asset_path, WString new_name)
        {
            auto path_without_file_name = asset_path.substr(0, asset_path.find_last_of(L"/") + 1);
            path_without_file_name += new_name;
            auto ext = asset_path.substr(asset_path.find_last_of(L"."));
            path_without_file_name += ext;
            return path_without_file_name;
        }

        WString ExtractExt(const WString &asset_path)
        {
            if (auto dot_pos = asset_path.find_last_of(L"."); dot_pos != asset_path.npos)
                return asset_path.substr(dot_pos);
            return WString();
        }

        WString ExtarctDirectory(const WString &asset_path)
        {
            WString dir = asset_path;
            FormatFilePathInPlace(dir);
            return dir.substr(0, dir.find_last_of(L"/") + 1);
        }

        WString NormalizePathWithoutTrailingSlash(const WString &path)
        {
            WString normalized = FormatFilePath(path);
            while (!normalized.empty() && normalized.back() == L'/')
                normalized.pop_back();
            return normalized;
        }

        String NormalizePathWithoutTrailingSlash(const String &path)
        {
            String normalized = FormatFilePath(path);
            while (!normalized.empty() && normalized.back() == '/')
                normalized.pop_back();
            return normalized;
        }

        WString NormalizeDirectoryPath(const WString &path)
        {
            WString normalized = FormatFilePath(path);
            if (!normalized.empty() && normalized.back() != L'/')
                normalized.push_back(L'/');
            return normalized;
        }

        String NormalizeDirectoryPath(const String &path)
        {
            String normalized = FormatFilePath(path);
            if (!normalized.empty() && normalized.back() != '/')
                normalized.push_back('/');
            return normalized;
        }

        WString NormalizeRelativeDirectory(const WString &path)
        {
            WString result = FormatFilePath(path);
            while (!result.empty() && result.front() == L'/')
                result.erase(result.begin());
            while (!result.empty() && result.back() == L'/')
                result.pop_back();
            return result;
        }

        String NormalizeRelativeDirectory(const String &path)
        {
            String result = FormatFilePath(path);
            while (!result.empty() && result.front() == '/')
                result.erase(result.begin());
            while (!result.empty() && result.back() == '/')
                result.pop_back();
            return result;
        }

    } // namespace PathUtils
}