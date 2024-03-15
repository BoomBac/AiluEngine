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
#include "Render/Mesh.h"
#include "Path.h"

namespace Ailu
{
	class ResourceMgr : public IRuntimeModule
	{
		using ResourceTask = std::function<void()>;
		using OnResourceTaskCompleted = std::function<void(Ref<void> asset)>;
		struct ImportInfo
		{
			String _msg;
			WString _sys_path;
			float _progress;
			std::filesystem::file_time_type _last_write_time;
			bool _is_succeed;
		};
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick(const float& delta_time) final;
		auto Begin() { return s_all_asset.begin(); };
		auto End() { return s_all_asset.end(); };
		void SaveScene(Scene* scene, std::string& scene_path);
		Scene* LoadScene(std::string& scene_path);
		void SaveAsset(const std::string& path, Material* mat);
		void SaveAsset(const Asset& asset);
		
		Ref<Texture2D> LoadTexture(const WString& asset_path);
		Ref<TextureCubeMap> LoadTexture(const Vector<WString>& asset_paths);
		List<Ref<Mesh>> LoadMesh(const WString& asset_path);
		//template<typename T>
		//static T* LoadAsset(const string& asset_path);
		static Material* LoadAsset(const String& asset_path);
		Ref<void> ImportAsset(const WString& sys_path);
		//async editon always reutn nullptr
		Ref<void> ImportAssetAsync(const WString& sys_path, OnResourceTaskCompleted callback = [](Ref<void> asset) {});

		//static void AddResourceTask(ResourceTask task, OnResourceTaskCompleted callback);
		static void AddResourceTask(ResourceTask task);
	public:
		Vector<ImportInfo> _import_infos;
	private:
		inline const static std::set<String> kCommonImageExt = { ".png",".PNG",".tga",".TGA",".jpg",".JPG",".jpg",".JPEG"};
		inline const static String kAssetDatabasePath = kEngineResRootPath + "assetdb.alasset";
		inline static ImportInfo* s_p_current_import_info;
		static Ref<Material> LoadMaterial(std::string path);
		static Ref<Material> LoadMaterial(const Vector<String>& blob);
		static bool ExistInAssetDB(const Guid& guid);
		static void AddToAssetDB(const Asset& asset,bool override = true);
		static void LoadAssetDB();
		static void SaveAssetDB();
		static void FormatLine(const String& line, String& key, String& value);
		void SaveMaterialImpl(const String& asset_path,Material* mat);
		void WatchDirectory();
		void SubmitResourceTask();
		Ref<void> ImportAssetImpl(const WString& sys_path);
	private:
		inline static List<ResourceTask> s_task_queue;
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
	//	//	s_all_asset.insert(Asset{ asset_path,GetResSysPath(asset_path),guid,AssetType::kMaterial});
	//	//	return mat.get();
	//	//}
	//	return nullptr;
	//}
}

#endif // !RESOURCE_MGR_H__
