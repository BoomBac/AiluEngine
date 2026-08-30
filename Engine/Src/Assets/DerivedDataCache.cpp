#include "Assets/DerivedDataCache.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include "pch.h"

#include <fstream>
#include <unordered_set>
#include <Windows.h>

namespace Ailu
{
    namespace
    {
        bool IsHexDigit(wchar_t value)
        {
            return (value >= L'0' && value <= L'9') || (value >= L'a' && value <= L'f') ||
                   (value >= L'A' && value <= L'F');
        }

        bool GetArtifactGuid(const fs::path &path, WString &out_guid)
        {
            WString file_name = path.filename().wstring();
            constexpr WStringView kArtifactExtension = L".artifact";
            constexpr WStringView kTemporaryExtension = L".tmp";
            if (file_name.ends_with(kTemporaryExtension))
                file_name.resize(file_name.size() - kTemporaryExtension.size());
            if (!file_name.ends_with(kArtifactExtension))
                return false;

            file_name.resize(file_name.size() - kArtifactExtension.size());
            const size_t separator = file_name.find_last_of(L'_');
            if (separator != 16u || file_name.size() <= separator + 1u)
                return false;
            for (size_t index = 0u; index < separator; ++index)
            {
                if (!IsHexDigit(file_name[index]))
                    return false;
            }
            out_guid = file_name.substr(separator + 1u);
            return true;
        }

        template<typename Callback>
        bool VisitArtifactFiles(const WString &artifact_root, Callback &&callback)
        {
            std::error_code error;
            fs::directory_iterator prefix_iterator(artifact_root, error);
            if (error)
            {
                if (error == std::errc::no_such_file_or_directory)
                    return true;
                LOG_WARNING("DerivedDataCache: enumerate artifact root failed: {}", error.message());
                return false;
            }

            bool success = true;
            const fs::directory_iterator end;
            while (prefix_iterator != end)
            {
                std::error_code directory_error;
                if (prefix_iterator->is_directory(directory_error))
                {
                    fs::directory_iterator artifact_iterator(prefix_iterator->path(), directory_error);
                    if (directory_error)
                    {
                        LOG_WARNING("DerivedDataCache: enumerate artifact directory failed: {}",
                                    directory_error.message());
                        success = false;
                    }
                    else
                    {
                        const fs::directory_iterator artifact_end;
                        while (artifact_iterator != artifact_end)
                        {
                            const fs::path artifact_path = artifact_iterator->path();
                            std::error_code file_error;
                            const bool is_regular_file = artifact_iterator->is_regular_file(file_error);
                            if (file_error)
                            {
                                LOG_WARNING("DerivedDataCache: inspect artifact file failed: {}", file_error.message());
                                success = false;
                                break;
                            }

                            artifact_iterator.increment(file_error);
                            if (file_error)
                            {
                                LOG_WARNING("DerivedDataCache: enumerate artifact files failed: {}",
                                            file_error.message());
                                success = false;
                                break;
                            }
                            if (is_regular_file && !callback(artifact_path))
                                success = false;
                        }
                    }
                }
                else if (directory_error)
                {
                    success = false;
                }

                prefix_iterator.increment(error);
                if (error)
                {
                    LOG_WARNING("DerivedDataCache: enumerate artifact root failed: {}", error.message());
                    success = false;
                    break;
                }
            }
            return success;
        }
    }

    DerivedDataCache::DerivedDataCache(WString library_root)
    {
        _artifact_root = PathUtils::NormalizeDirectoryPath((fs::path(library_root) / L"Artifacts").wstring());
        FileManager::CreateDirectory(_artifact_root);
    }

    WString DerivedDataCache::GetArtifactPath(const Guid &asset_guid, const AssetArtifactKey &key) const
    {
        const u64 key_hash = key.Hash();
        const WString prefix = std::format(L"{:02x}", static_cast<u32>(key_hash & 0xffu));
        const WString guid = ToWChar(asset_guid.ToString());
        const WString file_name = std::format(L"{:016x}_{}.artifact", key_hash, guid);
        return PathUtils::FormatFilePath((fs::path(_artifact_root) / prefix / file_name).wstring());
    }

    bool DerivedDataCache::TryLoad(const Guid &asset_guid, const AssetArtifactKey &key, Vector<u8> &out_data) const
    {
        const WString artifact_path = GetArtifactPath(asset_guid, key);
        std::ifstream file(artifact_path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return false;

        const std::streamsize file_size = file.tellg();
        if (file_size <= 0)
            return false;
        file.seekg(0, std::ios::beg);
        out_data.resize(static_cast<size_t>(file_size));
        if (!file.read(reinterpret_cast<char *>(out_data.data()), file_size))
        {
            out_data.clear();
            return false;
        }

        AssetArtifactReader reader;
        if (!reader.Load(out_data, key))
        {
            out_data.clear();
            return false;
        }
        return true;
    }

    bool DerivedDataCache::Store(const Guid &asset_guid, const AssetArtifactKey &key, std::span<const u8> data) const
    {
        AssetArtifactReader reader;
        if (!reader.Load(data, key))
        {
            LOG_WARNING("DerivedDataCache: refusing invalid artifact for key {:016x}", key.Hash());
            return false;
        }

        const WString target_path = GetArtifactPath(asset_guid, key);
        const fs::path target(target_path);
        const fs::path temporary = target.wstring() + L".tmp";
        try
        {
            FileManager::CreateDirectory(target.parent_path().wstring());
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                {
                    std::error_code ignored;
                    fs::remove(temporary, ignored);
                    return false;
                }
                file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
                file.flush();
                if (!file.good())
                {
                    std::error_code ignored;
                    fs::remove(temporary, ignored);
                    return false;
                }
            }
            if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
            {
                std::error_code ignored;
                fs::remove(temporary, ignored);
                return false;
            }
            return true;
        }
        catch (const fs::filesystem_error &error)
        {
            LOG_WARNING("DerivedDataCache: store failed: {}", error.what());
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
    }


    bool DerivedDataCache::RemoveArtifacts(const Guid &asset_guid) const
    {
        if (!asset_guid.IsValid())
            return true;

        const WString guid = ToWChar(asset_guid.ToString());
        u32 removed_count = 0u;
        const bool success = VisitArtifactFiles(_artifact_root, [&](const fs::path &path)
        {
            WString artifact_guid;
            if (!GetArtifactGuid(path, artifact_guid) || artifact_guid != guid)
                return true;

            std::error_code remove_error;
            if (!fs::remove(path, remove_error) || remove_error)
            {
                LOG_WARNING("DerivedDataCache: remove artifact failed: {}", remove_error.message());
                return false;
            }
            ++removed_count;
            return true;
        });
        if (removed_count > 0u)
            LOG_INFO("DerivedDataCache: removed {} artifact(s) for asset {}", removed_count, asset_guid.ToString());
        return success;
    }

    bool DerivedDataCache::RemoveOrphanArtifacts(const Vector<Guid> &live_asset_guids) const
    {
        std::unordered_set<WString> live_guids;
        live_guids.reserve(live_asset_guids.size());
        for (const Guid &asset_guid : live_asset_guids)
        {
            if (asset_guid.IsValid())
                live_guids.emplace(ToWChar(asset_guid.ToString()));
        }

        u32 removed_count = 0u;
        const bool success = VisitArtifactFiles(_artifact_root, [&](const fs::path &path)
        {
            WString artifact_guid;
            if (!GetArtifactGuid(path, artifact_guid) || live_guids.contains(artifact_guid))
                return true;

            std::error_code remove_error;
            if (!fs::remove(path, remove_error) || remove_error)
            {
                LOG_WARNING("DerivedDataCache: remove orphan artifact failed: {}", remove_error.message());
                return false;
            }
            ++removed_count;
            return true;
        });
        if (removed_count > 0u)
            LOG_INFO("DerivedDataCache: removed {} orphan artifact(s)", removed_count);
        return success;
    }
}
