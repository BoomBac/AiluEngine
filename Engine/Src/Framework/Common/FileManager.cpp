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

	bool FileManager::CreateFile(const WString& sys_path, bool override)
	{
		fs::path p(sys_path);
		if (fs::exists(p) && !override)
		{
			g_pLogMgr->LogWarningFormat(L"CreateFile: File {} already exist!", sys_path);
			return false;
		}
		std::wofstream out_file(sys_path);
		AL_ASSERT(!out_file.is_open());
		out_file.close();
		return true;
	}
	bool FileManager::WriteFile(const WString& sys_path, bool append, const WString& data)
	{
		fs::path p(sys_path);
		if (!fs::exists(p))
		{
			g_pLogMgr->LogWarningFormat(L"WriteFile: File {} not exist!", sys_path);
			return false;
		}
		std::wofstream out_asset_file(sys_path, append? std::ios::app : std::ios::trunc);
		if (!out_asset_file.is_open()) {
			if (out_asset_file.fail()) {
				std::cerr << "Failed to open file: Format error or file doesn't exist" << std::endl;
			}
			else if (out_asset_file.bad()) {
				std::cerr << "Failed to open file: Unrecoverable error occurred" << std::endl;
			}
			else {
				std::cerr << "Failed to open file: Unknown reason" << std::endl;
			}
		}
		out_asset_file << data;
		out_asset_file.close();
		return true;
	}
	bool FileManager::WriteFile(const WString& sys_path, bool append, const u8* data, u64 data_size)
	{
		fs::path p(sys_path);
		if (!fs::exists(p))
		{
			g_pLogMgr->LogWarningFormat(L"WriteFile: File {} not exist!", sys_path);
			return false;
		}
		auto flag = std::ios::trunc | std::ios::binary;
		std::ofstream out_asset_file(sys_path, append ? flag | std::ios::app : flag);
		out_asset_file.write(reinterpret_cast<const char*>(data), data_size);
		out_asset_file.close();
		return true;
	}
	bool FileManager::ReadFile(const WString& sys_path, WString& data)
	{
		fs::path p(sys_path);
		if (!fs::exists(p))
		{
			g_pLogMgr->LogWarningFormat(L"ReadFile: File {} not exist!", sys_path);
			return false;
		}
		std::wifstream in_file(sys_path,std::ios::in);
		AL_ASSERT(!in_file.is_open());
		WString line;
		while (std::getline(in_file,line))
		{
			data += line;
			data += L"\n";
		}
		in_file.close();
		return true;
	}
	bool FileManager::ReadFile(const WString& sys_path, u8* data, u64 data_start, u64 data_size)
	{
		fs::path p(sys_path);
		if (!fs::exists(p))
		{
			g_pLogMgr->LogWarningFormat(L"ReadFile: File {} not exist!", sys_path);
			return false;
		}
		std::ifstream in_file(sys_path, std::ios::binary);
		AL_ASSERT(!in_file.is_open());
		in_file.seekg(data_start, std::ios::beg);
		in_file.read(reinterpret_cast<char*>(data), data_size);
		if (in_file.fail()) 
		{
			g_pLogMgr->LogErrorFormat(L"Failed to read file {} at position {}!", sys_path, data_start);
			in_file.close();
			return false;
		}
		in_file.close();
		return true;
	}
}
