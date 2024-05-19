#pragma once
#ifndef __ASSET_H__
#define __ASSET_H__
#include "GlobalMarco.h"
#include "Framework/Math/Guid.h"
#include "Objects/Object.h"

namespace Ailu
{
	DECLARE_ENUM(EAssetType,kUndefined, kMesh, kMaterial,kTexture2D,kShader,kComputeShader,kScene)
	class Asset
	{
	public:
		Asset() = default;
		Asset(EAssetType::EAssetType type,const WString& asset_path);
		Asset(Guid guid, EAssetType::EAssetType type,const WString& asset_path);
		Asset(const Asset& other) = delete;
		Asset& operator=(const Asset& other) = delete;
		//Asset(Asset&& other);
		//Asset& operator=(Asset&& other);
		virtual ~Asset();
		bool operator<(const Asset& other) const{return _p_obj->ID() < _p_obj->ID();}
		
		void AssignGuid(const Guid& guid);
		const Guid& GetGuid() const { return _guid; };
		template<typename T>
		T* As() const
		{
			return dynamic_cast<T*>(_p_obj.get());
		}
		template<typename T>
		Ref<T> AsRef()
		{
			return std::dynamic_pointer_cast<T>(_p_obj);
		}
	public:
		WString _asset_path;
		//with ext
		WString _name;
		//使用额外的信息来定位资源对象，对于对于fbx文件，使用资源路径和文件内对象的名称来确定一个mesh，对于shader，目前使用vs/ps的入口。
		WString _addi_info;
		WString _external_asset_path;
		EAssetType::EAssetType _asset_type;
		Ref<Object> _p_obj;
	private:
		Guid _guid;
	};
}

#endif // !ASSET_H__

