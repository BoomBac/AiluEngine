#pragma once
#ifndef __ASSET_H__
#define __ASSET_H__
#include "GlobalMarco.h"
#include "Framework/Math/Guid.h"

namespace Ailu
{
	DECLARE_ENUM(EAssetType,kUndefined, kMesh, kMaterial, kTexture2D)

	class IAssetable;
	class Asset
	{
	public:
		inline static u32 s_instance_num = 0u;
	public:
		Asset() = default;
		Asset(EAssetType::EAssetType type,const WString& sys_path);
		Asset(Guid guid, EAssetType::EAssetType type,const WString& sys_path);
		Asset(const Asset& other) = delete;
		Asset& operator=(const Asset& other) = delete;
		//Asset(Asset&& other);
		//Asset& operator=(Asset&& other);
		virtual ~Asset();
		bool operator<(const Asset& other) const{return _instance_id < other._instance_id;}
		
		void AssignGuid(const Guid& guid);
		const Guid& GetGuid() const { return _guid; };
	public:
		WString _asset_path;
		WString _system_path;
		WString _name;
		WString _full_name;
		EAssetType::EAssetType _asset_type;
		IAssetable* _p_inst_asset;
	private:
		u32 _instance_id;
		Guid _guid;
	};
}

#endif // !ASSET_H__

