#pragma once
#ifndef __ASSET_H__
#define __ASSET_H__
#include "GlobalMarco.h"
#include "Framework/Math/Guid.h"

namespace Ailu
{
	DECLARE_ENUM(EAssetType,kUndefined, kMesh, kMaterial,kTexture2D,kShader,kScene)

	class IAssetable;
	class Asset
	{
	public:
		inline static u32 s_instance_num = 0u;
	public:
		Asset() = default;
		Asset(EAssetType::EAssetType type,const WString& asset_path);
		Asset(Guid guid, EAssetType::EAssetType type,const WString& asset_path);
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
		WString _sys_path;
		WString _name;
		WString _full_name;
		//使用额外的信息来定位资源对象，对于对于fbx文件，使用资源路径和文件内对象的名称来确定一个mesh，对于shader，目前使用vs/ps的入口。
		WString _addi_info;
		EAssetType::EAssetType _asset_type;
		IAssetable* _p_inst_asset;
	private:
		u32 _instance_id;
		Guid _guid;
	};
}

#endif // !ASSET_H__

