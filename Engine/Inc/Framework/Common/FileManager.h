#pragma once
#ifndef __FILEMGR_H__
#define __FILEMGR_H__
#include "GlobalMarco.h"
#include "Framework/Common/Path.h"

namespace Ailu
{
#undef CreateDirectory
#undef CopyFile
#undef CreateFile
	namespace fs = std::filesystem;
	class AILU_API FileManager
	{
	public:
		static void CreateDirectory(const WString& dir_name);
		static void DeleteDirectory(const WString& dir_name);
		static void RenameDirectory(const WString& old_name, const WString& new_name);
		static bool CopyFile(const WString& src_file, const WString& dest_file);
		static bool CopyDirectory(const WString& src_file, const WString& dest_file,bool recursive = true);
		static bool Exist(const WString& path);
		static void BackToParent();
		static const WString& GetCurSysPathStr() { return s_cur_path_str; };
		//系统路径 aa/bb
		static const WString& GetCurSysDirStr() { return s_cur_dir_str; }
		static const fs::path& GetCurSysPath() { return s_cur_path; }
		static WString GetCurAssetDirStr()
		{
			return PathUtils::ExtractAssetPath(s_cur_path_str);
		}
		static void SetCurPath(const fs::path& path);

		static bool CreateFile(const WString& sys_path,bool override = true);
		static bool WriteFile(const WString& sys_path, bool append, const WString& data);
		static bool WriteFile(const WString& sys_path, bool append, const u8* data,u64 data_size);
		static bool ReadFile(const WString& sys_path,WString& data);
		static bool ReadFile(const WString& sys_path,u8* data, u64 data_start = 0u,u64 data_size = -1);
		//使用结束后记得销毁
		static std::tuple<u8*,u64> ReadFile(const WString& sys_path,u64 data_start = 0u, u64 data_size = -1);
		//-------------------------static end-----------------------------
	public:

	private:
		inline static WString s_cur_path_str = kEngineResRootPathW;
		inline static WString s_cur_dir_str = kEngineResRootPathW;
		inline static fs::path s_cur_path = kEngineResRootPathW;
	};
}


#endif // !FILEMGR_H__

