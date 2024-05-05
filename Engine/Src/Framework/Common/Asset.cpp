#include "pch.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/Path.h"

namespace Ailu
{
    Asset::Asset(EAssetType::EAssetType type, const WString& asset_path) : Asset(Guid::Generate(), type, asset_path)
    {
    }

    Asset::Asset(Guid guid, EAssetType::EAssetType type, const WString& asset_path) :
    _instance_id(s_instance_num++), _guid(guid), _asset_type(type), _p_inst_asset(nullptr)
    {
        _asset_path = asset_path;
        _sys_path = PathUtils::GetResSysPath(_asset_path);
        _full_name = PathUtils::GetFileName(_asset_path,true);
        _name = _full_name.substr(0, _full_name.find_last_of('.'));
    }

    void Asset::AssignGuid(const Guid& guid)
    {
        _guid = guid;
    }

	Asset::~Asset()
	{
	}
}
