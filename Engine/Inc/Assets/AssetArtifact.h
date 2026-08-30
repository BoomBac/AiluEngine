#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "Framework/Math/Guid.h"

#include <span>

namespace Ailu
{
    struct TextureImportSetting;
    struct MeshImportSetting;

    inline constexpr u32 kAssetArtifactMagic = 0x41494C55u;
    inline constexpr u16 kAssetArtifactContainerVersion = 1u;
    inline constexpr u32 kTextureArtifactVersion = 1u;
    inline constexpr u32 kMeshArtifactVersion = 2u;

    enum class EAssetArtifactType : u16
    {
        kUnknown = 0u,
        kMesh = 1u,
        kTexture = 2u,
    };

    struct AILU_API AssetArtifactKey
    {
        u64 _source_hash = 0u;
        u64 _import_setting_hash = 0u;
        u64 _dependency_hash = 0u;
        u32 _importer_version = 0u;
        u32 _artifact_version = 0u;

        [[nodiscard]] u64 Hash() const;
        bool operator==(const AssetArtifactKey &other) const = default;
    };

    struct AILU_API SourceFingerprint
    {
        u64 _file_size = 0u;
        i64 _last_write_time = 0;
        u64 _content_hash = 0u;
    };

    AILU_API bool CalculateSourceFingerprint(const WString &system_path, SourceFingerprint &out_fingerprint);
    AILU_API u64 HashArtifactDependency(const String &dependency_uri, const SourceFingerprint &fingerprint);
    AILU_API u64 HashTextureImportSetting(const TextureImportSetting &setting);
    AILU_API u64 HashMeshImportSetting(const MeshImportSetting &setting);

    struct AILU_API AssetArtifactHeader
    {
        u32 _magic = kAssetArtifactMagic;
        u16 _container_version = kAssetArtifactContainerVersion;
        EAssetArtifactType _type = EAssetArtifactType::kUnknown;
        u32 _artifact_version = 0u;
        u32 _section_count = 0u;
        u64 _cache_key = 0u;
        u64 _file_size = 0u;
    };

    struct AILU_API AssetArtifactSection
    {
        u32 _type = 0u;
        u32 _flags = 0u;
        u64 _offset = 0u;
        u64 _size = 0u;
        u32 _element_count = 0u;
        u32 _element_stride = 0u;
    };

    class AILU_API AssetArtifactWriter final
    {
    public:
        void AddSection(u32 type, std::span<const u8> data, u32 element_count = 0u, u32 element_stride = 0u,
                        u32 flags = 0u);
        bool Serialize(EAssetArtifactType type, u32 artifact_version, const AssetArtifactKey &key,
                       Vector<u8> &out_data) const;

    private:
        struct PendingSection
        {
            AssetArtifactSection _section;
            Vector<u8> _data;
        };

        Vector<PendingSection> _sections;
    };

    class AILU_API AssetArtifactReader final
    {
    public:
        bool Load(std::span<const u8> data, const AssetArtifactKey &key,
                  EAssetArtifactType expected_type = EAssetArtifactType::kUnknown);
        [[nodiscard]] std::span<const u8> GetSectionData(u32 type) const;
        [[nodiscard]] const AssetArtifactHeader &Header() const { return _header; }

    private:
        AssetArtifactHeader _header;
        Vector<AssetArtifactSection> _sections;
        std::span<const u8> _data;
    };
}
