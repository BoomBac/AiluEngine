#pragma once
#ifndef __RESOURCE_MGR_H__
#define __RESOURCE_MGR_H__
#include <unordered_map>
#include <optional>
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/Asset.h"
#include "Render/Material.h"
#include "Render/Texture.h"
#include "Framework/Assets/Mesh.h"
#include "Path.h"

namespace Ailu
{
	class ResourceMgr : public IRuntimeModule
	{
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick(const float& delta_time) final;
		void SaveScene(Scene* scene, std::string& scene_path);
		Scene* LoadScene(std::string& scene_path);
		void SaveAsset(const std::string asset_path, Material* mat);
		void SaveAsset(const Asset& asset);
		
		Ref<Texture2D> LoadTexture(const String& asset_path);
		//template<typename T>
		//static T* LoadAsset(const string& asset_path);
		static Material* LoadAsset(const String& asset_path);
	private:
		inline const static String kAssetDatabasePath = kEngineResRootPath + "assetdb.alasset";
		static Ref<Material> LoadMaterial(std::string path);
		static Ref<Material> LoadMaterial(const Vector<String>& blob);
		Ref<Mesh> LoadMeshImpl(const String& asset_path);
		static bool ExistInAssetDB(const Guid& guid);
		static void AddToAssetDB(const Asset& asset,bool override = true);
		static void LoadAssetDB();
		static void SaveAssetDB();
		static void FormatLine(const String& line, String& key, String& value);
		void SaveMaterialImpl(const String& asset_path,Material* mat);
		void SaveSceneImpl(Scene* scene, std::string& scene_path);
		void WatchDirectory();
	private:
		inline static std::map<String, std::tuple<String, String>> s_asset_db{};
		inline static std::map<String, Scope<Asset>> s_all_asset{};

	};
	extern ResourceMgr* g_pResourceMgr;

	//template<typename T>
	//inline T* ResourceMgr::LoadAsset(const string& asset_path)
	//{
	//	//if (std::is_same<T, Material>::value)
	//	//{
	//	//	auto mat = LoadMaterial(path);
	//	//	s_all_asset.insert(Asset{ asset_path,GetResPath(asset_path),guid,AssetType::kMaterial});
	//	//	return mat.get();
	//	//}
	//	return nullptr;
	//}
}

#endif // !RESOURCE_MGR_H__
