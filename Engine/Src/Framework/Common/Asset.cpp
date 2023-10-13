#include "pch.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Utils.h"

namespace Ailu
{
	Asset::Asset(string asset_path, string system_path) : _asset_path(asset_path),_system_path(system_path)
	{
		_name = GetFileName(asset_path);
		_instance_id = s_instance_num++;
	}
	Asset::~Asset()
	{
	}
}
