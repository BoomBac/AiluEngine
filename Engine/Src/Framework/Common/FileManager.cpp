#include "pch.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"

namespace Ailu
{
	void FileManager::CreateDirectory(const WString& dir_name)
	{
		fs::create_directories(dir_name);
	}
	void FileManager::DeleteDirectory(const WString& dir_name)
	{
		try
		{
			fs::remove_all(dir_name);
		}
		catch (const std::exception& e)
		{
			LOG_WARNING(e.what());
		}
	}
	void FileManager::RenameDirectory(const WString& old_name, const WString& new_name)
	{
		fs::rename(fs::path(old_name), fs::path(new_name));
	}
	bool FileManager::CopyFile(const WString& src_file, const WString& dest_file)
	{
		if (PathUtils::FormatFilePath(src_file) == PathUtils::FormatFilePath(dest_file))
			return true;
		try 
		{
			if (!FileManager::Exist(src_file))
			{
				g_pLogMgr->LogErrorFormat(std::source_location::current(), L"CopyFile: Path {} not exist on the disk!", src_file);
				return false;
			}
			fs::copy_file(src_file, dest_file, fs::copy_options::overwrite_existing);
			LOG_INFO("File copied successfully.");
			return true;
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			LOG_ERROR("Failed to copy file: {}", e.what());
			return false;
		}
	}
	bool FileManager::CopyDirectory(const WString& src_file, const WString& dest_file, bool recursive)
	{
		if (PathUtils::FormatFilePath(src_file) == PathUtils::FormatFilePath(dest_file))
			return true;
		try
		{
			if (!FileManager::Exist(src_file))
			{
				g_pLogMgr->LogErrorFormat(std::source_location::current(), L"CopyDirectory: Path {} not exist on the disk!", src_file);
				return false;
			}
			auto flag = fs::copy_options::overwrite_existing;
			if (recursive)
			{
				flag |= fs::copy_options::recursive;
			}
			fs::copy(src_file, dest_file, flag);
			LOG_INFO("Directory copied successfully.");
			return true;
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			LOG_ERROR("Failed to copy file: {}", e.what());
			return false;
		}
	}
	bool FileManager::Exist(const WString& path)
	{
		return fs::exists(path);
	}

	void FileManager::BackToParent()
	{
		auto parent = s_cur_path.parent_path();
		if(parent.string().find("Res") != std::string::npos)
		{
			s_cur_path = parent;
			SetCurPath(s_cur_path);
		}
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
		AL_ASSERT(out_file.is_open());
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
	bool FileManager::WriteFile(const WString& sys_path, bool append, const String& data)
	{
		fs::path p(sys_path);
		if (!fs::exists(p))
		{
			g_pLogMgr->LogWarningFormat(L"WriteFile: File {} not exist!", sys_path);
			return false;
		}
		std::ofstream out_asset_file(sys_path, append ? std::ios::app : std::ios::trunc);
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
		AL_ASSERT(in_file.is_open());
		WString line;
		while (std::getline(in_file,line))
		{
			data += line;
			data += L"\n";
		}
		in_file.close();
		return true;
	}
	bool FileManager::ReadFile(const WString& sys_path, String& data)
	{
		fs::path p(sys_path);
		if (!fs::exists(p))
		{
			g_pLogMgr->LogWarningFormat(L"ReadFile: File {} not exist!", sys_path);
			return false;
		}
		std::ifstream in_file(sys_path, std::ios::in);
		AL_ASSERT(in_file.is_open());
		String line;
		while (std::getline(in_file, line))
		{
			data += line;
			data += "\n";
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
		AL_ASSERT(in_file.is_open());
		in_file.seekg(data_start, std::ios::end);
		auto file_size = in_file.tellg();
        in_file.seekg(data_start, std::ios::beg);
		in_file.read(reinterpret_cast<char*>(data), data_size == -1? (u64)file_size : data_size);
		if (in_file.fail()) 
		{
			g_pLogMgr->LogErrorFormat(L"Failed to read file {} at position {}!", sys_path, data_start);
			in_file.close();
			return false;
		}
		in_file.close();
		return true;
	}
	std::tuple<u8*, u64> FileManager::ReadFile(const WString& sys_path, u64 data_start, u64 data_size)
	{
		fs::path p(sys_path);
		u64 file_byte_size = -1;
		if (!fs::exists(p))
		{
			g_pLogMgr->LogWarningFormat(L"ReadFile: File {} not exist!", sys_path);
			return std::tuple<u8*, u64>(nullptr,-1);
		}
		std::ifstream in_file(sys_path, std::ios::binary);
		AL_ASSERT(in_file.is_open());
		in_file.seekg(data_start, std::ios::end);
		file_byte_size = (u64)in_file.tellg();
		in_file.seekg(data_start, std::ios::beg);
		data_size = data_size == -1 ? file_byte_size : data_size;
		u8* read_data = new u8[data_size];
		in_file.read(reinterpret_cast<char*>(read_data), data_size);
		in_file.close();
		if (in_file.fail())
		{
			g_pLogMgr->LogErrorFormat(L"Failed to read file {} at position {}!", sys_path, data_start);		
			delete[] read_data;
			return std::tuple<u8*, u64>(nullptr, -1);
		}
		return std::tuple<u8*, u64>(read_data,file_byte_size);
	}
}
