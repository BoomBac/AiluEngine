#pragma once
#ifndef __PATH_H__
#define __PATH_H__
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include <filesystem>

namespace Ailu
{
    using Path = std::filesystem::path;

    namespace EnginePath
    {
        static const String kEngineShaderPath = "Shaders/";
        static const String kEngineMaterialPath = "Materials/";
        static const String kEngineMeshPath = "Meshs/";
        static const String kEngineTexturePath = "Textures/";
        static const String kEngineScriptPath = "Scripts/";
        static const WString kEngineShaderPathW = L"Shaders/";
        static const WString kEngineMaterialPathW = L"Materials/";
        static const WString kEngineMeshPathW = L"Meshs/";
        static const WString kEngineTexturePathW = L"Textures/";
        static const WString kEngineScriptPathW = L"Scripts/";
        static const String kEngineIconPath = "Icons/";
        static const WString kEngineIconPathW = L"Icons/";
    }// namespace EnginePath

    namespace PathUtils
    {
        bool AILU_API IsSystemPath(const String &path);
        bool AILU_API IsSystemPath(const WString &path);
        bool AILU_API IsInAssetFolder(const WString &path);
        bool AILU_API IsInAssetFolder(const String &path);
        WString AILU_API Parent(const WString &path);
        String AILU_API Parent(const String &path);
        String AILU_API ExtractAssetPath(const String &path);
        WString AILU_API ExtractAssetPath(const WString &path);
        String AILU_API FormatFilePath(const String &file_path);
        WString AILU_API FormatFilePath(const WString &file_path);
        void AILU_API FormatFilePathInPlace(WString &file_path);
        void AILU_API FormatFilePathInPlace(String &file_path);
        std::filesystem::path AILU_API ResolveRelPath(const std::filesystem::path &relative_path, const std::filesystem::path &base_path);
        WString AILU_API ToPlatformPath(const WString &sys_path);
        WString AILU_API GetFileName(const std::wstring_view filePath, bool include_ext = false);
        String AILU_API GetFileName(const std::string_view filePath, bool include_ext = false);
        WString AILU_API RenameFile(const WString &asset_path, WString new_name);
        WString AILU_API ExtractExt(const WString &asset_path);
        /// @brief 返回格式化后的父目录
        /// @param asset_path 
        /// @return c:/xx/aa/
        WString AILU_API ExtarctDirectory(const WString &asset_path);

        /// @brief 规范化路径，去除末尾 '/'
        WString AILU_API NormalizePathWithoutTrailingSlash(const WString &path);
        /// @brief 规范化路径，去除末尾 '/'
        String AILU_API NormalizePathWithoutTrailingSlash(const String &path);

        /// @brief 规范化目录路径，确保以 '/' 结尾
        WString AILU_API NormalizeDirectoryPath(const WString &path);
        /// @brief 规范化目录路径，确保以 '/' 结尾
        String AILU_API NormalizeDirectoryPath(const String &path);
        /// @brief 规范化相对目录路径，去除首尾 '/'
        WString AILU_API NormalizeRelativeDirectory(const WString &path);
        /// @brief 规范化相对目录路径，去除首尾 '/'
        String AILU_API NormalizeRelativeDirectory(const String &path);
    } // namespace PathUtils

}// namespace Ailu


#endif// !PATH_H__
