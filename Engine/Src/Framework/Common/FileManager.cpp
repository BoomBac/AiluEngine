#include "pch.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/LogMgr.h"

namespace Ailu
{

	void FileManager::CreateDirectory(const WString& dir_name)
	{
		fs::create_directories(dir_name);
	}
	void FileManager::DeleteDirectory(const WString& dir_name)
	{
		fs::remove(fs::path(dir_name));
	}
	void FileManager::RenameDirectory(const WString& old_name, const WString& new_name)
	{
		fs::rename(fs::path(old_name), fs::path(new_name));
	}
	void FileManager::CopyFile(const WString& src_file, const WString& dest_file)
	{
		if (PathUtils::FormatFilePath(src_file) == PathUtils::FormatFilePath(dest_file))
			return;
		try 
		{
			if (!FileManager::Exist(src_file))
			{
				g_pLogMgr->LogErrorFormat(std::source_location::current(), L"Path {} not exist on the disk!", src_file);
				return;
			}
			fs::copy_file(src_file, dest_file, fs::copy_options::overwrite_existing);
			LOG_INFO("File copied successfully.");
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			LOG_ERROR("Failed to copy file: {}", e.what());
		}
	}
	bool FileManager::Exist(const WString& path)
	{
		return fs::exists(path);
	}
	void FileManager::BackToParent()
	{
		s_cur_path = s_cur_path.parent_path();
		SetCurPath(s_cur_path);
	}
	void FileManager::SetCurPath(const fs::path& path)
	{
		s_cur_path = path;
		fs::directory_entry cur_entry(path);
		if (cur_entry.is_directory())
		{
			s_cur_dir_str = path.wstring();
			PathUtils::FormatFilePathInPlace(s_cur_dir_str);
			s_cur_path_str = s_cur_dir_str;
		}
		else
		{
			s_cur_dir_str = path.parent_path().wstring();
			PathUtils::FormatFilePathInPlace(s_cur_dir_str);
			s_cur_path_str = path.wstring();
			PathUtils::FormatFilePathInPlace(s_cur_path_str);
		}
		s_cur_dir_str += L"/";
	}
}
