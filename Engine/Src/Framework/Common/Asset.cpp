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
		_name = PathUtils::GetFileName(_asset_path, true);
	}

	void Asset::CopyFrom(const Asset& other)
	{
		_addi_info = other._addi_info;
		_external_asset_path = other._external_asset_path;
		_p_obj = other._p_obj;
		_guid = other._guid;
		_asset_path = other._asset_path;
		_asset_type = other._asset_type;
		_name = other._name;
	}

	void Asset::AssignGuid(const Guid& guid)
	{
		_guid = guid;
	}

	Asset::~Asset()
	{
	}
}
