#pragma once
#ifndef __PATH_H__
#define __PATH_H__
#include "GlobalMarco.h"
#include <filesystem>

namespace Ailu
{
#ifdef COMPANY_ENV
	static const String kEngineRootPath = "C:/AiluEngine/Engine/";
	static const WString kEngineRootPathW = L"C:/AiluEngine/Engine/";
	static const String kEngineResRootPath = "C:/AiluEngine/Engine/Res/";
	static const WString kEngineResRootPathW = L"C:/AiluEngine/Engine/Res/";
#else
	static const String kEngineRootPath = "D:/ProjectCpp/AiluEngine/Engine/";
	static const WString kEngineRootPathW = L"D:/ProjectCpp/AiluEngine/Engine/";
	static const String kEngineResRootPath = "D:/ProjectCpp/AiluEngine/Engine/Res/";
	static const WString kEngineResRootPathW = L"D:/ProjectCpp/AiluEngine/Engine/Res/";
#endif

	namespace EnginePath
	{
		static const String kEngineShaderPath = "Shaders/";
		static const String kEngineMaterialPath = "Materials/";
		static const String kEngineMeshPath = "Meshs/";
		static const String kEngineTexturePath = "Textures/";
		static const WString kEngineShaderPathW = L"Shaders/";
		static const WString kEngineMaterialPathW = L"Materials/";
		static const WString kEngineMeshPathW = L"Meshs/";
		static const WString kEngineTexturePathW = L"Textures/";
		static const String kEngineIconPath = "Icons/";
		static const WString kEngineIconPathW = L"Icons/";
	}

	namespace PathUtils
	{
		inline static bool IsSystemPath(const String& path)
		{
			return path.find_first_of(":") == 1;
		}

		inline static bool IsSystemPath(const WString& path)
		{
			return path.find_first_of(L":") == 1;
		}

		static inline bool IsInAssetFolder(const WString& path)
		{
			return path.find(L"Res/") != path.npos || path.find(L"Res\\") != path.npos;
		}

		static inline bool IsInAssetFolder(const String& path)
		{
			return path.find("Res/") != path.npos || path.find("Res\\") != path.npos;
		}

		//begin from Res/  eg: Engien/Res/aa/b -> aa/b
		static inline String ExtractAssetPath(const String& path)
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

		//begin from Res/  eg: Engien/Res/aa/b -> aa/b
		static inline WString ExtractAssetPath(const WString& path)
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

		static String FormatFilePath(const String& file_path)
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

		static WString FormatFilePath(const WString& file_path)
		{
			std::wstring formattedPath = file_path;
			size_t pos = 0;
			while ((pos = formattedPath.find(L"\\\\", pos)) != std::string::npos)
			{
				formattedPath.replace(pos, 2, L"/");
			}
			pos = 0;
			while ((pos = formattedPath.find(L"\\", pos)) != std::string::npos)
			{
				formattedPath.replace(pos, 1, L"/");
			}
			return formattedPath;
		}

		static void FormatFilePathInPlace(WString& file_path)
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

		static void FormatFilePathInPlace(String& file_path)
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

		static std::string GetFileName(const std::string_view filePath, bool include_ext = false)
		{
			size_t found = filePath.find_last_of("/\\");
			size_t dot_pos = filePath.find_last_of(".");

			if (found != std::string::npos && dot_pos != std::string::npos)
			{
				if (include_ext)
					return std::string(filePath.substr(found + 1).data());
				else
				{
					size_t name_length = dot_pos - (found + 1);
					return std::string(filePath.substr(found + 1, name_length).data(), name_length);
				}
			}
			else
			{
				return std::string(filePath.data(), filePath.length());
			}
		}

		static WString ToPlatformPath(const WString& sys_path)
		{
#ifdef PLATFORM_WINDOWS
			auto convertedPath = sys_path;
			// 使用循环替换所有的斜杠
			for (size_t i = 0; i < convertedPath.length(); ++i) {
				if (convertedPath[i] == L'/') {
					convertedPath[i] = L'\\';
				}
			}
			return convertedPath;
#else
			return sys_path;
#endif // PLATFORM_WINDOWS
			return sys_path;
		}

		static WString GetFileName(const std::wstring_view filePath, bool include_ext = false)
		{
			size_t found = filePath.find_last_of(L"/\\");
			size_t dot_pos = filePath.find_last_of(L".");

			if (found != std::string::npos && dot_pos != std::string::npos)
			{
				if (include_ext)
					return WString(filePath.substr(found + 1).data());
				else
				{
					size_t name_length = dot_pos - (found + 1);
					return WString(filePath.substr(found + 1, name_length).data(), name_length);
				}
			}
			else
			{
				return WString(filePath.data(), filePath.length());
			}
		}

		static inline std::string GetResSysPath(const String& sub_path)
		{
			String path = sub_path;
			if (IsSystemPath(path)) 
				return sub_path;
			if (sub_path.starts_with("/"))
				path = path.substr(1);
			else if (sub_path.starts_with("\\"))
				path = path.substr(2);
			return kEngineResRootPath + path;
		}

		static inline WString GetResSysPath(const WString& sub_path)
		{
			WString path = sub_path;
			if (IsSystemPath(path))
				return sub_path;
			if (sub_path.starts_with(L"/"))
				path = path.substr(1);
			else if (sub_path.starts_with(L"\\"))
				path = path.substr(2);
			return kEngineResRootPathW + path;
		}

		static auto RenameFile(const WString& asset_path, WString new_name)
		{
			auto path_without_file_name = asset_path.substr(0, asset_path.find_last_of(L"/") + 1);
			path_without_file_name += new_name;
			auto ext = asset_path.substr(asset_path.find_last_of(L"."));
			path_without_file_name += ext;
			return path_without_file_name;
		}
		//return .*
		static WString ExtractExt(const WString& asset_path)
		{
			return asset_path.substr(asset_path.find_last_of(L"."));
		}

		static WString ExtarctDirectory(const WString& asset_path)
		{
			WString	dir = asset_path;
			if(dir.find(L"/\\") != dir.npos)
				FormatFilePathInPlace(dir);
			return dir.substr(0, asset_path.find_last_of(L"/") + 1);
		}
	};

}



#endif // !PATH_H__

