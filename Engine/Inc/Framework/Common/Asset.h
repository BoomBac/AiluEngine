#pragma once
#ifndef __ASSET_H__
#define __ASSET_H__
#include "GlobalMarco.h"
#include "Framework/Math/Guid.h"

namespace Ailu
{
	enum class EAssetType : uint8_t
	{
		kUndefined,kMesh,kMaterial,kTexture2D
	};

	class Asset
	{
	public:
		inline static uint32_t s_instance_num = 0u;
	public:
		Asset(String guid, EAssetType type);
		Asset(String asset_path, String system_path, String guid, EAssetType type);
		virtual ~Asset();
		bool operator<(const Asset& other) const
		{
			return _instance_id < other._instance_id;
		}
	public:
		String _asset_path;
		String _system_path;
		String _name;
		String _full_name;
		uint32_t _instance_id;
		Guid _guid;
		EAssetType _asset_type;
		void* _p_inst_asset;
	};

	struct AssetType
	{
		inline const static std::string kMaterial = "material";
		inline const static std::string kMesh = "mesh";
		inline const static std::string kTexture2D = "texture2d";
		inline const static std::string kUndefined = "undefined";
		static String GetTypeString(const EAssetType& type)
		{
			switch (type)
			{
			case Ailu::EAssetType::kMesh:
				return kMesh;
			case Ailu::EAssetType::kMaterial:
				return kMaterial;
			case Ailu::EAssetType::kTexture2D:
				return kTexture2D;
			case Ailu::EAssetType::kUndefined:
				return kUndefined;
			}
			return kUndefined;
		}
		static EAssetType GetType(const std::string& type_str)
		{
			if (type_str == AssetType::kMaterial) return EAssetType::kMaterial;
			else if (type_str == AssetType::kMesh) return EAssetType::kMesh;
			else if (type_str == AssetType::kTexture2D) return EAssetType::kTexture2D;
			else return EAssetType::kUndefined;
		}
	};
}

#endif // !ASSET_H__

