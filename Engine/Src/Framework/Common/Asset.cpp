#include "pch.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/Path.h"

namespace Ailu
{
    Asset::Asset(EAssetType::EAssetType type, const WString& sys_path) : Asset(Guid::Generate(), type, sys_path)
    {
    }

    Asset::Asset(Guid guid, EAssetType::EAssetType type, const WString& sys_path) : 
    _instance_id(s_instance_num++), _guid(guid), _asset_type(type), _p_inst_asset(nullptr)
    {
        _asset_path = PathUtils::ExtractAssetPath(sys_path);
        _system_path = sys_path;
    }

    void Asset::AssignGuid(const Guid& guid)
    {
        _guid = guid;
    }

	Asset::~Asset()
	{
	}
}
