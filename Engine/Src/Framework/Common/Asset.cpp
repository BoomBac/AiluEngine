#include "pch.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Utils.h"

namespace Ailu
{
    Asset::Asset(String guid, EAssetType type) : _instance_id(s_instance_num++), _guid(guid),_asset_type(type),_p_inst_asset(nullptr)
    {
    }

    Asset::Asset(String asset_path, String system_path, String guid, EAssetType type) : Asset(guid, type)
    {
        _asset_path = asset_path;
        _system_path = system_path;
    }

	Asset::~Asset()
	{
	}
}
