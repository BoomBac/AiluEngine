#include "Assets/AssetArtifact.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Interface/IParser.h"
#include "pch.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace Ailu
{
    namespace
    {
        constexpr u64 kFnvOffset = 14695981039346656037ull;
        constexpr u64 kFnvPrime = 1099511628211ull;

        void HashBytes(u64 &hash, const void *data, size_t size)
        {
            const auto *bytes = static_cast<const u8 *>(data);
            for (size_t index = 0u; index < size; ++index)
            {
                hash ^= bytes[index];
                hash *= kFnvPrime;
            }
        }

        template<typename T>
        void AppendValue(Vector<u8> &data, const T &value)
        {
            const auto *begin = reinterpret_cast<const u8 *>(&value);
            data.insert(data.end(), begin, begin + sizeof(T));
        }

        template<typename T>
        bool ReadValue(std::span<const u8> data, size_t &offset, T &value)
        {
            if (offset > data.size() || sizeof(T) > data.size() - offset)
                return false;
            memcpy(&value, data.data() + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

        bool ReadArtifactHeader(std::span<const u8> data, size_t &offset, AssetArtifactHeader &header)
        {
            u16 type = 0u;
            if (!ReadValue(data, offset, header._magic) || !ReadValue(data, offset, header._container_version) ||
                !ReadValue(data, offset, type) || !ReadValue(data, offset, header._artifact_version) ||
                !ReadValue(data, offset, header._section_count) || !ReadValue(data, offset, header._cache_key) ||
                !ReadValue(data, offset, header._file_size))
            {
                return false;
            }
            header._type = static_cast<EAssetArtifactType>(type);
            return true;
        }

        bool ReadArtifactSection(std::span<const u8> data, size_t &offset, AssetArtifactSection &section)
        {
            return ReadValue(data, offset, section._type) && ReadValue(data, offset, section._flags) &&
                   ReadValue(data, offset, section._offset) && ReadValue(data, offset, section._size) &&
                   ReadValue(data, offset, section._element_count) && ReadValue(data, offset, section._element_stride);
        }

        bool IsRangeValid(const AssetArtifactSection &section, u64 file_size, u64 data_start)
        {
            if (section._offset < data_start || section._offset > file_size)
                return false;
            return section._size <= file_size - section._offset;
        }

        u64 HashFile(const WString &system_path)
        {
            std::ifstream file(system_path, std::ios::binary);
            if (!file.is_open())
                return 0u;

            u64 hash = kFnvOffset;
            Vector<u8> buffer(64u * 1024u);
            while (file.good())
            {
                file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = file.gcount();
                if (count > 0)
                    HashBytes(hash, buffer.data(), static_cast<size_t>(count));
            }
            return hash;
        }

        struct CachedFingerprint
        {
            SourceFingerprint _fingerprint;
        };

        std::mutex s_fingerprint_mutex;
        std::unordered_map<WString, CachedFingerprint> s_fingerprints;
    }

    u64 AssetArtifactKey::Hash() const
    {
        u64 hash = kFnvOffset;
        HashBytes(hash, &_source_hash, sizeof(_source_hash));
        HashBytes(hash, &_import_setting_hash, sizeof(_import_setting_hash));
        HashBytes(hash, &_dependency_hash, sizeof(_dependency_hash));
        HashBytes(hash, &_importer_version, sizeof(_importer_version));
        HashBytes(hash, &_artifact_version, sizeof(_artifact_version));
        return hash;
    }

    bool CalculateSourceFingerprint(const WString &system_path, SourceFingerprint &out_fingerprint)
    {
        try
        {
            const fs::path path(system_path);
            if (!fs::is_regular_file(path))
                return false;

            const u64 file_size = static_cast<u64>(fs::file_size(path));
            const i64 last_write_time = static_cast<i64>(fs::last_write_time(path).time_since_epoch().count());
            {
                std::lock_guard lock(s_fingerprint_mutex);
                auto iter = s_fingerprints.find(system_path);
                if (iter != s_fingerprints.end() && iter->second._fingerprint._file_size == file_size &&
                    iter->second._fingerprint._last_write_time == last_write_time)
                {
                    out_fingerprint = iter->second._fingerprint;
                    return true;
                }
            }

            SourceFingerprint fingerprint;
            fingerprint._file_size = file_size;
            fingerprint._last_write_time = last_write_time;
            fingerprint._content_hash = HashFile(system_path);
            if (file_size != 0u && fingerprint._content_hash == 0u)
                return false;

            std::lock_guard lock(s_fingerprint_mutex);
            s_fingerprints[system_path] = CachedFingerprint{fingerprint};
            out_fingerprint = fingerprint;
            return true;
        }
        catch (const fs::filesystem_error &)
        {
            return false;
        }
    }

    u64 HashTextureImportSetting(const TextureImportSetting &setting)
    {
        u64 hash = kFnvOffset;
        const u32 name_length = static_cast<u32>(setting._name_id.size());
        HashBytes(hash, &name_length, sizeof(name_length));
        HashBytes(hash, setting._name_id.data(), setting._name_id.size());
        HashBytes(hash, &setting._is_copy, sizeof(setting._is_copy));
        HashBytes(hash, &setting._is_reimport, sizeof(setting._is_reimport));
        HashBytes(hash, &setting._content, sizeof(setting._content));
        HashBytes(hash, &setting._is_srgb, sizeof(setting._is_srgb));
        HashBytes(hash, &setting._generate_mipmap, sizeof(setting._generate_mipmap));
        HashBytes(hash, &setting._is_readable, sizeof(setting._is_readable));
        HashBytes(hash, &setting._max_size, sizeof(setting._max_size));
        HashBytes(hash, &setting._compression, sizeof(setting._compression));
        HashBytes(hash, &setting._compression_quality, sizeof(setting._compression_quality));
        return hash;
    }

    u64 HashArtifactDependency(const String &dependency_uri, const SourceFingerprint &fingerprint)
    {
        u64 hash = kFnvOffset;
        const u32 uri_length = static_cast<u32>(dependency_uri.size());
        HashBytes(hash, &uri_length, sizeof(uri_length));
        HashBytes(hash, dependency_uri.data(), dependency_uri.size());
        HashBytes(hash, &fingerprint._file_size, sizeof(fingerprint._file_size));
        HashBytes(hash, &fingerprint._last_write_time, sizeof(fingerprint._last_write_time));
        HashBytes(hash, &fingerprint._content_hash, sizeof(fingerprint._content_hash));
        return hash;
    }

    u64 HashMeshImportSetting(const MeshImportSetting &setting)
    {
        u64 hash = kFnvOffset;
        const u32 name_length = static_cast<u32>(setting._name_id.size());
        HashBytes(hash, &name_length, sizeof(name_length));
        HashBytes(hash, setting._name_id.data(), setting._name_id.size());
        HashBytes(hash, &setting._is_copy, sizeof(setting._is_copy));
        HashBytes(hash, &setting._is_reimport, sizeof(setting._is_reimport));
        HashBytes(hash, &setting._is_recalculate_normals, sizeof(setting._is_recalculate_normals));
        HashBytes(hash, &setting._import_flag, sizeof(setting._import_flag));
        HashBytes(hash, &setting._is_combine_mesh, sizeof(setting._is_combine_mesh));
        HashBytes(hash, setting._mesh_name.data(), setting._mesh_name.size());
        HashBytes(hash, &setting._animation_stack_index, sizeof(setting._animation_stack_index));
        HashBytes(hash, &setting._import_all_animation_stacks, sizeof(setting._import_all_animation_stacks));
        const String skeleton_guid = setting._skeleton.ToString();
        const u32 skeleton_guid_length = static_cast<u32>(skeleton_guid.size());
        HashBytes(hash, &skeleton_guid_length, sizeof(skeleton_guid_length));
        HashBytes(hash, skeleton_guid.data(), skeleton_guid.size());
        return hash;
    }

    void AssetArtifactWriter::AddSection(u32 type, std::span<const u8> data, u32 element_count,
                                         u32 element_stride, u32 flags)
    {
        PendingSection section;
        section._section._type = type;
        section._section._flags = flags;
        section._section._element_count = element_count;
        section._section._element_stride = element_stride;
        section._data.assign(data.begin(), data.end());
        section._section._size = section._data.size();
        _sections.emplace_back(std::move(section));
    }

    bool AssetArtifactWriter::Serialize(EAssetArtifactType type, u32 artifact_version,
                                        const AssetArtifactKey &key, Vector<u8> &out_data) const
    {
        if (type == EAssetArtifactType::kUnknown || artifact_version != key._artifact_version)
            return false;
        constexpr u64 kHeaderSize = sizeof(u32) + sizeof(u16) + sizeof(u16) + sizeof(u32) + sizeof(u32) +
                                     sizeof(u64) + sizeof(u64);
        constexpr u64 kSectionSize = sizeof(u32) + sizeof(u32) + sizeof(u64) + sizeof(u64) + sizeof(u32) +
                                      sizeof(u32);
        const u64 table_size = kSectionSize * _sections.size();
        u64 data_offset = kHeaderSize + table_size;
        if (data_offset > (std::numeric_limits<u64>::max)())
            return false;

        Vector<AssetArtifactSection> sections;
        sections.reserve(_sections.size());
        for (const PendingSection &pending : _sections)
        {
            AssetArtifactSection section = pending._section;
            section._offset = data_offset;
            if (section._size > (std::numeric_limits<u64>::max)() - data_offset)
                return false;
            data_offset += section._size;
            sections.emplace_back(section);
        }

        out_data.clear();
        out_data.reserve(static_cast<size_t>(data_offset));
        const u32 magic = kAssetArtifactMagic;
        const u16 container_version = kAssetArtifactContainerVersion;
        const u16 type_value = static_cast<u16>(type);
        const u32 section_count = static_cast<u32>(sections.size());
        AppendValue(out_data, magic);
        AppendValue(out_data, container_version);
        AppendValue(out_data, type_value);
        AppendValue(out_data, artifact_version);
        AppendValue(out_data, section_count);
        AppendValue(out_data, key.Hash());
        AppendValue(out_data, data_offset);
        for (const AssetArtifactSection &section : sections)
        {
            AppendValue(out_data, section._type);
            AppendValue(out_data, section._flags);
            AppendValue(out_data, section._offset);
            AppendValue(out_data, section._size);
            AppendValue(out_data, section._element_count);
            AppendValue(out_data, section._element_stride);
        }
        for (const PendingSection &pending : _sections)
            out_data.insert(out_data.end(), pending._data.begin(), pending._data.end());
        return out_data.size() == data_offset;
    }

    bool AssetArtifactReader::Load(std::span<const u8> data, const AssetArtifactKey &key,
                                   EAssetArtifactType expected_type)
    {
        size_t offset = 0u;
        AssetArtifactHeader header;
        if (!ReadArtifactHeader(data, offset, header) || header._magic != kAssetArtifactMagic ||
            header._container_version != kAssetArtifactContainerVersion ||
            (expected_type != EAssetArtifactType::kUnknown && header._type != expected_type) ||
            header._artifact_version != key._artifact_version || header._cache_key != key.Hash() ||
            header._file_size != data.size())
        {
            return false;
        }

        constexpr u64 kSectionSize = sizeof(u32) + sizeof(u32) + sizeof(u64) + sizeof(u64) + sizeof(u32) +
                                     sizeof(u32);
        if (header._section_count > (data.size() - offset) / kSectionSize)
            return false;
        const u64 data_start = offset + header._section_count * kSectionSize;
        if (data_start > data.size())
            return false;

        Vector<AssetArtifactSection> sections(header._section_count);
        for (AssetArtifactSection &section : sections)
        {
            if (!ReadArtifactSection(data, offset, section) ||
                !IsRangeValid(section, static_cast<u64>(data.size()), data_start))
            {
                return false;
            }
        }

        _header = header;
        _sections = std::move(sections);
        _data = data;
        return true;
    }

    std::span<const u8> AssetArtifactReader::GetSectionData(u32 type) const
    {
        for (const AssetArtifactSection &section : _sections)
        {
            if (section._type == type)
                return _data.subspan(static_cast<size_t>(section._offset), static_cast<size_t>(section._size));
        }
        return {};
    }
}
