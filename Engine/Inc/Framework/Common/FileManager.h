#pragma once
#ifndef __FILEMGR_H__
#define __FILEMGR_H__
#include "GlobalMarco.h"
#include "Framework/Common/Path.h"

namespace Ailu
{
#undef CreateDirectory
#undef CopyFile
	namespace fs = std::filesystem;
	class FileManager
	{
	public:
		static void CreateDirectory(const WString& dir_name);
		static void DeleteDirectory(const WString& dir_name);
		static void RenameDirectory(const WString& old_name, const WString& new_name);
		static void CopyFile(const WString& src_file, const WString& dest_file);
		static bool Exist(const WString& path);
		static void BackToParent();
		static const WString& GetCurPathStr() { return s_cur_path_str; };
		static const WString& GetCurDirStr() { return s_cur_dir_str; }
		static const fs::path& GetCurPath() { return s_cur_path; }
		static void SetCurPath(const fs::path& path);
	private:
		inline static WString s_cur_path_str = kEngineResRootPathW;
		inline static WString s_cur_dir_str = kEngineResRootPathW;
		inline static fs::path s_cur_path = kEngineResRootPathW;
	};
}


#endif // !FILEMGR_H__

