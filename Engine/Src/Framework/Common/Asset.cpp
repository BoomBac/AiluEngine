#include "pch.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/Path.h"

namespace Ailu
{
    Asset::Asset(EAssetType::EAssetType type, const WString& asset_path) : Asset(Guid::Generate(), type, asset_path)
    {
    }

    Asset::Asset(Guid guid, EAssetType::EAssetType type, const WString& asset_path) : _guid(guid), _asset_type(type), _p_obj(nullptr)
    {
        _asset_path = asset_path;
        _name = PathUtils::GetFileName(_asset_path,true);
    }

    void Asset::AssignGuid(const Guid& guid)
    {
        _guid = guid;
    }

	Asset::~Asset()
	{
	}
}
