#pragma once

#include "Assets/AssetArtifact.h"

namespace Ailu
{
    class AILU_API DerivedDataCache final
    {
    public:
        explicit DerivedDataCache(WString library_root);

        bool TryLoad(const Guid &asset_guid, const AssetArtifactKey &key, Vector<u8> &out_data) const;
        bool Store(const Guid &asset_guid, const AssetArtifactKey &key, std::span<const u8> data) const;
        bool RemoveArtifacts(const Guid &asset_guid) const;
        bool RemoveOrphanArtifacts(const Vector<Guid> &live_asset_guids) const;

        [[nodiscard]] WString GetArtifactPath(const Guid &asset_guid, const AssetArtifactKey &key) const;

    private:
        WString _artifact_root;
    };
}
