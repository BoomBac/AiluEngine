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
	struct ImportSetting
	{
	public:
		static ImportSetting& Default()
		{
			static ImportSetting s_default;
			return s_default;
		}
		String _name_id;
		bool _is_copy = true;
		ImportSetting() = default;
		ImportSetting(String name_id, bool is_copy = true)
		{
			_name_id = name_id;
			_is_copy = is_copy;
		}
		virtual ~ImportSetting() = default;
		//virtual void* operator new(size_t size) = 0;
		//virtual void* operator new[](size_t size) = 0;
		//virtual void operator delete(void* ptr) = 0;
		//virtual void operator delete[](void* ptr) = 0;)
	};

	struct TextureImportSetting : public ImportSetting
	{
	public:
		static TextureImportSetting& Default()
		{
			static TextureImportSetting s_default;
			return s_default;
		}
		bool _is_srgb = false;
		bool _generate_mipmap = true;
	};
	struct MeshImportSetting : public ImportSetting
	{
	public:
		static MeshImportSetting& Default()
		{
			static MeshImportSetting s_default;
			return s_default;
		}
		MeshImportSetting() = default;
		MeshImportSetting(String name_id, bool is_copy,bool is_import_material = true) : ImportSetting(name_id, is_copy)
		{
			_name_id = name_id;
			_is_import_material = is_import_material;
		}
		bool _is_import_material = true;
	};

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
		inline const static std::set<String> kLDRImageExt = { ".png",".PNG",".tga",".TGA",".jpg",".JPG",".jpg",".JPEG" };
		inline const static std::set<String> kHDRImageExt = { ".exr",".EXR",".hdr",".HDR" };
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick(const float& delta_time) final;
		auto Begin() { return s_asset_db.begin(); };
		auto End() { return s_asset_db.end(); };
		u64 AssetNum() { return s_asset_db.size(); }
		void SaveScene(Scene* scene, std::string& scene_path);
		Scene* LoadScene(std::string& scene_path);
		void SaveAsset(const WString& sys_path, Material* mat);
		void SaveAsset(const Asset* asset);
		//Load系函数负责加载生成资源，不会放置到对应的对象池，不会生成对应的asset
		Ref<Texture2D> LoadTexture(const WString& asset_path, const ImportSetting* setting = &ImportSetting::Default());
		Ref<TextureCubeMap> LoadTexture(const Vector<WString>& asset_paths);
		List<Ref<Mesh>> LoadMesh(const WString& asset_path);
		template<typename T>
		Ref<T> LoadAsset(const WString& asset_path);
		template<typename T>
		Ref<T> LoadAsset(const Guid& guid);
		//static Material* LoadAsset(const String& asset_path);
		Ref<void> ImportResource(const WString& sys_path, const ImportSetting& setting = ImportSetting::Default());
		//async editon always reutn nullptr
		Ref<void> ImportResourceAsync(const WString& sys_path, const ImportSetting& setting = ImportSetting::Default(), OnResourceTaskCompleted callback = [](Ref<void> asset) {});

		//static void AddResourceTask(ResourceTask task, OnResourceTaskCompleted callback);
		static void AddResourceTask(ResourceTask task);
		static bool ExistInAssetDB(const WString& asset_path);
		static WString GetAssetPath(const Guid& guid);
		Asset* GetAsset(const WString& asset_path) const;
	public:
		Vector<ImportInfo> _import_infos;
	private:
		inline const static String kAssetDatabasePath = kEngineResRootPath + "assetdb.alasset";
		inline static ImportInfo* s_p_current_import_info;
		static Ref<Material> LoadMaterialImpl(const WString& asset_path);
		//static Ref<Material> LoadMaterial(const Vector<String>& blob);
		static bool ExistInAssetDB(const Asset* asset);
		static void AddToAssetDB(Asset* asset, bool override = true);
		static void LoadAssetDB();
		static void SaveAssetDB();
		static void FormatLine(const String& line, String& key, String& value);
		void SaveMaterialImpl(const WString& sys_path, Material* mat);
		void WatchDirectory();
		void SubmitResourceTask();
		//导入外部资源并创建对应的asset
		Ref<void> ImportResourceImpl(const WString& sys_path, const ImportSetting* setting);
	private:
		inline static List<ResourceTask> s_task_queue;
		//<GUID,Asset>
		inline static std::map<Guid, Asset*> s_asset_db{};
		//AssetPath,GUID
		inline static std::map<WString, Guid> s_asset_looktable{};
	};
	extern ResourceMgr* g_pResourceMgr;

	template<typename T>
	inline Ref<T> ResourceMgr::LoadAsset(const WString& asset_path)
	{
		if (asset_path.empty())
		{
			return nullptr;
		}
		if (std::is_same<T, Material>::value)
		{
			auto mat = LoadMaterialImpl(asset_path);
			return mat;
		}
		return nullptr;
	}
	template<typename T>
	inline Ref<T> ResourceMgr::LoadAsset(const Guid& guid)
	{
		return LoadAsset<T>(GetAssetPath(guid));
	}
}

#endif // !RESOURCE_MGR_H__
