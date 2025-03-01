#include "pch.h"
#include "Framework/Common/Path.h"

namespace Ailu
{
    namespace PathUtils
    {
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
            size_t pos = 0;
            while ((pos = formattedPath.find("\\\\", pos)) != std::string::npos)
            {
                formattedPath.replace(pos, 2, "/");
            }
            pos = 0;
            while ((pos = formattedPath.find("\\", pos)) != std::string::npos)
            {
                formattedPath.replace(pos, 1, "/");
            }
            return formattedPath;
        }

        WString FormatFilePath(const WString &file_path)
        {
            std::wstring formattedPath = file_path;
            size_t pos = 0;
            while ((pos = formattedPath.find(L"\\\\", pos)) != std::wstring::npos)
            {
                formattedPath.replace(pos, 2, L"/");
            }
            pos = 0;
            while ((pos = formattedPath.find(L"\\", pos)) != std::wstring::npos)
            {
                formattedPath.replace(pos, 1, L"/");
            }
            return formattedPath;
        }

        void FormatFilePathInPlace(WString &file_path)
        {
            size_t pos = 0;
            while ((pos = file_path.find(L"\\\\", pos)) != std::wstring::npos)
            {
                file_path.replace(pos, 2, L"/");
            }
            pos = 0;
            while ((pos = file_path.find(L"\\", pos)) != std::wstring::npos)
            {
                file_path.replace(pos, 1, L"/");
            }
        }

        void FormatFilePathInPlace(String &file_path)
        {
            size_t pos = 0;
            while ((pos = file_path.find("\\\\", pos)) != std::wstring::npos)
            {
                file_path.replace(pos, 2, "/");
            }
            pos = 0;
            while ((pos = file_path.find("\\", pos)) != std::wstring::npos)
            {
                file_path.replace(pos, 1, "/");
            }
        }

        std::filesystem::path ResolveRelPath(const std::filesystem::path &relative_path, const std::filesystem::path &base_path)
        {
            std::filesystem::path absolute_base_path = std::filesystem::absolute(base_path);
            std::filesystem::path combined_path = absolute_base_path / relative_path;
            return std::filesystem::weakly_canonical(combined_path);
        }

        WString ToPlatformPath(const WString &sys_path)
        {
#ifdef PLATFORM_WINDOWS
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
#endif// PLATFORM_WINDOWS
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

    } // namespace PathUtils
}